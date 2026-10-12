#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <cstdint>
#include <thread>
#include <chrono>
#include <set>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <arpa/inet.h>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#endif /* _WIN32 */

#ifdef _WIN32
#define SOCK SOCKET
#define INV_SOCK INVALID_SOCKET
#define ERR_SOCK SOCKET_ERROR
#else
#define SOCK int
#define INV_SOCK (-1)
#define ERR_SOCK (-1)
#endif /* win32 */

#include "networking.h"
#include "filehashing.h"

void closeSocket(SOCK sc) {
    #ifdef _WIN32
        closesocket(sc);
    #else   
        close(sc);
    #endif
}

int receiveOverSocket(char* buf, int buflen, int port, const char* ip) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int a = 0;
    SOCK client_socket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == ERR_SOCK) {
        std::cout << "Connection failed.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        a = -1;
    } else {
        a = recv(client_socket, buf, buflen, 0);

#ifdef _WIN32
        closesocket(client_socket);
        WSACleanup();
#else
        close(client_socket);
#endif /* _WIN32 */
    }
    return a;
}

bool sendAll(SOCK sock, const char* buffer, size_t length){
    size_t totalSent = 0;
    while(totalSent < length){
        int sent = send(sock, buffer + totalSent, static_cast<int>(length-totalSent), 0);
        if(sent==0 || sent==ERR_SOCK){
            return false;
        }
        totalSent+=(sent);
    }
    return true;
}

bool recvAll(SOCK sock, char* buffer, size_t length){
    size_t totalRecd = 0;
    while(totalRecd < length){
        int recd = recv(sock, buffer + totalRecd, static_cast<int>(length-totalRecd), 0);
        if(recd==0 || recd==ERR_SOCK){
            return false;
        }
        totalRecd+=(recd);
    }
    return true;
}

bool sendFileOverSocket(SOCK client, const fs::path& fullPath){
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if(!file.is_open()){
        uint64_t errSize = 0;
        sendAll(client, reinterpret_cast<char*>(&errSize), sizeof(errSize));
        return false;
    }
    uint64_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    if(!sendAll(client, reinterpret_cast<char*>(&fileSize), sizeof(fileSize))){
        return false;
    }
    const size_t bufferSize = 65536; //64KB
    std::vector<char>buffer(bufferSize);
    while(fileSize > 0){
        size_t toRead = (std::min)(static_cast<uint64_t>(bufferSize), fileSize);
        file.read(buffer.data(), toRead);
        if(!sendAll(client, buffer.data(), toRead)){
            return false;
        }
        fileSize-=toRead;
    }
    return true;
}

bool downloadFile(const std::string& relativePath, const fs::path& localBaseFolder, int port, const char* ip){
    SOCK clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    if(connect(clientSocket, (sockaddr*)& server_addr, sizeof(server_addr)) == ERR_SOCK){
        closeSocket(clientSocket);
        return false;
    }
    std::string command = "GET_FILE " + relativePath;

    send(clientSocket, command.c_str(), static_cast<int>(command.size()), 0);
    uint64_t fileSize = 0;
    if(!recvAll(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize))){
        closeSocket(clientSocket);
        return false;
    }

    fs::path targetPath = localBaseFolder / relativePath;
    fs::create_directories(targetPath.parent_path());
    std::ofstream outFile(targetPath, std::ios::binary);
    if(!outFile.is_open()){
        closeSocket(clientSocket);
        return false;
    }
    const size_t bufferSize = 65536;
    std::vector<char>buffer(bufferSize);
    uint64_t remaining = fileSize;
    while(remaining > 0){
        size_t toRecv = (std::min)(remaining, static_cast<uint64_t>(bufferSize));
        if(!recvAll(clientSocket, buffer.data(), toRecv)){
            outFile.close();
            closeSocket(clientSocket);
            return false;
        }
        outFile.write(buffer.data(), toRecv);
        remaining-=toRecv;
    }
    outFile.close();
    closeSocket(clientSocket);
    return true;
}

