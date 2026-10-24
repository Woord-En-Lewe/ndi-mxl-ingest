// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ingest {
struct Rate {
    std::uint32_t numerator{60000}, denominator{1001};
    std::uint64_t index(std::uint64_t ns) const;
    std::uint64_t timestamp(std::uint64_t index) const;
};
struct Config {
    std::string source_name, extra_ips, discovery_server;
    std::string domain{"/dev/shm/mxl"};
    std::string flow_id{"c6895447-692c-4b36-93d8-b6acb50258f3"};
    std::uint32_t width{1920}, height{1080}, slots{8}, disconnect_ms{0};
    Rate rate;
    bool hold{true};
    std::optional<std::uint32_t> tai_offset;
    void validate() const;
};
Config load_config(const std::string& path);
std::uint32_t number(const std::string& text);
std::uint64_t tai_now(const Config& config);
enum class PixelFormat { rgba, rgbx, bgra, bgrx };
std::vector<std::uint8_t> normalize(std::span<const std::uint8_t> data,
    std::uint32_t width, std::uint32_t height, std::uint32_t stride, PixelFormat format);
std::size_t packed_size(std::uint32_t width, std::uint32_t height);
void pack_v210a(std::span<const std::uint8_t> rgba, std::uint32_t width,
    std::uint32_t height, std::span<std::uint8_t> destination);
struct Frame {
    std::vector<std::uint8_t> rgba;
    std::uint64_t arrival{};
};
class Mailbox {
    std::mutex mutex_;
    std::shared_ptr<const Frame> latest_;
public:
    void put(Frame frame);
    void clear();
    std::shared_ptr<const Frame> get();
};
bool visible(const Config& config, const Frame* frame, std::uint64_t now, bool connected);
class Source {
public:
    virtual ~Source() = default;
    virtual std::optional<Frame> capture() = 0;
    virtual bool connected() = 0;
};
class Sink {
public:
    virtual ~Sink() = default;
    virtual void publish(std::uint64_t index, std::span<const std::uint8_t> rgba) = 0;
};
std::unique_ptr<Source> make_ndi(const Config& config);
std::unique_ptr<Sink> make_mxl(const Config& config);
}
