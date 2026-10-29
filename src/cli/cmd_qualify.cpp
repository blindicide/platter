// SPDX-License-Identifier: LicenseRef-Proprietary
//
// platter-cli qualify run|history|show ... : destructive burn-in behind the
// safety pipeline. Run is destructive and always asks for a typed confirmation.
#include "commands.hpp"
#include "platter/qualify/qualify.hpp"
#include "platter/reports/qualify_report.hpp"
#include "platter/smart/smart.hpp"
#include "table.hpp"

namespace platter::cli {
namespace {

std::string stage_progress(const qualify::Qualification& q, const OperationProgress& p) {
    const auto [cur, total] = q.stage_position();
    char b[300];
    std::snprintf(b, sizeof b, "[%5.1f%%] stage %zu/%zu  %8.2f MB/s  ETA %s  errors %llu  %s", p.percent(), cur, total,
                  p.current_bytes_per_sec / 1e6, p.eta ? hms(*p.eta).c_str() : "--:--:--",
                  static_cast<unsigned long long>(p.errors), p.message.c_str());
    return b;
}

Result<qualify::QualifyConfig> parse_config(const Options& o) {
    qualify::QualifyConfig c;
    if (auto v = opt(o, "passes")) {
        auto n = parse_u64(*v);
        if (!n || *n < 1 || *n > qualify::kMaxWritePasses) return Error{Errc::invalid_argument, "--passes must be between 1 and 4"};
        c.write_passes = static_cast<std::uint32_t>(*n);
    }
    if (o.flags.count("no-verify")) c.verify_each_pass = false;
    if (o.flags.count("no-final-read")) c.final_read = false;
    if (auto v = opt(o, "samples")) {
        auto n = parse_u64(*v);
        if (!n || *n > 1'000'000) return Error{Errc::invalid_argument, "--samples must be between 0 and 1000000"};
        c.seek_samples = static_cast<std::uint32_t>(*n);
    }
    if (auto v = opt(o, "slow-ms")) {
        auto n = parse_u64(*v);
        if (!n || *n == 0 || *n > 600'000) return Error{Errc::invalid_argument, "--slow-ms must be a positive number of milliseconds"};
        c.slow_block_ms = static_cast<std::uint32_t>(*n);
    }
    if (auto v = opt(o, "block-size")) {
        auto n = parse_size(*v);
        if (!n || *n == 0 || *n > (64u << 20)) return Error{Errc::invalid_argument, "--block-size must be between 1 and 64 MiB"};
        c.block_bytes = static_cast<std::uint32_t>(*n);
    }
    return c;
}

std::string summary_for(const qualify::QualifyConfig& c) {
    std::string s = std::to_string(c.write_passes) + " write pass(es)";
    if (c.verify_each_pass) s += " with read-back";
    if (c.final_read) s += ", final read";
    if (c.seek_samples) s += ", " + std::to_string(c.seek_samples) + " random reads";
    return s;
}

// Best effort and strictly read-only; images and missing smartctl give "invalid".
qualify::SmartSnapshot read_smart(const Options& o, const std::string& target_arg) {
    if (o.flags.count("no-smart") || is_image_path(target_arg)) return {};
    auto r = smart::query(expand_device(target_arg));
    if (!r) return {};
    return qualify::smart_snapshot(*r);
}

}  // namespace

int cmd_qualify_run(const Options& o, const std::string& target_arg) {
    auto cfg = parse_config(o);
    if (!cfg) return usage_error(cfg.error().message);
    auto ranges = parse_target_ranges(o);
    if (!ranges) return usage_error(ranges.error().message);

    // The SMART baseline is read before the confirmation so it describes the drive as it was handed to us.
    const auto before = read_smart(o, target_arg);

    int code = kOk;
    auto target = authorize_from_cli(o, target_arg, safety::Kind::qualification, summary_for(*cfg), *ranges, "qualification", code);
    if (!target) return code;

    auto op = std::make_unique<qualify::Qualification>(std::move(*target), *cfg);
    if (auto v = op->validate(); !v) return fail(v.error(), "invalid qualification configuration");
    op->set_smart_before(before);
    if (!o.quiet) {
        std::cerr << "Qualifying... Ctrl+C stops after the current request.\n";
        if (!before.valid) std::cerr << "note: no SMART baseline (image, smartctl missing, or --no-smart); SMART will not be compared.\n";
    }
    if (auto s = op->start(); !s) return fail(s.error(), "starting qualification");
    const qualify::Qualification* raw = op.get();
    run_with_progress(*op, o, [raw](const OperationProgress& p) { return stage_progress(*raw, p); });
    op->set_smart_after(read_smart(o, target_arg));

    const auto result = op->result();
    reports::Privacy priv{o.show_serial};
    if (o.flags.count("save")) save_to_history(o, [&](storage::HistoryStore& h) { return h.record_qualification(result); });
    bool files_ok = true;
    if (auto f = opt(o, "report")) files_ok = write_text_file(*f, qualify::result_to_text(result, o.show_serial)) && files_ok;

    Out out(o);
    out.text() << qualify::result_to_text(result, o.show_serial);
    out.set_raw_json(reports::qualify_to_json(result, priv));
    switch (result.verdict) {
        case qualify::Verdict::pass:
        case qualify::Verdict::pass_with_warnings: code = kOk; break;
        case qualify::Verdict::fail: code = kCompletedWithErrors; break;
        case qualify::Verdict::incomplete: code = result.state == OperationState::cancelled ? kCancelled : kIoError; break;
    }
    if (result.state == OperationState::failed && !o.quiet && result.verdict == qualify::Verdict::incomplete)
        std::cerr << "platter-cli: qualification stopped: " << result.failure << "\n";
    code = out.flush(code);
    return files_ok ? code : kFailure;
}

int cmd_qualify_history(const Options& o) {
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto rows = (*store)->list(storage::SessionKind::qualification, {}, 100);
    if (!rows) return fail(rows.error(), "history");
    Out out(o);
    TextTable t({"Id", "Started (UTC)", "Device", "Plan", "State", "Result"});
    json arr = json::array();
    for (const auto& r : *rows) {
        t.add({std::to_string(r.id), r.started_utc, r.device_model, r.subtype, r.state, r.headline});
        arr.push_back({{"id", r.id}, {"started_utc", r.started_utc}, {"device", r.device_model}, {"plan", r.subtype},
                       {"state", r.state}, {"headline", r.headline}});
    }
    t.print(out.text());
    out.set_json(arr);
    return out.flush(kOk);
}

int cmd_qualify_show(const Options& o, const std::string& id_text) {
    auto id = parse_u64(id_text);
    if (!id) return usage_error("qualify show needs a numeric session id");
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto r = (*store)->load_qualification(static_cast<std::int64_t>(*id));
    if (!r) return fail(r.error(), "history");
    Out out(o);
    out.text() << qualify::result_to_text(*r, o.show_serial);
    out.set_raw_json(reports::qualify_to_json(*r, reports::Privacy{o.show_serial}));
    return out.flush(kOk);
}

}  // namespace platter::cli
