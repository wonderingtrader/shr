#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "types.h"
#include "config.h"
#include "logger.h"
#include "identity.h"
#include "database.h"
#include "crypto.h"
#include "network.h"
#include "transfer.h"
#include "messaging.h"
#include "peers.h"
#include "utils.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <filesystem>

static void usage() {
    std::cout <<
        "shr v" << SHR_VERSION << " - Decentralized file transfer & messaging\n"
        "\n"
        "Usage: shr <command> [args]\n"
        "\n"
        "Identity:\n"
        "  install                        Generate identity and configuration\n"
        "  whoami                         Print your user ID\n"
        "\n"
        "File Transfer:\n"
        "  send <file> <recipient_id>     Send a file to a peer\n"
        "  receive                        List and download pending transfers\n"
        "\n"
        "Messaging:\n"
        "  msg <recipient_id> <message>   Send a text message\n"
        "  inbox [page]                   Show received messages\n"
        "\n"
        "Network:\n"
        "  peers                          List known peers\n"
        "  discover                       Broadcast discovery on local network\n"
        "  connect <peer_id> <ip> [port]  Manually add a peer\n"
        "\n"
        "Utility:\n"
        "  status                         Show connection and transfer status\n"
        "  config                         Show current configuration\n"
        "  clean                          Remove stale transfers and cache\n"
        "\n";
}

static bool init_app(bool require_install = true) {
    if (!Config::instance().load()) {
        std::cerr << "error: failed to load config\n";
        return false;
    }

    auto& cfg = Config::instance().get();
    LogLevel lvl = LogLevel::Info;
    if      (cfg.log_level == "debug") lvl = LogLevel::Debug;
    else if (cfg.log_level == "warn")  lvl = LogLevel::Warn;
    else if (cfg.log_level == "error") lvl = LogLevel::Error;
    Logger::instance().init(cfg.log_path, lvl);

    if (require_install && !Config::instance().is_installed()) {
        std::cerr << "error: not installed. Run: shr install\n";
        return false;
    }

    if (Config::instance().is_installed()) {
        Identity::instance().load();

        if (!Database::instance().open(cfg.db_path)) {
            std::cerr << "error: failed to open database\n";
            return false;
        }

        if (!Crypto::instance().init(cfg.config_dir)) {
            std::cerr << "error: failed to initialize crypto\n";
            return false;
        }
    }

    return true;
}

static int cmd_install() {
    if (!Config::instance().load()) {
        std::cerr << "error: failed to load config\n";
        return 1;
    }
    Logger::instance().init("", LogLevel::Warn);

    if (Config::instance().is_installed()) {
        std::cout << "Already installed. Your ID: "
                  << Config::instance().get().user_id << "\n";
        return 0;
    }

    if (!Identity::instance().install()) {
        std::cerr << "error: installation failed\n";
        return 1;
    }

    if (!Database::instance().open(Config::instance().get().db_path)) {
        std::cerr << "error: failed to initialize database\n";
        return 1;
    }

    std::cout << "Installation complete. Your ID: "
              << Identity::instance().id() << "\n";
    std::cout << "Config directory: " << Config::instance().get().config_dir << "\n";
    std::cout << "Received files:   " << Config::instance().get().received_dir << "\n";
    return 0;
}

static int cmd_whoami() {
    if (!init_app()) return 1;
    std::cout << Identity::instance().id() << "\n";
    return 0;
}

static int cmd_send(const std::string& filepath, const std::string& recipient_id) {
    if (!init_app()) return 1;

    if (!utils::is_valid_uuid(recipient_id)) {
        std::cerr << "error: invalid recipient ID format\n";
        return 1;
    }

    if (!utils::file_exists(filepath)) {
        std::cerr << "error: file not found: " << filepath << "\n";
        return 1;
    }

    std::cout << "Sending " << std::filesystem::path(filepath).filename().string()
              << " to " << recipient_id << "...\n";

    std::string xfer_id = Transfer::instance().send_file(filepath, recipient_id);
    if (xfer_id.empty()) return 1;
    return 0;
}

static int cmd_receive(bool interactive) {
    if (!init_app()) return 1;
    Transfer::instance().receive_pending(interactive);
    return 0;
}

static int cmd_msg(const std::string& recipient_id, const std::string& message) {
    if (!init_app()) return 1;

    if (!utils::is_valid_uuid(recipient_id)) {
        std::cerr << "error: invalid recipient ID format\n";
        return 1;
    }

    bool ok = Messaging::instance().send_msg(recipient_id, message);
    return ok ? 0 : 1;
}

