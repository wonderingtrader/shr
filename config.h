#pragma once

#include <string>
#include <cstdint>
#include <filesystem>

struct AppConfig {
    std::string user_id;
    uint16_t    listen_port;
    std::string config_dir;
    std::string db_path;
    std::string log_path;
    std::string received_dir;
    int         max_concurrent;
    int         retry_limit;
    int         retry_delay_ms;
    bool        verbose;
    std::string log_level;
};

class Config {
public:
    static Config& instance();

    bool        load();
    bool        save();
    AppConfig&  get() { return cfg_; }
    bool        is_installed() const;

private:
    Config() = default;
    AppConfig   cfg_;
    std::string cfg_file_path();
    void        apply_env_overrides();
    void        set_defaults();
};
