#include "merkle_and_diff.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>

namespace {
fs::path statePath(const fs::path& root) { return root / SYNC_STATE_FILENAME; }

bool shouldSkip(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    const fs::path rel = fs::relative(path, root, ec);
    if (ec) return true;
    const std::string s = rel.generic_string();
    return s == SYNC_STATE_FILENAME || s == std::string(SYNC_STATE_FILENAME) + ".tmp" ||
           s.rfind(".p2psync_download_", 0) == 0 ||
           s.find("/.p2psync_download_") != std::string::npos;
}

void applyPathSafety(const std::string& path) {
    if (path.empty() || !isSafeRelativePath(fs::path(path)))
        throw std::runtime_error("unsafe relative path");
}

struct TempNode {
    fs::path path;
    bool isDirectory = true;
    uint64_t fileHash = FNV_OFFSET_BASIS;
    uint64_t mtime = 0;
    std::map<std::string, std::unique_ptr<TempNode>> children;
};

void insertStateFile(TempNode& root, const std::string& relative, uint64_t hash) {
    fs::path p(relative);
    TempNode* current = &root;
    fs::path accumulated;
    for (auto it = p.begin(); it != p.end(); ++it) {
        const std::string part = it->string();
        accumulated /= *it;
        auto& slot = current->children[part];
        if (!slot) {
            slot = std::make_unique<TempNode>();
            slot->path = accumulated;
            slot->isDirectory = true;
        }
        current = slot.get();
        if (std::next(it) == p.end()) {
            current->isDirectory = false;
            current->fileHash = hash;
            current->children.clear();
        }
    }
}

int flattenTempNode(const TempNode& source, std::vector<MerkleTreeNode>& pool) {
    MerkleTreeNode node;
    node.nodePath = source.path;
    node.isDirectory = source.isDirectory;
    node.hash = source.isDirectory ? FNV_OFFSET_BASIS : source.fileHash;
    node.mtime = source.mtime;
    const int index = static_cast<int>(pool.size());
    pool.push_back(node);

    if (source.isDirectory) {
        const std::string name = source.path.empty() ? "." : source.path.filename().string();
        uint64_t hash = combineHash(FNV_OFFSET_BASIS, calculateStringHash(name));
        for (const auto& [childName, child] : source.children) {
            (void)childName;
            const int childIndex = flattenTempNode(*child, pool);
            pool[static_cast<size_t>(index)].children.push_back(childIndex);
            const auto& childNode = pool[static_cast<size_t>(childIndex)];
            hash = combineHash(hash, childNode.isDirectory ? 0xAAULL : 0x55ULL);
            hash = combineHash(hash, childNode.hash);
        }
        pool[static_cast<size_t>(index)].hash = hash;
    }
    return index;
}

bool isTreeInternalPath(const fs::path& relative) {
    const std::string s = relative.generic_string();
    return s == SYNC_STATE_FILENAME || s == std::string(SYNC_STATE_FILENAME) + ".tmp" ||
           s.rfind(".p2psync_download_", 0) == 0 ||
           s.find("/.p2psync_download_") != std::string::npos;
}
}

std::string encodeHex(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() * 2);
    for (unsigned char c : value) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 0x0f]);
    }
    return out;
}

bool decodeHex(const std::string& encoded, std::string& value) {
    if (encoded.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string decoded;
    decoded.reserve(encoded.size() / 2);
    for (size_t i = 0; i < encoded.size(); i += 2) {
        int hi = nibble(encoded[i]), lo = nibble(encoded[i + 1]);
        if (hi < 0 || lo < 0) return false;
        decoded.push_back(static_cast<char>((hi << 4) | lo));
    }
    value = std::move(decoded);
    return true;
}

bool isSafeRelativePath(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& component : path) if (component == ".." || component == ".") return false;
    const std::string generic = path.generic_string();
    if (generic == SYNC_STATE_FILENAME || generic == std::string(SYNC_STATE_FILENAME) + ".tmp") return false;
    return !generic.empty();
}

std::string serializeState(const SyncState& state) {
    std::ostringstream out;
    out << "P2PSYNC_STATE_1\n";
    for (const auto& [path, record] : state) {
        out << encodeHex(path) << '|' << (record.deleted ? '1' : '0') << '|'
            << record.hash << '|' << record.clock.toString() << '\n';
    }
    return out.str();
}

bool deserializeState(const std::string& serialized, SyncState& state) {
    state.clear();
    std::istringstream in(serialized);
    std::string line;
    if (!std::getline(in, line) || line != "P2PSYNC_STATE_1") return false;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto p1 = line.find('|');
        const auto p2 = p1 == std::string::npos ? p1 : line.find('|', p1 + 1);
        const auto p3 = p2 == std::string::npos ? p2 : line.find('|', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) continue;
        std::string path;
        if (!decodeHex(line.substr(0, p1), path)) continue;
        try {
            applyPathSafety(path);
            SyncRecord record;
            record.path = path;
            record.deleted = line.substr(p1 + 1, p2 - p1 - 1) == "1";
            record.hash = std::stoull(line.substr(p2 + 1, p3 - p2 - 1));
            record.clock = VectorClock::fromString(line.substr(p3 + 1));
            state[path] = std::move(record);
        } catch (...) { continue; }
    }
    return true;
}

