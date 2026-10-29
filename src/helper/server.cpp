// SPDX-License-Identifier: TBD
//
// The privileged side. Everything a client sends is untrusted: this file is
// the security boundary of the privilege separation, so it re-derives every
// fact it relies on (device description, capacity) instead of believing the
// client, and it only ever refuses by default.
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>

#include "platter/core/broker.hpp"
#include "platter/helper/helper.hpp"
#include "platter/platform/direct_open.hpp"
#include "platter/platform/enumerate.hpp"
#include "wire.hpp"

namespace platter::helper {
namespace {

using wire::Op;
using wire::Reader;
using wire::Writer;

constexpr std::size_t kMaxHandles = 64;

struct Entry {
    BlockDevicePtr dev;
    bool rw = false;
    std::vector<ByteSpan> grants;  // validated; read-write handles only
    std::string path;
};

struct AlignedFree {
    void operator()(void* p) const { std::free(p); }
};
using AlignedBuf = std::unique_ptr<std::uint8_t, AlignedFree>;

AlignedBuf aligned_buffer(std::size_t n) {
    void* p = nullptr;
    const std::size_t align = 4096;
    const std::size_t sz = (std::max<std::size_t>(n, 1) + align - 1) / align * align;
    if (posix_memalign(&p, align, sz) != 0) return nullptr;
    return AlignedBuf(static_cast<std::uint8_t*>(p));
}

void log(const std::string& s) { std::fprintf(stderr, "platter-helper: %s\n", s.c_str()); }

std::vector<std::uint8_t> reply_error(const Error& e) {
    Writer w;
    w.u8(1);
    wire::put_error(w, e);
    return std::move(w.data());
}

std::vector<std::uint8_t> reply_ok(Writer&& body) {
    Writer w;
    w.u8(0);
    w.bytes(body.data().data(), body.data().size());
    return std::move(w.data());
}

std::vector<std::uint8_t> reply_ok() { return reply_ok(Writer{}); }

Error refused(const std::string& why) { return Error{Errc::refused, "the privileged helper refused: " + why}; }

bool within_grants(const std::vector<ByteSpan>& grants, std::uint64_t offset, std::uint64_t length) {
    const auto end = checked_add(offset, length);
    if (!end) return false;
    return std::any_of(grants.begin(), grants.end(), [&](const ByteSpan& g) {
        const auto gend = checked_add(g.offset, g.length);
        return gend && offset >= g.offset && *end <= *gend;
    });
}

class Server {
public:
    Server(int fd, const ServerPolicy& p) : fd_(fd), policy_(p) {}

    int run() {
        for (;;) {
            auto frame = wire::read_frame(fd_);
            if (!frame) {
                if (frame.error().code == Errc::not_found) return 0;  // client went away
                log(frame.error().describe());
                return 1;
            }
            Reader r(frame->data(), frame->size());
            const auto op = static_cast<Op>(r.u8());
            if (!r.ok()) return protocol_error("empty message");
            if (op == Op::quit) {
                send(reply_ok());
                return 0;
            }
            std::vector<std::uint8_t> out;
            switch (op) {
                case Op::hello: out = on_hello(r); break;
                case Op::open: out = on_open(r); break;
                case Op::read: out = on_read(r); break;
                case Op::write: out = on_write(r); break;
                case Op::flush: out = on_simple(r, [](Entry& e) { return e.dev->flush(); }); break;
                case Op::discard: out = on_discard(r); break;
                case Op::reread: out = on_reread(r); break;
                case Op::close: out = on_close(r); break;
                default: return protocol_error("unknown request");
            }
            if (!send(std::move(out))) return 1;
        }
    }

private:
    int protocol_error(const std::string& what) {
        log("protocol error: " + what);
        send(reply_error(Error{Errc::corrupt_data, what}));
        return 1;
    }
    bool send(std::vector<std::uint8_t> v) {
        if (auto s = wire::write_frame(fd_, v); !s) {
            log(s.error().describe());
            return false;
        }
        return true;
    }

