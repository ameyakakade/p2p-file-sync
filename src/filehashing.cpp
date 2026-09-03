#include "filehashing.h"
#include <fstream>
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

uint64_t calculateStringHash(const std::string& inp) {
    return calculateBufferHash(inp.data(), inp.size());
}

uint64_t hashFile(const fs::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << filepath << "\n";
        return 0;
    }

    const size_t bufferSize = 65536;
    std::vector<char> buffer(bufferSize);
    uint64_t hash = FNV_OFFSET_BASIS;

    while (file.read(buffer.data(), bufferSize) || file.gcount() > 0) {
        size_t bytesRead = file.gcount();
        for (size_t i = 0; i < bytesRead; ++i) {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(buffer[i]));
            hash *= FNV_PRIME;
        }
    }
    return hash;
}
