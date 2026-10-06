#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <map>
#include <set>
namespace fs = std::filesystem;

const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
const uint64_t FNV_PRIME        = 1099511628211ULL;

struct VectorClock{
    std::map<uint32_t, uint64_t>clocks;
    void increment(uint32_t nodeId){
        clocks[nodeId]++;
    }
    void merge(const VectorClock& other){ //changing our clock based on other's clock
        for(const auto&[nodeId, value] : other.clocks){
            clocks[nodeId] = (std::max)(clocks[nodeId], value);
        }
    }
    int compareTo(const VectorClock& other) const{
        bool thisHasGreater = false;
        bool otherHasGreater = false;
        std::set<uint32_t>allNodeIds;
        for(const auto&[id, _] : clocks) allNodeIds.insert(id);
        for(const auto&[id, _] : other.clocks) allNodeIds.insert(id);
        for(uint32_t id : allNodeIds){
            uint64_t thisValue = (clocks.count(id)) ? clocks.at(id) : 0;
            uint64_t otherValue = (other.clocks.count(id)) ? other.clocks.at(id) : 0;
            if(thisValue > otherValue) thisHasGreater = true;
            if(otherValue > thisValue) otherHasGreater = true;
        }
        if(thisHasGreater && !otherHasGreater) return 1;
        if(otherHasGreater && !thisHasGreater) return -1;
        if(otherHasGreater && thisHasGreater) return 0;
        return 0;
    }
    std::string toString() const {
        std::ostringstream oss;
        for (const auto& [id, val] : clocks) {
            oss << id << ":" << val << ",";
        }
        return oss.str();
    }
};

uint64_t combineHash(uint64_t currentHash, uint64_t newHash);
uint64_t calculateBufferHash(const char* data, size_t length);
uint64_t calculateStringHash(const std::string& inp);
uint64_t hashFile(const fs::path& filepath);


