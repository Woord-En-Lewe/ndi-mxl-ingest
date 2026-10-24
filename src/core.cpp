// SPDX-License-Identifier: MIT
#include "ingest.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <time.h>

namespace ingest {
using Wide = unsigned __int128;
namespace {
std::uint64_t narrow(Wide value) {
    if (value > std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("timestamp overflow");
    return static_cast<std::uint64_t>(value);
}
void word(std::span<std::uint8_t> out, std::size_t offset, std::uint32_t a,
          std::uint32_t b, std::uint32_t c) {
    const auto value = a | (b << 10) | (c << 20);
    for (unsigned i = 0; i < 4; ++i) out[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
}
std::uint64_t Rate::index(std::uint64_t ns) const {
    if (!numerator || !denominator) throw std::invalid_argument("invalid rate");
    return narrow(Wide(ns) * numerator / (Wide(1000000000) * denominator));
}
std::uint64_t Rate::timestamp(std::uint64_t i) const {
    if (!numerator || !denominator) throw std::invalid_argument("invalid rate");
    // Ceiling ensures index(timestamp(i)) == i, including fractional-nanosecond ticks.
    return narrow((Wide(i) * 1000000000 * denominator + numerator - 1) / numerator);
}
std::uint64_t tai_now(const Config& c) {
    timespec ts{};
#if defined(__linux__)
    if (!c.tai_offset) {
        if (clock_gettime(CLOCK_TAI, &ts) != 0) throw std::runtime_error("CLOCK_TAI unavailable");
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000 + static_cast<std::uint64_t>(ts.tv_nsec);
    }
#endif
    if (!c.tai_offset) throw std::runtime_error("this platform requires explicit tai_offset (development only)");
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) throw std::runtime_error("CLOCK_REALTIME unavailable");
    return (static_cast<std::uint64_t>(ts.tv_sec) + *c.tai_offset) * 1000000000 + static_cast<std::uint64_t>(ts.tv_nsec);
}
std::vector<std::uint8_t> normalize(std::span<const std::uint8_t> data, std::uint32_t w,
    std::uint32_t h, std::uint32_t stride, PixelFormat fmt) {
    if (!w || !h || w > 8192 || h > 8192 || stride < w * 4 || data.size() < std::size_t(stride) * h)
        throw std::invalid_argument("invalid frame geometry or truncated raster");
    std::vector<std::uint8_t> out(std::size_t(w) * h * 4);
    const bool swap = fmt == PixelFormat::bgra || fmt == PixelFormat::bgrx;
    const bool alpha = fmt == PixelFormat::bgra || fmt == PixelFormat::rgba;
    for (std::size_t y = 0; y < h; ++y) for (std::size_t x = 0; x < w; ++x) {
        auto* dst = out.data() + (y * w + x) * 4;
        const auto* src = data.data() + y * stride + x * 4;
        dst[0] = src[swap ? 2 : 0]; dst[1] = src[1]; dst[2] = src[swap ? 0 : 2];
        dst[3] = alpha ? src[3] : 255;
    }
    return out;
}
std::size_t packed_size(std::uint32_t w, std::uint32_t h) {
    if (!w || w > 8192 || w % 2 || !h || h > 8192) throw std::invalid_argument("invalid packed dimensions");
    return (std::size_t((w + 47) / 48) * 128 + std::size_t((w + 2) / 3) * 4) * h;
}
void pack_v210a(std::span<const std::uint8_t> rgba, std::uint32_t w, std::uint32_t h,
    std::span<std::uint8_t> out) {
    if (out.size() != packed_size(w, h) || rgba.size() != std::size_t(w) * h * 4)
        throw std::invalid_argument("packed raster size mismatch");
    std::fill(out.begin(), out.end(), 0);
    const std::size_t fill_stride = std::size_t((w + 47) / 48) * 128;
    const std::size_t key_stride = std::size_t((w + 2) / 3) * 4;
    const auto clamp = [](double v, int lo, int hi) { return static_cast<std::uint32_t>(std::clamp(v + 0.5, double(lo), double(hi))); };
    for (std::size_t row = 0; row < h; ++row) {
        const auto sample = [&](std::size_t x) {
            if (x >= w) return std::array<std::uint32_t, 4>{64, 512, 512, 0};
            const auto* p = rgba.data() + (row * w + x) * 4;
            const double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
            const double y = .2126 * r + .7152 * g + .0722 * b;
            return std::array<std::uint32_t, 4>{clamp(64 + 876 * y, 64, 940),
                clamp(512 + 896 * (b - y) / 1.8556, 64, 960),
                clamp(512 + 896 * (r - y) / 1.5748, 64, 960),
                clamp(64 + 876 * p[3] / 255.0, 64, 940)};
        };
        for (std::size_t x = 0; x < w; x += 6) {
            const auto a = sample(x), b = sample(x+1), c = sample(x+2), d = sample(x+3), e = sample(x+4), f = sample(x+5);
            const auto off = row * fill_stride + (x / 6) * 16;
            word(out, off, (a[1]+b[1]+1)/2, a[0], (a[2]+b[2]+1)/2);
            word(out, off+4, b[0], (c[1]+d[1]+1)/2, c[0]);
            word(out, off+8, (c[2]+d[2]+1)/2, d[0], (e[1]+f[1]+1)/2);
            word(out, off+12, e[0], (e[2]+f[2]+1)/2, f[0]);
        }
        for (std::size_t x = 0; x < w; x += 3)
            word(out, fill_stride*h + row*key_stride + (x/3)*4, sample(x)[3], sample(x+1)[3], sample(x+2)[3]);
    }
}
void Mailbox::put(Frame frame) { auto ptr = std::make_shared<Frame>(std::move(frame)); std::lock_guard lock(mutex_); latest_ = std::move(ptr); }
void Mailbox::clear() { std::lock_guard lock(mutex_); latest_.reset(); }
std::shared_ptr<const Frame> Mailbox::get() { std::lock_guard lock(mutex_); return latest_; }
bool visible(const Config& c, const Frame* f, std::uint64_t now, bool connected) {
    if (!f || !connected || now < f->arrival) return false;
    if (c.disconnect_ms && now - f->arrival >= std::uint64_t(c.disconnect_ms)*1000000) return false;
    return c.hold || now - f->arrival <= c.rate.timestamp(1);
}
}
