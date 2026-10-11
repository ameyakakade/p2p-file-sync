#include "merkle_and_diff.h"

bool sendAll(SOCK sock, const char* buffer, size_t length);
bool sendFileOverSocket(SOCK client, const fs::path& fullPath);
bool recvAll(SOCK sock, char* buffer, size_t length);
bool handleConflict(const FileDifference& diff, const fs::path& localBaseFolder, int remotePort, const char* remoteIp);
bool uploadFile(const std::string& relativePath, const fs::path& localBaseFolder, int remotePort, const char* remoteIp);
bool downloadFile(const std::string& relativePath, const fs::path& localBaseFolder, int port, const char* ip);
