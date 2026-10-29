// SPDX-License-Identifier: TBD
#include "help_dialog.hpp"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace platter::gui {

namespace {

struct Topic {
    const char* title;
    const char* html;
};

const Topic kTopics[] = {
    {QT_TRANSLATE_NOOP("HelpDialog", "Getting started"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Getting started</h2>"
     "<p>platter lists the storage devices it can see on the left of the INFO tab. Pick one and use the tabs to look at it. "
     "Everything on the INFO, SMART, SURFACE (read scan), BENCHMARK (read), PARTITIONS (view) and HEX tabs only <b>reads</b>.</p>"
     "<p>You can also open a raw disk image (<i>File &gt; Open Disk Image</i>) and use almost every feature on it with no risk to real hardware. "
     "That is the best way to try things out.</p>"
     "<p>Reading raw sectors of a physical disk needs administrator rights. On Linux you can either start platter as root or use "
     "<i>File &gt; Use Privileged Helper</i>, which asks for your password and opens the disks through a small helper process while the "
     "rest of the program stays unprivileged. On Windows start platter as Administrator.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Safety model"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Safety model</h2>"
     "<p>Anything that <b>writes</b> to a device (erase, write scans, write benchmarks, qualification, partition edits, formatting, "
     "restoring an image, cloning) follows the same steps:</p>"
     "<ol><li>A <b>preview</b> shows exactly which device and which sector range will be changed, and lists every reason it would be refused "
     "(mounted, system disk, held by LVM/RAID, read-only...).</li>"
     "<li>You <b>type a confirmation</b> text. It is not a button you can click through by reflex.</li>"
     "<li>platter looks at the device again, from scratch. If anything differs from the preview (a different disk took the same name, "
     "a partition was mounted in the meantime) it stops.</li>"
     "<li>Writes can only land inside the range you confirmed.</li></ol>"
     "<p>The system disk and anything mounted are blocked. A disk that is <i>unplugged</i> is the only completely safe disk: "
     "if you have a drive you care about, disconnect it before running destructive operations on another one.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Surface scan"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Surface scan</h2>"
     "<p>Reads the disk block by block and colours each block by how long it took. Slow or unreadable blocks are early signs of a failing "
     "disk. Read scans are safe. Write scan modes destroy the data in the chosen range and ask for confirmation.</p>"
     "<p>Block size, order (forward, reverse, random, sampled), retries and the latency thresholds are on the tab. Scans can be paused, resumed "
     "and saved as JSON, CSV or text. A scan of a disk through the OS cache measures the cache, not the disk: enable direct I/O to bypass it.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Benchmark"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Benchmark</h2>"
     "<p>Sequential, random, access-time, full-disk speed curve, block-size and queue-depth tests. Results include latency percentiles "
     "and explicit warnings when a number may come from a cache or a virtual disk instead of the device.</p>"
     "<p>Write benchmarks need an explicit region and the usual typed confirmation. Queue-depth tests are not meaningful through the privileged helper.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "SMART"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>SMART</h2>"
     "<p>Health data comes from <i>smartmontools</i> (<code>smartctl</code> must be installed). platter shows the raw attributes and a deliberately "
     "conservative assessment: GOOD, WARNING, CRITICAL or UNKNOWN, with every observation that led to it. A passing SMART status does not "
     "guarantee a healthy disk.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Hex viewer"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Hex viewer</h2>"
     "<p>Shows sectors read-only, decodes what it recognises there (MBR, GPT, FAT, exFAT, NTFS and ext structures) and searches a range for bytes, "
     "ASCII or UTF-16 text. Nothing is read until you press <b>Go</b>; selecting a device never reads it.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Erase"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Erase</h2>"
     "<p>Overwrites a whole device or a range with zeros, ones, random data, multiple passes, DoD-style patterns or a custom pattern, then reads "
     "it back to verify. Reports say honestly what was and was not verified.</p>"
     "<p><b>Limits:</b> overwriting cannot reach sectors the drive has remapped, over-provisioned flash, or hidden areas. Hardware secure erase "
     "commands (ATA Secure Erase, NVMe format/sanitize) are intentionally not implemented.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Qualification"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Drive qualification</h2>"
     "<p>A burn-in test for a drive you are about to trust: several full write passes with read-back, a final read pass, random reads, and a SMART "
     "before/after comparison, ending in PASS / PASS WITH WARNINGS / FAIL. It destroys all data on the drive. The thresholds are engineering judgement, "
     "not a standard: read the listed observations, not just the verdict.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Partitions and formatting"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Partitions and formatting</h2>"
     "<p>View GPT/MBR tables (with CRC and overlap checks) on the PARTITIONS tab. <i>Device &gt; Edit Partitions</i> stages changes in memory and "
     "writes them only after the typed confirmation. Quick formatting to FAT32 and exFAT is built in; NTFS and ext4 are not.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "Imaging and cloning"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>Clone / Image</h2>"
     "<p>Create an image of a device (raw or zstd, with a SHA-256 recorded), restore an image to a device, clone one device onto another, or "
     "verify an image against its checksum or a device. With <i>recover</i> enabled, unreadable sectors are zero-filled, counted and listed, "
     "and the result is flagged so a damaged copy never looks perfect.</p>")},
    {QT_TRANSLATE_NOOP("HelpDialog", "History"),
     QT_TRANSLATE_NOOP("HelpDialog",
     "<h2>History</h2>"
     "<p>Finished benchmarks, scans, SMART queries, erases, imaging sessions and qualifications are saved to a local SQLite database "
     "(<i>Tools &gt; Operation History</i>), shared with <code>platter-cli</code>. Serial numbers are never stored. Turn recording off in "
     "Preferences. Deleting a history record never touches a device.</p>")},
};

}  // namespace

HelpDialog::HelpDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("platter Help"));
    resize(820, 560);
    auto* col = new QVBoxLayout(this);
    auto* split = new QSplitter(this);
    auto* list = new QListWidget(split);
    auto* text = new QTextBrowser(split);
    text->setOpenLinks(false);
    for (const auto& t : kTopics) list->addItem(QCoreApplication::translate("HelpDialog", t.title));
    split->addWidget(list);
    split->addWidget(text);
    split->setStretchFactor(1, 1);
    split->setSizes({200, 620});
    col->addWidget(split, 1);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    auto* close = new QPushButton(tr("&Close"), this);
    row->addWidget(close);
    col->addLayout(row);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(list, &QListWidget::currentRowChanged, this, [text](int r) {
        if (r >= 0 && r < static_cast<int>(sizeof kTopics / sizeof kTopics[0])) text->setHtml(QCoreApplication::translate("HelpDialog", kTopics[r].html));
    });
    list->setCurrentRow(0);
}

}  // namespace platter::gui
