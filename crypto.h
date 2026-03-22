#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>

struct KeyPair {
    std::array<uint8_t, 32> pub_key;
    std::array<uint8_t, 32> priv_key;
};

struct SessionKey {
    std::array<uint8_t, 32> key;
    std::array<uint8_t, 12> nonce;
};

class Crypto {
public:
    static Crypto& instance();

    bool    init(const std::string& key_dir);
    KeyPair generate_keypair();
    bool    load_or_generate_keypair(const std::string& key_dir);

    std::vector<uint8_t> derive_shared_secret(
        const std::array<uint8_t, 32>& priv_key,
        const std::array<uint8_t, 32>& peer_pub_key);

    std::vector<uint8_t> encrypt(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& key,
        std::array<uint8_t, 12>&    out_nonce);

    std::vector<uint8_t> decrypt(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& key,
        const std::array<uint8_t, 12>& nonce);

    std::array<uint8_t, 32> hmac_sha256(
        const uint8_t* data, size_t len,
        const uint8_t* key,  size_t key_len);

    bool verify_hmac(
        const uint8_t* data, size_t len,
        const uint8_t* key,  size_t key_len,
        const std::array<uint8_t, 32>& expected);

    std::string cert_fingerprint();
    const KeyPair& keypair() const { return keypair_; }

private:
    Crypto() = default;
    KeyPair keypair_;
    bool    loaded_ = false;
};
