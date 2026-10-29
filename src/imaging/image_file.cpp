// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/imaging/image_file.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef PLATTER_HAVE_ZSTD
#include <zstd.h>
#endif

namespace platter::imaging {

namespace fs = std::filesystem;

std::string_view compression_name(Compression c) noexcept { return c == Compression::zstd ? "zstd" : "none"; }

bool zstd_available() noexcept {
#ifdef PLATTER_HAVE_ZSTD
    return true;
#else
    return false;
#endif
}

namespace {

bool all_zero(const std::uint8_t* p, std::size_t n) {
    return std::all_of(p, p + n, [](std::uint8_t b) { return b == 0; });
}

Error io(const std::string& what, const std::string& path) {
    return Error{Errc::io_error, what + " " + path};
}

// ------------------------------------------------------------------ raw writer
class RawWriter final : public ImageWriter {
public:
    RawWriter(std::string final_path, std::string partial, const WriterOptions& o, std::ofstream f)
        : final_(std::move(final_path)), partial_(std::move(partial)), opts_(o), f_(std::move(f)) {}

    Status write(const void* data, std::size_t length) override {
        if (finished_) return Error{Errc::internal, "image already finished"};
        if (in_ + length > opts_.total_bytes) return Error{Errc::overflow, "more data than the announced image size"};
        const auto* p = static_cast<const std::uint8_t*>(data);
        if (opts_.sparse && length > 0 && all_zero(p, length)) {
            f_.seekp(static_cast<std::streamoff>(length), std::ios::cur);
        } else {
            f_.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(length));
        }
        if (!f_) return io("cannot write", partial_);
        in_ += length;
        return {};
    }

    Status finish() override {
        if (finished_) return {};
        if (in_ != opts_.total_bytes)
            return Error{Errc::internal, "image is incomplete: " + std::to_string(in_) + " of " + std::to_string(opts_.total_bytes) + " bytes"};
        f_.flush();
        if (!f_) return io("cannot flush", partial_);
        f_.close();
        std::error_code ec;
        if (opts_.sparse) {
            fs::resize_file(partial_, opts_.total_bytes, ec);  // trailing holes
            if (ec) return Error{Errc::io_error, "cannot size " + partial_ + ": " + ec.message()};
        }
        if (opts_.overwrite) fs::remove(final_, ec);
        fs::rename(partial_, final_, ec);
        if (ec) return Error{Errc::io_error, "cannot move " + partial_ + " to " + final_ + ": " + ec.message()};
        finished_ = true;
        return {};
    }

    std::uint64_t bytes_in() const noexcept override { return in_; }
    std::uint64_t bytes_out() const noexcept override { return in_; }
    const std::string& partial_path() const noexcept override { return partial_; }

private:
    std::string final_, partial_;
    WriterOptions opts_;
    std::ofstream f_;
    std::uint64_t in_ = 0;
    bool finished_ = false;
};

#ifdef PLATTER_HAVE_ZSTD
// ---------------------------------------------------------------- zstd writer
class ZstdWriter final : public ImageWriter {
public:
    ZstdWriter(std::string final_path, std::string partial, const WriterOptions& o, std::ofstream f)
        : final_(std::move(final_path)), partial_(std::move(partial)), opts_(o), f_(std::move(f)),
          ctx_(ZSTD_createCCtx()), out_(ZSTD_CStreamOutSize()) {
        ZSTD_CCtx_setParameter(ctx_, ZSTD_c_compressionLevel, o.level);
        ZSTD_CCtx_setParameter(ctx_, ZSTD_c_checksumFlag, 1);
        ZSTD_CCtx_setPledgedSrcSize(ctx_, o.total_bytes);
    }
    ~ZstdWriter() override { ZSTD_freeCCtx(ctx_); }

    Status write(const void* data, std::size_t length) override {
        if (finished_) return Error{Errc::internal, "image already finished"};
        if (in_ + length > opts_.total_bytes) return Error{Errc::overflow, "more data than the announced image size"};
        ZSTD_inBuffer in{data, length, 0};
        while (in.pos < in.size) {
            if (auto s = pump(in, ZSTD_e_continue); !s) return s;
        }
        in_ += length;
        return {};
    }

