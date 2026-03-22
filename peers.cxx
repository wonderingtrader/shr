#include <nlohmann/json.hpp>
#include "peers.h"
#include "database.h"
#include "network.h"
#include "config.h"
#include "identity.h"
#include "utils.h"
#include "logger.h"

#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <iostream>

using json = nlohmann::json;

Peers& Peers::instance() {
    static Peers inst;
    return inst;
}

bool Peers::add_peer(const std::string& id, const std::string& ip, uint16_t port) {
    if (!utils::is_valid_uuid(id)) {
        std::cerr << "Invalid peer ID format\n";
        return false;
    }
    PeerInfo p;
    p.id        = id;
    p.ip        = ip;
    p.port      = port;
    p.online    = false;
    p.last_seen = 0;
    p.alias     = "";
    return Database::instance().upsert_peer(p);
}

bool Peers::remove_peer(const std::string& id) {
    return Database::instance().delete_peer(id);
}

std::vector<PeerInfo> Peers::list_peers() {
    return Database::instance().all_peers();
}

std::optional<PeerInfo> Peers::find_peer(const std::string& id) {
    return Database::instance().get_peer(id);
}

bool Peers::ping_peer(const std::string& id) {
    auto peer = Database::instance().get_peer(id);
    if (!peer) return false;

    json payload;
    payload["from"] = Identity::instance().id();
    payload["ts"]   = now_unix();
    auto bytes = json::to_cbor(payload);

    bool ok = Network::instance().send_packet(peer->ip, peer->port,
                                               PacketType::Ping,
                                               std::vector<uint8_t>(bytes.begin(), bytes.end()));
    if (ok) {
        peer->online    = true;
        peer->last_seen = now_unix();
        Database::instance().upsert_peer(*peer);
    } else {
        peer->online = false;
        Database::instance().upsert_peer(*peer);
    }
    return ok;
}

void Peers::discover() {
    json announce;
    announce["id"]   = Identity::instance().id();
    announce["port"] = Config::instance().get().listen_port;
    auto bytes = json::to_cbor(announce);

    std::cout << "Broadcasting discovery on local network...\n";
    bool ok = Network::instance().broadcast_udp(
        Config::instance().get().listen_port,
        std::vector<uint8_t>(bytes.begin(), bytes.end()));

    if (ok) std::cout << "Discovery broadcast sent.\n";
    else    std::cout << "Discovery broadcast failed (no network interface?).\n";
}

void Peers::handle_discover(const std::vector<uint8_t>& payload, int conn) {
    try {
        auto j    = json::from_cbor(payload);
        std::string peer_id = j.value("id",   "");
        uint16_t    peer_port= j.value("port", (uint16_t)SHR_DEFAULT_PORT);

        if (!utils::is_valid_uuid(peer_id)) return;
        if (peer_id == Identity::instance().id()) return;

        auto existing = Database::instance().get_peer(peer_id);
        if (!existing) {
            PeerInfo p;
            p.id        = peer_id;
            p.ip        = "";
            p.port      = peer_port;
            p.online    = true;
            p.last_seen = now_unix();
            p.alias     = "";
            Database::instance().upsert_peer(p);
            LOG_INFO("Discovered new peer: " + peer_id);
        }

        json ack;
        ack["id"]   = Identity::instance().id();
        ack["port"] = Config::instance().get().listen_port;
        auto bytes = json::to_cbor(ack);
        Network::instance().send_packet_conn(static_cast<socket_t>(conn),
            PacketType::PeerAnnounce,
            std::vector<uint8_t>(bytes.begin(), bytes.end()));
    } catch (...) {
        LOG_WARN("handle_discover: malformed payload");
    }
}

void Peers::handle_announce(const std::vector<uint8_t>& payload, int conn) {
    try {
        auto j    = json::from_cbor(payload);
        std::string peer_id  = j.value("id",   "");
        uint16_t    peer_port= j.value("port", (uint16_t)SHR_DEFAULT_PORT);
        if (!utils::is_valid_uuid(peer_id)) return;
        if (peer_id == Identity::instance().id()) return;

        PeerInfo p;
        p.id        = peer_id;
        p.ip        = "";
        p.port      = peer_port;
        p.online    = true;
        p.last_seen = now_unix();
        p.alias     = "";
        Database::instance().upsert_peer(p);
        LOG_INFO("Peer announced: " + peer_id);
    } catch (...) {}
}

void Peers::handle_ping(const std::vector<uint8_t>& payload, int conn) {
    try {
        auto j = json::from_cbor(payload);
        std::string from = j.value("from", "");

        auto peer = Database::instance().get_peer(from);
        if (peer) {
            peer->online    = true;
            peer->last_seen = now_unix();
            Database::instance().upsert_peer(*peer);
        }

        json pong;
        pong["from"] = Identity::instance().id();
        pong["ts"]   = now_unix();
        auto bytes = json::to_cbor(pong);
        Network::instance().send_packet_conn(static_cast<socket_t>(conn),
            PacketType::Pong,
            std::vector<uint8_t>(bytes.begin(), bytes.end()));
    } catch (...) {}
}

void Peers::handle_pong(const std::vector<uint8_t>& payload, int /*conn*/) {
    try {
        auto j    = json::from_cbor(payload);
        std::string from = j.value("from", "");
        auto peer = Database::instance().get_peer(from);
        if (peer) {
            peer->online    = true;
            peer->last_seen = now_unix();
            Database::instance().upsert_peer(*peer);
        }
    } catch (...) {}
}

void Peers::start_beacon() {
    beacon_running_ = true;
    beacon_thread_  = std::thread(&Peers::beacon_loop, this);
}

void Peers::stop_beacon() {
    beacon_running_ = false;
    if (beacon_thread_.joinable()) beacon_thread_.join();
}

void Peers::beacon_loop() {
    while (beacon_running_) {
        auto peers = Database::instance().all_peers();
        for (auto& p : peers) {
            if (p.ip.empty()) continue;
            json ping_j;
            ping_j["from"] = Identity::instance().id();
            ping_j["ts"]   = now_unix();
            auto bytes = json::to_cbor(ping_j);
            bool alive = Network::instance().send_packet(p.ip, p.port,
                PacketType::Ping,
                std::vector<uint8_t>(bytes.begin(), bytes.end()));
            p.online = alive;
            if (alive) p.last_seen = now_unix();
            Database::instance().upsert_peer(p);
        }
        for (int i = 0; i < 30 && beacon_running_; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