static int cmd_inbox(int page) {
    if (!init_app()) return 1;

    int page_size = 20;
    auto msgs = Messaging::instance().get_inbox(page, page_size);

    if (msgs.empty()) {
        std::cout << "Inbox is empty.\n";
        return 0;
    }

    int unread = Messaging::instance().unread_count();
    std::cout << "Inbox (page " << page << ")  -  " << unread << " unread\n";
    std::cout << std::string(72, '=') << "\n";

    for (auto& m : msgs) {
        std::string marker = m.read ? "  " : "* ";
        std::cout << marker << "[" << utils::timestamp_to_str(m.timestamp) << "] "
                  << "From: " << m.sender_id << "\n"
                  << "  " << m.content << "\n"
                  << std::string(72, '-') << "\n";
        if (!m.read) Database::instance().mark_message_read(m.id);
    }

    if (static_cast<int>(msgs.size()) == page_size) {
        std::cout << "-- More messages: shr inbox " << (page + 1) << " --\n";
    }
    return 0;
}

static int cmd_peers() {
    if (!init_app()) return 1;

    auto peers = Peers::instance().list_peers();
    if (peers.empty()) {
        std::cout << "No known peers. Run: shr discover\n";
        return 0;
    }

    std::cout << std::left
              << std::setw(38) << "ID"
              << std::setw(18) << "IP:Port"
              << std::setw(8)  << "Status"
              << "Last Seen\n";
    std::cout << std::string(80, '-') << "\n";

    for (auto& p : peers) {
        std::string addr = p.ip + ":" + std::to_string(p.port);
        std::string status = p.online ? "online" : "offline";
        std::string last = p.last_seen > 0 ? utils::timestamp_to_str(p.last_seen) : "never";
        std::cout << std::left
                  << std::setw(38) << p.id
                  << std::setw(18) << addr
                  << std::setw(8)  << status
                  << last << "\n";
    }
    return 0;
}

static int cmd_discover() {
    if (!init_app()) return 1;
    Peers::instance().discover();
    return 0;
}

static int cmd_connect(const std::string& peer_id,
                       const std::string& ip,
                       uint16_t           port)
{
    if (!init_app()) return 1;

    if (!utils::is_valid_uuid(peer_id)) {
        std::cerr << "error: invalid peer ID format\n";
        return 1;
    }

    bool ok = Peers::instance().add_peer(peer_id, ip, port);
    if (!ok) {
        std::cerr << "error: failed to store peer\n";
        return 1;
    }

    std::cout << "Peer added: " << peer_id << " at " << ip << ":" << port << "\n";

    bool alive = Peers::instance().ping_peer(peer_id);
    std::cout << "Ping: " << (alive ? "reachable" : "unreachable") << "\n";
    return 0;
}

static int cmd_status() {
    if (!init_app()) return 1;

    auto& cfg = Config::instance().get();

    std::cout << "shr v" << SHR_VERSION << "\n";
    std::cout << std::string(50, '-') << "\n";
    std::cout << "User ID:     " << Identity::instance().id() << "\n";
    std::cout << "Listen port: " << cfg.listen_port << "\n";
    std::cout << "Local IP:    " << utils::get_local_ip() << "\n";
    std::cout << "\n";

    auto active = Transfer::instance().list_active();
    std::cout << "Active/Pending Transfers: " << active.size() << "\n";
    if (!active.empty()) {
        std::cout << std::string(50, '-') << "\n";
        for (auto& t : active) {
            int pct = t.total_bytes > 0
                ? static_cast<int>(t.transferred_bytes * 100 / t.total_bytes) : 0;
            std::cout << (t.outbound ? "OUT" : " IN") << " "
                      << std::setw(3) << pct << "% "
                      << std::left << std::setw(30) << t.filename
                      << " [" << transfer_state_str(t.state) << "]\n";
        }
    }

    std::cout << "\n";
    int unread = Messaging::instance().unread_count();
    std::cout << "Unread messages: " << unread << "\n";

    uint64_t storage = Database::instance().storage_used_bytes();
    std::cout << "Storage used:    " << utils::format_bytes(storage) << "\n";

    auto peers = Peers::instance().list_peers();
    long online = std::count_if(peers.begin(), peers.end(), [](const PeerInfo& p){ return p.online; });
    std::cout << "Peers online:    " << online << "/" << peers.size() << "\n";

    return 0;
}

static int cmd_config() {
    if (!init_app(false)) {
        Config::instance().load();
    }
    auto& cfg = Config::instance().get();

    std::cout << "shr configuration\n";
    std::cout << std::string(50, '-') << "\n";
    std::cout << "user_id:        " << (cfg.user_id.empty() ? "(not installed)" : cfg.user_id) << "\n";
    std::cout << "config_dir:     " << cfg.config_dir << "\n";
    std::cout << "db_path:        " << cfg.db_path << "\n";
    std::cout << "log_path:       " << cfg.log_path << "\n";
    std::cout << "received_dir:   " << cfg.received_dir << "\n";
    std::cout << "listen_port:    " << cfg.listen_port << "\n";
    std::cout << "max_concurrent: " << cfg.max_concurrent << "\n";
    std::cout << "retry_limit:    " << cfg.retry_limit << "\n";
    std::cout << "retry_delay_ms: " << cfg.retry_delay_ms << "\n";
    std::cout << "log_level:      " << cfg.log_level << "\n";
    std::cout << "verbose:        " << (cfg.verbose ? "true" : "false") << "\n";
    std::cout << "\nEnvironment overrides: SHR_PORT, SHR_VERBOSE, SHR_LOG_LEVEL\n";
    return 0;
}

