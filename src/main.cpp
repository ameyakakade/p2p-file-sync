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

#include "filehashing.h"
#define PARSER_IMPLEMENTATION
#include "parser.h"
#include "merkle_and_diff.h"

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
        #ifdef _WIN32
            closesocket(clientSocket);
        #else
            close(clientSocket);
        #endif
            return false;
    }
    std::string command = "GET_FILE " + relativePath;
    send(clientSocket, command.c_str(), static_cast<int>(command.size()), 0);
    uint64_t fileSize = 0;
    if(!recvAll(clientSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize))){
        #ifdef _WIN32
            closesocket(clientSocket);
        #else   
            close(clientSocket);
        #endif
            return false;
    }

    fs::path targetPath = localBaseFolder / relativePath;
    fs::create_directories(targetPath.parent_path());
    std::ofstream outFile(targetPath, std::ios::binary);
    if(!outFile.is_open()){
        #ifdef _WIN32
            closesocket(clientSocket);
        #else   
            close(clientSocket);
        #endif
            return false;
    }
    const size_t bufferSize = 65536;
    std::vector<char>buffer(bufferSize);
    uint64_t remaining = fileSize;
    while(remaining > 0){
        size_t toRecv = (std::min)(remaining, static_cast<uint64_t>(bufferSize));
        if(!recvAll(clientSocket, buffer.data(), toRecv)){
            outFile.close();
            #ifdef _WIN32
                closesocket(clientSocket);
            #else   
                close(clientSocket);
            #endif
                return false;
        }
        outFile.write(buffer.data(), toRecv);
        remaining-=toRecv;
    }
    outFile.close();
    #ifdef _WIN32
        closesocket(clientSocket);
    #else   
        close(clientSocket);
    #endif
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
#ifdef _WIN32
        closesocket(clientSocket);
#else
        close(clientSocket);
#endif
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
#ifdef _WIN32
    closesocket(clientSocket);
#else
    close(clientSocket);
#endif
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
        #ifdef _WIN32
            closesocket(clientSocket);
        #else
            close(clientSocket);
        #endif
        return false;
    }
    std::string command = "PUT_FILE" + relativePath;
    if(!sendAll(clientSocket, command.c_str(), command.size())){
        #ifdef _WIN32
            closesocket(clientSocket);
        #else
            close(clientSocket);
        #endif
        return false;
    }
    bool result = sendFileOverSocket(clientSocket, fullPath);
    #ifdef _WIN32
        closesocket(clientSocket);
    #else
        close(clientSocket);
    #endif
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
        #ifdef _WIN32
            closesocket(clientSocket);
        #else
            close(clientSocket);
        #endif
        return false;
    }
    std::string command = "DEL_FILE" + relativePath;
    bool result = sendAll(clientSocket, command.c_str(), command.size());
    #ifdef _WIN32
        closesocket(clientSocket);
    #else
        close(clientSocket);
    #endif
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
std::mutex treeMutex;
// =================
void runServer(const fs::path& localFolder, int port = 8080, uint32_t nodeId = 1) {
    MerkleTree localTree;
    

    SOCK server_socket = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr))==ERR_SOCK){
        std::cerr << "[Server] Bind Failed on Port" << std::endl;
        return;
    }
    listen(server_socket, 10);

    std::cout << "Server listening for folder: " << localFolder << "\n";

    while (true) {
        sockaddr_in client_addr;
        int client_size = sizeof(client_addr);

#ifdef _WIN32
        SOCK client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_size);
#else
        SOCK client_socket = accept(server_socket, (sockaddr*)&client_addr, (socklen_t*)&client_size);
