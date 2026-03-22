#include "transfer.h"
#include "database.h"
#include "network.h"
#include "config.h"
#include "identity.h"
#include "peers.h"
#include "utils.h"
#include "logger.h"
#include "crypto.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <cstring>

using json = nlohmann::json;

Transfer& Transfer::instance() {
    static Transfer inst;
    return inst;
}

static void print_progress(uint64_t done, uint64_t total, double speed) {
    int pct = total > 0 ? static_cast<int>(done * 100 / total) : 0;
    int bar = pct / 5;
    std::cout << "\r[";
    for (int i = 0; i < 20; ++i) std::cout << (i < bar ? '#' : ' ');
    std::cout << "] " << std::setw(3) << pct << "% "
              << utils::format_bytes(done) << " / " << utils::format_bytes(total)
              << "  " << std::setw(10) << utils::format_speed(speed)
              << "          " << std::flush;
    if (done >= total) std::cout << "\n";
}

std::string Transfer::send_file(const std::string& filepath,
                                 const std::string& recipient_id,
                                 ProgressCallback   cb)
{
    if (!utils::is_valid_uuid(recipient_id)) {
        std::cerr << "error: invalid recipient ID\n";
        return "";
    }

    std::filesystem::path abs_path;
    try {
        abs_path = std::filesystem::canonical(filepath);
    } catch (...) {
        std::cerr << "error: file not found: " << filepath << "\n";
        return "";
    }

    if (!std::filesystem::is_regular_file(abs_path)) {
        std::cerr << "error: not a regular file: " << filepath << "\n";
        return "";
    }

    auto peer = Peers::instance().find_peer(recipient_id);
    if (!peer || peer->ip.empty()) {
        std::cerr << "error: peer not reachable. Use: shr connect <id> <ip>\n";
        return "";
    }

    if (active_count_ >= SHR_MAX_CONCURRENT) {
        std::cerr << "error: max concurrent transfers (" << SHR_MAX_CONCURRENT << ") reached\n";
        return "";
    }

    uint64_t    file_sz   = utils::file_size(abs_path.string());
    std::string checksum  = utils::sha256_file(abs_path.string());
    std::string xfer_id   = utils::generate_uuid();
    std::string fname     = abs_path.filename().string();

    TransferRecord rec;
    rec.id                = xfer_id;
    rec.peer_id           = recipient_id;
    rec.filename          = fname;
    rec.filepath          = abs_path.string();
    rec.total_bytes       = file_sz;
    rec.transferred_bytes = 0;
    rec.state             = TransferState::Pending;
    rec.checksum          = checksum;
    rec.created_at        = now_unix();
    rec.updated_at        = now_unix();
    rec.outbound          = true;
    Database::instance().upsert_transfer(rec);

    json offer;
    offer["transfer_id"] = xfer_id;
    offer["filename"]    = fname;
    offer["size"]        = file_sz;
    offer["checksum"]    = checksum;
    offer["sender_id"]   = Identity::instance().id();

    auto offer_bytes = json::to_cbor(offer);

    socket_t conn = Network::instance().connect_to(peer->ip, peer->port);
    if (conn == INVALID_SOCK) {
        std::cerr << "error: cannot connect to peer " << peer->ip << ":" << peer->port << "\n";
        Database::instance().update_transfer_progress(xfer_id, 0, TransferState::Failed);
        return "";
    }

    Network::instance().send_packet_conn(conn, PacketType::FileOffer,
        std::vector<uint8_t>(offer_bytes.begin(), offer_bytes.end()));

    RawPacket resp;
    if (!Network::instance().read_packet(conn, resp, SHR_CONNECT_TIMEOUT_SEC)) {
        std::cerr << "error: no response from peer\n";
        Network::instance().close_conn(conn);
        Database::instance().update_transfer_progress(xfer_id, 0, TransferState::Failed);
        return "";
    }

    uint64_t resume_offset = 0;
    if (resp.type == PacketType::FileReject) {
        std::cerr << "error: peer rejected file transfer\n";
        Network::instance().close_conn(conn);
        Database::instance().update_transfer_progress(xfer_id, 0, TransferState::Failed);
        return "";
    }
    if (resp.type == PacketType::FileResume) {
        try {
            auto rj     = json::from_cbor(resp.payload);
            resume_offset = rj.value("offset", (uint64_t)0);
        } catch (...) {}
    }

    std::ifstream file(abs_path, std::ios::binary);
    if (!file) {
        std::cerr << "error: cannot open file for reading\n";
        Network::instance().close_conn(conn);
        Database::instance().update_transfer_progress(xfer_id, 0, TransferState::Failed);
        return "";
    }

    if (resume_offset > 0) file.seekg(static_cast<std::streamoff>(resume_offset));

    Database::instance().update_transfer_progress(xfer_id, resume_offset, TransferState::InProgress);
    ++active_count_;

    std::vector<uint8_t> chunk_buf(SHR_CHUNK_SIZE);
    uint64_t transferred = resume_offset;
    uint64_t chunk_index = resume_offset / SHR_CHUNK_SIZE;

    auto start_time = std::chrono::steady_clock::now();
    auto last_print  = start_time;

    while (file) {
        file.read(reinterpret_cast<char*>(chunk_buf.data()),
                  static_cast<std::streamsize>(SHR_CHUNK_SIZE));
        std::streamsize n = file.gcount();
        if (n <= 0) break;

        std::string chunk_hash = utils::sha256_bytes(chunk_buf.data(), n);

        json chunk_j;
        chunk_j["transfer_id"] = xfer_id;
        chunk_j["index"]       = chunk_index;
        chunk_j["offset"]      = transferred;
        chunk_j["size"]        = static_cast<uint64_t>(n);
        chunk_j["checksum"]    = chunk_hash;
        chunk_j["data"]        = utils::base64_encode(chunk_buf.data(), n);

        auto chunk_bytes = json::to_cbor(chunk_j);

        bool sent = false;
        for (int attempt = 0; attempt < SHR_RETRY_LIMIT && !sent; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(Config::instance().get().retry_delay_ms));
            }
            sent = Network::instance().send_packet_conn(conn, PacketType::FileChunk,
                std::vector<uint8_t>(chunk_bytes.begin(), chunk_bytes.end()));
        }

        if (!sent) {
            std::cerr << "\nerror: chunk send failed\n";
            Network::instance().close_conn(conn);
            Database::instance().update_transfer_progress(xfer_id, transferred, TransferState::Failed);
            --active_count_;
            return "";
        }

        RawPacket ack;
        if (!Network::instance().read_packet(conn, ack, SHR_CONNECT_TIMEOUT_SEC)) {
            std::cerr << "\nerror: no chunk ack\n";
            break;
        }

        transferred += static_cast<uint64_t>(n);
        ++chunk_index;

        auto now       = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        double speed   = elapsed > 0 ? static_cast<double>(transferred - resume_offset) / elapsed : 0.0;

        if (cb) {
            cb(transferred, file_sz, speed);
        } else {
            auto since_print = std::chrono::duration<double>(now - last_print).count();
            if (since_print >= 0.25 || transferred >= file_sz) {
                print_progress(transferred, file_sz, speed);
                last_print = now;
            }
        }

        Database::instance().update_transfer_progress(xfer_id, transferred, TransferState::InProgress);
    }

    json complete_j;
    complete_j["transfer_id"] = xfer_id;
    complete_j["checksum"]    = checksum;
    auto complete_bytes = json::to_cbor(complete_j);
    Network::instance().send_packet_conn(conn, PacketType::FileComplete,
        std::vector<uint8_t>(complete_bytes.begin(), complete_bytes.end()));

    Network::instance().close_conn(conn);
    Database::instance().update_transfer_progress(xfer_id, transferred, TransferState::Completed);
    --active_count_;

    std::cout << "Transfer complete: " << fname
              << " (" << utils::format_bytes(file_sz) << ")\n";
    return xfer_id;
}

