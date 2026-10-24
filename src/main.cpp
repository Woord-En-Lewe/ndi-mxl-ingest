// SPDX-License-Identifier: MIT
#include "ingest.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t stopped=0;
extern "C" void stop(int) { stopped=1; }
}
int main(int argc, char** argv) {
    try {
        ingest::Config c;
        bool simulate=false;
        std::uint32_t frames=0;
        for (int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg=="--help") {
                std::cout << "ndi-mxl-ingest [--config PATH] [--simulate] [--frames N]\n"; return 0;
            }
            if (arg=="--simulate") simulate=true;
            else if ((arg=="--config" || arg=="--frames") && i+1<argc) {
                const std::string value=argv[++i];
                if (arg=="--config") c=ingest::load_config(value);
                else { frames=ingest::number(value); if (!frames) throw std::invalid_argument("frames must be positive"); }
            } else throw std::invalid_argument("unknown/incomplete option: "+arg);
        }
        c.validate();
        std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
        if (simulate) {
            std::vector<std::uint8_t> rgba(std::size_t(c.width)*c.height*4,0);
            std::vector<std::uint8_t> packed(ingest::packed_size(c.width,c.height));
            for (std::uint32_t i=0; !stopped && (!frames || i<frames); ++i) {
                ingest::pack_v210a(rgba,c.width,c.height,packed);
            }
            std::cout << "simulation complete (no NDI, TAI clock or MXL required)\n"; return 0;
        }
        (void)ingest::tai_now(c); // Fail before starting capture if platform clock is unsupported.
        auto sink=ingest::make_mxl(c);
        auto source=ingest::make_ndi(c);
        ingest::Mailbox mailbox;
        std::atomic<bool> connected{false}, failed{false};
        std::exception_ptr capture_error;
        std::jthread capture([&](std::stop_token token) {
            try {
                while (!token.stop_requested()) {
                    auto f=source->capture();
                    if (f) mailbox.put(std::move(*f));
                    const bool online=source->connected();
                    if (!online) mailbox.clear();
                    connected.store(online);
                }
            } catch (...) { capture_error=std::current_exception(); failed.store(true); }
        });
        const std::vector<std::uint8_t> transparent(std::size_t(c.width)*c.height*4,0);
        std::optional<std::uint64_t> last;
        std::uint32_t published=0;
        while (!stopped && !failed.load() && (!frames || published<frames)) {
            const auto now=ingest::tai_now(c), index=c.rate.index(now);
            if (!last || index>*last) {
                auto f=mailbox.get();
                const bool show=ingest::visible(c,f.get(),now,connected.load());
                sink->publish(index,show ? std::span<const std::uint8_t>(f->rgba) : std::span<const std::uint8_t>(transparent));
                if (last && index>*last+1) std::cerr << "cadence skipped " << index-*last-1 << " ticks\n";
                last=index; ++published;
            }
            // Re-evaluate TAI frequently: never use a remote timestamp or manufacture future indices.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        capture.request_stop(); capture.join();
        if (capture_error) std::rethrow_exception(capture_error);
        std::cerr << "stopped; published " << published << " grains\n";
    } catch (const std::exception& e) { std::cerr << "fatal: " << e.what() << '\n'; return 1; }
}
