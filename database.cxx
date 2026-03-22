#include "database.h"
#include "logger.h"
#include <sstream>
#include <stdexcept>

Database& Database::instance() {
    static Database inst;
    return inst;
}

bool Database::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mu_);
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        LOG_ERROR("sqlite3_open failed: " + std::string(sqlite3_errmsg(db_)));
        db_ = nullptr;
        return false;
    }
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;",  nullptr, nullptr, nullptr);
    return create_schema();
}

void Database::close() {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

bool Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        LOG_ERROR("SQL error: " + std::string(err));
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool Database::create_schema() {
    return exec(R"(
        CREATE TABLE IF NOT EXISTS peers (
            id TEXT PRIMARY KEY,
            ip TEXT NOT NULL,
            port INTEGER NOT NULL,
            online INTEGER NOT NULL DEFAULT 0,
            last_seen INTEGER NOT NULL DEFAULT 0,
            alias TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS messages (
            id TEXT PRIMARY KEY,
            sender_id TEXT NOT NULL,
            recipient_id TEXT NOT NULL,
            content TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            read INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_msg_ts ON messages(timestamp DESC);
        CREATE TABLE IF NOT EXISTS transfers (
            id TEXT PRIMARY KEY,
            peer_id TEXT NOT NULL,
            filename TEXT NOT NULL,
            filepath TEXT NOT NULL,
            total_bytes INTEGER NOT NULL DEFAULT 0,
            transferred_bytes INTEGER NOT NULL DEFAULT 0,
            state TEXT NOT NULL DEFAULT 'pending',
            checksum TEXT NOT NULL DEFAULT '',
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            outbound INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_transfer_state ON transfers(state);
    )");
}

bool Database::upsert_peer(const PeerInfo& p) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    const char* sql = R"(INSERT OR REPLACE INTO peers
        (id,ip,port,online,last_seen,alias) VALUES (?,?,?,?,?,?))";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt,  1, p.id.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  2, p.ip.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_int( stmt,  3, p.port);
    sqlite3_bind_int( stmt,  4, p.online ? 1 : 0);
    sqlite3_bind_int64(stmt, 5, p.last_seen);
    sqlite3_bind_text(stmt,  6, p.alias.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::delete_peer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, "DELETE FROM peers WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<PeerInfo> Database::all_peers() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<PeerInfo> out;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, "SELECT id,ip,port,online,last_seen,alias FROM peers", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PeerInfo p;
        p.id        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        p.ip        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.port      = static_cast<uint16_t>(sqlite3_column_int(stmt, 2));
        p.online    = sqlite3_column_int(stmt, 3) != 0;
        p.last_seen = sqlite3_column_int64(stmt, 4);
        p.alias     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        out.push_back(p);
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<PeerInfo> Database::get_peer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, "SELECT id,ip,port,online,last_seen,alias FROM peers WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        PeerInfo p;
        p.id        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        p.ip        = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.port      = static_cast<uint16_t>(sqlite3_column_int(stmt, 2));
        p.online    = sqlite3_column_int(stmt, 3) != 0;
        p.last_seen = sqlite3_column_int64(stmt, 4);
        p.alias     = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        sqlite3_finalize(stmt);
        return p;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

bool Database::insert_message(const Message& m) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    const char* sql = R"(INSERT OR IGNORE INTO messages
        (id,sender_id,recipient_id,content,timestamp,read) VALUES (?,?,?,?,?,?))";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt,  1, m.id.c_str(),           -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  2, m.sender_id.c_str(),    -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  3, m.recipient_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  4, m.content.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, m.timestamp);
    sqlite3_bind_int( stmt,  6, m.read ? 1 : 0);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<Message> Database::get_inbox(int page, int page_size) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Message> out;
    sqlite3_stmt* stmt;
    const char* sql = R"(SELECT id,sender_id,recipient_id,content,timestamp,read
        FROM messages ORDER BY timestamp DESC LIMIT ? OFFSET ?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, page_size);
    sqlite3_bind_int(stmt, 2, (page - 1) * page_size);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message m;
        m.id           = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.sender_id    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        m.recipient_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        m.content      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        m.timestamp    = sqlite3_column_int64(stmt, 4);
        m.read         = sqlite3_column_int(stmt, 5) != 0;
        out.push_back(m);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool Database::mark_message_read(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, "UPDATE messages SET read=1 WHERE id=?", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int Database::unread_count() {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM messages WHERE read=0", -1, &stmt, nullptr);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return count;
}

bool Database::upsert_transfer(const TransferRecord& t) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    const char* sql = R"(INSERT OR REPLACE INTO transfers
        (id,peer_id,filename,filepath,total_bytes,transferred_bytes,state,checksum,created_at,updated_at,outbound)
        VALUES (?,?,?,?,?,?,?,?,?,?,?))";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt,  1, t.id.c_str(),       -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  2, t.peer_id.c_str(),  -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  3, t.filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  4, t.filepath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(t.total_bytes));
    sqlite3_bind_int64(stmt, 6, static_cast<int64_t>(t.transferred_bytes));
    sqlite3_bind_text(stmt,  7, transfer_state_str(t.state).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,  8, t.checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 9, t.created_at);
    sqlite3_bind_int64(stmt, 10, t.updated_at);
    sqlite3_bind_int(stmt,  11, t.outbound ? 1 : 0);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::update_transfer_progress(const std::string& id, uint64_t transferred, TransferState state) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE transfers SET transferred_bytes=?,state=?,updated_at=? WHERE id=?";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(transferred));
    sqlite3_bind_text(stmt,  2, transfer_state_str(state).c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now_unix());
    sqlite3_bind_text(stmt,  4, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

static TransferRecord row_to_transfer(sqlite3_stmt* stmt) {
    TransferRecord t;
    t.id                = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    t.peer_id           = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    t.filename          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    t.filepath          = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    t.total_bytes       = static_cast<uint64_t>(sqlite3_column_int64(stmt, 4));
    t.transferred_bytes = static_cast<uint64_t>(sqlite3_column_int64(stmt, 5));
    std::string state_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    if      (state_str == "pending")     t.state = TransferState::Pending;
    else if (state_str == "in_progress") t.state = TransferState::InProgress;
    else if (state_str == "completed")   t.state = TransferState::Completed;
    else if (state_str == "failed")      t.state = TransferState::Failed;
    else if (state_str == "paused")      t.state = TransferState::Paused;
    else                                 t.state = TransferState::Cancelled;
    t.checksum    = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    t.created_at  = sqlite3_column_int64(stmt, 8);
    t.updated_at  = sqlite3_column_int64(stmt, 9);
    t.outbound    = sqlite3_column_int(stmt, 10) != 0;
    return t;
}

std::vector<TransferRecord> Database::pending_inbound() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<TransferRecord> out;
    sqlite3_stmt* stmt;
    const char* sql = R"(SELECT id,peer_id,filename,filepath,total_bytes,transferred_bytes,
        state,checksum,created_at,updated_at,outbound FROM transfers
        WHERE outbound=0 AND state IN ('pending','paused'))";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(row_to_transfer(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::vector<TransferRecord> Database::active_transfers() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<TransferRecord> out;
    sqlite3_stmt* stmt;
    const char* sql = R"(SELECT id,peer_id,filename,filepath,total_bytes,transferred_bytes,
        state,checksum,created_at,updated_at,outbound FROM transfers
        WHERE state IN ('pending','in_progress','paused') ORDER BY created_at DESC)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) out.push_back(row_to_transfer(stmt));
    sqlite3_finalize(stmt);
    return out;
}

std::optional<TransferRecord> Database::get_transfer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    const char* sql = R"(SELECT id,peer_id,filename,filepath,total_bytes,transferred_bytes,
        state,checksum,created_at,updated_at,outbound FROM transfers WHERE id=?)";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        auto t = row_to_transfer(stmt);
        sqlite3_finalize(stmt);
        return t;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

bool Database::delete_stale_transfers(int older_than_days) {
    std::lock_guard<std::mutex> lock(mu_);
    int64_t cutoff = now_unix() - older_than_days * 86400LL;
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM transfers WHERE updated_at<? AND state IN ('completed','failed','cancelled')";
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, cutoff);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

uint64_t Database::storage_used_bytes() {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, "SELECT SUM(transferred_bytes) FROM transfers WHERE outbound=0 AND state='completed'", -1, &stmt, nullptr);
    uint64_t total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        total = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return total;
}