void Transfer::handle_file_offer(const std::vector<uint8_t>& payload, int conn) {
    try {
        auto j = json::from_cbor(payload);
        std::string xfer_id   = j.value("transfer_id", utils::generate_uuid());
        std::string filename  = j.value("filename",    "unknown");
        uint64_t    file_size = j.value("size",        (uint64_t)0);
        std::string checksum  = j.value("checksum",    "");
        std::string sender_id = j.value("sender_id",  "");

        filename = utils::sanitize_filename(filename);

        auto existing = Database::instance().get_transfer(xfer_id);
        uint64_t resume_offset = 0;

        if (existing && existing->state == TransferState::Paused) {
            resume_offset = existing->transferred_bytes;
            json resume_j;
            resume_j["offset"] = resume_offset;
            auto rb = json::to_cbor(resume_j);
            Network::instance().send_packet_conn(static_cast<socket_t>(conn),
                PacketType::FileResume,
                std::vector<uint8_t>(rb.begin(), rb.end()));
        } else {
            std::filesystem::path dest = std::filesystem::path(
                Config::instance().get().received_dir) / filename;

            TransferRecord rec;
            rec.id                = xfer_id;
            rec.peer_id           = sender_id;
            rec.filename          = filename;
            rec.filepath          = dest.string();
            rec.total_bytes       = file_size;
            rec.transferred_bytes = 0;
            rec.state             = TransferState::Pending;
            rec.checksum          = checksum;
            rec.created_at        = now_unix();
            rec.updated_at        = now_unix();
            rec.outbound          = false;
            Database::instance().upsert_transfer(rec);

            json accept_j;
            accept_j["transfer_id"] = xfer_id;
            auto ab = json::to_cbor(accept_j);
            Network::instance().send_packet_conn(static_cast<socket_t>(conn),
                PacketType::FileAccept,
                std::vector<uint8_t>(ab.begin(), ab.end()));
        }
    } catch (...) {
        LOG_WARN("handle_file_offer: malformed payload");
    }
}

