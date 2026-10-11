#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
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
#include "networking.h"

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