#endif
        if(client_socket==INV_SOCK){continue;}
        char reqBuff[1024] = {0};
        int bytesRead = recv(client_socket, reqBuff, sizeof(reqBuff)-1, 0);
        if(bytesRead > 0){ // means there is a request by the client
            std::string request(reqBuff, bytesRead);
            if(request=="GET_TREE"){ // send the merkle tree
                std::lock_guard<std::mutex> lock(treeMutex);
                localTree.clear();
                localTree.buildTree(localFolder);
                std::string serialisedTree = localTree.dumpTreeString();
                uint64_t treeSize = serialisedTree.size();
                sendAll(client_socket, reinterpret_cast<char*>(&treeSize), sizeof(treeSize));
                sendAll(client_socket, serialisedTree.c_str(), serialisedTree.size());
            } else if(request.substr(0, 9) == "GET_FILE "){ // send the file
                std::string relativePath = request.substr(9);
                fs::path fullPath = localFolder / relativePath;
                sendFileOverSocket(client_socket, fullPath);
            } else if(request.substr(0, 9)=="PUT_FILE"){
                std::string relativePath = request.substr(9);
                fs::path targetPath = localFolder / relativePath;
                fs::create_directories(targetPath.parent_path());
                uint64_t fileSize = 0;
                if(recvAll(client_socket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize)) && fileSize > 0){
                    std::ofstream outFile(targetPath, std::ios::binary);
                    if(outFile.is_open()){
                        const size_t bufferSize = 65536;
                        std::vector<char>buffer(bufferSize);
                        uint64_t remaining = fileSize;
                        while(remaining > 0){
                            size_t toRecv = (std::min)(remaining, static_cast<uint64_t>(bufferSize));
                            if(recvAll(client_socket, buffer.data(), toRecv)){
                                outFile.write(buffer.data(), toRecv);
                                remaining-=toRecv;
                            } else{
                                break;
                            }
                        }
                        outFile.close();
                        std::cout << "Received pushed file: " << relativePath << "\n";
                    }
                }
            } else if(request.substr(0, 9)=="DEL_FILE"){
                std::string relativePath = request.substr(9);
                fs::path targetPath = localFolder / relativePath;
                std::error_code ec;
                if(fs::exists(targetPath)){
                    fs::remove_all(targetPath, ec);
                    std::cout << "Deleted remote file: " << relativePath << "\n";
                }
            }
        }
    #ifdef _WIN32
        closesocket(client_socket);
    #else   
        close(client_socket);
    #endif
    }
}
struct peerEndPoints{
    std::string ip;
    int port;
};

