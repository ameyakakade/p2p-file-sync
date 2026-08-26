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
#include "parser.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
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

namespace fs = std::filesystem;

const uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
const uint64_t FNV_PRIME        = 1099511628211ULL;

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

// merkle tree structure

struct MerkleTreeNode {
    fs::path         nodePath;      
    bool             isDirectory;
    uint64_t         hash;
    std::vector<int> children;     // Indices into the pool vector
};

enum class DiffType {
    ADDED,      // Exists locally, missing remote
    MODIFIED,   // Content/hash mismatch
    DELETED    // Exists locally, missing in Remote
};

struct FileDifference {
    std::string nodePath;
    DiffType    type;
    bool isDirectory;
};

class MerkleTree {
  public:
    std::vector<MerkleTreeNode> pool;

    void clear() {
        pool.clear();
    }
    
    int buildTree(const fs::path& path, const fs::path& rootPath = "") {
        fs::path baseRoot = rootPath.empty() ? path : rootPath;

        if (!fs::exists(path)) {
            std::cerr << "Error: Path does not exist -> " << path << "\n";
            return -1;
        }

        MerkleTreeNode curr;
        curr.nodePath = fs::relative(path, baseRoot);
        curr.hash = FNV_OFFSET_BASIS;

        int myIndex = static_cast<int>(pool.size());
        pool.push_back(curr); 

        if (fs::is_directory(path)) {
            curr.isDirectory = true;

            std::error_code ec;
            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(path, ec)) {
                entries.push_back(entry);
            }
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.path().filename().string() < b.path().filename().string();
            });

            // Mix directory name into hash
            std::string dirName = curr.nodePath.empty() ? "." : curr.nodePath.filename().string();
            curr.hash = combineHash(curr.hash, calculateStringHash(dirName));

            for (const auto& entry : entries) {
                int childIndex = buildTree(entry.path(), baseRoot);
                if (childIndex == -1) continue;

                curr.children.push_back(childIndex);
                uint64_t typeTag = pool[childIndex].isDirectory ? 0xAA : 0x55;
                curr.hash = combineHash(curr.hash, typeTag);
                curr.hash = combineHash(curr.hash, pool[childIndex].hash);
            }

        } else if (fs::is_regular_file(path)) {
            curr.isDirectory = false;
            curr.hash = hashFile(path);
        }

        pool[myIndex] = curr; // Save updated data back into reserved index
        return myIndex;
    }

    void buildTreeString(const char* a) {
        clear();
        Sp sp = createSp(a);

        FIELD_PARSE(&sp, "poolSize", pool.resize(natParser(&sp));)
            while(gc(&sp) != '\0') {
                OBJECT_PARSE(&sp,
                             int i = 0;
                             MerkleTreeNode curr;
                             FIELD_PARSE(&sp, "index",       i = natParser(&sp);)
                             FIELD_PARSE(&sp, "nodePath",    curr.nodePath = stringParser(&sp);)
                             FIELD_PARSE(&sp, "isDirectory", curr.isDirectory = boolParser(&sp);)
                             FIELD_PARSE(&sp, "hash",        curr.hash = natParser(&sp);)
                             FIELD_PARSE(&sp, "children",
                                         ARRAY_PARSE(&sp, curr.children.push_back(natParser(&sp));)
                                 )
                             pool[i] = curr;
                    )
                    }
    }

    std::string dumpTreeString() {
        std::ostringstream os;
        os << "poolSize:" << pool.size();
        for(int i=0; (size_t)i<pool.size(); i++) {
            os << "{";
            os << "index:" << i << ",";
            os << "nodePath:" << pool[i].nodePath << ",";
            os << "isDirectory:" << (pool[i].isDirectory ? "true" : "false") << ",";
            os << "hash:" << pool[i].hash << ",";
            os << "children:[";
            for(int child : pool[i].children) {
                os << child << ",";
            }
            os << "]";
            os << "}";
        }
        return os.str();
    }

    bool checkIfEqual(const MerkleTree& other) const {
        if (pool.empty() || other.pool.empty()) return false;
        return pool[0].hash == other.pool[0].hash;
    }

    void printMerkleTree(int nodeIndex = 0, int depth = 0) const {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(pool.size())) return;

        for (int i = 0; i < depth * 4; i++) std::cout << ' ';
        const auto& node = pool[nodeIndex];

        std::string displayPath = node.nodePath.empty() ? "." : node.nodePath.generic_string();

        if (node.isDirectory) {
            std::cout << "[DIR]  " << displayPath 
                      << " (Hash: 0x" << std::hex << std::setw(16) << std::setfill('0') 
                      << node.hash << std::dec << ")\n";
            for (int child : node.children) {
                printMerkleTree(child, depth + 1);
            }
        } else {
            std::cout << "[FILE] " << displayPath 
                      << " (Hash: 0x" << std::hex << std::setw(16) << std::setfill('0') 
                      << node.hash << std::dec << ")\n";
        }
    }
    static void collectAll(const MerkleTree& tree, int idx, DiffType type, std::vector<FileDifference>& diffs){
        if(idx<0 || idx>=static_cast<int>(tree.pool.size())){
            return;
        }
        const auto node = tree.pool[idx];
        diffs.push_back({
            node.nodePath.generic_string(),
            type,
            node.isDirectory
        });
        if(node.isDirectory){
            for(int childidx : node.children){
                collectAll(tree, childidx, type, diffs);
            }
        }
    }
    static void compareNodes(const MerkleTree& localTree, int localIdx,
                             const MerkleTree& remoteTree, int remoteIdx,
                             std::vector<FileDifference>& diffs) {
        const auto& lNode = localTree.pool[localIdx];
        const auto& rNode = remoteTree.pool[remoteIdx];

        // If subtree hashes match, skip checking this entire branch
        if (lNode.hash == rNode.hash) return;

        // Leaf file comparison
        if (!lNode.isDirectory && !rNode.isDirectory) {
            diffs.push_back({lNode.nodePath.generic_string(), DiffType::MODIFIED, false});
            return;
        }

        // Map child relative paths to their pool index for remote directory
        std::unordered_map<std::string, int> remoteChildrenMap;
        for (int rChild : rNode.children) {
            remoteChildrenMap[remoteTree.pool[rChild].nodePath.generic_string()] = rChild;
        }

        // Check local children against remote
        for (int lChild : lNode.children) {
            std::string lPath = localTree.pool[lChild].nodePath.generic_string();
            auto it = remoteChildrenMap.find(lPath);

            if (it == remoteChildrenMap.end()) {
                // Present locally but absent in remote
                collectAll(localTree, lChild, DiffType::DELETED, diffs);
            } else {
                // Present in both : dive deeper into children
                compareNodes(localTree, lChild, remoteTree, it->second, diffs);
                remoteChildrenMap.erase(it); // Mark as checked
            }
        }

        // Any remaining items in remoteChildrenMap exist only in remote
        for (const auto& [rPath, rChild] : remoteChildrenMap) {
            collectAll(remoteTree, rChild, DiffType::ADDED, diffs);  
        }
    }

    static std::vector<FileDifference> findDifferences(const MerkleTree& localTree, const MerkleTree& remoteTree) {
        std::vector<FileDifference> diffs;
        if (localTree.pool.empty() || remoteTree.pool.empty()) return diffs;
        compareNodes(localTree, 0, remoteTree, 0, diffs);
        return diffs;
    }

};

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
int runClient(const fs::path& localFolder) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    while (true) {
        MerkleTree localTree;
        localTree.buildTree(localFolder);

        // Step 1: Request Merkle Tree from server
        SOCK client_socket = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(8080);
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

        if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) != ERR_SOCK) {
            std::string req = "GET_TREE";
            send(client_socket, req.c_str(), static_cast<int>(req.size()), 0);

            std::vector<char> treeBuf(1024 * 64, 0);
            int r = recv(client_socket, treeBuf.data(), static_cast<int>(treeBuf.size()) - 1, 0);

#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif

            if (r > 0) {
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
                                downloadFile(diff.nodePath, localFolder, 8080, "127.0.0.1");
                            }
                            
                        } else if (diff.type == DiffType::DELETED) {
                            std::cout << "[✕] Deleting local: " << diff.nodePath << "\n";
                            std::error_code ec;
                            fs::remove_all(targetFilePath, ec);
                        }
                    }
                    std::cout << "[✓] Sync complete.\n";
                } else {
                    std::cout << "[✓] Local folder is up-to-date.\n";
                }
            }
        }

        // Checking every 5 seconds
        #ifdef _WIN32
            Sleep(5000);
        #else
            usleep(5000*1000);
        #endif
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