    Status finish() override {
        if (finished_) return {};
        if (in_ != opts_.total_bytes)
            return Error{Errc::internal, "image is incomplete: " + std::to_string(in_) + " of " + std::to_string(opts_.total_bytes) + " bytes"};
        ZSTD_inBuffer in{nullptr, 0, 0};
        for (;;) {
            std::size_t remaining = 0;
            if (auto s = pump(in, ZSTD_e_end, &remaining); !s) return s;
            if (remaining == 0) break;
        }
        f_.flush();
        if (!f_) return io("cannot flush", partial_);
        f_.close();
        std::error_code ec;
        if (opts_.overwrite) fs::remove(final_, ec);
        fs::rename(partial_, final_, ec);
        if (ec) return Error{Errc::io_error, "cannot move " + partial_ + " to " + final_ + ": " + ec.message()};
        finished_ = true;
        return {};
    }

    std::uint64_t bytes_in() const noexcept override { return in_; }
    std::uint64_t bytes_out() const noexcept override { return out_bytes_; }
    const std::string& partial_path() const noexcept override { return partial_; }

private:
    Status pump(ZSTD_inBuffer& in, ZSTD_EndDirective mode, std::size_t* remaining = nullptr) {
        ZSTD_outBuffer out{out_.data(), out_.size(), 0};
        const std::size_t r = ZSTD_compressStream2(ctx_, &out, &in, mode);
        if (ZSTD_isError(r)) return Error{Errc::internal, std::string("zstd: ") + ZSTD_getErrorName(r)};
        if (out.pos > 0) {
            f_.write(static_cast<const char*>(out.dst), static_cast<std::streamsize>(out.pos));
            if (!f_) return io("cannot write", partial_);
            out_bytes_ += out.pos;
        }
        if (remaining) *remaining = r;
        return {};
    }

    std::string final_, partial_;
    WriterOptions opts_;
    std::ofstream f_;
    ZSTD_CCtx* ctx_;
    std::vector<char> out_;
    std::uint64_t in_ = 0, out_bytes_ = 0;
    bool finished_ = false;
};
#endif

// ---------------------------------------------------------------- raw reader
class RawReader final : public ImageReader {
public:
    RawReader(std::ifstream f, std::uint64_t size) : f_(std::move(f)), size_(size) {}
    Compression compression() const noexcept override { return Compression::none; }
    std::optional<std::uint64_t> size() const noexcept override { return size_; }
    std::uint64_t file_bytes() const noexcept override { return size_; }
    Result<std::size_t> read(void* buffer, std::size_t length) override {
        f_.read(static_cast<char*>(buffer), static_cast<std::streamsize>(length));
        const auto n = static_cast<std::size_t>(f_.gcount());
        if (f_.bad()) return Error{Errc::io_error, "cannot read the image file"};
        if (n < length) f_.clear(std::ios::eofbit);
        return n;
    }

private:
    std::ifstream f_;
    std::uint64_t size_;
};

#ifdef PLATTER_HAVE_ZSTD
// -------------------------------------------------------------- zstd reader
class ZstdReader final : public ImageReader {
public:
    ZstdReader(std::ifstream f, std::uint64_t file_bytes, std::optional<std::uint64_t> size)
        : f_(std::move(f)), file_bytes_(file_bytes), size_(size), ctx_(ZSTD_createDCtx()), in_buf_(ZSTD_DStreamInSize()) {}
    ~ZstdReader() override { ZSTD_freeDCtx(ctx_); }

    Compression compression() const noexcept override { return Compression::zstd; }
    std::optional<std::uint64_t> size() const noexcept override { return size_; }
    std::uint64_t file_bytes() const noexcept override { return file_bytes_; }