    std::vector<std::uint8_t> on_hello(Reader& r) {
        const auto proto = r.u32();
        if (!r.ok() || proto != wire::kProtocol)
            return reply_error(Error{Errc::unsupported, "helper protocol version mismatch (helper speaks " +
                                                            std::to_string(wire::kProtocol) + ")"});
        Writer w;
        w.u32(wire::kProtocol);
        w.u32(static_cast<std::uint32_t>(::geteuid()));
        w.u32(static_cast<std::uint32_t>(::getpid()));
        return reply_ok(std::move(w));
    }

    Entry* entry(Reader& r, Error& err) {
        const auto h = r.u32();
        if (!r.ok()) {
            err = Error{Errc::corrupt_data, "truncated request"};
            return nullptr;
        }
        auto it = entries_.find(h);
        if (it == entries_.end()) {
            err = Error{Errc::invalid_argument, "unknown device handle"};
            return nullptr;
        }
        return &it->second;
    }

    template <class F>
    std::vector<std::uint8_t> on_simple(Reader& r, F&& f) {
        Error err;
        Entry* e = entry(r, err);
        if (!e) return reply_error(err);
        if (auto s = f(*e); !s) return reply_error(s.error());
        return reply_ok();
    }

    std::vector<std::uint8_t> on_open(Reader& r) {
        const auto path = r.str();
        const auto mode = r.u8();
        OpenOptions oo;
        oo.mode = mode == 1 ? OpenMode::read_write : OpenMode::read_only;
        oo.direct_io = r.u8() != 0;
        oo.exclusive = r.u8() != 0;
        std::vector<ByteSpan> asked;
        for (auto n = r.u32(); n > 0 && r.ok(); --n) {
            ByteSpan s;
            s.offset = r.u64();
            s.length = r.u64();
            asked.push_back(s);
            if (asked.size() > 4096) return reply_error(refused("too many write ranges"));
        }
        if (!r.ok() || mode > 1) return reply_error(Error{Errc::corrupt_data, "malformed open request"});
        if (entries_.size() >= kMaxHandles) return reply_error(refused("too many open devices"));
        const bool rw = oo.mode == OpenMode::read_write;

        if (rw && policy_.read_only) return reply_error(refused("this helper was started read-only"));
        if (rw && asked.empty()) return reply_error(refused("a read-write open needs the confirmed byte ranges"));
        for (const auto& s : asked)
            if (s.length == 0 || !checked_add(s.offset, s.length))
                return reply_error(refused("invalid write range"));

        std::error_code ec;
        const auto canon = std::filesystem::weakly_canonical(path, ec);
        if (ec || !canon.is_absolute()) return reply_error(Error{Errc::not_found, "cannot resolve '" + path + "'"});
        struct stat st {};
        if (::stat(canon.c_str(), &st) != 0)
            return reply_error(Error{Errc::not_found, "cannot stat " + canon.string() + ": " + std::strerror(errno), errno});

        Result<BlockDevicePtr> dev = Error{Errc::internal, "unreachable"};
        if (S_ISBLK(st.st_mode)) {
            if (canon.parent_path() != "/dev") return reply_error(refused("block devices are only served from /dev"));
            auto info = describe_device(canon.string());
            if (!info) return reply_error(info.error());
            if (rw) {
                if (info->in_use())
                    return reply_error(refused(canon.string() + " is in use (mounted, held, or the system disk); refusing a read-write open"));
                if (info->read_only == Tri::yes) return reply_error(Error{Errc::read_only, canon.string() + " is read-only"});
            }
            dev = open_device_direct(canon.string(), oo);
        } else if (S_ISREG(st.st_mode) && policy_.allow_regular_files) {
            dev = open_image(canon.string(), oo, 512);
        } else {
            return reply_error(refused(canon.string() + " is not a whole-disk block device"));
        }
        if (!dev) return reply_error(dev.error());

        Entry e;
        e.rw = rw;
        e.path = canon.string();
        if (rw) {
            const auto cap = (*dev)->capacity();
            for (const auto& s : asked) {
                const auto end = checked_add(s.offset, s.length);
                if (!end || *end > cap) return reply_error(refused("a write range lies beyond the end of the device"));
            }
            e.grants = asked;
        }
        e.dev = std::move(dev).value();
        const auto handle = next_++;
        Writer w;
        w.u32(handle);
        w.u8(rw ? 1 : 0);
        w.u32(e.dev->required_alignment());
        wire::put_info(w, e.dev->info());
        if (rw) {
            std::uint64_t total = 0;
            for (const auto& s : e.grants) total += s.length;
            log("opened " + e.path + " READ-WRITE, " + std::to_string(e.grants.size()) + " granted range(s), " +
                std::to_string(total) + " bytes");
        }
        entries_.emplace(handle, std::move(e));
        return reply_ok(std::move(w));
    }

