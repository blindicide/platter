// SPDX-License-Identifier: TBD
//
// platter-cli hex <device> ...   hex dump of sectors with structure interpretation
// platter-cli find <device> ...  byte pattern search
// Both are strictly read-only.
#include "commands.hpp"
#include "platter/inspect/inspect.hpp"

namespace platter::cli {
namespace {

json structure_json(const inspect::Structure& s, std::uint32_t base) {
    json j;
    j["kind"] = std::string(inspect::struct_kind_name(s.kind));
    j["title"] = s.title;
    j["length"] = s.length;
    j["base_offset"] = base;
    j["fields"] = json::array();
    for (const auto& f : s.fields)
        j["fields"].push_back({{"offset", f.offset}, {"size", f.size}, {"name", f.name}, {"value", f.value}, {"note", f.note}});
    j["notes"] = s.notes;
    return j;
}

std::string hex_string(const std::vector<std::uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(v.size() * 2);
    for (auto b : v) {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}

}  // namespace

int cmd_hex(const Options& o, const std::string& target) {
    std::uint64_t lba = 0, sectors = 1;
    std::optional<std::uint64_t> byte_offset;
    if (auto v = opt(o, "lba")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--lba must be a sector number");
        lba = *n;
    }
    if (auto v = opt(o, "offset")) {
        if (opt(o, "lba")) return usage_error("use either --lba or --offset, not both");
        auto n = parse_size(*v);
        if (!n) return usage_error("--offset must be a byte offset such as 4096 or 1M");
        byte_offset = *n;
    }
    if (auto v = opt(o, "sectors")) {
        auto n = parse_u64(*v);
        if (!n || *n == 0 || *n > 256) return usage_error("--sectors must be between 1 and 256");
        sectors = *n;
    }
    std::optional<inspect::StructKind> forced;
    const bool auto_kind = !opt(o, "as") || *opt(o, "as") == "auto";
    if (!auto_kind) {
        forced = inspect::parse_struct_kind(*opt(o, "as"));
        if (!forced)
            return usage_error("--as must be one of: auto, mbr, gpt-header, gpt-entries, fat, exfat, ntfs, mft, ext");
    }

    auto dev = open_target_read_only(target, o);
    if (!dev) return fail(dev.error(), target);
    const auto ss = (*dev)->sector_size();
    std::uint64_t start = byte_offset ? *byte_offset : lba * ss;
    std::uint64_t length = sectors * ss;
    if (byte_offset) {
        // Reads are sector-granular: round the window out to whole sectors.
        const std::uint64_t first = start / ss * ss;
        length = ((start + length + ss - 1) / ss * ss) - first;
        start = first;
    }
    if (start >= (*dev)->capacity()) return fail(Error{Errc::out_of_range, "offset is beyond the end of the device"}, target);
    length = std::min<std::uint64_t>(length, (*dev)->capacity() - start);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(length));
    if (auto r = (*dev)->read(start, buf.data(), buf.size()); !r) return fail(r.error(), "reading sectors");

    std::optional<inspect::Structure> st;
    std::uint32_t base = 0;
    std::string why;
    if (!o.flags.count("no-interpret")) {
        std::optional<inspect::Detected> det;
        if (forced) {
            det = inspect::Detected{*forced, 0};
        } else {
            det = inspect::detect(buf, start / ss, ss);
        }
        if (det) {
            auto s = inspect::interpret(det->kind, std::span<const std::uint8_t>(buf).subspan(det->offset), ss);
            if (s) {
                st = std::move(s).value();
                base = det->offset;
            } else {
                why = s.error().describe();
            }
        }
    }

