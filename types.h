#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <functional>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <array>

static constexpr uint16_t SHR_PORT_MIN = 60000;
static constexpr uint16_t SHR_PORT_MAX = 61000;
static constexpr uint16_t SHR_DEFAULT_PORT = 60000;
static constexpr size_t   SHR_CHUNK_SIZE = 1024 * 1024;
static constexpr int      SHR_MAX_CONCURRENT = 5;
static constexpr int      SHR_CONNECT_TIMEOUT_SEC = 10;
static constexpr int      SHR_RETRY_LIMIT = 5;
static constexpr int      SHR_RETRY_DELAY_MS = 2000;

static constexpr const char* SHR_VERSION = "1.0.0";
static constexpr const char* SHR_PROTOCOL_MAGIC = "SHR1";

enum class TransferState {
    Pending,
    InProgress,
    Paused,
    Completed,
    Failed,
    Cancelled
};

enum class MsgDirection {
    Inbound,
    Outbound
};

struct PeerInfo {
    std::string id;
    std::string ip;
    uint16_t    port;
    bool        online;
    int64_t     last_seen;
    std::string alias;
};

struct TransferRecord {
    std::string   id;
    std::string   peer_id;
    std::string   filename;
    std::string   filepath;
    uint64_t      total_bytes;
    uint64_t      transferred_bytes;
    TransferState state;
    std::string   checksum;
    int64_t       created_at;
    int64_t       updated_at;
    bool          outbound;
};

struct Message {
    std::string id;
    std::string sender_id;
    std::string recipient_id;
    std::string content;
    int64_t     timestamp;
    bool        read;
};

enum class PacketType : uint8_t {
    Handshake       = 0x01,
    HandshakeAck    = 0x02,
    FileOffer       = 0x10,
    FileAccept      = 0x11,
    FileReject      = 0x12,
    FileChunk       = 0x13,
    FileChunkAck    = 0x14,
    FileComplete    = 0x15,
    FileResume      = 0x16,
    MsgSend         = 0x20,
    MsgAck          = 0x21,
    PeerDiscover    = 0x30,
    PeerAnnounce    = 0x31,
    PeerList        = 0x32,
    Ping            = 0x40,
    Pong            = 0x41,
    Error           = 0xFF
};

struct PacketHeader {
    char     magic[4];
    uint8_t  type;
    uint32_t payload_len;
    uint8_t  hmac[32];
} __attribute__((packed));

inline std::string transfer_state_str(TransferState s) {
    switch (s) {
        case TransferState::Pending:     return "pending";
        case TransferState::InProgress:  return "in_progress";
        case TransferState::Paused:      return "paused";
        case TransferState::Completed:   return "completed";
        case TransferState::Failed:      return "failed";
        case TransferState::Cancelled:   return "cancelled";
        default:                         return "unknown";
    }
}

inline int64_t now_unix() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}
