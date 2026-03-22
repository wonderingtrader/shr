#include "utils.h"
#include "logger.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <ctime>
#include <chrono>
#include <random>
#include <regex>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #pragma comment(lib, "iphlpapi.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <unistd.h>
#endif

namespace utils {

std::string generate_uuid() {
    unsigned char bytes[16];
    RAND_bytes(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;
    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) oss << '-';
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)bytes[i];
    }
    return oss.str();
}

bool is_valid_uuid(const std::string& s) {
    static const std::regex uuid_re(
        "^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$",
        std::regex::icase);
    return std::regex_match(s, uuid_re);
}

std::string hex_encode(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i)
        oss << std::hex << std::setfill('0') << std::setw(2) << (int)data[i];
    return oss.str();
}

std::vector<uint8_t> hex_decode(const std::string& hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto byte = std::stoul(hex.substr(i, 2), nullptr, 16);
        out.push_back(static_cast<uint8_t>(byte));
    }
    return out;
}

std::string sha256_bytes(const uint8_t* data, size_t len) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  hash_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    return hex_encode(hash, hash_len);
}

std::string sha256_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount()) {
        EVP_DigestUpdate(ctx, buf, f.gcount());
    }
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    return hex_encode(hash, hash_len);
}

std::string base64_encode(const uint8_t* data, size_t len) {
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem);
    BIO_write(b64, data, (int)len);
    BIO_flush(b64);
    BUF_MEM* ptr;
    BIO_get_mem_ptr(b64, &ptr);
    std::string result(ptr->data, ptr->length);
    BIO_free_all(b64);
    return result;
}

std::vector<uint8_t> base64_decode(const std::string& s) {
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(s.data(), (int)s.size());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_push(b64, bmem);
    std::vector<uint8_t> out(s.size());
    int len = BIO_read(bmem, out.data(), (int)out.size());
    BIO_free_all(bmem);
    if (len > 0) out.resize(len);
    else         out.clear();
    return out;
}

std::string format_bytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int idx = 0;
    double val = static_cast<double>(bytes);
    while (val >= 1024.0 && idx < 4) { val /= 1024.0; ++idx; }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(idx > 0 ? 2 : 0) << val << " " << units[idx];
    return oss.str();
}

std::string format_duration(double seconds) {
    int h = (int)(seconds / 3600);
    int m = (int)(seconds / 60) % 60;
    int s = (int)seconds % 60;
    std::ostringstream oss;
    if (h > 0) oss << h << "h ";
    if (m > 0) oss << m << "m ";
    oss << s << "s";
    return oss.str();
}

std::string format_speed(double bps) {
    return format_bytes(static_cast<uint64_t>(bps)) + "/s";
}

std::string timestamp_to_str(int64_t ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buf);
}

std::string current_timestamp_str() {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    return timestamp_to_str(static_cast<int64_t>(t));
}

std::filesystem::path shr_config_dir() {
    std::filesystem::path home;
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    home = appdata ? appdata : ".";
#else
    const char* h = std::getenv("HOME");
    home = h ? h : ".";
#endif
    auto dir = home / ".shr";
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path shr_received_dir() {
    std::filesystem::path home;
#ifdef _WIN32
    const char* ud = std::getenv("USERPROFILE");
    home = ud ? ud : ".";
#else
    const char* h = std::getenv("HOME");
    home = h ? h : ".";
#endif
    auto dir = home / "shr_received";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string sanitize_filename(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            out += '_';
        else
            out += c;
    }
    if (out.empty() || out == "." || out == "..")
        out = "received_file";
    return out;
}

bool path_is_safe(const std::string& base, const std::string& target) {
    auto b = std::filesystem::canonical(base);
    auto t = std::filesystem::weakly_canonical(target);
    auto rel = t.lexically_relative(b);
    return !rel.empty() && rel.native().find("..") == std::string::npos;
}

std::string read_file_str(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

bool write_file_str(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << content;
    return f.good();
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) parts.push_back(token);
    return parts;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

uint64_t file_size(const std::string& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return ec ? 0 : sz;
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

uint16_t find_free_port(uint16_t start, uint16_t end) {
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    for (uint16_t p = start; p <= end; ++p) {
        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(p);
        if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
#ifdef _WIN32
            closesocket(s);
#else
            close(s);
#endif
            return p;
        }
    }
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return 0;
}

std::string get_local_ip() {
    auto ips = get_local_ips();
    return ips.empty() ? "127.0.0.1" : ips[0];
}

std::vector<std::string> get_local_ips() {
    std::vector<std::string> ips;
#ifndef _WIN32
    struct ifaddrs* ifap = nullptr;
    getifaddrs(&ifap);
    for (auto* p = ifap; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
        if (!(p->ifa_flags & IFF_UP) || (p->ifa_flags & IFF_LOOPBACK)) continue;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,
                  &((struct sockaddr_in*)p->ifa_addr)->sin_addr,
                  buf, sizeof(buf));
        ips.push_back(buf);
    }
    if (ifap) freeifaddrs(ifap);
#endif
    return ips;
}

std::string get_broadcast_addr(const std::string& ip, const std::string& /*netmask*/) {
    auto parts = split(ip, '.');
    if (parts.size() != 4) return "255.255.255.255";
    return parts[0] + "." + parts[1] + "." + parts[2] + ".255";
}

}
