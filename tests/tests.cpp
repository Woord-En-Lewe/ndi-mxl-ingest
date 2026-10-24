// SPDX-License-Identifier: MIT
#include "ingest.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition) { if (!condition) throw std::runtime_error("test failed"); }
template<class F> void rejects(F f) { bool threw=false; try { f(); } catch (const std::exception&) { threw=true; } require(threw); }
std::uint32_t read(const std::vector<std::uint8_t>& v, std::size_t i) {
    return std::uint32_t(v[i]) | (std::uint32_t(v[i+1])<<8) | (std::uint32_t(v[i+2])<<16) | (std::uint32_t(v[i+3])<<24);
}
}
int main() {
    try {
        ingest::Rate r;
        for (auto i : {0ULL,1ULL,59ULL,100000ULL,100000000000ULL}) {
            require(r.index(r.timestamp(i))==i);
            if (i) require(r.index(r.timestamp(i)-1)==i-1);
        }
        require(r.timestamp(60000)==1001000000000ULL);
        rejects([] { (void)ingest::number("-1"); });
        rejects([] { (void)ingest::number("3x"); });
        rejects([] { ingest::Config c; c.width=3; c.validate(); });
        rejects([] { ingest::Config c; c.rate.numerator=0; c.validate(); });
        std::vector<std::uint8_t> bgra{1,2,3,4,99,99,99,99};
        auto rgba=ingest::normalize(bgra,1,1,8,ingest::PixelFormat::bgra);
        require(rgba==std::vector<std::uint8_t>({3,2,1,4}));
        require(ingest::normalize(bgra,1,1,8,ingest::PixelFormat::bgrx)[3]==255);
        rejects([&] { (void)ingest::normalize(bgra,2,2,8,ingest::PixelFormat::rgba); });
        for (const auto width : {2U,6U,48U,1280U,1920U}) {
            std::vector<std::uint8_t> pixels(std::size_t(width)*4,255);
            pixels[3]=0;
            std::vector<std::uint8_t> out(ingest::packed_size(width,1));
            ingest::pack_v210a(pixels,width,1,out);
            auto key_offset=std::size_t((width+47)/48)*128;
            require((read(out,0)&1023)==512);
            require(((read(out,0)>>10)&1023)==940);
            require((read(out,key_offset)&1023)==64);
            require(((read(out,key_offset)>>10)&1023)==940);
            if (width%3) require((read(out,out.size()-4)>>(10*(width%3)))==0);
        }
        {
            std::vector<std::uint8_t> ramp(256*4,0);
            for (std::size_t i=0;i<256;++i) ramp[i*4+3]=static_cast<std::uint8_t>(i);
            std::vector<std::uint8_t> out(ingest::packed_size(256,1));
            ingest::pack_v210a(ramp,256,1,out);
            const std::size_t key=6*128;
            std::uint32_t previous=0;
            for (std::size_t i=0;i<256;++i) {
                const auto alpha=(read(out,key+(i/3)*4)>>(10*(i%3)))&1023;
                require(alpha>previous); previous=alpha;
            }
            require(previous==940);
        }
        ingest::Config c;
        ingest::Frame f{{},1000000000};
        require(ingest::visible(c,&f,1000000001,true));
        require(!ingest::visible(c,&f,1000000001,false));
        require(ingest::visible(c,&f,3000000000,true));
        c.disconnect_ms=2000;
        require(!ingest::visible(c,&f,3000000000,true));
        require(!ingest::visible(c,&f,999999999,true));
        c.hold=false;
        require(!ingest::visible(c,&f,1100000000,true));
        ingest::Mailbox box;
        std::jthread writer([&] { for (std::uint64_t i=0;i<1000;++i) box.put({{1,2,3,4},i}); });
        for (int i=0;i<1000;++i) { auto p=box.get(); if (p) require(p->rgba.size()==4); }
        writer.join(); require(box.get()->arrival==999);
        box.clear(); require(!box.get());
        std::cout << "all core tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