int runServer(const fs::path& localFolder) {
    MerkleTree localTree;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    SOCK server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 5);

    std::cout << "Server listening on port 8080 for folder: " << localFolder << "\n";

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
        int bytesRead = recv(client_socket, reqBuff, sizeof(reqBuff), 0);
        if(bytesRead > 0){ // means there is a request by the client
            std::string request(reqBuff, bytesRead);
            if(request=="GET_TREE"){ // send the merkle tree
                localTree.clear();
                localTree.buildTree(localFolder);
                std::string serialisedTree = localTree.dumpTreeString();
                sendAll(client_socket, serialisedTree.c_str(), serialisedTree.size());
            } else if(request.substr(0, 9) == "GET_FILE "){ // send the file
                std::string relativePath = request.substr(9);
                fs::path fullPath = localFolder / relativePath;
                std::cout << "Sending File: " << relativePath << "\n";
                sendFileOverSocket(client_socket, fullPath);
            }
        }
        #ifdef _WIN32
            closesocket(client_socket);
        #else   
            close(client_socket);
        #endif
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage:\n";
        std::cerr << "  Server: " << argv[0] << " --server <path_to_folder>\n";
        std::cerr << "  Client: " << argv[0] << " --client <path_to_folder>\n";
        return 1;
    }

    std::string mode = argv[1];
    fs::path targetFolder = argv[2];

    if (!fs::exists(targetFolder)) {
        std::cerr << "Error: Directory does not exist -> " << targetFolder << "\n";
        return 1;
    }

    if (mode == "--server" || mode == "-s") {
        return runServer(targetFolder);
    } else if (mode == "--client" || mode == "-c") {
        return runClient(targetFolder);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        std::cerr << "Use --server or --client\n";
        return 1;
    }
}