    std::vector<std::uint8_t> on_read(Reader& r) {
        Error err;
        Entry* e = entry(r, err);
        if (!e) return reply_error(err);
        const auto offset = r.u64();
        const auto length = r.u32();
        if (!r.ok() || length > wire::kMaxIo) return reply_error(refused("bad read request"));
        auto buf = aligned_buffer(length);
        if (!buf) return reply_error(Error{Errc::internal, "out of memory"});
        auto res = e->dev->read(offset, buf.get(), length);
        if (!res) return reply_error(res.error());
        Writer w;
        w.u32(static_cast<std::uint32_t>(res->bytes));
        w.u64(res->duration_ns);
        w.bytes(buf.get(), res->bytes);
        return reply_ok(std::move(w));
    }

    std::vector<std::uint8_t> on_write(Reader& r) {
        Error err;
        Entry* e = entry(r, err);
        if (!e) return reply_error(err);
        const auto offset = r.u64();
        const auto length = r.u32();
        const auto* data = r.bytes(length);
        if (!r.ok() || length > wire::kMaxIo) return reply_error(refused("bad write request"));
        if (!e->rw) return reply_error(Error{Errc::read_only, "device was opened read-only"});
        if (!within_grants(e->grants, offset, length)) {
            log("REFUSED write of " + std::to_string(length) + " bytes at " + std::to_string(offset) + " on " + e->path +
                ": outside the confirmed ranges");
            return reply_error(refused("write outside the confirmed ranges"));
        }
        auto buf = aligned_buffer(length);
        if (!buf) return reply_error(Error{Errc::internal, "out of memory"});
        if (length) std::memcpy(buf.get(), data, length);
        auto res = e->dev->write(offset, buf.get(), length);
        if (!res) return reply_error(res.error());
        Writer w;
        w.u32(static_cast<std::uint32_t>(res->bytes));
        w.u64(res->duration_ns);
        return reply_ok(std::move(w));
    }

    std::vector<std::uint8_t> on_discard(Reader& r) {
        Error err;
        Entry* e = entry(r, err);
        if (!e) return reply_error(err);
        const auto offset = r.u64();
        const auto length = r.u64();
        if (!r.ok()) return reply_error(Error{Errc::corrupt_data, "truncated request"});
        if (!e->rw) return reply_error(Error{Errc::read_only, "device was opened read-only"});
        if (!within_grants(e->grants, offset, length)) return reply_error(refused("discard outside the confirmed ranges"));
        if (auto s = e->dev->discard(offset, length); !s) return reply_error(s.error());
        return reply_ok();
    }

    std::vector<std::uint8_t> on_reread(Reader& r) {
        return on_simple(r, [](Entry& e) -> Status {
            if (!e.rw) return Error{Errc::read_only, "device was opened read-only"};
            return e.dev->reread_partitions();
        });
    }

    std::vector<std::uint8_t> on_close(Reader& r) {
        const auto h = r.u32();
        if (!r.ok()) return reply_error(Error{Errc::corrupt_data, "truncated request"});
        entries_.erase(h);
        return reply_ok();
    }

    int fd_;
    ServerPolicy policy_;
    std::map<std::uint32_t, Entry> entries_;
    std::uint32_t next_ = 1;
};

}  // namespace

int serve(int fd, const ServerPolicy& policy) {
    Server s(fd, policy);
    return s.run();
}

}  // namespace platter::helper