static int cmd_clean() {
    if (!init_app()) return 1;

    int deleted_transfers = 0;
    if (Database::instance().delete_stale_transfers(7)) {
        std::cout << "Removed completed/failed transfers older than 7 days.\n";
    }

    auto& cfg = Config::instance().get();
    std::filesystem::path tmp_dir = std::filesystem::path(cfg.config_dir) / "tmp";
    if (std::filesystem::exists(tmp_dir)) {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
        if (!ec) std::cout << "Cleared temporary files.\n";
    }

    std::cout << "Clean complete.\n";
    return 0;
}

static void setup_network_handlers() {
    auto& net = Network::instance();

    net.register_handler(PacketType::FileOffer, [](const RawPacket& pkt, socket_t conn) {
        Transfer::instance().handle_file_offer(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::FileChunk, [](const RawPacket& pkt, socket_t conn) {
        Transfer::instance().handle_file_chunk(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::FileComplete, [](const RawPacket& pkt, socket_t conn) {
        Transfer::instance().handle_file_complete(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::FileAccept, [](const RawPacket& pkt, socket_t conn) {
        Transfer::instance().handle_file_accept(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::FileResume, [](const RawPacket& pkt, socket_t conn) {
        Transfer::instance().handle_file_resume(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::MsgSend, [](const RawPacket& pkt, socket_t conn) {
        Messaging::instance().handle_msg_receive(pkt.payload);
        json ack;
        ack["ok"] = true;
        auto ab = nlohmann::json::to_cbor(ack);
        Network::instance().send_packet_conn(conn, PacketType::MsgAck,
            std::vector<uint8_t>(ab.begin(), ab.end()));
    });
    net.register_handler(PacketType::PeerDiscover, [](const RawPacket& pkt, socket_t conn) {
        Peers::instance().handle_discover(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::PeerAnnounce, [](const RawPacket& pkt, socket_t conn) {
        Peers::instance().handle_announce(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::Ping, [](const RawPacket& pkt, socket_t conn) {
        Peers::instance().handle_ping(pkt.payload, static_cast<int>(conn));
    });
    net.register_handler(PacketType::Pong, [](const RawPacket& pkt, socket_t conn) {
        Peers::instance().handle_pong(pkt.payload, static_cast<int>(conn));
    });
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage();
        return 0;
    }

    std::string cmd = argv[1];

    if (cmd == "install") return cmd_install();
    if (cmd == "--help" || cmd == "-h" || cmd == "help") { usage(); return 0; }
    if (cmd == "--version" || cmd == "-v") {
        std::cout << "shr " << SHR_VERSION << "\n";
        return 0;
    }

    if (cmd == "whoami") return cmd_whoami();
    if (cmd == "config") return cmd_config();

    if (cmd == "send") {
        if (argc < 4) {
            std::cerr << "usage: shr send <file_path> <recipient_id>\n";
            return 1;
        }
        setup_network_handlers();
        return cmd_send(argv[2], argv[3]);
    }

    if (cmd == "receive") {
        setup_network_handlers();
        return cmd_receive(true);
    }

    if (cmd == "msg") {
        if (argc < 4) {
            std::cerr << "usage: shr msg <recipient_id> <message>\n";
            return 1;
        }
        std::string message;
        for (int i = 3; i < argc; ++i) {
            if (i > 3) message += ' ';
            message += argv[i];
        }
        setup_network_handlers();
        return cmd_msg(argv[2], message);
    }

    if (cmd == "inbox") {
        int page = (argc >= 3) ? std::atoi(argv[2]) : 1;
        if (page < 1) page = 1;
        return cmd_inbox(page);
    }

    if (cmd == "peers") return cmd_peers();

    if (cmd == "discover") {
        setup_network_handlers();
        return cmd_discover();
    }

    if (cmd == "connect") {
        if (argc < 4) {
            std::cerr << "usage: shr connect <peer_id> <ip> [port]\n";
            return 1;
        }
        uint16_t port = (argc >= 5) ? static_cast<uint16_t>(std::atoi(argv[4])) : SHR_DEFAULT_PORT;
        return cmd_connect(argv[2], argv[3], port);
    }

    if (cmd == "status") return cmd_status();
    if (cmd == "clean")  return cmd_clean();

    std::cerr << "error: unknown command '" << cmd << "'\n";
    std::cerr << "Run 'shr help' for usage.\n";
    return 1;
}
