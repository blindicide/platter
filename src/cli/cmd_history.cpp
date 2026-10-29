// SPDX-License-Identifier: TBD
//
// platter-cli history list|show|delete: one browser over every saved session
// (benchmarks, scans, SMART snapshots, erase, imaging and qualification).
// Deleting removes only the saved record, never anything on a device.
#include "commands.hpp"
#include "table.hpp"

namespace platter::cli {

int cmd_history_list(const Options& o, const std::string* device_arg) {
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    std::string key;
    if (device_arg) {
        auto info = describe_target(*device_arg, o);
        if (!info) return fail(info.error(), *device_arg);
        key = storage::HistoryStore::device_key(*info);
    }
    std::optional<storage::SessionKind> only;
    if (auto k = opt(o, "kind")) {
        only = storage::parse_session_kind(*k);
        if (!only) return usage_error("--kind must be one of: benchmark surface-scan smart erase imaging qualification");
    }
    int limit = 100;
    if (auto v = opt(o, "limit")) {
        auto n = parse_u64(*v);
        if (!n || *n == 0 || *n > 10000) return usage_error("--limit must be between 1 and 10000");
        limit = static_cast<int>(*n);
    }
    auto rows = only ? (*store)->list(*only, key, limit) : (*store)->list_all(key, limit);
    if (!rows) return fail(rows.error(), "history");

    Out out(o);
    TextTable t({"Id", "Kind", "Started (UTC)", "Device", "State", "Result"});
    json arr = json::array();
    for (const auto& r : *rows) {
        const std::string kind(storage::session_kind_name(r.kind));
        t.add({std::to_string(r.id), kind, r.started_utc, r.device_model, r.state, r.headline});
        arr.push_back({{"id", r.id}, {"kind", kind}, {"started_utc", r.started_utc}, {"device", r.device_model},
                       {"subtype", r.subtype}, {"state", r.state}, {"headline", r.headline}});
    }
    t.print(out.text());
    out.set_json(arr);
    return out.flush(kOk);
}

int cmd_history_show(const Options& o, const std::string& id_text) {
    auto id = parse_u64(id_text);
    if (!id) return usage_error("history show needs a numeric session id (see: history list)");
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto s = (*store)->find(static_cast<std::int64_t>(*id));
    if (!s) return fail(s.error(), "history");
    auto doc = (*store)->load_document(s->kind, s->id);
    if (!doc) return fail(doc.error(), "history");
    Out out(o);
    auto text = (*store)->render_text(*s, o.show_serial);
    if (!text) return fail(text.error(), "history");
    out.text() << "# session " << s->id << " (" << storage::session_kind_name(s->kind) << ")\n" << *text << "\n";
    out.set_raw_json(*doc);
    return out.flush(kOk);
}

int cmd_history_delete(const Options& o, const std::string& id_text) {
    auto id = parse_u64(id_text);
    if (!id) return usage_error("history delete needs a numeric session id");
    auto store = storage::HistoryStore::open(history_path(o));
    if (!store) return fail(store.error(), "history");
    auto s = (*store)->find(static_cast<std::int64_t>(*id));
    if (!s) return fail(s.error(), "history");
    if (auto r = (*store)->remove(s->kind, s->id); !r) return fail(r.error(), "history");
    if (!o.quiet) std::cerr << "Removed saved " << storage::session_kind_name(s->kind) << " session " << s->id << " (" << s->headline << ").\n";
    return kOk;
}

}  // namespace platter::cli
