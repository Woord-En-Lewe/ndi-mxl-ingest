// SPDX-License-Identifier: MIT
#include "ingest.hpp"
#include <stdexcept>
#include <cstring>
#ifdef INGEST_WITH_MXL
#include <mxl/flow.h>
#endif
namespace ingest {
#ifdef INGEST_WITH_MXL
namespace {
void check(mxlStatus status, const char* op) {
    if (status != MXL_STATUS_OK) throw std::runtime_error(std::string(op)+": MXL status "+std::to_string(status));
}
class Mxl final : public Sink {
    Config c_;
    mxlInstance instance_{};
    mxlFlowWriter writer_{};
    void cleanup() noexcept {
        if (writer_) mxlReleaseFlowWriter(instance_, writer_);
        if (instance_) mxlDestroyInstance(instance_);
    }
public:
    explicit Mxl(const Config& c) : c_(c) {
        mxlVersionType version{};
        check(mxlGetVersion(&version), "version");
        if (version.major!=1 || version.minor!=1) throw std::runtime_error("requires MXL 1.1.x");
        instance_=mxlCreateInstance(c.domain.c_str(), nullptr);
        if (!instance_) throw std::runtime_error("cannot open MXL domain");
        try {
            const auto w=std::to_string(c.width), h=std::to_string(c.height);
            const std::string def=R"({"id":")"+c.flow_id+R"(","format":"urn:x-nmos:format:video","media_type":"video/v210a",
                "label":"NDI Graphics","description":"NDI retimed graphics output",
                "tags":{"urn:x-nmos:tag:grouphint/v1.0":["NDI )"+c.flow_id+R"(:Video"]},"parents":[],
                "grain_rate":{"numerator":)"+std::to_string(c.rate.numerator)+R"(,"denominator":)"+std::to_string(c.rate.denominator)+R"(},
                "frame_width":)"+w+R"(,"frame_height":)"+h+R"(,"interlace_mode":"progressive","colorspace":"BT709",
                "components":[{"name":"Y","width":)"+w+R"(,"height":)"+h+R"(,"bit_depth":10},
                {"name":"Cb","width":)"+std::to_string(c.width/2)+R"(,"height":)"+h+R"(,"bit_depth":10},
                {"name":"Cr","width":)"+std::to_string(c.width/2)+R"(,"height":)"+h+R"(,"bit_depth":10}]})";
            bool created{};
            mxlFlowConfigInfo info{};
            check(mxlCreateFlowWriter(instance_, def.c_str(), nullptr, &writer_, &info, &created), "create writer");
            if (!created) {
                std::size_t size{};
                const auto status=mxlGetFlowDef(instance_, c.flow_id.c_str(), nullptr, &size);
                if (status!=MXL_ERR_INVALID_ARG || !size || size>65536) throw std::runtime_error("cannot inspect existing flow");
                std::vector<char> existing(size);
                check(mxlGetFlowDef(instance_, c.flow_id.c_str(), existing.data(), &size), "flow definition");
                if (size!=def.size()+1 || std::memcmp(existing.data(), def.c_str(), size)!=0)
                    throw std::runtime_error("existing descriptor differs; use a fresh flow UUID");
            }
            if (info.common.payloadLocation!=MXL_PAYLOAD_LOCATION_HOST_MEMORY ||
                info.discrete.sliceSizes[0]!=((c.width+47)/48)*128 ||
                info.discrete.sliceSizes[1]!=((c.width+2)/3)*4 ||
                info.discrete.grainCount!=c.slots || info.common.grainRate.numerator!=c.rate.numerator ||
                info.common.grainRate.denominator!=c.rate.denominator)
                throw std::runtime_error("MXL ring/rate mismatch; set domain options.json history for requested slots");
        } catch (...) { cleanup(); throw; }
    }
    ~Mxl() override { cleanup(); }
    void publish(std::uint64_t index, std::span<const std::uint8_t> rgba) override {
        mxlGrainInfo grain{}; grain.version=2; grain.size=sizeof(grain);
        std::uint8_t* payload{};
        check(mxlFlowWriterOpenGrain(writer_, index, &grain, &payload), "open grain");
        try {
            if (!payload || grain.grainSize!=packed_size(c_.width,c_.height)) throw std::runtime_error("MXL v210a geometry mismatch");
            pack_v210a(rgba,c_.width,c_.height,{payload,grain.grainSize});
            grain.flags=0; grain.validSlices=grain.totalSlices;
            check(mxlFlowWriterCommitGrain(writer_, &grain), "commit grain");
        } catch (...) { mxlFlowWriterCancelGrain(writer_); throw; }
    }
};
}
#endif
std::unique_ptr<Sink> make_mxl(const Config& c) {
#ifdef INGEST_WITH_MXL
    return std::make_unique<Mxl>(c);
#else
    (void)c; throw std::runtime_error("MXL unavailable: build with INGEST_WITH_MXL=ON");
#endif
}
}
