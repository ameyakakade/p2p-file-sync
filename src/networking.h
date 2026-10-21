#ifndef NOMINMAX
#define NOMINMAX
#endif

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SOCK = SOCKET;
constexpr SOCK INV_SOCK = INVALID_SOCKET;
constexpr int ERR_SOCK = SOCKET_ERROR;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using SOCK = int;
constexpr SOCK INV_SOCK = -1;
constexpr int ERR_SOCK = -1;
#endif

namespace fs = std::filesystem;

class MerkleTree;

void closeSocket(SOCK socket);
bool sendAll(SOCK socket, const char* buffer, size_t length);
bool recvAll(SOCK socket, char* buffer, size_t length);
bool readLine(SOCK socket, std::string& line, size_t maxLength = 65536);

// Starts the request server. It serves the persisted state and files under root.
void runServer(const fs::path& root, int port);

// Client-side, framed TCP requests.
bool fetchRemoteState(const std::string& ip, int port, std::string& serializedState);
bool fetchRemoteMerkleTree(const std::string& ip, int port, MerkleTree& tree);
bool createRemoteDirectory(const std::string& ip, int port, const std::string& relativePath);
bool downloadRemoteFile(const std::string& ip, int port,
                        const std::string& relativePath, const fs::path& destination);
