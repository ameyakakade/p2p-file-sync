#include <cstdint>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
const uint64_t FNV_PRIME        = 1099511628211ULL;

uint64_t combineHash(uint64_t currentHash, uint64_t newHash);
uint64_t calculateBufferHash(const char* data, size_t length);
uint64_t calculateStringHash(const std::string& inp);
uint64_t hashFile(const fs::path& filepath);


