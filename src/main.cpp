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

void runServer(const fs::path& localFolder, int port = 8080) {
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

void runSync(const fs::path& localFolder,const std::vector<peerEndPoints>& peers){
    while(true){
        for(const auto& peer : peers){
            MerkleTree localTree;
            localTree.buildTree(localFolder);
            SOCK client_socket = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(peer.port);
            inet_pton(AF_INET, peer.ip.c_str(), &server_addr.sin_addr);
            if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) != ERR_SOCK){
                std::string req = "GET_TREE";
                send(client_socket, req.c_str(), static_cast<int>(req.size()), 0);
                uint64_t treeSize = 0;
                if(recvAll(client_socket, reinterpret_cast<char*>(&treeSize), sizeof(treeSize)) && treeSize>0){
                    std::vector<char>treeBuf(treeSize+1, 0);   
                    if(recvAll(client_socket, treeBuf.data(), treeSize)){
                        #ifdef _WIN32
                            closesocket(client_socket);
                        #else   
                            close(client_socket);
                        #endif
                        MerkleTree remoteTree;
                        remoteTree.buildTreeString(treeBuf.data());
                        if (!localTree.checkIfEqual(remoteTree)) {
                            std::cout << "\n[!] Sync discrepancy detected. Resolving...\n";

                            std::vector<FileDifference> diffs = MerkleTree::findDifferences(localTree, remoteTree);

                            for (const auto& diff : diffs) {
                                fs::path targetFilePath = localFolder / diff.nodePath;

                                if (diff.type == DiffType::ADDED || diff.type == DiffType::MODIFIED) {
                                    if(diff.isDirectory){
                                        std::cout << "[+] Creating Directory : " << diff.nodePath << "\n";
                                        fs::create_directories(targetFilePath);
                                    } else{
                                        std::cout << "[↓] Downloading: " << diff.nodePath << "\n";
                                        downloadFile(diff.nodePath, localFolder, peer.port, peer.ip.c_str());
                                    }
                                } else if (diff.type == DiffType::DELETED) {
                                    std::cout << "[✕] Deleting local: " << diff.nodePath << "\n";
                                    std::error_code ec;
                                    fs::remove_all(targetFilePath, ec);
                                }
                            }
                            std::cout << "[✓] Sync complete.\n";
                        }
                        continue;
                    }
                }
                #ifdef _WIN32
                    closesocket(client_socket);
                #else
                    close(client_socket);
                #endif
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <sync_folder> <my_port> [peer_ip:port ...]\n\n";
        std::cerr << "Example for Testing on Same Laptop:\n";
        std::cerr << "  Terminal 1: " << argv[0] << " C:\\path\\to\\f1 8080 127.0.0.1:8081\n";
        std::cerr << "  Terminal 2: " << argv[0] << " C:\\path\\to\\f2 8081 127.0.0.1:8080\n";
        return 1;
    }

    fs::path targetFolder = argv[1];
    int myPort = std::stoi(argv[2]);

    if (!fs::exists(targetFolder)) {
        fs::create_directories(targetFolder);
    }
    std::vector<peerEndPoints> peerIPs;
    for (int i = 3; i < argc; ++i) {
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

    std::thread serverThread([targetFolder, myPort]() {
        runServer(targetFolder, myPort);
    });
    serverThread.detach();
    runSync(targetFolder, peerIPs);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
