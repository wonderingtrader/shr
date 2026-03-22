#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>

class Peers {
public:
    static Peers& instance();

    bool add_peer(const std::string& id, const std::string& ip, uint16_t port);
    bool remove_peer(const std::string& id);
    std::vector<PeerInfo> list_peers();
    std::optional<PeerInfo> find_peer(const std::string& id);

    void discover();
    void start_beacon();
    void stop_beacon();
    void handle_discover(const std::vector<uint8_t>& payload, int conn);
    void handle_announce(const std::vector<uint8_t>& payload, int conn);
    void handle_ping(const std::vector<uint8_t>& payload, int conn);
    void handle_pong(const std::vector<uint8_t>& payload, int conn);

    bool ping_peer(const std::string& id);

private:
    Peers() = default;
    void beacon_loop();
    std::thread       beacon_thread_;
    std::atomic<bool> beacon_running_{false};
};
