#include "filehashing.h"
#include "merkle_and_diff.h"
#include "networking.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <system_error>

#ifdef _WIN32
#include <winsock2.h>
#endif

struct PeerEndpoint {
    std::string ip;
    int port = 0;
};

namespace {
std::mutex stateMutex;

bool fileMatches(const fs::path& path, uint64_t expectedHash) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && hashFile(path) == expectedHash;
}

bool replaceFileFromPeer(const fs::path& root, const SyncRecord& record,
                         const std::string& ip, int port) {
    const fs::path relative(record.path);
    if (!isSafeRelativePath(relative)) return false;
    const fs::path target = root / relative;
    std::error_code ec;
    if (record.deleted) {
        fs::remove_all(target, ec);
        if (ec) {
            std::cerr << "[Delete] Failed for " << record.path << ": " << ec.message() << "\n";
            return false;
        }
        return true;
    }

    fs::create_directories(target.parent_path(), ec);
    if (ec) return false;
    const fs::path temp = target.parent_path() /
        (".p2psync_download_" + hashToHex(record.hash) + "_" + target.filename().string());
    fs::remove(temp, ec);
    ec.clear();
    if (!downloadRemoteFile(ip, port, record.path, temp)) {
        fs::remove(temp, ec);
        return false;
    }
    if (hashFile(temp) != record.hash) {
        fs::remove(temp, ec);
        std::cerr << "[Download] Hash verification failed for " << record.path << "\n";
        return false;
    }

    fs::rename(temp, target, ec);
    if (ec) {
        ec.clear();
        fs::remove(target, ec);
        ec.clear();
        fs::rename(temp, target, ec);
    }
    if (ec) {
        fs::remove(temp, ec);
        std::cerr << "[Download] Could not replace " << record.path << ": " << ec.message() << "\n";
        return false;
    }
    return true;
}

std::string conflictPathFor(const SyncRecord& version) {
    return version.path + ".conflict_" + hashToHex(version.hash);
}

bool preserveLocalVersion(const fs::path& root, const SyncRecord& version, SyncState& state) {
    if (version.deleted) return true;
    const fs::path source = root / fs::path(version.path);
    const std::string conflictRelative = conflictPathFor(version);
    const fs::path conflictTarget = root / fs::path(conflictRelative);
    std::error_code ec;
    fs::create_directories(conflictTarget.parent_path(), ec);
    if (ec) return false;
    if (fs::exists(conflictTarget, ec)) {
        if (fileMatches(conflictTarget, version.hash)) {
            SyncRecord copy = version;
            copy.path = conflictRelative;
            copy.deleted = false;
            state[conflictRelative] = copy;
            return true;
        }
        std::cerr << "[Conflict] Conflict-copy path already exists with different content: " << conflictRelative << "\n";
        return false;
    }
    fs::copy_file(source, conflictTarget, fs::copy_options::none, ec);
    if (ec) {
        std::cerr << "[Conflict] Could not preserve local version " << version.path << ": " << ec.message() << "\n";
        return false;
    }
    SyncRecord copy = version;
    copy.path = conflictRelative;
    copy.deleted = false;
    state[conflictRelative] = copy;
    std::cout << "[Conflict] Preserved local version as " << conflictRelative << "\n";
    return true;
}

bool preserveRemoteVersion(const fs::path& root, const SyncRecord& version,
                           const std::string& ip, int port, SyncState& state) {
    if (version.deleted) return true;
    const std::string conflictRelative = conflictPathFor(version);
    const fs::path conflictTarget = root / fs::path(conflictRelative);
    std::error_code ec;
    fs::create_directories(conflictTarget.parent_path(), ec);
    if (ec) return false;
    if (!fs::exists(conflictTarget, ec)) {
        const fs::path temp = conflictTarget.parent_path() /
            (".p2psync_download_" + hashToHex(version.hash) + "_" + conflictTarget.filename().string());
        fs::remove(temp, ec);
        ec.clear();
        if (!downloadRemoteFile(ip, port, version.path, temp)) {
            fs::remove(temp, ec);
            return false;
        }
        if (hashFile(temp) != version.hash) {
            fs::remove(temp, ec);
            std::cerr << "[Conflict] Remote conflict-copy hash mismatch for " << version.path << "\n";
            return false;
        }
        fs::rename(temp, conflictTarget, ec);
        if (ec) {
            ec.clear();
            fs::remove(temp, ec);
            if (!fileMatches(conflictTarget, version.hash)) return false;
        }
    } else if (!fileMatches(conflictTarget, version.hash)) {
        std::cerr << "[Conflict] Conflict-copy path already exists with different content: " << conflictRelative << "\n";
        return false;
    }
    SyncRecord copy = version;
    copy.path = conflictRelative;
    copy.deleted = false;
    state[conflictRelative] = copy;
    std::cout << "[Conflict] Preserved remote version as " << conflictRelative << "\n";
    return true;
}

