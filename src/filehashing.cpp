#include "filehashing.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

uint64_t combineHash(uint64_t currentHash, uint64_t newHash) {
    currentHash ^= newHash;
    currentHash *= FNV_PRIME;
    return currentHash;
}

uint64_t calculateBufferHash(const char* data, size_t length) {
    uint64_t hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(data[i]));
        hash *= FNV_PRIME;
    }
    return hash;
}

uint64_t calculateStringHash(const std::string& input) {
    return calculateBufferHash(input.data(), input.size());
}

uint64_t hashFile(const fs::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return 0;

    constexpr size_t bufferSize = 65536;
    std::vector<char> buffer(bufferSize);
    uint64_t hash = FNV_OFFSET_BASIS;
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = file.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(buffer[static_cast<size_t>(i)]));
            hash *= FNV_PRIME;
        }
    }
    if (file.bad()) return 0;
    return hash;
}

std::string hashToHex(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}
