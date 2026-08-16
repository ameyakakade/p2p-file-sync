#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <time.h>

namespace fs = std::filesystem;

uint64_t calculateBufferHash(const char* data, size_t length) {
    uint64_t hash = 14695981039346656037ULL;
    const uint64_t fnvPrime = 1099511628211ULL;

    for (size_t i = 0; i < length; ++i) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(data[i]));
        hash *= fnvPrime;
    }

    return hash;
}

uint64_t calculateStringHash(const std::string& inp){
    return calculateBufferHash(inp.data(), inp.size());
}

uint64_t hashFile(const fs::path & filepath){
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if(!file.is_open()){
        std::cerr << "Could not open file\n";
        return 0;
    }
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buffer(fileSize);
    if(!file.read(buffer.data(), fileSize)) return 0;
    // TODO: Write a function that reads the file in chunks when hashing
    return calculateBufferHash(buffer.data(), buffer.size());
}

std::vector<fs::path> getAllFiles(const fs::path& dir){
    std::vector<fs::path> filePaths;
    if(!fs::is_directory(dir) || !fs::exists(dir)){
        std::cout << "Invalid Directory! \n";
        return filePaths;
    }
    for(const auto& file : fs::recursive_directory_iterator(dir)){
        if(file.is_regular_file()){
            filePaths.push_back(file.path());
        }
    }
    std::sort(filePaths.begin(), filePaths.end());
    return filePaths;
}

struct MerkleTreeNode{
    fs::path         nodePath;
    bool             isDirectory;
    uint64_t         hash;
    std::vector<int> children;
};

class MerkleTree {
  public:
    std::vector<MerkleTreeNode> pool;

    // bool checkIfEqual(const MerkleTree& other) {
    //     return hash == other.hash;
    // }

    int buildTree(const fs::path& path){
        MerkleTreeNode curr;
        int i = pool.size();
        pool.push_back(curr);
        uint64_t manifest = calculateStringHash(path);
        curr.nodePath = path;

        if(fs::is_directory(path)) {
            curr.isDirectory = true;

            if(!fs::exists(path) || !fs::is_directory(path)){
                std::cerr << "Error: Invalid directory path -> " << path << "\n";
                return -1;
            }

            std::error_code ec;
            std::vector<fs::directory_entry> entries;
 
           for(const auto& entry : fs::directory_iterator(path, ec)){
                entries.push_back(entry);
            }

            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.path().string() < b.path().string();
            }); // to ensure consistency in calculating the merkle tree

            for(const auto& entry : entries){
                int childNode = buildTree(entry.path());
                curr.children.push_back(childNode);
                int typeTag = pool[childNode].isDirectory ? 0 : 1;
                manifest += typeTag + calculateStringHash(pool[childNode].nodePath) + pool[childNode].hash;
            }
            curr.hash = manifest;

        } else if(fs::is_regular_file(path)) {
            curr.isDirectory = false;
            curr.hash = hashFile(path);
        }

        pool[i] = curr;

        return i;
    }

    void printMerkleTree(int node, int depth = 0){
        for(int i = 0; i<depth*4; i++){ std::cout << ' ';}
        if(pool[node].isDirectory){
            std::cout << "[DIR]  " << pool[node].nodePath << " (Folder Hash: " << pool[node].hash << ")\n";
            for(auto& child : pool[node].children){
                printMerkleTree(child, depth+1);
            }
        } else{
            std::cout << "->     " << pool[node].nodePath << " (File Hash: " << pool[node].hash << ")\n";
        }
    }
};

const std::string tempDirs[]  = {"dir1", "dir2", "dir3"};
const std::string tempDirs2[] = {"nd1" , "nd2" , "nd3"};

void traverseDirectory(fs::path directoryPath, int depth) {
    for(auto dir : fs::directory_iterator(directoryPath)) {
        for(int i=0; i<depth*4; i++) putchar(' ');
        printf("'%s'\n", dir.path().c_str());
        if(dir.is_directory()) {
            traverseDirectory(dir.path(), depth+1);
        }
    }
}

int main(int argv, char** argc) {
    bool removeTempDir  = false;
    bool createTempDirs = true;
    if(argv > 1) {
        std::string i = argc[1];
        if(i == "rmd") removeTempDir = true;
    }

    const fs::path tempDir{"temp"};
    fs::create_directory(tempDir); // may error

    if(createTempDirs) {
        printf("Creating temporary directories.\n");
        for(auto it : tempDirs) {
            for(auto it2 : tempDirs2) {
                fs::create_directories(tempDir/it/it2); // may error
            }
        }
    }

    MerkleTree old;
    old.buildTree(tempDir);
    old.printMerkleTree(0);
    for (auto& a : old.pool) {
        for(int b : a.children) {
            std::cout << " " << b;
        }
        std::cout << a.nodePath << "\n";
    }
    
    // while(true) {
    //     MerkleTree root;
    //     root.buildTree(tempDir);
    //     //std::cout << root.name;
    //     if (!root.checkIfEqual(old)) {
    //         time_t my_time = time(NULL);
    //         std::cout << "CHANGE DETECTED ";
    //         printf("At time, %s ", ctime(&my_time));
    //     }
    //     old = root;
    // }

    printf("Traversing directory tree\n");
    // traverseDirectory(tempDir, 0);

    if(removeTempDir) {
        printf("Deleting temporary directories.\n");
        fs::remove_all(tempDir);
    }
    return 0;
}
