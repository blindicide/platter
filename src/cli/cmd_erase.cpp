// SPDX-License-Identifier: LicenseRef-Proprietary
//
// platter-cli erase ... : overwrite erasure behind the safety pipeline.
#include "commands.hpp"
#include "platter/erase/erase.hpp"
#include "platter/reports/erase_report.hpp"
#include "table.hpp"

namespace platter::cli {
namespace {

std::string pass_progress(const erase::Erase& e, const OperationProgress& p) {
    const auto [cur, total] = e.pass_position();
    char b[260];
    std::snprintf(b, sizeof b, "[%5.1f%%] pass %zu/%zu  %8.2f MB/s  ETA %s  errors %llu  %s", p.percent(), cur, total,
                  p.current_bytes_per_sec / 1e6, p.eta ? hms(*p.eta).c_str() : "--:--:--",
                  static_cast<unsigned long long>(p.errors), p.message.c_str());
    return b;
}

Result<erase::EraseConfig> parse_erase_config(const Options& o) {
    erase::EraseConfig c;
    if (auto v = opt(o, "method")) {
        auto m = erase::parse_method(*v);
        if (!m) return Error{Errc::invalid_argument, "unknown --method '" + *v + "' (try: platter-cli erase methods)"};
        c.method = *m;
    }
    if (auto v = opt(o, "passes")) {
        auto n = parse_u64(*v);
        if (!n || *n < 1 || *n > 99) return Error{Errc::invalid_argument, "--passes must be between 1 and 99"};
        c.random_passes = static_cast<std::uint32_t>(*n);
        if (c.method == erase::Method::random && *n > 1) c.method = erase::Method::random_n;
    }
    if (auto v = opt(o, "pattern")) {
        auto bytes = parse_hex_pattern(*v);
        if (!bytes) return bytes.error();
        c.pattern = std::move(*bytes);
        if (c.method == erase::Method::zero && !opt(o, "method")) c.method = erase::Method::pattern;
    }
    if (c.method == erase::Method::pattern && c.pattern.empty())
        return Error{Errc::invalid_argument, "--method pattern needs --pattern HEX"};
    if (auto v = opt(o, "verify")) {
        auto m = erase::parse_verify_mode(*v);
        if (!m) return Error{Errc::invalid_argument, "--verify must be none, last or every"};
        c.verify = *m;
    }
    if (auto v = opt(o, "block-size")) {
        auto n = parse_size(*v);
        if (!n || *n == 0 || *n > (64u << 20)) return Error{Errc::invalid_argument, "--block-size must be between 1 and 64 MiB"};
        c.block_bytes = static_cast<std::uint32_t>(*n);
    }
    return c;
}

std::string summary_for(const erase::EraseConfig& c) {
    std::string s = std::string(erase::method_label(c.method));
    if (c.method == erase::Method::random_n) s = std::to_string(c.random_passes) + " passes of random data";
    s += "; verification: " + std::string(erase::verify_mode_name(c.verify));
    return s;
}

}  // namespace

int cmd_erase_methods(const Options& o) {
    Out out(o);
    TextTable t({"Method", "Passes", "Description"});
    json arr = json::array();
    for (int i = 0; i < erase::kMethodCount; ++i) {
        const auto m = static_cast<erase::Method>(i);
        erase::EraseConfig c;
        c.method = m;
        c.pattern = {0};
        const auto n = erase::passes_for(c).size();
        t.add({std::string(erase::method_name(m)), std::to_string(n), std::string(erase::method_label(m))});
        arr.push_back({{"method", std::string(erase::method_name(m))}, {"passes", n}, {"label", std::string(erase::method_label(m))}});
    }
    t.print(out.text());
    out.text() << "\nOverwriting cannot reach remapped or over-provisioned blocks (flash, bad-sector reallocation).\n"
                  "DoD 5220.22-M is withdrawn; NIST SP 800-88 considers one verified pass sufficient for HDDs.\n";
    out.set_json(arr);
    return out.flush(kOk);
}

int cmd_erase_run(const Options& o, const std::string& target_arg) {
    auto cfg = parse_erase_config(o);
    if (!cfg) return usage_error(cfg.error().message);
    auto ranges = parse_target_ranges(o);
    if (!ranges) return usage_error(ranges.error().message);

    int code = kOk;
    auto target = authorize_from_cli(o, target_arg, safety::Kind::erase, summary_for(*cfg), *ranges, "erasing", code);
    if (!target) return code;

    auto op = std::make_unique<erase::Erase>(std::move(*target), *cfg);
    if (auto v = op->validate(); !v) return fail(v.error(), "invalid erase configuration");
    if (!o.quiet) std::cerr << "Erasing... Ctrl+C stops after the current request.\n";
    if (auto s = op->start(); !s) return fail(s.error(), "starting erase");
    const erase::Erase* raw = op.get();
    run_with_progress(*op, o, [raw](const OperationProgress& p) { return pass_progress(*raw, p); });

    const auto result = op->result();
    reports::Privacy priv{o.show_serial};
    if (o.flags.count("save")) save_to_history(o, [&](storage::HistoryStore& h) { return h.record_erase(result); });
    bool files_ok = true;
    if (auto f = opt(o, "report")) files_ok = write_text_file(*f, reports::erase_to_text(result, priv)) && files_ok;

    Out out(o);
    out.text() << reports::erase_to_text(result, priv);
    out.set_raw_json(reports::erase_to_json(result, priv));
    switch (result.state) {
        case OperationState::completed: code = kOk; break;
        case OperationState::cancelled: code = kCancelled; break;
        default: code = result.verdict == erase::Verdict::erased_with_errors ? kCompletedWithErrors : kIoError; break;
    }
    if (result.state == OperationState::failed && !o.quiet) std::cerr << "platter-cli: erase stopped: " << result.failure << "\n";
    code = out.flush(code);
    return files_ok ? code : kFailure;
}

int cmd_erase_history(const Options& o) {
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto rows = (*store)->list(storage::SessionKind::erase, {}, 100);
    if (!rows) return fail(rows.error(), "history");
    Out out(o);
    TextTable t({"Id", "Started (UTC)", "Device", "Method", "State", "Result"});
    json arr = json::array();
    for (const auto& r : *rows) {
        t.add({std::to_string(r.id), r.started_utc, r.device_model, r.subtype, r.state, r.headline});
        arr.push_back({{"id", r.id}, {"started_utc", r.started_utc}, {"device", r.device_model}, {"method", r.subtype},
                       {"state", r.state}, {"headline", r.headline}});
    }
    t.print(out.text());
    out.set_json(arr);
    return out.flush(kOk);
}

int cmd_erase_show(const Options& o, const std::string& id_text) {
    auto id = parse_u64(id_text);
    if (!id) return usage_error("erase show needs a numeric session id");
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto r = (*store)->load_erase(static_cast<std::int64_t>(*id));
    if (!r) return fail(r.error(), "history");
    Out out(o);
    out.text() << reports::erase_to_text(*r, reports::Privacy{o.show_serial});
    out.set_raw_json(reports::erase_to_json(*r, reports::Privacy{o.show_serial}));
    return out.flush(kOk);
}

}  // namespace platter::cli
