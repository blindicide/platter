// SPDX-License-Identifier: TBD
//
// Disk image files: raw (byte for byte) or a single zstd frame. Images are
// written to "<name>.partial" and renamed only when complete, so an
// interrupted run can never be mistaken for a finished image.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "platter/core/error.hpp"

namespace platter::imaging {

enum class Compression : std::uint8_t { none, zstd };
std::string_view compression_name(Compression c) noexcept;
// False when this build was made without zstd.
bool zstd_available() noexcept;

struct WriterOptions {
    Compression compression = Compression::none;
    int level = 3;                  // zstd 1..19
    bool sparse = false;            // raw only: all-zero blocks become holes
    bool overwrite = false;         // replace an existing output file
    std::uint64_t total_bytes = 0;  // exact number of bytes that will be written
};

class ImageWriter {
public:
    static Result<std::unique_ptr<ImageWriter>> create(const std::string& path, const WriterOptions& opts);
    virtual ~ImageWriter() = default;

    // Appends data; blocks must arrive in order and add up to total_bytes.
    virtual Status write(const void* data, std::size_t length) = 0;
    // Completes the file, flushes it to disk and renames it into place.
    virtual Status finish() = 0;

    virtual std::uint64_t bytes_in() const noexcept = 0;
    virtual std::uint64_t bytes_out() const noexcept = 0;  // size of the file so far
    virtual const std::string& partial_path() const noexcept = 0;
};

class ImageReader {
public:
    static Result<std::unique_ptr<ImageReader>> open(const std::string& path);
    virtual ~ImageReader() = default;

    virtual Compression compression() const noexcept = 0;
    // Uncompressed size: exact for raw files and for zstd frames that record it.
    virtual std::optional<std::uint64_t> size() const noexcept = 0;
    virtual std::uint64_t file_bytes() const noexcept = 0;
    // Fills `buffer` completely unless the end of the image is reached; returns
    // the number of bytes delivered (0 = end).
    virtual Result<std::size_t> read(void* buffer, std::size_t length) = 0;
};

// "<hex>  <name>\n" as sha256sum prints it.
std::string sha256_sidecar_text(const std::string& hex, const std::string& image_path, Compression c);
// Reads the first hex digest out of a sidecar file ("" when unreadable).
std::string read_sha256_sidecar(const std::string& path);

}  // namespace platter::imaging
