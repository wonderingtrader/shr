#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>

using ProgressCallback = std::function<void(uint64_t transferred, uint64_t total, double speed_bps)>;

class Transfer {
public:
    static Transfer& instance();

    std::string send_file(const std::string& filepath,
                          const std::string& recipient_id,
                          ProgressCallback   cb = nullptr);

    bool        receive_pending(bool interactive);
    bool        download_transfer(const std::string& transfer_id, ProgressCallback cb = nullptr);

    void        handle_file_offer(const std::vector<uint8_t>& payload, int conn);
    void        handle_file_chunk(const std::vector<uint8_t>& payload, int conn);
    void        handle_file_complete(const std::vector<uint8_t>& payload, int conn);
    void        handle_file_accept(const std::vector<uint8_t>& payload, int conn);
    void        handle_file_resume(const std::vector<uint8_t>& payload, int conn);

    std::vector<TransferRecord> list_active();
    std::vector<TransferRecord> list_pending_inbound();

    bool cancel_transfer(const std::string& id);

private:
    Transfer() = default;

    void send_loop(const std::string& transfer_id,
                   const std::string& peer_ip,
                   uint16_t           peer_port,
                   ProgressCallback   cb);

    std::map<std::string, std::thread> send_threads_;
    std::mutex                         mu_;
    std::atomic<int>                   active_count_{0};
};
