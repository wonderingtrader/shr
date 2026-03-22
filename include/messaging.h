#pragma once

#include "types.h"
#include <string>
#include <vector>

class Messaging {
public:
    static Messaging& instance();

    bool send_msg(const std::string& recipient_id, const std::string& content);
    std::vector<Message> get_inbox(int page = 1, int page_size = 20);
    void handle_msg_receive(const std::vector<uint8_t>& payload);
    int  unread_count();

private:
    Messaging() = default;
};
