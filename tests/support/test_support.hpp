// SPDX-License-Identifier: TBD
//
// Test helpers: disposable temp dirs and synthetic disk images. Everything
// here writes ONLY to files under a fresh temp directory.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace platter::test {

class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

// Creates a sparse file of `size` bytes (zero-filled on read).
void make_sparse_file(const std::filesystem::path& p, std::uint64_t size);
void write_at(const std::filesystem::path& p, std::uint64_t offset, const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> read_at(const std::filesystem::path& p, std::uint64_t offset, std::size_t len);

struct GptPart {
    std::string type_guid;
    std::uint64_t first_lba;
    std::uint64_t last_lba;
    std::string name;  // ASCII only, for simplicity
};

// Writes a protective MBR, primary GPT (LBA 1, entries at LBA 2) and backup
// GPT (entries before the last LBA, header at the last LBA). 128 entries of
// 128 bytes, like every mainstream tool.
void write_gpt(const std::filesystem::path& image, std::uint64_t total_lbas, std::uint32_t ss,
               const std::vector<GptPart>& parts, const std::string& disk_guid);

struct MbrPart {
    std::uint8_t type;
    std::uint32_t first_lba;
    std::uint32_t count;
    bool bootable = false;
};

// Writes an MBR into sector 0. If `logicals` is non-empty, the first extended
// entry in `primaries` gets an EBR chain with those logicals (first_lba of a
// logical is absolute; each EBR is placed 2048 sectors before its logical).
void write_mbr(const std::filesystem::path& image, std::uint32_t ss, const std::vector<MbrPart>& primaries,
               const std::vector<MbrPart>& logicals = {}, std::uint32_t disk_signature = 0xC0FFEE11);

}  // namespace platter::test