SyncState loadState(const fs::path& root) {
    SyncState state;
    std::ifstream in(statePath(root), std::ios::binary);
    if (!in) return state;
    std::ostringstream contents;
    contents << in.rdbuf();
    if (!deserializeState(contents.str(), state)) {
        std::cerr << "Warning: state file is invalid; starting with an empty sync state.\n";
        state.clear();
    }
    return state;
}

bool saveState(const fs::path& root, const SyncState& state) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return false;
    const fs::path target = statePath(root), temp = target.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const std::string data = serializeState(state);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) return false;
    }
    fs::rename(temp, target, ec);
    if (ec) {
        ec.clear(); fs::remove(target, ec); ec.clear(); fs::rename(temp, target, ec);
    }
    if (ec) { fs::remove(temp, ec); return false; }
    return true;
}

bool scanLocalChanges(const fs::path& root, uint32_t nodeId, SyncState& state) {
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return false;
    std::set<std::string> present;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const fs::path path = it->path();
        if (shouldSkip(root, path)) continue;
        std::error_code typeEc;
        if (it->is_symlink(typeEc)) { if (it->is_directory(typeEc)) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file(typeEc)) continue;
        const fs::path relPath = fs::relative(path, root, typeEc);
        if (typeEc || !isSafeRelativePath(relPath)) continue;
        const std::string rel = relPath.generic_string();
        present.insert(rel);
        const uint64_t hash = hashFile(path);
        auto found = state.find(rel);
        if (found == state.end()) {
            SyncRecord record; record.path = rel; record.hash = hash; record.clock.increment(nodeId);
            state[rel] = std::move(record);
        } else if (found->second.deleted) {
            found->second.clock.increment(nodeId); found->second.deleted = false; found->second.hash = hash;
        } else if (found->second.hash != hash) {
            found->second.clock.increment(nodeId); found->second.hash = hash;
        }
    }
    for (auto& [path, record] : state) {
        if (!record.deleted && present.find(path) == present.end()) {
            record.deleted = true; record.hash = 0; record.clock.increment(nodeId);
        }
    }
    return true;
}

void MerkleTree::clear() { pool.clear(); }

int MerkleTree::buildTree(const fs::path& path, const fs::path& rootPath) {
    clear();
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return -1;
    const fs::path base = rootPath.empty() ? path : rootPath;
    TempNode root;
    std::function<bool(const fs::path&, TempNode&)> fill = [&](const fs::path& currentPath, TempNode& out) -> bool {
        std::error_code localEc;
        const fs::path relative = fs::relative(currentPath, base, localEc);
        if (localEc) return false;
        out.path = relative == "." ? fs::path{} : relative;
        if (fs::is_symlink(fs::symlink_status(currentPath, localEc))) return false;
        if (fs::is_directory(currentPath, localEc)) {
            out.isDirectory = true;
            std::vector<fs::directory_entry> entries;
            for (const auto& entry : fs::directory_iterator(currentPath, localEc)) {
                if (localEc) { localEc.clear(); continue; }
                if (shouldSkip(base, entry.path())) continue;
                std::error_code typeEc;
                if (entry.is_symlink(typeEc)) continue;
                entries.push_back(entry);
            }
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
                return a.path().filename().generic_string() < b.path().filename().generic_string();
            });
            for (const auto& entry : entries) {
                auto child = std::make_unique<TempNode>();
                if (!fill(entry.path(), *child)) continue;
                out.children[entry.path().filename().generic_string()] = std::move(child);
            }
            return true;
        }
        if (!fs::is_regular_file(currentPath, localEc) || localEc) return false;
        out.isDirectory = false;
        out.fileHash = hashFile(currentPath);
        const auto time = fs::last_write_time(currentPath, localEc);
        if (!localEc) out.mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count());
        return true;
    };
    if (!fill(path, root)) return -1;
    return flattenTempNode(root, pool);
}

int MerkleTree::buildTreeFromState(const fs::path& root, const SyncState& state) {
    (void)root;
    clear();
    TempNode tempRoot;
    tempRoot.path.clear(); tempRoot.isDirectory = true;
    for (const auto& [path, record] : state) {
        if (record.deleted || !isSafeRelativePath(fs::path(path)) || isTreeInternalPath(path)) continue;
        insertStateFile(tempRoot, path, record.hash);
    }
    return flattenTempNode(tempRoot, pool);
}

std::string MerkleTree::dumpTreeString() const {
    std::ostringstream out;
    out << "MERKLE_TREE_1\n";
    for (size_t i = 0; i < pool.size(); ++i) {
        const auto& node = pool[i];
        out << i << '\t' << encodeHex(node.nodePath.generic_string()) << '\t'
            << (node.isDirectory ? 1 : 0) << '\t' << node.hash << '\t' << node.mtime << '\t';
        for (size_t j = 0; j < node.children.size(); ++j) {
            if (j) out << ',';
            out << node.children[j];
        }
        out << '\n';
    }
    return out.str();
}

