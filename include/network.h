#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <cstdint>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  using socket_t = int;
  static constexpr socket_t INVALID_SOCK = -1;
#endif

struct RawPacket {
    PacketType           type;
    std::vector<uint8_t> payload;
    std::string          peer_ip;
    uint16_t             peer_port;
};

using PacketHandler = std::function<void(const RawPacket&, socket_t conn)>;

class Network {
public:
    static Network& instance();

    bool start(uint16_t port);
    void stop();
    bool is_running() const { return running_; }
    uint16_t port() const { return port_; }

    bool send_packet(const std::string& ip, uint16_t port,
                     PacketType type, const std::vector<uint8_t>& payload);

    bool send_packet_conn(socket_t conn,
                          PacketType type, const std::vector<uint8_t>& payload);

    socket_t connect_to(const std::string& ip, uint16_t port);
    void close_conn(socket_t conn);

    void register_handler(PacketType type, PacketHandler handler);

    bool broadcast_udp(uint16_t port, const std::vector<uint8_t>& payload);

    bool read_packet(socket_t conn, RawPacket& out, int timeout_sec = SHR_CONNECT_TIMEOUT_SEC);

private:
    Network() = default;
    void accept_loop();
    void handle_conn(socket_t conn, const std::string& peer_ip);
    bool write_raw(socket_t conn, const uint8_t* data, size_t len);
    bool read_raw(socket_t conn, uint8_t* buf, size_t len, int timeout_sec);

    socket_t              listen_sock_ = INVALID_SOCK;
    uint16_t              port_ = 0;
    std::atomic<bool>     running_{false};
    std::thread           accept_thread_;
    std::map<PacketType, PacketHandler> handlers_;
    std::mutex            handlers_mu_;
};

std::vector<uint8_t> serialize_packet(PacketType type, const std::vector<uint8_t>& payload,
                                       const uint8_t* hmac_key = nullptr, size_t hmac_key_len = 0);
bool parse_packet_header(const uint8_t* buf, size_t buf_len,
                          PacketHeader& hdr_out, size_t& header_size_out);
