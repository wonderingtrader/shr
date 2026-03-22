#include <nlohmann/json.hpp>
#include "messaging.h"
#include "database.h"
#include "network.h"
#include "config.h"
#include "identity.h"
#include "peers.h"
#include "utils.h"
#include "logger.h"

#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <thread>

using json = nlohmann::json;

Messaging& Messaging::instance() {
    static Messaging inst;
    return inst;
}

bool Messaging::send_msg(const std::string& recipient_id, const std::string& content) {
    if (!utils::is_valid_uuid(recipient_id)) {
        std::cerr << "error: invalid recipient ID\n";
        return false;
    }
    if (content.empty()) {
        std::cerr << "error: message is empty\n";
        return false;
    }

    auto peer = Peers::instance().find_peer(recipient_id);
    if (!peer || peer->ip.empty()) {
        std::cerr << "error: peer not found or IP unknown. Use: shr connect <id> <ip>\n";
        return false;
    }

    std::string msg_id = utils::generate_uuid();
    int64_t     ts     = now_unix();

    json j;
    j["id"]           = msg_id;
    j["sender_id"]    = Identity::instance().id();
    j["recipient_id"] = recipient_id;
    j["content"]      = content;
    j["timestamp"]    = ts;

    auto bytes = json::to_cbor(j);

    bool ok = false;
    for (int attempt = 0; attempt < SHR_RETRY_LIMIT && !ok; ++attempt) {
        if (attempt > 0)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(Config::instance().get().retry_delay_ms));

        ok = Network::instance().send_packet(peer->ip, peer->port,
            PacketType::MsgSend,
            std::vector<uint8_t>(bytes.begin(), bytes.end()));
    }

    if (!ok) {
        std::cerr << "error: failed to deliver message after " << SHR_RETRY_LIMIT << " attempts\n";
        return false;
    }

    Message m;
    m.id           = msg_id;
    m.sender_id    = Identity::instance().id();
    m.recipient_id = recipient_id;
    m.content      = content;
    m.timestamp    = ts;
    m.read         = true;
    Database::instance().insert_message(m);

    std::cout << "[" << utils::timestamp_to_str(ts) << "] Message sent to " << recipient_id << "\n";
    return true;
}

std::vector<Message> Messaging::get_inbox(int page, int page_size) {
    return Database::instance().get_inbox(page, page_size);
}

int Messaging::unread_count() {
    return Database::instance().unread_count();
}

void Messaging::handle_msg_receive(const std::vector<uint8_t>& payload) {
    try {
        auto j = json::from_cbor(payload);

        Message m;
        m.id           = j.value("id",           utils::generate_uuid());
        m.sender_id    = j.value("sender_id",    "unknown");
        m.recipient_id = j.value("recipient_id", Identity::instance().id());
        m.content      = j.value("content",      "");
        m.timestamp    = j.value("timestamp",    now_unix());
        m.read         = false;

        Database::instance().insert_message(m);
        LOG_INFO("Message received from " + m.sender_id);
    } catch (...) {
        LOG_WARN("handle_msg_receive: malformed payload");
    }
}