void runSync(const fs::path& localFolder, const std::vector<peerEndPoints>& peers, uint32_t nodeId){
    std::set<std::string> recentlySyncedFiles;
    auto lastCleanup = std::chrono::steady_clock::now();
    
    while(true){
        bool anyChanges = false;
        
        for(const auto& peer : peers){
            MerkleTree localTree(nodeId);
            {
                std::lock_guard<std::mutex> lock(treeMutex);
                localTree.buildTree(localFolder);
            }
 
            SOCK client_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (client_socket == INV_SOCK) continue;
 
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(peer.port);
            inet_pton(AF_INET, peer.ip.c_str(), &server_addr.sin_addr);
 
            if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == ERR_SOCK){
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
 
            const std::string req = "GET_TREE";
            if (!sendAll(client_socket, req.c_str(), req.size())) {
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
 
            uint64_t treeSize = 0;
            if (!recvAll(client_socket, reinterpret_cast<char*>(&treeSize), sizeof(treeSize)) ||
                treeSize == 0 || treeSize > 128ULL * 1024ULL * 1024ULL) {
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
 
            std::vector<char> treeBuf(treeSize + 1, 0);
            if (!recvAll(client_socket, treeBuf.data(), treeSize)) {
#ifdef _WIN32
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                continue;
            }
 
#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
 
            MerkleTree remoteTree;
            remoteTree.buildTreeString(treeBuf.data());
            if (localTree.checkIfEqual(remoteTree)) {
                std::cout << "[✓] In sync with " << peer.ip << ":" << peer.port << "\n";
                continue;
            }
 
            std::cout << "\n[!] Sync discrepancy detected with " << peer.ip << ":" << peer.port << "\n";
 
            const auto diffs = MerkleTree::findDifferences(localTree, remoteTree);
 
            for (const auto& diff : diffs) {
                // FIX #4: Skip if recently synced
                if (recentlySyncedFiles.count(diff.nodePath)) {
                    std::cout << "Already synced this cycle: " << diff.nodePath << "\n";
                    continue;
                }
 
                fs::path target = localFolder / diff.nodePath;
 
                if (diff.hasConflict) {
                    if (!handleConflict(diff, localFolder, peer.port, peer.ip.c_str())) {
                        std::cout << "[CONFLICT] Could not fully resolve: " << diff.nodePath << "\n";
                    }
                    recentlySyncedFiles.insert(diff.nodePath);
                    anyChanges = true;
                    continue;
                }
 
                switch (diff.type) {
                    case DiffType::LOCAL_ONLY:
                        if (diff.isDirectory) {
                            std::cout << "[+] Local directory: " << diff.nodePath << "\n";
                        } else {
                            std::cout << "[↑] Uploading: " << diff.nodePath << "\n";
                            if (uploadFile(diff.nodePath, localFolder, peer.port, peer.ip.c_str())) {
                                recentlySyncedFiles.insert(diff.nodePath);
                                anyChanges = true;
                            }
                        }
                        break;
 
                    case DiffType::REMOTE_ONLY:
                        if (diff.isDirectory) {
                            std::cout << "[+] Creating directory: " << diff.nodePath << "\n";
                            fs::create_directories(target);
                        } else {
                            std::cout << "[↓] Downloading: " << diff.nodePath << "\n";
                            if (downloadFile(diff.nodePath, localFolder, peer.port, peer.ip.c_str())) {
                                recentlySyncedFiles.insert(diff.nodePath);
                                anyChanges = true;
                            }
                        }
                        break;
 
                    case DiffType::MODIFIED:
                        std::cout << "File modified: " << diff.nodePath << "\n";
                        recentlySyncedFiles.insert(diff.nodePath);
                        break;
 
                    case DiffType::DELETED:
                        // Needs tombstone implementation
                        break;
                }
            }
 
            std::cout << "Sync cycle complete with " << peer.ip << ":" << peer.port << "\n";
        }
 
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCleanup).count() > 15) {
            recentlySyncedFiles.clear();
            lastCleanup = now;
        }
        if (anyChanges) {
            std::cout << "Changes made, waiting 5 seconds for peer to update...\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }
}
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <sync_folder> <my_port> <nodeId> [peer_ip:port ...]\n\n";
        std::cerr << "Example for Testing on Same Laptop:\n";
        std::cerr << "  Terminal 1: " << argv[0] << " C:\\path\\to\\f1 8080 1 127.0.0.1:8081\n";
        std::cerr << "  Terminal 2: " << argv[0] << " C:\\path\\to\\f2 8081 2 127.0.0.1:8080\n";
        std::cerr << "NodeId must be unique across all peers" << std::endl;
        return 1;
    }

    fs::path targetFolder = argv[1];
    int myPort = std::stoi(argv[2]);
    uint32_t nodeId = std::stoi(argv[3]);
    if (!fs::exists(targetFolder)) {
        fs::create_directories(targetFolder);
    }
    std::vector<peerEndPoints> peerIPs;
    for (int i = 4; i < argc; ++i) {
        std::string s = argv[i];
        size_t colonPos = s.find(':');
        if (colonPos != std::string::npos) {
            std::string ip = s.substr(0, colonPos);
            int port = std::stoi(s.substr(colonPos + 1));
            peerIPs.push_back({ip, port});
        }
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif
    std::cout << "========================================\n";
    std::cout << "P2P File Sync\n";
    std::cout << "Node ID: " << nodeId << "\n";
    std::cout << "Port: " << myPort << "\n";
    std::cout << "Folder: " << targetFolder << "\n";
    std::cout << "Peers: " << peerIPs.size() << "\n";
    std::cout << "========================================\n";
    std::thread serverThread([targetFolder, myPort, nodeId]() {
        runServer(targetFolder, myPort, nodeId);
    });
    serverThread.detach();
    runSync(targetFolder, peerIPs, nodeId);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
