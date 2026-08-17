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
    fs::path         relPath;      
    bool             isDirectory;
    uint64_t         hash;
    std::vector<int> children;     // Indices into the pool vector
};

enum class DiffType {
    ADDED,      // Exists in Remote, missing locally
    MODIFIED,   // Content/hash mismatch
    DELETED     // Exists locally, missing in Remote
};

struct FileDifference {
    std::string relPath;
    DiffType    type;
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
        curr.relPath = fs::relative(path, baseRoot);
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
            std::string dirName = curr.relPath.empty() ? "." : curr.relPath.filename().string();
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

    bool checkIfEqual(const MerkleTree& other) const {
        if (pool.empty() || other.pool.empty()) return false;
        return pool[0].hash == other.pool[0].hash;
    }

    void printMerkleTree(int nodeIndex = 0, int depth = 0) const {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(pool.size())) return;

        for (int i = 0; i < depth * 4; i++) std::cout << ' ';
        const auto& node = pool[nodeIndex];

        std::string displayPath = node.relPath.empty() ? "." : node.relPath.generic_string();

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
    
    static void compareNodes(const MerkleTree& localTree, int localIdx,
                             const MerkleTree& remoteTree, int remoteIdx,
                             std::vector<FileDifference>& diffs) {
        const auto& lNode = localTree.pool[localIdx];
        const auto& rNode = remoteTree.pool[remoteIdx];

        // If subtree hashes match, skip checking this entire branch
        if (lNode.hash == rNode.hash) return;

        // Leaf file comparison
        if (!lNode.isDirectory && !rNode.isDirectory) {
            diffs.push_back({lNode.relPath.generic_string(), DiffType::MODIFIED});
            return;
        }

        // Map child relative paths to their pool index for remote directory
        std::unordered_map<std::string, int> remoteChildrenMap;
        for (int rChild : rNode.children) {
            remoteChildrenMap[remoteTree.pool[rChild].relPath.generic_string()] = rChild;
        }

        // Check local children against remote
        for (int lChild : lNode.children) {
            std::string lPath = localTree.pool[lChild].relPath.generic_string();
            auto it = remoteChildrenMap.find(lPath);

            if (it == remoteChildrenMap.end()) {
                // Present locally but absent in remote
                diffs.push_back({lPath, DiffType::DELETED});
            } else {
                // Present in both : dive deeper into children
                compareNodes(localTree, lChild, remoteTree, it->second, diffs);
                remoteChildrenMap.erase(it); // Mark as checked
            }
        }

        // Any remaining items in remoteChildrenMap exist only in remote
        for (const auto& [rPath, rChild] : remoteChildrenMap) {
            diffs.push_back({rPath, DiffType::ADDED});
        }
    }

    static std::vector<FileDifference> findDifferences(const MerkleTree& localTree, const MerkleTree& remoteTree) {
        std::vector<FileDifference> diffs;
        if (localTree.pool.empty() || remoteTree.pool.empty()) return diffs;
        compareNodes(localTree, 0, remoteTree, 0, diffs);
        return diffs;
    }
};

int main() {
    fs::path localFolder = "test_local";
    fs::path remoteFolder = "test_remote";

    //mock local directory
    fs::create_directories(localFolder / "docs");
    fs::create_directories(localFolder / "photos");
    {
        std::ofstream(localFolder / "docs" / "report.txt") << "Local version of report";
        std::ofstream(localFolder / "docs" / "notes.txt")  << "Shared notes text";
        std::ofstream(localFolder / "photos" / "old.png")  << "Binary photo data";
    }

    //mock remote peer directory 
    fs::create_directories(remoteFolder / "docs");
    fs::create_directories(remoteFolder / "photos");
    {
        std::ofstream(remoteFolder / "docs" / "report.txt") << "Remote MODIFIED version"; // MODIFIED
        std::ofstream(remoteFolder / "docs" / "notes.txt")  << "Shared notes text";        // SAME
        std::ofstream(remoteFolder / "photos" / "new.jpg")  << "New photo uploaded";      // ADDED in remote
        // photos/old.png is missing in remote                                             // DELETED
    }
    
    MerkleTree localTree;
    localTree.buildTree(localFolder);

    MerkleTree remoteTree;
    remoteTree.buildTree(remoteFolder);

    std::cout << "Local Tree" << std::endl;
    localTree.printMerkleTree(0);

    std::cout << "\nRemote Tree" << std::endl;
    remoteTree.printMerkleTree(0);

    std::cout << "\nRoot Hash Comparison : " << std::endl;
    if (localTree.checkIfEqual(remoteTree)) {
        std::cout << "All folders and files are in sync!\n";
    } else {
        std::cout << "Root mismatch detected!\n\n";
        
        std::vector<FileDifference> diffs = MerkleTree::findDifferences(localTree, remoteTree);
        for (const auto& diff : diffs) {
            if (diff.type == DiffType::ADDED) {
                std::cout << "[+] ADDED on Remote   : " << diff.relPath << " (Needs download)\n";
            } else if (diff.type == DiffType::MODIFIED) {
                std::cout << "[*] MODIFIED on Remote: " << diff.relPath << " (Needs update)\n";
            } else if (diff.type == DiffType::DELETED) {
                std::cout << "[-] MISSING on Remote : " << diff.relPath << " (Local-only / deleted)\n";
            }
        }
    }

    
    fs::remove_all(localFolder);
    fs::remove_all(remoteFolder);

    return 0;
}