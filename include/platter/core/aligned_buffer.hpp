// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Heap buffer whose data pointer is aligned for direct (unbuffered) I/O.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace platter {

class AlignedBuffer {
public:
    AlignedBuffer() = default;
    AlignedBuffer(std::size_t size, std::size_t align = 4096)
        : raw_(new std::byte[size + align]), size_(size) {
        const auto addr = reinterpret_cast<std::uintptr_t>(raw_.get());
        const auto aligned = (addr + align - 1) / align * align;
        data_ = raw_.get() + (aligned - addr);
    }
    std::byte* data() noexcept { return data_; }
    const std::byte* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }
    void fill(std::uint8_t v) noexcept { if (data_) std::memset(data_, v, size_); }

private:
    std::unique_ptr<std::byte[]> raw_;
    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace platter
