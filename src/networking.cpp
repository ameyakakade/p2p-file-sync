#include "networking.h"
#include "merkle_and_diff.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

namespace {
constexpr uint64_t MAX_STATE_SIZE = 128ULL * 1024ULL * 1024ULL;
constexpr uint64_t ERROR_FILE_SIZE = (std::numeric_limits<uint64_t>::max)();

SOCK connectTo(const std::string& ip, int port) {
    SOCK sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INV_SOCK) return INV_SOCK;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1 ||
        ::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == ERR_SOCK) {
        closeSocket(sock);
        return INV_SOCK;
    }
    return sock;
}

bool sendSize(SOCK sock, uint64_t size) {
    return sendAll(sock, reinterpret_cast<const char*>(&size), sizeof(size));
}

bool sendFile(SOCK client, const fs::path& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) return sendSize(client, ERROR_FILE_SIZE);
    const auto end = file.tellg();
    if (end < 0) return sendSize(client, ERROR_FILE_SIZE);
    uint64_t remaining = static_cast<uint64_t>(end);
    if (!sendSize(client, remaining)) return false;
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(65536);
    while (remaining > 0) {
        const size_t amount = static_cast<size_t>((std::min)(remaining, static_cast<uint64_t>(buffer.size())));
        file.read(buffer.data(), static_cast<std::streamsize>(amount));
        if (file.gcount() != static_cast<std::streamsize>(amount)) return false;
        if (!sendAll(client, buffer.data(), amount)) return false;
        remaining -= amount;
    }
    return true;
}

void handleClient(SOCK client, const fs::path& root) {
    std::string command;
    if (!readLine(client, command)) return;

    if (command == "GET_STATE") {
        const std::string data = serializeState(loadState(root));
        if (data.size() > MAX_STATE_SIZE) {
            sendSize(client, ERROR_FILE_SIZE);
            return;
        }
        if (sendSize(client, static_cast<uint64_t>(data.size())) && !data.empty())
            sendAll(client, data.data(), data.size());
        return;
    }

    if (command == "GET_TREE") {
        MerkleTree tree;
        if (tree.buildTree(root, root) < 0) {
            sendSize(client, ERROR_FILE_SIZE);
            return;
        }
        const std::string data = tree.dumpTreeString();
        if (data.size() > MAX_STATE_SIZE) {
            sendSize(client, ERROR_FILE_SIZE);
            return;
        }
        if (sendSize(client, static_cast<uint64_t>(data.size())) && !data.empty())
            sendAll(client, data.data(), data.size());
        return;
    }

    const std::string mkdirPrefix = "MAKE_DIR ";
    if (command.rfind(mkdirPrefix, 0) == 0) {
        std::string decoded;
        if (!decodeHex(command.substr(mkdirPrefix.size()), decoded) ||
            !isSafeRelativePath(fs::path(decoded))) return;
        std::error_code ec;
        fs::create_directories(root / fs::path(decoded), ec);
        return;
    }

    const std::string prefix = "GET_FILE ";
    if (command.rfind(prefix, 0) == 0) {
        std::string decoded;
        if (!decodeHex(command.substr(prefix.size()), decoded) || !isSafeRelativePath(fs::path(decoded))) {
            sendSize(client, ERROR_FILE_SIZE);
            return;
        }
        const fs::path fullPath = root / fs::path(decoded);
        std::error_code ec;
        if (!fs::is_regular_file(fullPath, ec) || ec) {
            sendSize(client, ERROR_FILE_SIZE);
            return;
        }
        sendFile(client, fullPath);
        return;
    }

    sendSize(client, ERROR_FILE_SIZE);
}
}

void closeSocket(SOCK socket) {
    if (socket == INV_SOCK) return;
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

bool sendAll(SOCK socket, const char* buffer, size_t length) {
    size_t sentTotal = 0;
    while (sentTotal < length) {
        const size_t remaining = length - sentTotal;
#ifdef _WIN32
        const int sent = ::send(socket, buffer + sentTotal,
                                static_cast<int>((std::min)(remaining, static_cast<size_t>((std::numeric_limits<int>::max)()))), 0);
#else
        const ssize_t sent = ::send(socket, buffer + sentTotal, remaining, 0);
#endif
        if (sent <= 0) return false;
        sentTotal += static_cast<size_t>(sent);
    }
    return true;
}

bool recvAll(SOCK socket, char* buffer, size_t length) {
    size_t receivedTotal = 0;
    while (receivedTotal < length) {
        const size_t remaining = length - receivedTotal;
#ifdef _WIN32
        const int received = ::recv(socket, buffer + receivedTotal,
                                    static_cast<int>((std::min)(remaining, static_cast<size_t>((std::numeric_limits<int>::max)()))), 0);
#else
        const ssize_t received = ::recv(socket, buffer + receivedTotal, remaining, 0);
#endif
        if (received <= 0) return false;
        receivedTotal += static_cast<size_t>(received);
    }
    return true;
}

bool readLine(SOCK socket, std::string& line, size_t maxLength) {
    line.clear();
    while (line.size() < maxLength) {
        char c = '\0';
        if (!recvAll(socket, &c, 1)) return false;
        if (c == '\n') return true;
        if (c != '\r') line.push_back(c);
    }
    return false;
}

void runServer(const fs::path& root, int port) {
    SOCK server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server == INV_SOCK) {
        std::cerr << "[Server] socket() failed\n";
        return;
    }
    int reuse = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == ERR_SOCK) {
        std::cerr << "[Server] bind failed on port " << port << "\n";
        closeSocket(server);
        return;
    }
    if (::listen(server, 16) == ERR_SOCK) {
        std::cerr << "[Server] listen failed\n";
        closeSocket(server);
        return;
    }
    std::cout << "[Server] Listening on port " << port << " for " << root << "\n";
    while (true) {
        sockaddr_in clientAddress{};
#ifdef _WIN32
        int addressLength = sizeof(clientAddress);
        SOCK client = ::accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &addressLength);
