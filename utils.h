#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <filesystem>

namespace utils {

std::string generate_uuid();
bool        is_valid_uuid(const std::string& s);
std::string sha256_file(const std::string& path);
std::string sha256_bytes(const uint8_t* data, size_t len);
std::string hex_encode(const uint8_t* data, size_t len);
std::vector<uint8_t> hex_decode(const std::string& hex);
std::string base64_encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64_decode(const std::string& s);
std::string format_bytes(uint64_t bytes);
std::string format_duration(double seconds);
std::string format_speed(double bytes_per_sec);
std::string timestamp_to_str(int64_t ts);
std::filesystem::path shr_config_dir();
std::filesystem::path shr_received_dir();
std::string sanitize_filename(const std::string& name);
bool        path_is_safe(const std::string& base, const std::string& target);
std::string read_file_str(const std::string& path);
bool        write_file_str(const std::string& path, const std::string& content);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
std::string join(const std::vector<std::string>& v, const std::string& sep);
uint16_t    find_free_port(uint16_t start, uint16_t end);
std::string get_local_ip();
std::vector<std::string> get_local_ips();
std::string get_broadcast_addr(const std::string& ip, const std::string& netmask);
std::string current_timestamp_str();
uint64_t    file_size(const std::string& path);
bool        file_exists(const std::string& path);

}
