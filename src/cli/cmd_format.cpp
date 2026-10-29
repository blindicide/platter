// SPDX-License-Identifier: TBD
//
// platter-cli format <device> [--partition N] --fs fat32|exfat: native quick
// format of a partition (or of a whole partition-less device / image).
#include "commands.hpp"
#include "platter/mkfs/mkfs.hpp"
#include "platter/partitions/partition_table.hpp"

namespace platter::cli {

int cmd_format(const Options& o, const std::string& target_arg) {
    const auto* fs = opt(o, "fs");
    if (!fs || (*fs != "fat32" && *fs != "exfat")) return usage_error("format needs --fs fat32|exfat");
    mkfs::FormatConfig cfg;
    cfg.type = *fs == "fat32" ? mkfs::FsType::fat32 : mkfs::FsType::exfat;
    if (auto v = opt(o, "label")) cfg.label = *v;
    if (auto v = opt(o, "cluster-size")) {
        auto n = parse_size(*v);
        if (!n || *n > UINT32_MAX) return usage_error("--cluster-size must be a size such as 4K or 32K");
        cfg.cluster_bytes = static_cast<std::uint32_t>(*n);
    }

    auto dev = open_target_read_only(target_arg, o);
    if (!dev) return fail(dev.error(), target_arg);
    auto table = partitions::read_partition_table(**dev);
    if (!table) return fail(table.error(), "reading the partition table");

    LbaRange range{0, (*dev)->info().total_lbas()};
    std::string what = "the whole device";
    if (auto p = opt(o, "partition")) {
        auto n = parse_u64(*p);
        if (!n) return usage_error("--partition must be a partition number");
        const partitions::Partition* found = nullptr;
        for (const auto& part : table->partitions)
            if (part.number == *n) found = &part;
        if (!found) return usage_error("the device has no partition " + *p);
        if (found->extended_container) return usage_error("an extended partition holds other partitions; format a logical one");
        range = {found->first_lba, found->lba_count()};
        what = "partition #" + *p;
    } else if (table->type != partitions::TableType::none) {
        return usage_error("the device has a " + std::string(partitions::table_type_name(table->type)) +
                           " partition table: pass --partition N (to format the whole device anyway, create a new table or erase the first sectors first)");
    }
    dev->reset();

    int code = kOk;
    auto target = authorize_from_cli(o, target_arg, safety::Kind::filesystem_create,
                                     "Format " + what + " as " + std::string(mkfs::fs_name(cfg.type)) +
                                         (cfg.label.empty() ? "" : " (label " + cfg.label + ")"),
                                     {range}, "formatting", code);
    if (!target) return code;

    auto op = std::make_unique<mkfs::Format>(std::move(*target), cfg);
    if (auto v = op->validate(); !v) return fail(v.error(), "invalid format request");
    if (auto s = op->start(); !s) return fail(s.error(), "starting format");
    run_with_progress(*op, o, [](const OperationProgress& p) { return standard_progress_line(p); });
    const auto r = op->result();
    Out out(o);
    out.text() << mkfs::format_to_text(r);
    if (r.state == OperationState::cancelled) return out.flush(kCancelled);
    if (r.state != OperationState::completed) {
        if (!o.quiet) std::cerr << "platter-cli: format failed: " << r.failure << "\n";
        return out.flush(kIoError);
    }
    return out.flush(kOk);
}

}  // namespace platter::cli
