#include "identity.h"
#include "config.h"
#include "utils.h"
#include "crypto.h"
#include "logger.h"
#include <filesystem>
#include <iostream>

Identity& Identity::instance() {
    static Identity inst;
    return inst;
}

bool Identity::is_installed() const {
    return !id_.empty() && Config::instance().is_installed();
}

bool Identity::install() {
    auto& cfg = Config::instance().get();

    std::string new_id = utils::generate_uuid();
    cfg.user_id = new_id;
    id_ = new_id;

    std::filesystem::create_directories(cfg.config_dir);
    std::filesystem::create_directories(cfg.received_dir);

    std::string key_dir = cfg.config_dir;
    if (!Crypto::instance().load_or_generate_keypair(key_dir)) {
        LOG_ERROR("Failed to generate keypair");
        return false;
    }

    if (!Config::instance().save()) {
        LOG_ERROR("Failed to save config");
        return false;
    }

    return true;
}

bool Identity::load() {
    auto& cfg = Config::instance().get();
    if (cfg.user_id.empty()) return false;
    id_ = cfg.user_id;
    return true;
}
