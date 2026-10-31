#include <psa/crypto.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <stdexcept>
#include <string>

namespace wemod
{
namespace
{

constexpr auto sha256_len = std::size_t{32};

// One-shot SHA-256 over a span. Throws std::runtime_error on PSA failure.
std::string psa_sha256_hex(std::span<const unsigned char> data);

// Streaming SHA-256 accumulator used for whole-file hashing while the
// per-block hashes above go through the one-shot call.
struct psa_sha256_stream
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool active = false;

    psa_sha256_stream()
    {
        auto status = psa_crypto_init();
        if (status != PSA_SUCCESS) {
            throw std::runtime_error(std::format("psa_crypto_init failed: {}", static_cast<int>(status)));
        }
        status = psa_hash_setup(&op, PSA_ALG_SHA_256);
        if (status != PSA_SUCCESS) {
            throw std::runtime_error(std::format("psa_hash_setup failed: {}", static_cast<int>(status)));
        }
        active = true;
    }

    ~psa_sha256_stream()
    {
        if (active) {
            (void)psa_hash_abort(&op);
        }
    }

    psa_sha256_stream(const psa_sha256_stream&) = delete;
    psa_sha256_stream& operator=(const psa_sha256_stream&) = delete;
    psa_sha256_stream(psa_sha256_stream&&) = delete;
    psa_sha256_stream& operator=(psa_sha256_stream&&) = delete;

    void update(std::span<const unsigned char> chunk)
    {
        auto status = psa_hash_update(&op, chunk.data(), chunk.size());
        if (status != PSA_SUCCESS) {
            active = false;
            throw std::runtime_error(std::format("psa_hash_update failed: {}", static_cast<int>(status)));
        }
    }

    std::array<unsigned char, sha256_len> finish()
    {
        auto out = std::array<unsigned char, sha256_len>();
        auto out_len = std::size_t{0};
        auto status = psa_hash_finish(&op, out.data(), out.size(), &out_len);
        active = false;
        if (status != PSA_SUCCESS) {
            throw std::runtime_error(std::format("psa_hash_finish failed: {}", static_cast<int>(status)));
        }
        if (out_len != out.size()) {
            throw std::runtime_error("psa_hash_finish returned short hash");
        }
        return out;
    }
};

} // namespace
} // namespace wemod