    Result<std::size_t> read(void* buffer, std::size_t length) override {
        ZSTD_outBuffer out{buffer, length, 0};
        while (out.pos < out.size && !frame_done_) {
            if (in_.pos == in_.size) {
                f_.read(in_buf_.data(), static_cast<std::streamsize>(in_buf_.size()));
                const auto got = static_cast<std::size_t>(f_.gcount());
                if (f_.bad()) return Error{Errc::io_error, "cannot read the image file"};
                if (got == 0) return Error{Errc::corrupt_data, "the compressed image is truncated"};
                in_ = ZSTD_inBuffer{in_buf_.data(), got, 0};
            }
            const std::size_t r = ZSTD_decompressStream(ctx_, &out, &in_);
            if (ZSTD_isError(r)) return Error{Errc::corrupt_data, std::string("the compressed image is damaged: ") + ZSTD_getErrorName(r)};
            if (r == 0) frame_done_ = true;
        }
        return out.pos;
    }

private:
    std::ifstream f_;
    std::uint64_t file_bytes_;
    std::optional<std::uint64_t> size_;
    ZSTD_DCtx* ctx_;
    std::vector<char> in_buf_;
    ZSTD_inBuffer in_{nullptr, 0, 0};
    bool frame_done_ = false;
};
#endif

}  // namespace

Result<std::unique_ptr<ImageWriter>> ImageWriter::create(const std::string& path, const WriterOptions& opts) {
    if (path.empty()) return Error{Errc::invalid_argument, "no output file given"};
    if (opts.compression == Compression::zstd && !zstd_available())
        return Error{Errc::unsupported, "this build has no zstd support"};
    if (opts.compression == Compression::zstd && (opts.level < 1 || opts.level > 19))
        return Error{Errc::invalid_argument, "the zstd level must be between 1 and 19"};
    std::error_code ec;
    if (fs::exists(path, ec) && !opts.overwrite)
        return Error{Errc::invalid_argument, "the output file already exists: " + path + " (use --force to replace it)"};
    if (fs::is_directory(path, ec)) return Error{Errc::invalid_argument, path + " is a directory"};
    const std::string partial = path + ".partial";
    std::ofstream f(partial, std::ios::binary | std::ios::trunc);
    if (!f) return Error{Errc::io_error, "cannot create " + partial};
    WriterOptions o = opts;
    if (o.compression == Compression::zstd) o.sparse = false;
#ifdef PLATTER_HAVE_ZSTD
    if (o.compression == Compression::zstd) return std::unique_ptr<ImageWriter>(new ZstdWriter(path, partial, o, std::move(f)));
#endif
    return std::unique_ptr<ImageWriter>(new RawWriter(path, partial, o, std::move(f)));
}

Result<std::unique_ptr<ImageReader>> ImageReader::open(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return Error{Errc::not_found, "image file not found: " + path};
    const auto bytes = static_cast<std::uint64_t>(fs::file_size(path, ec));
    std::ifstream f(path, std::ios::binary);
    if (!f) return Error{Errc::io_error, "cannot open " + path};
    std::uint8_t head[64] = {};
    f.read(reinterpret_cast<char*>(head), sizeof head);
    const auto got = static_cast<std::size_t>(f.gcount());
    f.clear();
    f.seekg(0);
    const bool zst = got >= 4 && head[0] == 0x28 && head[1] == 0xB5 && head[2] == 0x2F && head[3] == 0xFD;
    if (!zst) return std::unique_ptr<ImageReader>(new RawReader(std::move(f), bytes));
#ifdef PLATTER_HAVE_ZSTD
    std::optional<std::uint64_t> size;
    const auto cs = ZSTD_getFrameContentSize(head, got);
    if (cs != ZSTD_CONTENTSIZE_UNKNOWN && cs != ZSTD_CONTENTSIZE_ERROR) size = cs;
    return std::unique_ptr<ImageReader>(new ZstdReader(std::move(f), bytes, size));
#else
    return Error{Errc::unsupported, path + " is zstd-compressed but this build has no zstd support"};
#endif
}

std::string sha256_sidecar_text(const std::string& hex, const std::string& image_path, Compression c) {
    std::string name = fs::path(image_path).filename().string();
    if (c == Compression::zstd && name.size() > 4 && name.compare(name.size() - 4, 4, ".zst") == 0) name.resize(name.size() - 4);
    return hex + "  " + name + "\n";
}

std::string read_sha256_sidecar(const std::string& path) {
    std::ifstream f(path);
    std::string tok;
    if (!(f >> tok)) return {};
    if (tok.size() != 64 || !std::all_of(tok.begin(), tok.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) return {};
    std::transform(tok.begin(), tok.end(), tok.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tok;
}

}  // namespace platter::imaging
