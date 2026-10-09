#include <filesystem>
#include <vector>
#include "filehashing.h"

namespace fs = std::filesystem;

enum class ConflictResolution{
    REMOTE_WINS,
    LOCAL_WINS,
    KEEP_BOTH,
    VECTOR_CLOCK
};

struct ConflictRecord{
    std::string filepath;
    uint64_t localHash = 0;
    uint64_t remoteHash = 0;
    VectorClock localClock;
    VectorClock remoteClock;
    ConflictResolution resolution;
    std::chrono::system_clock::time_point timestamp;
};


struct MerkleTreeNode {
    fs::path         nodePath;      
    bool             isDirectory;
    uint64_t         hash;
    uint64_t mtime = 0;
    std::vector<int> children;     // Indices into the pool vector
    VectorClock vectorClock;
    bool isDeleted = false;
};

enum class DiffType {
    LOCAL_ONLY,
    REMOTE_ONLY,
    MODIFIED,   // Content/hash mismatch
    DELETED    // Exists locally, missing in Remote
};

struct FileDifference {
    std::string nodePath;
    DiffType    type;
    bool isDirectory;
    bool hasConflict = false;
    ConflictRecord conflict;
};

class MerkleTree {
  public:
    uint32_t nodeId;
    std::vector<ConflictRecord>conflicts;
    MerkleTree(uint32_t id = 0) : nodeId(id){}
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
