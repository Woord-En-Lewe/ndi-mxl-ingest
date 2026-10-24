// SPDX-License-Identifier: MIT
#include "ingest.hpp"
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>

namespace ingest {
std::uint32_t number(const std::string& s) {
    std::uint32_t n{};
    auto [end, ec] = std::from_chars(s.data(), s.data()+s.size(), n);
    if (ec != std::errc{} || end != s.data()+s.size() || s.empty()) throw std::invalid_argument("invalid unsigned integer: " + s);
    return n;
}
void Config::validate() const {
    (void)packed_size(width, height);
    if (!rate.numerator || !rate.denominator || double(rate.numerator)/rate.denominator > 240 || double(rate.numerator)/rate.denominator < 1)
        throw std::invalid_argument("rate must be between 1 and 240 fps");
    if (slots < 2 || slots > 256 || (disconnect_ms && disconnect_ms < 100) || disconnect_ms > 3600000)
        throw std::invalid_argument("invalid slots/disconnect_ms");
    if (domain.empty() || flow_id.size() != 36) throw std::invalid_argument("invalid domain or UUID");
    for (std::size_t i=0; i<flow_id.size(); ++i) {
        const auto ch = flow_id[i];
        if (i==8 || i==13 || i==18 || i==23) { if (ch!='-') throw std::invalid_argument("invalid UUID"); }
        else if (!((ch>='0' && ch<='9') || (ch>='a' && ch<='f') || (ch>='A' && ch<='F'))) throw std::invalid_argument("invalid UUID");
    }
    if (tai_offset && *tai_offset > 1000) throw std::invalid_argument("invalid tai_offset");
}
Config load_config(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open config: " + path);
    Config c;
    std::set<std::string> keys;
    const auto trim = [](std::string s) {
        auto first = s.find_first_not_of(" \t\r");
        return first == std::string::npos ? std::string{} : s.substr(first, s.find_last_not_of(" \t\r")-first+1);
    };
    for (std::string line; std::getline(file,line);) {
        line = trim(line);
        if (line.empty() || line[0]=='#') continue;
        auto eq = line.find('=');
        if (eq==std::string::npos) throw std::invalid_argument("expected key=value");
        auto key=trim(line.substr(0,eq)), value=trim(line.substr(eq+1));
        if (!keys.insert(key).second) throw std::invalid_argument("duplicate key: " + key);
        if (key=="source_name") c.source_name=value;
        else if (key=="extra_ips") c.extra_ips=value;
        else if (key=="discovery_server") c.discovery_server=value;
        else if (key=="domain") c.domain=value;
        else if (key=="flow_id") c.flow_id=value;
        else if (key=="width") c.width=number(value);
        else if (key=="height") c.height=number(value);
        else if (key=="slots") c.slots=number(value);
        else if (key=="rate_numerator") c.rate.numerator=number(value);
        else if (key=="rate_denominator") c.rate.denominator=number(value);
        else if (key=="disconnect_ms") c.disconnect_ms=number(value);
        else if (key=="tai_offset") c.tai_offset=number(value);
        else if (key=="gap_policy") {
            if (value!="hold" && value!="transparent") throw std::invalid_argument("invalid gap_policy");
            c.hold=value=="hold";
        } else throw std::invalid_argument("unknown config key: " + key);
    }
    c.validate(); return c;
}
}