bool downloadFileTo(const std::string& remoteRelativePath, const fs::path& localTargetPath, int port, const char* ip){
    SOCK clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INV_SOCK) return false;
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    if (connect(clientSocket, (sockaddr*)&server_addr, sizeof(server_addr)) == ERR_SOCK) {
        closeSocket(clientSocket);
        return false;
    }
    std::string command = "GET_FILE " + remoteRelativePath;
    if(!sendAll(clientSocket, command.c_str(), command.size())){
        return false;
    }
    uint64_t fileSize = 0;
    if(!recvAll(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize))){
        return false;
    }
    fs::create_directories(localTargetPath.parent_path());
    std::ofstream outFile(localTargetPath, std::ios::binary);
    if(!outFile.is_open()){
        return false;
    }
    std::vector<char>buffer(65536);
    uint64_t remaining = fileSize;
    while(remaining > 0){
        size_t chunk = (std::min)(remaining, static_cast<uint64_t>(buffer.size()));
        if(!recvAll(clientSocket, buffer.data(), chunk)){
            return false;
        }
        outFile.write(buffer.data(), static_cast<std::streamsize>(chunk));
        remaining-=chunk;
    }
    outFile.close();
    closeSocket(clientSocket);
    return true;  
}

//=================
// upload file fxn to send local file to remote
bool uploadFile(const std::string& relativePath, const fs::path& localBaseFolder, int remotePort, const char* remoteIp){
    fs::path fullPath = localBaseFolder / relativePath;
    SOCK clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(remotePort);
    inet_pton(AF_INET, remoteIp, &server_addr.sin_addr);

    if(connect(clientSocket, (sockaddr*)&server_addr, sizeof(server_addr)) == ERR_SOCK){
        closeSocket(clientSocket);
        return false;
    }
    std::string command = "PUT_FILE" + relativePath;
    if(!sendAll(clientSocket, command.c_str(), command.size())){
        closeSocket(clientSocket);
        return false;
    }
    bool result = sendFileOverSocket(clientSocket, fullPath);
    closeSocket(clientSocket);
    return result;
}

// tell remote peer to delete a file on their side
bool deleteRemoteFile(const std::string& relativePath, int remotePort, const char* remoteIp){
    SOCK clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(remotePort);
    inet_pton(AF_INET, remoteIp, &server_addr.sin_addr);
    if(connect(clientSocket, (sockaddr*)&server_addr, sizeof(server_addr))==ERR_SOCK){
        closeSocket(clientSocket);
        return false;
    }
    std::string command = "DEL_FILE" + relativePath;
    bool result = sendAll(clientSocket, command.c_str(), command.size());
    closeSocket(clientSocket);
    return result;
    
}

// handling conflict using keep both strategy
bool handleConflict(const FileDifference& diff, const fs::path& localBaseFolder, int remotePort, const char* remoteIp){
    if(!diff.hasConflict || diff.isDirectory){
        return false;
    }
    fs:: path targetPath = localBaseFolder / diff.nodePath;
    const uint64_t localHash = diff.conflict.localHash;
    const uint64_t remoteHash = diff.conflict.remoteHash;
    if(localHash==remoteHash){
        return true;
    }
    const bool localWins = localHash > remoteHash;
    const uint64_t losingHash = localWins ? remoteHash : localHash;
    std::string conflictSuffix = ".conflict_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path conflictPath = targetPath.parent_path() / (targetPath.filename().string() + conflictSuffix);
    std::error_code ec;
    bool success = true;
    if(localWins){
        std::cout << "  Saving remote version to: " << conflictPath.filename() << "\n";
        if(!fs::exists(conflictPath)){
            if(!downloadFileTo(diff.nodePath, conflictPath, remotePort, remoteIp)){
                std::cout << "Failed to download conflict copy" << std::endl;
                success = false;
            }
        }
        if(success && !uploadFile(diff.nodePath, conflictPath, remotePort, remoteIp)){
            std::cout << "Failed to upload winning version\n";
            success = false;
        }
    } else{ 
        std::cout << "  Saving local version to: " << conflictPath.filename() << "\n";
        if(!fs::exists(conflictPath)){
            fs::copy_file(targetPath, conflictPath, fs::copy_options::overwrite_existing, ec);
            if(ec){
                std::cout << "Failed to save local version" << std::endl;
                success = false;
            }
        }
        if (success && !downloadFile(diff.nodePath, localBaseFolder, remotePort, remoteIp)) {
            std::cout << "Failed to download winning version\n";
            success = false;
        }
    }
    if(success){
        std::cout << "Conflict resolved!\n";    
    }
    return success;
}
