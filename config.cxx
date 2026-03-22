#include <nlohmann/json.hpp>
#include "config.h"
#include "utils.h"
#include "logger.h"
#include "types.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <filesystem>

using json = nlohmann::json;

Config& Config::instance() {
    static Config inst;
    return inst;
}

std::string Config::cfg_file_path() {
    return (utils::shr_config_dir() / "config.json").string();
}

void Config::set_defaults() {
    cfg_.config_dir    = utils::shr_config_dir().string();
    cfg_.db_path       = (utils::shr_config_dir() / "shr.db").string();
    cfg_.log_path      = (utils::shr_config_dir() / "shr.log").string();
    cfg_.received_dir  = utils::shr_received_dir().string();
    cfg_.listen_port   = SHR_DEFAULT_PORT;
    cfg_.max_concurrent = SHR_MAX_CONCURRENT;
    cfg_.retry_limit    = SHR_RETRY_LIMIT;
    cfg_.retry_delay_ms = SHR_RETRY_DELAY_MS;
    cfg_.verbose        = false;
    cfg_.log_level      = "info";
}

void Config::apply_env_overrides() {
    if (const char* p = std::getenv("SHR_PORT"))
        cfg_.listen_port = static_cast<uint16_t>(std::atoi(p));
    if (const char* v = std::getenv("SHR_VERBOSE"))
        cfg_.verbose = (std::string(v) == "1" || std::string(v) == "true");
    if (const char* l = std::getenv("SHR_LOG_LEVEL"))
        cfg_.log_level = l;
}

bool Config::load() {
    set_defaults();
    apply_env_overrides();
    std::string path = cfg_file_path();
    if (!std::filesystem::exists(path)) return true;
    std::ifstream f(path);
    if (!f) return false;
    try {
        json j;
        f >> j;
        if (j.contains("user_id"))       cfg_.user_id       = j["user_id"];
        if (j.contains("listen_port"))   cfg_.listen_port   = j["listen_port"];
        if (j.contains("max_concurrent"))cfg_.max_concurrent= j["max_concurrent"];
        if (j.contains("retry_limit"))   cfg_.retry_limit   = j["retry_limit"];
        if (j.contains("retry_delay_ms"))cfg_.retry_delay_ms= j["retry_delay_ms"];
        if (j.contains("log_level"))     cfg_.log_level     = j["log_level"];
        if (j.contains("verbose"))       cfg_.verbose       = j["verbose"];
    } catch (...) {
        return false;
    }
    apply_env_overrides();
    return true;
}

bool Config::save() {
    json j;
    j["user_id"]        = cfg_.user_id;
    j["listen_port"]    = cfg_.listen_port;
    j["max_concurrent"] = cfg_.max_concurrent;
    j["retry_limit"]    = cfg_.retry_limit;
    j["retry_delay_ms"] = cfg_.retry_delay_ms;
    j["log_level"]      = cfg_.log_level;
    j["verbose"]        = cfg_.verbose;
    std::ofstream f(cfg_file_path());
    if (!f) return false;
    f << j.dump(2);
    return f.good();
}

bool Config::is_installed() const {
    return !cfg_.user_id.empty();
}
