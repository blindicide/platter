// SPDX-License-Identifier: LicenseRef-Proprietary
//
// platter-cli partitions init|add|delete|set|resize|types: partition table
// edits. Every edit is planned in memory, shown as a diff, and written only
// through the safety pipeline confirmed for exactly the table areas touched.
#include "commands.hpp"
#include "platter/partitions/partition_edit.hpp"
#include "table.hpp"

namespace platter::cli {
namespace {

using namespace platter::partitions;

// Converts a byte size or offset to LBAs (must be sector aligned).
Result<std::uint64_t> bytes_to_lbas(const std::string& text, std::uint32_t ss, const char* what) {
    auto n = parse_size(text);
    if (!n) return Error{Errc::invalid_argument, std::string(what) + " must be a size such as 1M, 512G or max"};
    if (*n % ss != 0) return Error{Errc::invalid_argument, std::string(what) + " must be a multiple of the " + std::to_string(ss) + "-byte sector size"};
    return *n / ss;
}

struct ParsedType {
    std::optional<std::uint8_t> mbr;
    std::optional<Guid> guid;
};

Result<ParsedType> parse_type(const std::string& text) {
    ParsedType t;
    if (auto a = find_type_alias(text)) {
        if (a->mbr_type != 0) t.mbr = a->mbr_type;
        t.guid = Guid::parse(a->gpt_guid);
        return t;
    }
    if (auto g = Guid::parse(text)) {
        t.guid = *g;
        return t;
    }
    std::string hex = text;
    if (hex.size() > 2 && (hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X')) hex = hex.substr(2);
    if (!hex.empty() && hex.size() <= 2 && hex.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
        t.mbr = static_cast<std::uint8_t>(std::stoul(hex, nullptr, 16));
        return t;
    }
    return Error{Errc::invalid_argument, "unknown --type '" + text + "' (try: platter-cli partitions types)"};
}

struct Loaded {
    std::string path;
    PartitionTable table;
    std::vector<std::uint8_t> sector0;
};

// Reads the current table through a read-only handle.
Result<Loaded> load_table(const std::string& arg, const Options& o) {
    auto dev = open_target_read_only(arg, o);
    if (!dev) return dev.error();
    auto t = read_partition_table(**dev);
    if (!t) return t.error();
    Loaded l;
    l.path = (*dev)->info().path;
    l.table = std::move(t).value();
    l.sector0.resize((*dev)->sector_size());
    if (auto s = read_sectors(**dev, 0, 1, l.sector0.data(), l.sector0.size()); !s) return s.error();
    return l;
}

// Shared tail: show the diff, then authorize and write.
int commit_plan(const Options& o, const std::string& target_arg, const PartitionPlan& plan, const PartitionTable& before,
                const std::string& summary) {
    if (auto pr = plan.problems(); !pr.empty()) {
        std::cerr << "platter-cli: the resulting table would be invalid: " << pr.front() << "\n";
        return kUsage;
    }
    auto ranges = plan.metadata_ranges();
    if (!ranges) return fail(ranges.error(), "planning");
    if (!o.quiet) {
        std::cerr << "Planned changes:\n" << plan.diff(before);
        for (const auto& w : plan.warnings()) std::cerr << "warning: " << w << "\n";
    }
    int code = kOk;
    auto target = authorize_from_cli(o, target_arg, safety::Kind::partition_edit, summary, *ranges, "a partition edit", code);
    if (!target) return code;
    std::vector<std::string> notes;
    if (auto s = apply_plan(*target, plan, &notes); !s) return fail(s.error(), "writing the partition table");
    for (const auto& n : notes) std::cerr << "note: " << n << "\n";
    if (!o.quiet) std::cerr << "Partition table written and verified (" << ranges->size() << " region(s)).\n";
    return kOk;
}

Result<PartitionPlan> plan_from(const Loaded& l) { return PartitionPlan::from_table(l.table, l.sector0); }

std::string need_number(const std::vector<std::string>& args, std::uint32_t& n) {
    if (args.empty()) return "a partition number is required";
    auto v = parse_u64(args[0]);
    if (!v || *v == 0 || *v > 1024) return "'" + args[0] + "' is not a partition number";
    n = static_cast<std::uint32_t>(*v);
    return {};
}

}  // namespace

int cmd_partitions_types(const Options& o) {
    Out out(o);
    TextTable t({"Name", "MBR", "GPT type GUID", "Meaning"});
    json arr = json::array();
    for (const auto& a : type_aliases()) {
        char mbr[8];
        std::snprintf(mbr, sizeof mbr, "0x%02X", a.mbr_type);
        t.add({a.name, a.mbr_type ? mbr : "-", a.gpt_guid, a.description});
        arr.push_back({{"name", a.name}, {"mbr", a.mbr_type}, {"gpt_guid", a.gpt_guid}, {"description", a.description}});
    }
    t.print(out.text());
    out.text() << "\n--type also accepts a raw MBR type (0x83) or a GPT GUID.\n";
    out.set_json(arr);
    return out.flush(kOk);
}

int cmd_partitions_init(const Options& o, const std::string& target_arg) {
    const auto* table = opt(o, "table");
    if (!table || (*table != "gpt" && *table != "mbr")) return usage_error("partitions init needs --table gpt|mbr");
    auto desc = describe_target(target_arg, o);
    if (!desc) return fail(desc.error(), target_arg);
    auto plan = PartitionPlan::blank(*table == "gpt" ? TableType::gpt : TableType::mbr, desc->logical_sector_size,
                                     desc->total_lbas());
    if (!plan) return fail(plan.error(), "planning");
    auto loaded = load_table(target_arg, o);
    PartitionTable before;
    if (loaded) before = loaded->table;
    return commit_plan(o, target_arg, *plan, before,
                       "Create a new empty " + *table + " partition table (existing partitions are forgotten)");
}

int cmd_partitions_add(const Options& o, const std::string& target_arg) {
    auto loaded = load_table(target_arg, o);
    if (!loaded) return fail(loaded.error(), target_arg);
    auto plan = plan_from(*loaded);
    if (!plan) return fail(plan.error(), "planning");
    const auto ss = plan->sector_size();

    PartitionSpec s;
    s.logical = o.flags.count("logical") != 0;
    s.extended = o.flags.count("extended") != 0;
    s.bootable = o.flags.count("bootable") != 0;
    if (auto v = opt(o, "offset")) {
        auto n = bytes_to_lbas(*v, ss, "--offset");
        if (!n) return usage_error(n.error().message);
        s.first_lba = *n;
    } else {
        auto fit = plan->first_fit_start(kMiB1, s.logical);
        if (!fit) return usage_error("there is no free space left on the disk");
        s.first_lba = *fit;
    }
    if (auto v = opt(o, "size"); v && *v != "max") {
        auto n = bytes_to_lbas(*v, ss, "--size");
        if (!n || *n == 0) return usage_error(n ? "--size must not be zero" : n.error().message);
        s.lba_count = *n;
    }  // else 0 = fill the free span
    if (auto v = opt(o, "type")) {
        auto t = parse_type(*v);
        if (!t) return usage_error(t.error().message);
        if (plan->type() == TableType::mbr) {
            if (!t->mbr) return usage_error("--type '" + *v + "' has no MBR equivalent");
            s.mbr_type = *t->mbr;
        } else {
            if (!t->guid) return usage_error("--type '" + *v + "' is not a GPT type; use a name or a GUID");
            s.type_guid = *t->guid;
        }
    } else if (plan->type() == TableType::gpt) {
        s.type_guid = *Guid::parse(find_type_alias("linux")->gpt_guid);
    }
    if (auto v = opt(o, "name")) s.name = *v;
    if (auto v = opt(o, "attributes")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--attributes must be a number");
        s.attributes = *n;
    }
    auto n = plan->add(s);
    if (!n) {
        std::cerr << "platter-cli: cannot add the partition: " << n.error().message << "\n";
        return kUsage;
    }
    return commit_plan(o, target_arg, *plan, loaded->table, "Add partition #" + std::to_string(*n));
}

int cmd_partitions_delete(const Options& o, const std::string& target_arg, const std::vector<std::string>& rest) {
    std::uint32_t number = 0;
    if (auto e = need_number(rest, number); !e.empty()) return usage_error(e);
    auto loaded = load_table(target_arg, o);
    if (!loaded) return fail(loaded.error(), target_arg);
    auto plan = plan_from(*loaded);
    if (!plan) return fail(plan.error(), "planning");
    if (auto s = plan->remove(number); !s) return usage_error(s.error().message);
    return commit_plan(o, target_arg, *plan, loaded->table, "Delete partition #" + std::to_string(number));
}

int cmd_partitions_set(const Options& o, const std::string& target_arg, const std::vector<std::string>& rest) {
    std::uint32_t number = 0;
    if (auto e = need_number(rest, number); !e.empty()) return usage_error(e);
    auto loaded = load_table(target_arg, o);
    if (!loaded) return fail(loaded.error(), target_arg);
    auto plan = plan_from(*loaded);
    if (!plan) return fail(plan.error(), "planning");
    bool any = false;
    auto check = [&](const Status& s) {
        if (!s) {
            std::cerr << "platter-cli: " << s.error().message << "\n";
            return false;
        }
        any = true;
        return true;
    };
    if (auto v = opt(o, "type")) {
        auto t = parse_type(*v);
        if (!t) return usage_error(t.error().message);
        if (plan->type() == TableType::mbr) {
            if (!t->mbr) return usage_error("--type '" + *v + "' has no MBR equivalent");
            if (!check(plan->set_mbr_type(number, *t->mbr))) return kUsage;
        } else {
            if (!t->guid) return usage_error("--type '" + *v + "' is not a GPT type");
            if (!check(plan->set_type_guid(number, *t->guid))) return kUsage;
        }
    }
    if (auto v = opt(o, "name"))
        if (!check(plan->set_name(number, *v))) return kUsage;
    if (auto v = opt(o, "attributes")) {
        auto n = parse_u64(*v);
        if (!n) return usage_error("--attributes must be a number");
        if (!check(plan->set_attributes(number, *n))) return kUsage;
    }
    if (o.flags.count("bootable"))
        if (!check(plan->set_bootable(number, true))) return kUsage;
    if (o.flags.count("no-bootable"))
        if (!check(plan->set_bootable(number, false))) return kUsage;
    if (!any) return usage_error("partitions set needs --type, --name, --attributes, --bootable or --no-bootable");
    return commit_plan(o, target_arg, *plan, loaded->table, "Change properties of partition #" + std::to_string(number));
}

int cmd_partitions_resize(const Options& o, const std::string& target_arg, const std::vector<std::string>& rest) {
    std::uint32_t number = 0;
    if (auto e = need_number(rest, number); !e.empty()) return usage_error(e);
    auto loaded = load_table(target_arg, o);
    if (!loaded) return fail(loaded.error(), target_arg);
    auto plan = plan_from(*loaded);
    if (!plan) return fail(plan.error(), "planning");
    const auto* p = plan->find(number);
    if (!p) return usage_error("no partition " + std::to_string(number));
    const auto ss = plan->sector_size();
    std::uint64_t first = p->first_lba, count = p->lba_count();
    if (auto v = opt(o, "offset")) {
        auto n = bytes_to_lbas(*v, ss, "--offset");
        if (!n) return usage_error(n.error().message);
        first = *n;
    }
    if (auto v = opt(o, "size")) {
        if (*v == "max") {
            count = 0;
            for (const auto& span : plan->free_spans(p->logical))
                if (first >= span.first && first < span.first + span.count) count = span.first + span.count - first;
            // The partition's own current extent counts as free for this purpose.
            count = std::max(count, p->last_lba >= first ? p->last_lba - first + 1 : 0);
            if (count == 0) return usage_error("no room to grow at that offset");
        } else {
            auto n = bytes_to_lbas(*v, ss, "--size");
            if (!n || *n == 0) return usage_error(n ? "--size must not be zero" : n.error().message);
            count = *n;
        }
    }
    if (auto s = plan->resize(number, first, first + count - 1); !s) {
        std::cerr << "platter-cli: cannot resize: " << s.error().message << "\n";
        return kUsage;
    }
    std::cerr << "note: only the partition table changes. The file system inside is NOT resized or moved.\n";
    return commit_plan(o, target_arg, *plan, loaded->table, "Change the bounds of partition #" + std::to_string(number));
}

}  // namespace platter::cli
