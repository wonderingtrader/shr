#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <mutex>

class Database {
public:
    static Database& instance();

    bool open(const std::string& path);
    void close();
    bool is_open() const { return db_ != nullptr; }

    bool upsert_peer(const PeerInfo& p);
    bool delete_peer(const std::string& id);
    std::vector<PeerInfo> all_peers();
    std::optional<PeerInfo> get_peer(const std::string& id);

    bool insert_message(const Message& m);
    std::vector<Message> get_inbox(int page, int page_size);
    bool mark_message_read(const std::string& id);
    int  unread_count();

    bool upsert_transfer(const TransferRecord& t);
    bool update_transfer_progress(const std::string& id, uint64_t transferred, TransferState state);
    std::vector<TransferRecord> pending_inbound();
    std::vector<TransferRecord> active_transfers();
    std::optional<TransferRecord> get_transfer(const std::string& id);
    bool delete_stale_transfers(int older_than_days);

    uint64_t storage_used_bytes();

private:
    Database() = default;
    sqlite3*   db_ = nullptr;
    std::mutex mu_;

    bool exec(const std::string& sql);
    bool create_schema();
    static int  callback_noop(void*, int, char**, char**) { return 0; }
};