void MerkleTree::buildTreeString(const char* serialized) {
    clear();
    if (!serialized) return;
    std::istringstream in(serialized);
    std::string line;
    if (!std::getline(in, line) || line != "MERKLE_TREE_1") return;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields;
        size_t begin = 0;
        while (true) {
            const size_t tab = line.find('\t', begin);
            fields.push_back(line.substr(begin, tab == std::string::npos ? tab : tab - begin));
            if (tab == std::string::npos) break;
            begin = tab + 1;
        }
        if (fields.size() != 6) continue;
        try {
            const size_t index = static_cast<size_t>(std::stoull(fields[0]));
            std::string decodedPath;
            if (!decodeHex(fields[1], decodedPath)) continue;
            MerkleTreeNode node;
            node.nodePath = fs::path(decodedPath);
            node.isDirectory = std::stoi(fields[2]) != 0;
            node.hash = std::stoull(fields[3]);
            node.mtime = std::stoull(fields[4]);
            std::istringstream childStream(fields[5]);
            std::string token;
            while (std::getline(childStream, token, ',')) {
                if (!token.empty()) node.children.push_back(std::stoi(token));
            }
            if (index >= pool.size()) pool.resize(index + 1);
            pool[index] = std::move(node);
        } catch (...) { continue; }
    }
}

bool MerkleTree::checkIfEqual(const MerkleTree& other) const {
    return !pool.empty() && !other.pool.empty() && pool[0].hash == other.pool[0].hash;
}

void MerkleTree::printMerkleTree(int nodeIndex, int depth) const {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= pool.size()) return;
    const auto& node = pool[static_cast<size_t>(nodeIndex)];
    for (int i = 0; i < depth * 4; ++i) std::cout << ' ';
    std::cout << (node.isDirectory ? "[DIR]  " : "[FILE] ")
              << (node.nodePath.empty() ? "." : node.nodePath.generic_string())
              << " (Hash: 0x" << std::hex << std::setw(16) << std::setfill('0') << node.hash << std::dec << ")\n";
    for (int child : node.children) printMerkleTree(child, depth + 1);
}

void MerkleTree::collectAll(const MerkleTree& tree, int idx, DiffType type,
                            std::vector<FileDifference>& diffs) {
    if (idx < 0 || static_cast<size_t>(idx) >= tree.pool.size()) return;
    const auto& node = tree.pool[static_cast<size_t>(idx)];
    diffs.push_back({node.nodePath.generic_string(), type, node.isDirectory, false});
    if (node.isDirectory) for (int child : node.children) collectAll(tree, child, type, diffs);
}

void MerkleTree::compareNodes(const MerkleTree& localTree, int localIdx,
                              const MerkleTree& remoteTree, int remoteIdx,
                              std::vector<FileDifference>& diffs) {
    if (localIdx < 0 || remoteIdx < 0 || static_cast<size_t>(localIdx) >= localTree.pool.size() ||
        static_cast<size_t>(remoteIdx) >= remoteTree.pool.size()) return;
    const auto& local = localTree.pool[static_cast<size_t>(localIdx)];
    const auto& remote = remoteTree.pool[static_cast<size_t>(remoteIdx)];
    if (local.hash == remote.hash && local.isDirectory == remote.isDirectory) return;
    if (!local.isDirectory && !remote.isDirectory) {
        diffs.push_back({local.nodePath.generic_string(), DiffType::MODIFIED, false, false});
        return;
    }
    if (local.isDirectory && remote.isDirectory) {
        std::map<std::string, int> remoteChildren;
        for (int child : remote.children) {
            const auto& n = remoteTree.pool[static_cast<size_t>(child)];
            remoteChildren[n.nodePath.generic_string()] = child;
        }
        for (int child : local.children) {
            const auto& n = localTree.pool[static_cast<size_t>(child)];
            const auto key = n.nodePath.generic_string();
            auto it = remoteChildren.find(key);
            if (it == remoteChildren.end()) collectAll(localTree, child, DiffType::LOCAL_ONLY, diffs);
            else { compareNodes(localTree, child, remoteTree, it->second, diffs); remoteChildren.erase(it); }
        }
        for (const auto& [key, child] : remoteChildren) {
            (void)key; collectAll(remoteTree, child, DiffType::REMOTE_ONLY, diffs);
        }
        return;
    }
    if (!local.isDirectory) diffs.push_back({local.nodePath.generic_string(), DiffType::MODIFIED, false, false});
    else collectAll(localTree, localIdx, DiffType::LOCAL_ONLY, diffs);
    if (!remote.isDirectory) diffs.push_back({remote.nodePath.generic_string(), DiffType::MODIFIED, false, false});
    else collectAll(remoteTree, remoteIdx, DiffType::REMOTE_ONLY, diffs);
}

std::vector<FileDifference> MerkleTree::findDifferences(const MerkleTree& localTree,
                                                         const MerkleTree& remoteTree) {
    std::vector<FileDifference> differences;
    if (localTree.pool.empty() || remoteTree.pool.empty()) return differences;
    compareNodes(localTree, 0, remoteTree, 0, differences);
    return differences;
}