// Deterministic tie-breaker used ONLY after vector clocks establish concurrency
// (or reveal inconsistent equal-clock metadata). It is not used to infer recency.
bool localVersionWinsTieBreak(const SyncRecord& local, const SyncRecord& remote) {
    if (local.deleted != remote.deleted) return local.deleted; // Explicit policy: concurrent delete wins.
    if (local.hash != remote.hash) return local.hash > remote.hash;
    return local.clock.toString() >= remote.clock.toString();
}

bool reconcileConcurrent(const fs::path& root, const std::string& ip, int port,
                         const SyncRecord& localRecord, const SyncRecord& remoteRecord,
                         SyncState& state) {
    VectorClock mergedClock = localRecord.clock;
    mergedClock.merge(remoteRecord.clock);

    if (localRecord.deleted != remoteRecord.deleted) {
        // Concurrent delete-versus-edit: deletion wins the original path, but the
        // edited contents are preserved at a deterministic conflict-copy path.
        const SyncRecord& liveVersion = localRecord.deleted ? remoteRecord : localRecord;
        bool preserved = localRecord.deleted
            ? preserveRemoteVersion(root, liveVersion, ip, port, state)
            : preserveLocalVersion(root, liveVersion, state);
        if (!preserved) return false;
        SyncRecord tombstone;
        tombstone.path = localRecord.path;
        tombstone.deleted = true;
        tombstone.hash = 0;
        tombstone.clock = mergedClock;
        std::error_code ec;
        fs::remove_all(root / fs::path(tombstone.path), ec);
        if (ec) return false;
        state[tombstone.path] = tombstone;
        std::cout << "[Conflict] Concurrent delete/edit: deletion kept at " << tombstone.path << "\n";
        return true;
    }

    if (localRecord.deleted && remoteRecord.deleted) {
        SyncRecord merged = localRecord;
        merged.clock = mergedClock;
        state[merged.path] = merged;
        return true;
    }

    if (localRecord.hash == remoteRecord.hash) {
        SyncRecord merged = localRecord;
        merged.clock = mergedClock;
        state[merged.path] = merged;
        return true;
    }

    const bool localWins = localVersionWinsTieBreak(localRecord, remoteRecord);
    const SyncRecord& loser = localWins ? remoteRecord : localRecord;
    const bool preserved = localWins
        ? preserveRemoteVersion(root, loser, ip, port, state)
        : preserveLocalVersion(root, loser, state);
    if (!preserved) return false;

    if (!localWins) {
        if (!replaceFileFromPeer(root, remoteRecord, ip, port)) return false;
    }
    SyncRecord resolved = localWins ? localRecord : remoteRecord;
    resolved.clock = mergedClock;
    resolved.deleted = false;
    state[resolved.path] = resolved;
    std::cout << "[Conflict] Concurrent edits preserved; deterministic winner retained at "
              << resolved.path << "\n";
    return true;
}

