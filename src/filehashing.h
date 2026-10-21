#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

// compareTo(): 1 = this clock dominates, -1 = other dominates,
// 0 = equal, 2 = concurrent (neither clock dominates).
struct VectorClock {
    std::map<uint32_t, uint64_t> clocks;

    void increment(uint32_t nodeId) { ++clocks[nodeId]; }

    void merge(const VectorClock& other) {
        for (const auto& [nodeId, value] : other.clocks) {
            auto& current = clocks[nodeId];
            current = (std::max)(current, value);
        }
    }

    int compareTo(const VectorClock& other) const {
        bool thisGreater = false;
        bool otherGreater = false;
        std::set<uint32_t> ids;
        for (const auto& [id, value] : clocks) { (void)value; ids.insert(id); }
        for (const auto& [id, value] : other.clocks) { (void)value; ids.insert(id); }
        for (uint32_t id : ids) {
            const uint64_t a = clocks.count(id) ? clocks.at(id) : 0;
            const uint64_t b = other.clocks.count(id) ? other.clocks.at(id) : 0;
            if (a > b) thisGreater = true;
            if (b > a) otherGreater = true;
        }
        if (thisGreater && !otherGreater) return 1;
        if (otherGreater && !thisGreater) return -1;
        if (thisGreater && otherGreater) return 2;
        return 0;
    }

    std::string toString() const {
        std::ostringstream out;
        bool first = true;
        for (const auto& [id, value] : clocks) {
            if (!first) out << ',';
            first = false;
            out << id << ':' << value;
        }
        return out.str();
    }

    static VectorClock fromString(const std::string& value) {
        VectorClock result;
        std::istringstream in(value);
        std::string item;
        while (std::getline(in, item, ',')) {
            if (item.empty()) continue;
            const auto colon = item.find(':');
            if (colon == std::string::npos) continue;
            try {
                const auto id = static_cast<uint32_t>(std::stoul(item.substr(0, colon)));
                const auto count = static_cast<uint64_t>(std::stoull(item.substr(colon + 1)));
                result.clocks[id] = count;
            } catch (...) {
                // Ignore malformed clock entries; malformed records are handled by state parsing.
            }
        }
        return result;
    }
};

uint64_t combineHash(uint64_t currentHash, uint64_t newHash);
uint64_t calculateBufferHash(const char* data, size_t length);
uint64_t calculateStringHash(const std::string& input);
uint64_t hashFile(const fs::path& filepath);
std::string hashToHex(uint64_t value);
