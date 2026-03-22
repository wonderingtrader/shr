#include "network.h"
#include "crypto.h"
#include "logger.h"
#include "utils.h"

#include <cstring>
#include <stdexcept>
#include <algorithm>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define CLOSE_SOCK(s) closesocket(s)
  #define SOCK_ERR      WSAGetLastError()
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #define CLOSE_SOCK(s) ::close(s)
  #define SOCK_ERR      errno
#endif

static constexpr size_t HEADER_SIZE = sizeof(PacketHeader);

std::vector<uint8_t> serialize_packet(PacketType type,
                                       const std::vector<uint8_t>& payload,
                                       const uint8_t* hmac_key,
                                       size_t hmac_key_len)
{
    PacketHeader hdr{};
    std::memcpy(hdr.magic, SHR_PROTOCOL_MAGIC, 4);
    hdr.type        = static_cast<uint8_t>(type);
    hdr.payload_len = static_cast<uint32_t>(payload.size());

    if (hmac_key && hmac_key_len > 0) {
        auto mac = Crypto::instance().hmac_sha256(
            payload.data(), payload.size(), hmac_key, hmac_key_len);
        std::memcpy(hdr.hmac, mac.data(), 32);
    }

    std::vector<uint8_t> out(HEADER_SIZE + payload.size());
    std::memcpy(out.data(), &hdr, HEADER_SIZE);
    if (!payload.empty())
        std::memcpy(out.data() + HEADER_SIZE, payload.data(), payload.size());
    return out;
}

bool parse_packet_header(const uint8_t* buf, size_t buf_len,
                          PacketHeader& hdr_out, size_t& header_size_out)
{
    if (buf_len < HEADER_SIZE) return false;
    std::memcpy(&hdr_out, buf, HEADER_SIZE);
    if (std::memcmp(hdr_out.magic, SHR_PROTOCOL_MAGIC, 4) != 0) return false;
    header_size_out = HEADER_SIZE;
    return true;
}

Network& Network::instance() {
    static Network inst;
    return inst;
}

bool Network::write_raw(socket_t conn, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = ::send(conn, reinterpret_cast<const char*>(data + sent),
                       static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

bool Network::read_raw(socket_t conn, uint8_t* buf, size_t len, int timeout_sec) {
    size_t received = 0;
    while (received < len) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(conn, &fds);
        struct timeval tv{ timeout_sec, 0 };
        int sel = select(static_cast<int>(conn) + 1, &fds, nullptr, nullptr, &tv);
        if (sel <= 0) return false;
        int n = ::recv(conn, reinterpret_cast<char*>(buf + received),
                       static_cast<int>(len - received), 0);
        if (n <= 0) return false;
        received += n;
    }
    return true;
}

bool Network::read_packet(socket_t conn, RawPacket& out, int timeout_sec) {
    uint8_t hdr_buf[HEADER_SIZE];
    if (!read_raw(conn, hdr_buf, HEADER_SIZE, timeout_sec)) return false;

    PacketHeader hdr{};
    size_t hdr_size = 0;
    if (!parse_packet_header(hdr_buf, HEADER_SIZE, hdr, hdr_size)) return false;

    out.type = static_cast<PacketType>(hdr.type);
    out.payload.resize(hdr.payload_len);
    if (hdr.payload_len > 0) {
        if (!read_raw(conn, out.payload.data(), hdr.payload_len, timeout_sec))
            return false;
    }
    return true;
}

bool Network::send_packet_conn(socket_t conn, PacketType type,
                                const std::vector<uint8_t>& payload)
{
    auto frame = serialize_packet(type, payload);
    return write_raw(conn, frame.data(), frame.size());
}

bool Network::send_packet(const std::string& ip, uint16_t port,
                           PacketType type, const std::vector<uint8_t>& payload)
{
    socket_t conn = connect_to(ip, port);
    if (conn == INVALID_SOCK) return false;
    bool ok = send_packet_conn(conn, type, payload);
    CLOSE_SOCK(conn);
    return ok;
}

socket_t Network::connect_to(const std::string& ip, uint16_t port) {
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCK) return INVALID_SOCK;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    struct timeval tv{ SHR_CONNECT_TIMEOUT_SEC, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));

    if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        CLOSE_SOCK(s);
        return INVALID_SOCK;
    }

    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag));
    return s;
}

void Network::close_conn(socket_t conn) {
    CLOSE_SOCK(conn);
}

void Network::register_handler(PacketType type, PacketHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mu_);
    handlers_[type] = std::move(handler);
}

bool Network::start(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock_ == INVALID_SOCK) return false;

    int opt = 1;
    setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<char*>(&opt), sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(listen_sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        CLOSE_SOCK(listen_sock_);
        listen_sock_ = INVALID_SOCK;
        return false;
    }
    if (listen(listen_sock_, 32) != 0) {
        CLOSE_SOCK(listen_sock_);
        listen_sock_ = INVALID_SOCK;
        return false;
    }

    port_    = port;
    running_ = true;
    accept_thread_ = std::thread(&Network::accept_loop, this);
    LOG_INFO("Network listening on port " + std::to_string(port));
    return true;
}

void Network::stop() {
    running_ = false;
    if (listen_sock_ != INVALID_SOCK) {
        CLOSE_SOCK(listen_sock_);
        listen_sock_ = INVALID_SOCK;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

void Network::accept_loop() {
    while (running_) {
        struct sockaddr_in peer_addr{};
        socklen_t addr_len = sizeof(peer_addr);
        socket_t conn = accept(listen_sock_,
                               reinterpret_cast<struct sockaddr*>(&peer_addr),
                               &addr_len);
        if (conn == INVALID_SOCK) {
            if (running_) LOG_WARN("accept() failed");
            continue;
        }
        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip));

        std::thread([this, conn, ip = std::string(peer_ip)]() {
            handle_conn(conn, ip);
        }).detach();
    }
}

void Network::handle_conn(socket_t conn, const std::string& peer_ip) {
    struct timeval tv{ SHR_CONNECT_TIMEOUT_SEC * 3, 0 };
    setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));

    RawPacket pkt;
    pkt.peer_ip = peer_ip;

    while (true) {
        if (!read_packet(conn, pkt, SHR_CONNECT_TIMEOUT_SEC * 3)) break;

        PacketHandler handler;
        {
            std::lock_guard<std::mutex> lock(handlers_mu_);
            auto it = handlers_.find(pkt.type);
            if (it != handlers_.end()) handler = it->second;
        }
        if (handler) {
            handler(pkt, conn);
        }

        if (pkt.type == PacketType::FileComplete ||
            pkt.type == PacketType::MsgAck       ||
            pkt.type == PacketType::Pong          ||
            pkt.type == PacketType::Error) {
            break;
        }
    }
    CLOSE_SOCK(conn);
}

bool Network::broadcast_udp(uint16_t port, const std::vector<uint8_t>& payload) {
    socket_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCK) return false;

    int bcast = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<char*>(&bcast), sizeof(bcast));

    auto ips = utils::get_local_ips();
    bool any_ok = false;
    for (auto& ip : ips) {
        std::string bcast_ip = utils::get_broadcast_addr(ip, "255.255.255.0");
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        inet_pton(AF_INET, bcast_ip.c_str(), &addr.sin_addr);

        auto frame = serialize_packet(PacketType::PeerDiscover, payload);
        int n = ::sendto(s,
                         reinterpret_cast<const char*>(frame.data()),
                         static_cast<int>(frame.size()), 0,
                         reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        if (n > 0) any_ok = true;
    }
    CLOSE_SOCK(s);
    return any_ok;
}