bool reconcileWithPeer(const fs::path& root, const PeerEndpoint& peer,
                       uint32_t nodeId, SyncState& localState) {
    (void)nodeId;
    std::string serialized;
    if (!fetchRemoteState(peer.ip, peer.port, serialized)) {
        std::cout << "[Peer] Cannot fetch state from " << peer.ip << ':' << peer.port << "\n";
        return false;
    }
    SyncState remoteState;
    if (!deserializeState(serialized, remoteState)) {
        std::cerr << "[Peer] Invalid state received from " << peer.ip << ':' << peer.port << "\n";
        return false;
    }

    // Use the original hierarchical Merkle workflow: each peer serves its
    // filesystem tree, and equal subtree hashes stop recursive comparison.
    // Vector clocks and tombstones remain authoritative for causal decisions.
    MerkleTree localMerkle(nodeId), remoteMerkle;
    if (localMerkle.buildTree(root, root) < 0 ||
        !fetchRemoteMerkleTree(peer.ip, peer.port, remoteMerkle)) {
        std::cerr << "[Merkle] Could not obtain a valid tree from " << peer.ip << ':' << peer.port << "\n";
        return false;
    }
    const auto merkleDiffs = MerkleTree::findDifferences(localMerkle, remoteMerkle);
    std::set<std::string> merkleDifferentPaths;
    std::set<std::string> remoteOnlyDirectories;
    std::set<std::string> localOnlyDirectories;
    for (const auto& difference : merkleDiffs) {
        if (difference.nodePath.empty()) continue;
        merkleDifferentPaths.insert(difference.nodePath);
        if (difference.isDirectory && difference.type == DiffType::REMOTE_ONLY)
            remoteOnlyDirectories.insert(difference.nodePath);
        if (difference.isDirectory && difference.type == DiffType::LOCAL_ONLY)
            localOnlyDirectories.insert(difference.nodePath);
    }
    if (localMerkle.checkIfEqual(remoteMerkle)) {
        std::cout << "[Merkle] Trees match for " << peer.ip << ':' << peer.port
                  << "; reconciling causal metadata.\n";
    } else {
        std::cout << "[Merkle] Trees differ; recursive comparison identified "
                  << merkleDifferentPaths.size() << " differing path(s) against "
                  << peer.ip << ':' << peer.port << "\n";
    }

    bool changed = false;
    for (const auto& [path, remoteRecord] : remoteState) {
        auto localIt = localState.find(path);
        if (localIt == localState.end()) {
            // A record known only by the peer: obtain its current content or tombstone.
            if (replaceFileFromPeer(root, remoteRecord, peer.ip, peer.port)) {
                localState[path] = remoteRecord;
                changed = true;
                std::cout << (remoteRecord.deleted ? "[Delete] Applied remote tombstone: " : "[Download] New file: ")
                          << path << "\n";
            } else {
                std::cerr << "[Peer] Failed to apply remote record: " << path << "\n";
            }
            continue;
        }

        const SyncRecord localRecord = localIt->second;
        const int relation = localRecord.clock.compareTo(remoteRecord.clock);
        const bool sameVersion = localRecord.deleted == remoteRecord.deleted &&
                                 (localRecord.deleted || localRecord.hash == remoteRecord.hash);
        if (sameVersion) {
            // Content can already match while one peer has newer causal metadata.
            // Propagate that metadata too; otherwise tombstones and unchanged files
            // can remain permanently on stale clocks.
            if (relation == -1) {
                localState[path] = remoteRecord;
                changed = true;
            } else if (relation == 2) {
                SyncRecord merged = localRecord;
                merged.clock.merge(remoteRecord.clock);
                localState[path] = merged;
                changed = true;
            }
            continue;
        }

        if (relation == 1) {
            // Local causally dominates the peer; peer will pull this version on its cycle.
            continue;
        }
        if (relation == -1) {
            // A live file whose subtree is unchanged needs only its causal
            // metadata updated; do not transfer identical bytes. If hashes in
            // the records disagree with the Merkle result, fail safe and fetch.
            const bool merkleSaysDifferent = merkleDifferentPaths.count(path) != 0;
            if (!localRecord.deleted && !remoteRecord.deleted && !merkleSaysDifferent) {
                if (localRecord.hash == remoteRecord.hash) {
                    localState[path] = remoteRecord;
                    changed = true;
                    continue;
                }
                // Defensive fallback: state metadata says hashes differ but the
                // tree comparison did not report the path. Never let a tree bug
                // suppress a required transfer.
                std::cerr << "[Merkle] Path hash mismatch not represented in diff for "
                          << path << "; falling back to record-level sync.\n";
            }
            if (replaceFileFromPeer(root, remoteRecord, peer.ip, peer.port)) {
                localState[path] = remoteRecord;
                changed = true;
                std::cout << (remoteRecord.deleted ? "[Delete] Remote version deleted: " : "[Download] Remote version is newer: ")
                          << path << "\n";
            } else {
                std::cerr << "[Peer] Failed to apply newer remote version: " << path << "\n";
            }
            continue;
        }

        // relation == 0 with different data, or relation == 2: resolve safely.
        if (reconcileConcurrent(root, peer.ip, peer.port, localRecord, remoteRecord, localState)) {
            changed = true;
        } else {
            std::cerr << "[Conflict] Resolution failed for " << path << "; keeping current local state.\n";
        }
    }

    // Empty directories have no file record/vector clock, so propagate their
    // structure directly from the Merkle diff in either direction.
    for (const auto& relative : remoteOnlyDirectories) {
        std::error_code ec;
        const fs::path target = root / fs::path(relative);
        const bool existed = fs::exists(target, ec) && !ec;
        fs::create_directories(target, ec);
        if (!ec && !existed) changed = true;
    }
    for (const auto& relative : localOnlyDirectories) {
        if (!createRemoteDirectory(peer.ip, peer.port, relative)) {
            std::cerr << "[Merkle] Failed to create remote directory: " << relative << "\n";
        }
    }

    if (!saveState(root, localState)) {
        std::cerr << "[State] Failed to persist state for " << root << "\n";
        return false;
    }
    return changed;
}

