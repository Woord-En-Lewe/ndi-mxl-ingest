// SPDX-License-Identifier: MIT
#include "ingest.hpp"
#include <stdexcept>
#include <cstdlib>
#include <chrono>
#ifdef INGEST_WITH_NDI
#include <Processing.NDI.Lib.h>
#endif

namespace ingest {
#ifdef INGEST_WITH_NDI
namespace {
class Ndi final : public Source {
    Config config_;
    NDIlib_find_instance_t finder_{};
    NDIlib_recv_instance_t receiver_{};
    bool initialized_{};
    bool selected_{};
    std::chrono::steady_clock::time_point next_discovery_{};
    void cleanup() noexcept {
        if (receiver_) NDIlib_recv_destroy(receiver_);
        if (finder_) NDIlib_find_destroy(finder_);
        if (initialized_) NDIlib_destroy();
    }
public:
    explicit Ndi(const Config& c) : config_(c) {
        if (c.source_name.empty()) throw std::invalid_argument("source_name is required for NDI");
        // Must be set before SDK initialization. No process-global changes after startup.
        if (!c.discovery_server.empty() && setenv("NDI_DISCOVERY_SERVER", c.discovery_server.c_str(), 1) != 0)
            throw std::runtime_error("cannot set NDI_DISCOVERY_SERVER");
        if (!(initialized_ = NDIlib_initialize())) throw std::runtime_error("NDI initialization failed");
        try {
            NDIlib_find_create_t find{};
            find.show_local_sources = true;
            find.p_extra_ips = c.extra_ips.empty() ? nullptr : config_.extra_ips.c_str();
            finder_ = NDIlib_find_create_v2(&find);
            NDIlib_recv_create_v3_t recv{};
            recv.color_format = NDIlib_recv_color_format_RGBX_RGBA;
            recv.bandwidth = NDIlib_recv_bandwidth_highest;
            recv.allow_video_fields = false;
            recv.p_ndi_recv_name = "NDI MXL graphics ingest";
            receiver_ = NDIlib_recv_create_v3(&recv);
            if (!finder_ || !receiver_) throw std::runtime_error("NDI finder/receiver creation failed");
        } catch (...) { cleanup(); throw; }
    }
    ~Ndi() override { cleanup(); }
    bool connected() override { return selected_ && NDIlib_recv_get_no_connections(receiver_) > 0; }
    std::optional<Frame> capture() override {
        const auto now = std::chrono::steady_clock::now();
        if (!connected() && now >= next_discovery_) {
            next_discovery_ = now + std::chrono::seconds(2);
            std::uint32_t count{};
            const auto* sources = NDIlib_find_get_current_sources(finder_, &count);
            for (std::uint32_t i=0; i<count; ++i) {
                if (sources[i].p_ndi_name && config_.source_name == sources[i].p_ndi_name) {
                    NDIlib_recv_connect(receiver_, &sources[i]); selected_ = true; break;
                }
            }
        }
        NDIlib_video_frame_v2_t video{};
        const auto type = NDIlib_recv_capture_v2(receiver_, &video, nullptr, nullptr, 20);
        if (type == NDIlib_frame_type_error) throw std::runtime_error("NDI receiver error");
        if (type != NDIlib_frame_type_video) return {};
        struct Guard {
            NDIlib_recv_instance_t receiver;
            NDIlib_video_frame_v2_t* video;
            ~Guard() { NDIlib_recv_free_video_v2(receiver, video); }
        } guard{receiver_, &video};
        const auto arrival = tai_now(config_);
        if (video.xres != static_cast<int>(config_.width) || video.yres != static_cast<int>(config_.height) ||
            video.frame_format_type != NDIlib_frame_format_type_progressive || video.line_stride_in_bytes < video.xres*4 || !video.p_data)
            throw std::runtime_error("unsupported NDI geometry/field mode; configure a matching progressive raster");
        PixelFormat format;
        switch (video.FourCC) {
            case NDIlib_FourCC_type_RGBA: format=PixelFormat::rgba; break;
            case NDIlib_FourCC_type_RGBX: format=PixelFormat::rgbx; break;
            case NDIlib_FourCC_type_BGRA: format=PixelFormat::bgra; break;
            case NDIlib_FourCC_type_BGRX: format=PixelFormat::bgrx; break;
            default: throw std::runtime_error("NDI did not return requested 8-bit RGB raster");
        }
        const auto stride = static_cast<std::uint32_t>(video.line_stride_in_bytes);
        return Frame{normalize({video.p_data, std::size_t(stride)*config_.height},
            config_.width, config_.height, stride, format), arrival};
    }
};
}
#endif
std::unique_ptr<Source> make_ndi(const Config& c) {
#ifdef INGEST_WITH_NDI
    return std::make_unique<Ndi>(c);
#else
    (void)c; throw std::runtime_error("NDI unavailable: build with INGEST_WITH_NDI=ON");
#endif
}
}
