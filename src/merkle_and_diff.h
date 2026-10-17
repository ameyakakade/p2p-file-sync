#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma once

#include "filehashing.h"
#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Persistent per-path causal state. Tombstones remain in SyncState, but are
// intentionally excluded from the content Merkle tree.
struct SyncRecord {
    std::string path;
    uint64_t hash = 0;
    bool deleted = false;
    VectorClock clock;
};
using SyncState = std::map<std::string, SyncRecord>;
constexpr const char* SYNC_STATE_FILENAME = ".p2psync_state";

std::string encodeHex(const std::string& value);
bool decodeHex(const std::string& encoded, std::string& value);
bool isSafeRelativePath(const fs::path& path);
SyncState loadState(const fs::path& root);
bool saveState(const fs::path& root, const SyncState& state);
std::string serializeState(const SyncState& state);
bool deserializeState(const std::string& serialized, SyncState& state);
bool scanLocalChanges(const fs::path& root, uint32_t nodeId, SyncState& state);

enum class DiffType { LOCAL_ONLY, REMOTE_ONLY, MODIFIED, DELETED };

struct FileDifference {
    std::string nodePath;
    DiffType type = DiffType::MODIFIED;
    bool isDirectory = false;
    bool hasConflict = false;
};

struct MerkleTreeNode {
    fs::path nodePath;              // Path relative to the tree root.
    bool isDirectory = false;
    uint64_t hash = FNV_OFFSET_BASIS;
    uint64_t mtime = 0;
    std::vector<int> children;      // Indices into MerkleTree::pool.
    VectorClock vectorClock;        // Kept for source compatibility; not mutated by tree scans.
    bool isDeleted = false;
};

class MerkleTree {
public:
    explicit MerkleTree(uint32_t id = 0) : nodeId(id) {}

    uint32_t nodeId = 0;
    std::vector<MerkleTreeNode> pool;

    void clear();
    int buildTree(const fs::path& path, const fs::path& rootPath = "");
    // Build a content tree from persisted sync records (used for the peer tree).
    int buildTreeFromState(const fs::path& root, const SyncState& state);
    void buildTreeString(const char* serialized);
    std::string dumpTreeString() const;
    bool checkIfEqual(const MerkleTree& other) const;
    void printMerkleTree(int nodeIndex = 0, int depth = 0) const;

    static std::vector<FileDifference> findDifferences(const MerkleTree& localTree,
                                                        const MerkleTree& remoteTree);
    static void collectAll(const MerkleTree& tree, int idx, DiffType type,
                           std::vector<FileDifference>& diffs);
    static void compareNodes(const MerkleTree& localTree, int localIdx,
                             const MerkleTree& remoteTree, int remoteIdx,
                             std::vector<FileDifference>& diffs);
};
