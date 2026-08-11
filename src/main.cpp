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

class MerkleTree{
  public:
    std::string             name;
    fs::path                fullpath;
    bool                    isDirectory;
    uint64_t                hash;
    std::vector<MerkleTree> children;

    void buildTree(const fs::path& dirPath){
        name = dirPath.filename().empty() ? dirPath.string() : dirPath.filename().string();
        fullpath = dirPath;
        isDirectory = true;

        if(!fs::exists(dirPath) || !fs::is_directory(dirPath)){
            std::cerr << "Error: Invalid directory path -> " << dirPath << "\n";
            return;
        }
        std::error_code ec;
        std::vector<fs::directory_entry> entries;
        for(const auto& entry : fs::directory_iterator(dirPath, ec)){
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.path().filename().string() < b.path().filename().string();
        }); // to ensure consistency in calculating the merkle tree

        std::string manifest = "";
        for(const auto& entry : entries){
            MerkleTree childNode;
            childNode.name = entry.path().filename().string();
            childNode.fullpath = entry.path();
            if(entry.is_directory(ec)){
                childNode.isDirectory = true;
                childNode.buildTree(entry.path());
            } else if(entry.is_regular_file(ec)){
                childNode.isDirectory = false;
                childNode.hash = hashFile(entry.path());
            } else{
                continue;
            }
            children.push_back(childNode);

            int typeTag = childNode.isDirectory ? 0 : 1;
            manifest += typeTag + calculateStringHash(childNode.name) + childNode.hash;
        
        }
        hash = calculateStringHash(manifest);
    }

    void printMerkleTree(int depth = 0){
        for(int i = 0; i<depth*4; i++){ std::cout << ' ';}
        if(isDirectory){
            std::cout << "[DIR]  " << name << " (Folder Hash: " << hash << ")\n";
            for(auto& child : children){
                child.printMerkleTree(depth+1);
            }
        } else{
            std::cout << "->     " << name << " (File Hash: " << hash << ")\n";
        }
    }

    bool checkIfEqual(const MerkleTree& other) {
        return hash == other.hash;
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
    old.buildTree("../baskell");
    
    while(true) {
        MerkleTree root;
        root.buildTree("../baskell");
        //std::cout << root.name;
        if (!root.checkIfEqual(old)) {
            time_t my_time = time(NULL);
            std::cout << "CHANGE DETECTED ";
            printf("At time, %s ", ctime(&my_time));
        }
        old = root;
        //root.printMerkleTree(0);
    }

    printf("Traversing directory tree\n");
    traverseDirectory(tempDir, 0);

    if(removeTempDir) {
        printf("Deleting temporary directories.\n");
        fs::remove_all(tempDir);
    }
    return 0;
}