void runClient(const fs::path& root, const std::vector<PeerEndpoint>& peers, uint32_t nodeId) {
    while (true) {
        SyncState state;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state = loadState(root);
            if (!scanLocalChanges(root, nodeId, state)) {
                std::cerr << "[Scan] Failed to scan " << root << "\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            if (!saveState(root, state)) {
                std::cerr << "[State] Could not persist local state.\n";
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
        }

        bool changed = false;
        for (const auto& peer : peers) {
            changed = reconcileWithPeer(root, peer, nodeId, state) || changed;
        }

        // A just-downloaded file must not be interpreted as a local edit. Its hash
        // and remote clock are already stored, so this scan should leave it unchanged.
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (scanLocalChanges(root, nodeId, state)) saveState(root, state);
        }

        std::this_thread::sleep_for(changed ? std::chrono::seconds(2) : std::chrono::seconds(3));
    }
}
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <sync_folder> <my_port> <node_id> [peer_ip:port ...]\n";
        std::cerr << "Example: " << argv[0] << " ./f1 8080 1 127.0.0.1:8081\n";
        return 1;
    }

    fs::path root = fs::absolute(fs::path(argv[1]));
    int myPort = 0;
    uint32_t nodeId = 0;
    try {
        myPort = std::stoi(argv[2]);
        nodeId = static_cast<uint32_t>(std::stoul(argv[3]));
    } catch (...) {
        std::cerr << "Port and node_id must be integers.\n";
        return 1;
    }
    if (myPort < 1 || myPort > 65535 || nodeId == 0) {
        std::cerr << "Use a port from 1 to 65535 and a non-zero unique node_id.\n";
        return 1;
    }
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        std::cerr << "Could not create sync folder: " << ec.message() << "\n";
        return 1;
    }

    std::vector<PeerEndpoint> peers;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        const size_t colon = arg.rfind(':');
        if (colon == std::string::npos) {
            std::cerr << "Ignoring invalid peer address: " << arg << "\n";
            continue;
        }
        try {
            PeerEndpoint peer{arg.substr(0, colon), std::stoi(arg.substr(colon + 1))};
            if (peer.ip.empty() || peer.port < 1 || peer.port > 65535) throw std::invalid_argument("invalid peer");
            peers.push_back(peer);
        } catch (...) {
            std::cerr << "Ignoring invalid peer address: " << arg << "\n";
        }
    }

#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    std::cout << "========================================\n"
              << "P2P File Sync (vector clocks + tombstones)\n"
              << "Node ID: " << nodeId << "\n"
              << "Port: " << myPort << "\n"
              << "Folder: " << root << "\n"
              << "Peers: " << peers.size() << "\n"
              << "State file: " << (root / SYNC_STATE_FILENAME) << "\n"
              << "========================================\n";

    std::thread serverThread([root, myPort]() { runServer(root, myPort); });
    serverThread.detach();
    runClient(root, peers, nodeId);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
