#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

struct MerkleTreeNode {
    fs::path         nodePath;      
    bool             isDirectory;
    uint64_t         hash;
    uint64_t mtime = 0;
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
    void clear();
    int buildTree(const fs::path& path, const fs::path& rootPath = "");
    void buildTreeString(const char* a);
    std::string dumpTreeString();
    bool checkIfEqual(const MerkleTree& other);
    void printMerkleTree(int nodeIndex = 0, int depth = 0);
    static void compareNodes(const MerkleTree& localTree, int localid);
    static std::vector<FileDifference> findDifferences(const MerkleTree& localTree, const MerkleTree& remoteTree);
    static void collectAll(const MerkleTree& tree, int idx, DiffType type, std::vector<FileDifference>& diffs);
    static void compareNodes(const MerkleTree& localTree, int localIdx,
                                         const MerkleTree& remoteTree, int remoteIdx,
                                         std::vector<FileDifference>& diffs);
};