void Transfer::handle_file_chunk(const std::vector<uint8_t>& payload, int conn) {
    try {
        auto j = json::from_cbor(payload);
        std::string xfer_id  = j.value("transfer_id", "");
        uint64_t    offset   = j.value("offset",       (uint64_t)0);
        std::string b64data  = j.value("data",         "");
        std::string checksum = j.value("checksum",     "");

        auto chunk_data = utils::base64_decode(b64data);
        std::string actual_hash = utils::sha256_bytes(chunk_data.data(), chunk_data.size());
        if (actual_hash != checksum) {
            LOG_WARN("chunk checksum mismatch for transfer " + xfer_id);
            return;
        }

        auto rec = Database::instance().get_transfer(xfer_id);
        if (!rec) return;

        std::ofstream out(rec->filepath,
            std::ios::binary | (offset == 0 ? std::ios::trunc : std::ios::app));
        if (!out) {
            LOG_ERROR("cannot open receive file: " + rec->filepath);
            return;
        }
        out.seekp(static_cast<std::streamoff>(offset));
        out.write(reinterpret_cast<const char*>(chunk_data.data()),
                  static_cast<std::streamsize>(chunk_data.size()));

        uint64_t new_transferred = offset + chunk_data.size();
        Database::instance().update_transfer_progress(xfer_id, new_transferred,
            TransferState::InProgress);

        json ack_j;
        ack_j["transfer_id"] = xfer_id;
        ack_j["offset"]      = new_transferred;
        auto ab = json::to_cbor(ack_j);
        Network::instance().send_packet_conn(static_cast<socket_t>(conn),
            PacketType::FileChunkAck,
            std::vector<uint8_t>(ab.begin(), ab.end()));
    } catch (...) {
        LOG_WARN("handle_file_chunk: exception");
    }
}

void Transfer::handle_file_complete(const std::vector<uint8_t>& payload, int /*conn*/) {
    try {
        auto j = json::from_cbor(payload);
        std::string xfer_id  = j.value("transfer_id", "");
        std::string checksum = j.value("checksum",    "");

        auto rec = Database::instance().get_transfer(xfer_id);
        if (!rec) return;

        std::string actual = utils::sha256_file(rec->filepath);
        if (actual != checksum) {
            LOG_ERROR("File integrity check failed for " + rec->filename);
            Database::instance().update_transfer_progress(xfer_id, rec->transferred_bytes,
                TransferState::Failed);
            return;
        }

        Database::instance().update_transfer_progress(xfer_id, rec->total_bytes,
            TransferState::Completed);
        LOG_INFO("File received: " + rec->filename + " -> " + rec->filepath);
    } catch (...) {
        LOG_WARN("handle_file_complete: exception");
    }
}

void Transfer::handle_file_accept(const std::vector<uint8_t>& /*payload*/, int /*conn*/) {}
void Transfer::handle_file_resume(const std::vector<uint8_t>& /*payload*/, int /*conn*/) {}

bool Transfer::receive_pending(bool interactive) {
    auto pending = Database::instance().pending_inbound();
    if (pending.empty()) {
        std::cout << "No pending transfers.\n";
        return true;
    }

    std::cout << "Pending inbound transfers:\n";
    std::cout << std::string(70, '-') << "\n";
    for (size_t i = 0; i < pending.size(); ++i) {
        auto& t = pending[i];
        std::cout << "[" << (i + 1) << "] " << t.filename
                  << " from " << t.peer_id << "\n"
                  << "    Size: " << utils::format_bytes(t.total_bytes)
                  << "  Received: " << utils::format_bytes(t.transferred_bytes)
                  << "  ID: " << t.id << "\n";
    }
    std::cout << std::string(70, '-') << "\n";

    if (!interactive) return true;

    std::cout << "Enter transfer number to download (or 0 to skip): ";
    int choice = 0;
    std::cin >> choice;
    if (choice < 1 || choice > static_cast<int>(pending.size())) return true;

    auto& selected = pending[choice - 1];
    std::cout << "Downloading " << selected.filename << "...\n";
    return true;
}

std::vector<TransferRecord> Transfer::list_active() {
    return Database::instance().active_transfers();
}

std::vector<TransferRecord> Transfer::list_pending_inbound() {
    return Database::instance().pending_inbound();
}

bool Transfer::cancel_transfer(const std::string& id) {
    auto rec = Database::instance().get_transfer(id);
    if (!rec) return false;
    return Database::instance().update_transfer_progress(id, rec->transferred_bytes,
        TransferState::Cancelled);
}