    Out out(o);
    if (o.json) {
        json j;
        j["device"] = target;
        j["sector_size"] = ss;
        j["offset"] = start;
        j["lba"] = start / ss;
        j["length"] = buf.size();
        j["hex"] = hex_string(buf);
        j["structure"] = st ? structure_json(*st, base) : json(nullptr);
        out.set_json(std::move(j));
        return out.flush(kOk);
    }
    out.text() << "# " << target << ": LBA " << start / ss << " (byte offset " << start << "), " << buf.size() << " bytes, sector size " << ss << "\n";
    out.text() << inspect::hex_dump(buf, start);
    if (st) {
        out.text() << "\n" << inspect::structure_to_text(*st);
    } else if (forced && !why.empty()) {
        out.text() << "\ncannot interpret as " << inspect::struct_kind_name(*forced) << ": " << why << "\n";
    }
    return out.flush(kOk);
}

int cmd_find(const Options& o, const std::string& target) {
    const auto* hex = opt(o, "hex");
    const auto* ascii = opt(o, "ascii");
    const auto* utf16 = opt(o, "utf16");
    if ((hex ? 1 : 0) + (ascii ? 1 : 0) + (utf16 ? 1 : 0) != 1)
        return usage_error("find needs exactly one of --hex PATTERN, --ascii TEXT, --utf16 TEXT");
    const bool ci = o.flags.count("ignore-case") > 0;
    inspect::Pattern pat;
    if (hex) {
        auto p = inspect::parse_hex_pattern(*hex);
        if (!p) return usage_error(p.error().describe());
        pat = std::move(p).value();
    } else if (ascii) {
        if (ascii->empty()) return usage_error("--ascii needs some text");
        pat = inspect::ascii_pattern(*ascii, ci);
    } else {
        auto p = inspect::utf16le_pattern(*utf16, ci);
        if (!p) return usage_error(p.error().describe());
        pat = std::move(p).value();
    }
    std::size_t limit = 100;
    if (auto v = opt(o, "limit")) {
        auto n = parse_u64(*v);
        if (!n || *n == 0) return usage_error("--limit must be a positive number");
        limit = static_cast<std::size_t>(*n);
    }

    auto dev = open_target_read_only(target, o);
    if (!dev) return fail(dev.error(), target);
    const auto ss = (*dev)->sector_size();
    std::uint64_t first = 0, end = (*dev)->capacity();
    if (auto v = opt(o, "start")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--start must be an LBA number");
        first = *n * ss;
    }
    if (auto v = opt(o, "end")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--end must be an LBA number");
        end = std::min<std::uint64_t>(end, (*n + 1) * ss);
    }

    const bool tty = !o.quiet && !o.json;
    auto last = std::chrono::steady_clock::now();
    auto res = inspect::search_device(**dev, pat, first, end, limit, [&](std::uint64_t scanned) {
        if (tty && std::chrono::steady_clock::now() - last > std::chrono::seconds(2)) {
            last = std::chrono::steady_clock::now();
            std::cerr << "scanned " << scanned / (1024 * 1024) << " MiB...\n";
        }
        return true;
    });
    if (!res) return fail(res.error(), "searching");

    Out out(o);
    if (o.json) {
        json j;
        j["device"] = target;
        j["pattern_bytes"] = pat.size();
        j["scanned_bytes"] = res->scanned_bytes;
        j["unreadable_sectors"] = res->unreadable_sectors;
        j["truncated"] = res->truncated;
        j["hits"] = json::array();
        for (auto h : res->hits) j["hits"].push_back({{"offset", h}, {"lba", h / ss}, {"offset_in_sector", h % ss}});
        out.set_json(std::move(j));
        return out.flush(kOk);
    }
    for (auto h : res->hits) out.text() << "offset " << h << "  LBA " << h / ss << " + " << h % ss << "\n";
    out.text() << res->hits.size() << (res->hits.size() == 1 ? " match" : " matches") << " in " << res->scanned_bytes
               << " bytes";
    if (res->truncated) out.text() << " (stopped at --limit " << limit << ")";
    if (res->unreadable_sectors) out.text() << "; " << res->unreadable_sectors << " unreadable sectors skipped";
    out.text() << "\n";
    return out.flush(res->unreadable_sectors ? kScanErrors : kOk);
}

}  // namespace platter::cli