#else
        socklen_t addressLength = sizeof(clientAddress);
        SOCK client = ::accept(server, reinterpret_cast<sockaddr*>(&clientAddress), &addressLength);
#endif
        if (client == INV_SOCK) continue;
        handleClient(client, root);
        closeSocket(client);
    }
}

bool fetchRemoteState(const std::string& ip, int port, std::string& serializedState) {
    SOCK sock = connectTo(ip, port);
    if (sock == INV_SOCK) return false;
    const std::string request = "GET_STATE\n";
    uint64_t size = 0;
    bool ok = sendAll(sock, request.data(), request.size()) && recvAll(sock, reinterpret_cast<char*>(&size), sizeof(size));
    if (!ok || size == ERROR_FILE_SIZE || size > MAX_STATE_SIZE) {
        closeSocket(sock);
        return false;
    }
    serializedState.assign(static_cast<size_t>(size), '\0');
    if (size > 0) ok = recvAll(sock, serializedState.data(), static_cast<size_t>(size));
    closeSocket(sock);
    return ok;
}

bool downloadRemoteFile(const std::string& ip, int port,
                        const std::string& relativePath, const fs::path& destination) {
    if (!isSafeRelativePath(fs::path(relativePath))) return false;
    SOCK sock = connectTo(ip, port);
    if (sock == INV_SOCK) return false;
    const std::string request = "GET_FILE " + encodeHex(relativePath) + "\n";
    uint64_t size = 0;
    bool ok = sendAll(sock, request.data(), request.size()) && recvAll(sock, reinterpret_cast<char*>(&size), sizeof(size));
    if (!ok || size == ERROR_FILE_SIZE || size > static_cast<uint64_t>((std::numeric_limits<std::streamsize>::max)())) {
        closeSocket(sock);
        return false;
    }
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) { closeSocket(sock); return false; }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) { closeSocket(sock); return false; }
    std::vector<char> buffer(65536);
    uint64_t remaining = size;
    while (remaining > 0) {
        const size_t amount = static_cast<size_t>((std::min)(remaining, static_cast<uint64_t>(buffer.size())));
        if (!recvAll(sock, buffer.data(), amount)) { ok = false; break; }
        out.write(buffer.data(), static_cast<std::streamsize>(amount));
        if (!out) { ok = false; break; }
        remaining -= amount;
    }
    out.close();
    closeSocket(sock);
    if (!ok) fs::remove(destination, ec);
    return ok;
}


bool fetchRemoteMerkleTree(const std::string& ip, int port, MerkleTree& tree) {
    SOCK sock = connectTo(ip, port);
    if (sock == INV_SOCK) return false;
    const std::string request = "GET_TREE\n";
    uint64_t size = 0;
    bool ok = sendAll(sock, request.data(), request.size()) &&
              recvAll(sock, reinterpret_cast<char*>(&size), sizeof(size));
    if (!ok || size == ERROR_FILE_SIZE || size == 0 || size > MAX_STATE_SIZE) {
        closeSocket(sock);
        return false;
    }
    std::string serialized(static_cast<size_t>(size), '\0');
    ok = recvAll(sock, serialized.data(), serialized.size());
    closeSocket(sock);
    if (!ok) return false;
    tree.buildTreeString(serialized.c_str());
    return !tree.pool.empty();
}


bool createRemoteDirectory(const std::string& ip, int port, const std::string& relativePath) {
    if (!isSafeRelativePath(fs::path(relativePath))) return false;
    SOCK sock = connectTo(ip, port);
    if (sock == INV_SOCK) return false;
    const std::string request = "MAKE_DIR " + encodeHex(relativePath) + "\n";
    const bool ok = sendAll(sock, request.data(), request.size());
    closeSocket(sock);
    return ok;
}
