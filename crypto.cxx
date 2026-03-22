#include "crypto.h"
#include "utils.h"
#include "logger.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>
#include <openssl/ecdh.h>

#include <fstream>
#include <filesystem>
#include <cstring>
#include <stdexcept>

Crypto& Crypto::instance() {
    static Crypto inst;
    return inst;
}

bool Crypto::init(const std::string& key_dir) {
    return load_or_generate_keypair(key_dir);
}

KeyPair Crypto::generate_keypair() {
    KeyPair kp;
    RAND_bytes(kp.priv_key.data(), 32);
    kp.priv_key[0]  &= 248;
    kp.priv_key[31] &= 127;
    kp.priv_key[31] |= 64;

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_keygen(ctx, &pkey);

    size_t len = 32;
    EVP_PKEY_get_raw_private_key(pkey, kp.priv_key.data(), &len);
    len = 32;
    EVP_PKEY_get_raw_public_key(pkey, kp.pub_key.data(), &len);

    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return kp;
}

bool Crypto::load_or_generate_keypair(const std::string& key_dir) {
    std::string priv_path = key_dir + "/identity.key";
    std::string pub_path  = key_dir + "/identity.pub";

    if (std::filesystem::exists(priv_path) && std::filesystem::exists(pub_path)) {
        std::ifstream pf(priv_path, std::ios::binary);
        std::ifstream qf(pub_path,  std::ios::binary);
        if (!pf || !qf) return false;
        pf.read(reinterpret_cast<char*>(keypair_.priv_key.data()), 32);
        qf.read(reinterpret_cast<char*>(keypair_.pub_key.data()),  32);
        loaded_ = true;
        return true;
    }

    keypair_ = generate_keypair();

    std::ofstream pf(priv_path, std::ios::binary | std::ios::trunc);
    std::ofstream qf(pub_path,  std::ios::binary | std::ios::trunc);
    if (!pf || !qf) return false;
    pf.write(reinterpret_cast<char*>(keypair_.priv_key.data()), 32);
    qf.write(reinterpret_cast<char*>(keypair_.pub_key.data()),  32);

    std::filesystem::permissions(priv_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);

    loaded_ = true;
    return true;
}

std::vector<uint8_t> Crypto::derive_shared_secret(
    const std::array<uint8_t, 32>& priv_key,
    const std::array<uint8_t, 32>& peer_pub_key)
{
    EVP_PKEY* priv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv_key.data(), 32);
    EVP_PKEY* pub  = EVP_PKEY_new_raw_public_key( EVP_PKEY_X25519, nullptr, peer_pub_key.data(), 32);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(priv, nullptr);
    EVP_PKEY_derive_init(ctx);
    EVP_PKEY_derive_set_peer(ctx, pub);

    size_t secret_len = 0;
    EVP_PKEY_derive(ctx, nullptr, &secret_len);
    std::vector<uint8_t> secret(secret_len);
    EVP_PKEY_derive(ctx, secret.data(), &secret_len);

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);
    return secret;
}

std::vector<uint8_t> Crypto::encrypt(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    std::array<uint8_t, 12>&    out_nonce)
{
    RAND_bytes(out_nonce.data(), 12);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<uint8_t> ciphertext(plaintext.size() + 16);
    int len = 0, total = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
    EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), out_nonce.data());
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), (int)plaintext.size());
    total = len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + total, &len);
    total += len;

    uint8_t tag[16];
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(total);
    ciphertext.insert(ciphertext.end(), tag, tag + 16);
    return ciphertext;
}

std::vector<uint8_t> Crypto::decrypt(
    const std::vector<uint8_t>& ciphertext,
    const std::vector<uint8_t>& key,
    const std::array<uint8_t, 12>& nonce)
{
    if (ciphertext.size() < 16) return {};
    size_t data_len = ciphertext.size() - 16;
    const uint8_t* tag = ciphertext.data() + data_len;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<uint8_t> plaintext(data_len);
    int len = 0, total = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr);
    EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data());
    EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), (int)data_len);
    total = len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag);
    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + total, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret <= 0) return {};
    total += len;
    plaintext.resize(total);
    return plaintext;
}

std::array<uint8_t, 32> Crypto::hmac_sha256(
    const uint8_t* data, size_t len,
    const uint8_t* key,  size_t key_len)
{
    std::array<uint8_t, 32> out{};
    unsigned int out_len = 32;
    HMAC(EVP_sha256(), key, (int)key_len, data, len, out.data(), &out_len);
    return out;
}

bool Crypto::verify_hmac(
    const uint8_t* data, size_t len,
    const uint8_t* key,  size_t key_len,
    const std::array<uint8_t, 32>& expected)
{
    auto computed = hmac_sha256(data, len, key, key_len);
    return CRYPTO_memcmp(computed.data(), expected.data(), 32) == 0;
}

std::string Crypto::cert_fingerprint() {
    return utils::hex_encode(keypair_.pub_key.data(), 32);
}
