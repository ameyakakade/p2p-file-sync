#include <iostream>
#include <algorithm>
#include <unordered_map>

#include "merkle_and_diff.h" 
#include "filehashing.h" 
#include "parser.h" 

// merkle tree functions

void MerkleTree::clear() {
    pool.clear();
}
    
int MerkleTree::buildTree(const fs::path& path, const fs::path& rootPath) {
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
        auto ftime = fs::last_write_time(path);
        curr.mtime = std::chrono::duration_cast<std::chrono::seconds>(ftime.time_since_epoch()).count();    
    }

    pool[myIndex] = curr; // Save updated data back into reserved index
    return myIndex;
}

void MerkleTree::buildTreeString(const char* a) {
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
                         FIELD_PARSE(&sp, "mtime", curr.mtime = natParser(&sp);)
                         FIELD_PARSE(&sp, "children",
                                     ARRAY_PARSE(&sp, curr.children.push_back(natParser(&sp));)
                             )
                         pool[i] = curr;
                )
                }
}

std::string MerkleTree::dumpTreeString() {
    std::ostringstream os;
    os << "poolSize:" << pool.size();
    for(int i=0; (size_t)i<pool.size(); i++) {
        os << "{";
        os << "index:" << i << ",";
        os << "nodePath:" << pool[i].nodePath << ",";
        os << "isDirectory:" << (pool[i].isDirectory ? "true" : "false") << ",";
        os << "hash:" << pool[i].hash << ",";
        os << "mtime:" << pool[i].mtime << ",";
        os << "children:[";
        for(int child : pool[i].children) {
            os << child << ",";
        }
        os << "]";
        os << "}";
    }
    return os.str();
}

bool MerkleTree::checkIfEqual(const MerkleTree& other) {
    if (pool.empty() || other.pool.empty()) return false;
    return pool[0].hash == other.pool[0].hash;
}

void MerkleTree::printMerkleTree(int nodeIndex, int depth) {
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

void MerkleTree::collectAll(const MerkleTree& tree, int idx, DiffType type, std::vector<FileDifference>& diffs){
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

void MerkleTree::compareNodes(const MerkleTree& localTree, int localIdx,
                         const MerkleTree& remoteTree, int remoteIdx,
                         std::vector<FileDifference>& diffs) {
    const auto& lNode = localTree.pool[localIdx];
    const auto& rNode = remoteTree.pool[remoteIdx];

    // If subtree hashes match, skip checking this entire branch
    if (lNode.hash == rNode.hash) return;

    // Leaf file comparison
    if (!lNode.isDirectory && !rNode.isDirectory) {
        if(lNode.hash!=rNode.hash){
            if(rNode.mtime > lNode.mtime){
                diffs.push_back({lNode.nodePath.generic_string(), DiffType::MODIFIED, false});
            }
        }
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
            // collectAll(localTree, lChild, DiffType::DELETED, diffs);
            continue;
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

std::vector<FileDifference> MerkleTree::findDifferences(const MerkleTree& localTree, const MerkleTree& remoteTree) {
    std::vector<FileDifference> diffs;
    if (localTree.pool.empty() || remoteTree.pool.empty()) return diffs;
    compareNodes(localTree, 0, remoteTree, 0, diffs);
    return diffs;
}
