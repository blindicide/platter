// SPDX-License-Identifier: LicenseRef-Proprietary
//
// platter-cli image create|restore|verify ... and clone: imaging behind the
// safety pipeline (restore and clone are destructive, create and verify only
// read their sources).
#include "commands.hpp"
#include "platter/imaging/imaging.hpp"
#include "platter/reports/imaging_report.hpp"
#include "table.hpp"

namespace platter::cli {
namespace {

using imaging::ImagingResult;
using imaging::Verdict;

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string progress_line(const OperationProgress& p) {
    char b[260];
    std::snprintf(b, sizeof b, "[%5.1f%%] %8.2f MB/s  ETA %s  errors %llu  %s", p.percent(), p.current_bytes_per_sec / 1e6,
                  p.eta ? hms(*p.eta).c_str() : "--:--:--", static_cast<unsigned long long>(p.errors), p.message.c_str());
    return b;
}

std::optional<std::uint32_t> block_size_opt(const Options& o, std::string& err) {
    if (auto v = opt(o, "block-size")) {
        auto n = parse_size(*v);
        if (!n || *n == 0 || *n > (64u << 20)) {
            err = "--block-size must be between 1 and 64 MiB";
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*n);
    }
    return imaging::kDefaultBlockBytes;
}

int exit_for_result(const ImagingResult& r) {
    switch (r.state) {
        case OperationState::completed: break;
        case OperationState::cancelled: return kCancelled;
        default: return kIoError;
    }
    switch (r.verdict) {
        case Verdict::ok: return kOk;
        case Verdict::ok_with_unreadable: return kCompletedWithErrors;
        case Verdict::check_failed: return kVerifyFailed;
        case Verdict::incomplete: return kIoError;
    }
    return kFailure;
}

// Common tail: run, report, save, exit code.
int run_and_report(Operation& op, const Options& o, const char* verb, std::function<ImagingResult()> result) {
    if (!o.quiet) std::cerr << verb << "... Ctrl+C stops after the current block.\n";
    if (auto s = op.start(); !s) return fail(s.error(), std::string("starting ") + verb);
    run_with_progress(op, o, progress_line);
    const auto r = result();
    reports::Privacy priv{o.show_serial};
    if (o.flags.count("save")) save_to_history(o, [&](storage::HistoryStore& h) { return h.record_imaging(r); });
    bool files_ok = true;
    if (auto f = opt(o, "report")) files_ok = write_text_file(*f, imaging::result_to_text(r, o.show_serial)) && files_ok;
    Out out(o);
    out.text() << imaging::result_to_text(r, o.show_serial);
    out.set_raw_json(reports::imaging_to_json(r, priv));
    if (r.state == OperationState::failed && !o.quiet) std::cerr << "platter-cli: " << verb << " stopped: " << r.failure << "\n";
    const int code = out.flush(exit_for_result(r));
    return files_ok ? code : kFailure;
}

}  // namespace

int cmd_image_create(const Options& o, const std::string& device, const std::string& file) {
    imaging::ImageConfig c;
    c.output = file;
    if (auto v = opt(o, "compress")) {
        if (*v == "zstd") c.compression = imaging::Compression::zstd;
        else if (*v == "none") c.compression = imaging::Compression::none;
        else return usage_error("--compress must be none or zstd");
    } else if (ends_with(file, ".zst")) {
        c.compression = imaging::Compression::zstd;
    }
    if (auto v = opt(o, "level")) {
        auto n = parse_u64(*v);
        if (!n || *n < 1 || *n > 19) return usage_error("--level must be between 1 and 19");
        c.level = static_cast<int>(*n);
    }
    c.sparse = o.flags.count("sparse") != 0;
    c.overwrite = o.flags.count("force") != 0;
    c.recover = o.flags.count("recover") != 0;
    c.verify_image = o.flags.count("no-verify") == 0;
    c.write_sidecar = o.flags.count("no-sidecar") == 0;
    if (auto v = opt(o, "retries")) {
        auto n = parse_u64(*v);
        if (!n || *n > 16) return usage_error("--retries must be between 0 and 16");
        c.retries = static_cast<std::uint32_t>(*n);
    }
    std::string err;
    auto bs = block_size_opt(o, err);
    if (!bs) return usage_error(err);
    c.block_bytes = *bs;
    if (auto v = opt(o, "start")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--start must be an LBA");
        c.start_lba = *n;
    }
    if (auto v = opt(o, "end")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--end must be an LBA");
        c.end_lba = *n;
    }
    auto dev = open_target_read_only(device, o);
    if (!dev) return fail(dev.error(), device);
    auto op = std::make_unique<imaging::CreateImage>(std::move(*dev), c);
    if (auto v = op->validate(); !v) return fail(v.error(), "invalid image request");
    const auto* raw = op.get();
    return run_and_report(*op, o, "Imaging", [raw] { return raw->result(); });
}

int cmd_image_restore(const Options& o, const std::string& file, const std::string& device) {
    imaging::RestoreConfig c;
    c.input = file;
    c.verify = o.flags.count("no-verify") == 0;
    if (auto v = opt(o, "expect-sha256")) c.expect_sha256 = *v;
    else if (auto s = imaging::read_sha256_sidecar(file + ".sha256"); !s.empty() && o.flags.count("use-sidecar")) c.expect_sha256 = s;
    std::string err;
    auto bs = block_size_opt(o, err);
    if (!bs) return usage_error(err);
    c.block_bytes = *bs;
    std::uint64_t first = 0;
    if (auto v = opt(o, "target-offset")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--target-offset must be an LBA");
        first = *n;
    }
    auto info = describe_target(device, o);
    if (!info) return fail(info.error(), device);
    auto sectors = imaging::RestoreImage::sectors_needed(file, info->logical_sector_size);
    if (!sectors) return fail(sectors.error(), file);

    int code = kOk;
    auto target = authorize_from_cli(o, device, safety::Kind::restore_image,
                                     "Restore image " + file + " (" + format_capacity(*sectors * info->logical_sector_size) + ")",
                                     {LbaRange{first, *sectors}}, "restoring an image", code);
    if (!target) return code;
    auto op = std::make_unique<imaging::RestoreImage>(std::move(*target), c);
    if (auto v = op->validate(); !v) return fail(v.error(), "invalid restore request");
    const auto* raw = op.get();
    return run_and_report(*op, o, "Restoring", [raw] { return raw->result(); });
}

int cmd_clone(const Options& o, const std::string& source, const std::string& target_arg) {
    imaging::CloneConfig c;
    c.recover = o.flags.count("recover") != 0;
    c.verify = o.flags.count("no-verify") == 0;
    if (auto v = opt(o, "retries")) {
        auto n = parse_u64(*v);
        if (!n || *n > 16) return usage_error("--retries must be between 0 and 16");
        c.retries = static_cast<std::uint32_t>(*n);
    }
    std::string err;
    auto bs = block_size_opt(o, err);
    if (!bs) return usage_error(err);
    c.block_bytes = *bs;
    if (auto v = opt(o, "start")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--start must be an LBA");
        c.source_first_lba = *n;
    }
    std::uint64_t target_first = 0;
    if (auto v = opt(o, "target-offset")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--target-offset must be an LBA");
        target_first = *n;
    }
    auto src = open_target_read_only(source, o);
    if (!src) return fail(src.error(), source);
    const std::uint64_t total = (*src)->info().total_lbas();
    std::uint64_t end = total ? total - 1 : 0;
    if (auto v = opt(o, "end")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--end must be an LBA");
        end = *n;
    }
    if (c.source_first_lba > end || end >= total) return usage_error("the source range is outside the device");
    const std::uint64_t sectors = end - c.source_first_lba + 1;

    int code = kOk;
    auto target = authorize_from_cli(o, target_arg, safety::Kind::clone_target,
                                     "Clone " + source + " (" + format_capacity(sectors * (*src)->sector_size()) + ") onto this device",
                                     {LbaRange{target_first, sectors}}, "being a clone target", code);
    if (!target) return code;
    auto op = std::make_unique<imaging::CloneDevice>(std::move(*src), std::move(*target), c);
    if (auto v = op->validate(); !v) return fail(v.error(), "invalid clone request");
    const auto* raw = op.get();
    return run_and_report(*op, o, "Cloning", [raw] { return raw->result(); });
}

int cmd_image_verify(const Options& o, const std::string& file) {
    imaging::VerifyConfig c;
    c.input = file;
    if (auto v = opt(o, "sha256")) c.expect_sha256 = *v;
    else if (auto s = imaging::read_sha256_sidecar(file + ".sha256"); !s.empty()) {
        c.expect_sha256 = s;
        if (!o.quiet) std::cerr << "Using the checksum in " << file << ".sha256\n";
    }
    std::string err;
    auto bs = block_size_opt(o, err);
    if (!bs) return usage_error(err);
    c.block_bytes = *bs;
    if (auto v = opt(o, "against")) {
        auto dev = open_target_read_only(*v, o);
        if (!dev) return fail(dev.error(), *v);
        c.compare_to = std::move(*dev);
        if (auto s = opt(o, "start")) {
            auto n = parse_u64(*s);
            if (!n) return usage_error("--start must be an LBA");
            c.compare_first_lba = *n;
        }
    }
    auto op = std::make_unique<imaging::VerifyImage>(std::move(c));
    if (auto v = op->validate(); !v) return fail(v.error(), "invalid verify request");
    const auto* raw = op.get();
    return run_and_report(*op, o, "Verifying", [raw] { return raw->result(); });
}

int cmd_image_history(const Options& o) {
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto rows = (*store)->list(storage::SessionKind::imaging, {}, 100);
    if (!rows) return fail(rows.error(), "history");
    Out out(o);
    TextTable t({"Id", "Started (UTC)", "Device", "Job", "State", "Result"});
    json arr = json::array();
    for (const auto& r : *rows) {
        t.add({std::to_string(r.id), r.started_utc, r.device_model, r.subtype, r.state, r.headline});
        arr.push_back({{"id", r.id}, {"started_utc", r.started_utc}, {"device", r.device_model}, {"job", r.subtype},
                       {"state", r.state}, {"headline", r.headline}});
    }
    t.print(out.text());
    out.set_json(arr);
    return out.flush(kOk);
}

int cmd_image_show(const Options& o, const std::string& id_text) {
    auto id = parse_u64(id_text);
    if (!id) return usage_error("image show needs a numeric session id");
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto r = (*store)->load_imaging(static_cast<std::int64_t>(*id));
    if (!r) return fail(r.error(), "history");
    Out out(o);
    out.text() << imaging::result_to_text(*r, o.show_serial);
    out.set_raw_json(reports::imaging_to_json(*r, reports::Privacy{o.show_serial}));
    return out.flush(kOk);
}

}  // namespace platter::cli
