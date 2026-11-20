#include "crypto.hpp"

namespace trist::crypto {

static uint32_t rotate_left(uint32_t value, uint32_t amount) {
    return (value << amount) | (value >> (32 - amount));
}

static uint32_t load32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static void store32(uint8_t* p, uint32_t value) {
    p[0] = value & 0xff;
    p[1] = (value >> 8) & 0xff;
    p[2] = (value >> 16) & 0xff;
    p[3] = (value >> 24) & 0xff;
}

static void quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d ^= a; d = rotate_left(d, 16);
    c += d; b ^= c; b = rotate_left(b, 12);
    a += b; d ^= a; d = rotate_left(d, 8);
    c += d; b ^= c; b = rotate_left(b, 7);
}

void ChaCha20::init(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12]) {
    state[0] = 0x61707865; state[1] = 0x3320646e; state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (uint32_t i = 0; i < 8; ++i) state[4 + i] = load32(key + i * 4);
    state[12] = counter;
    state[13] = load32(nonce);
    state[14] = load32(nonce + 4);
    state[15] = load32(nonce + 8);
}

void ChaCha20::crypt(uint8_t* data, uint64_t length) {
    uint8_t stream[64];
    while (length != 0) {
        uint32_t working[16];
        for (uint32_t i = 0; i < 16; ++i) working[i] = state[i];
        for (uint32_t round = 0; round < 10; ++round) {
            quarter_round(working[0], working[4], working[8], working[12]);
            quarter_round(working[1], working[5], working[9], working[13]);
            quarter_round(working[2], working[6], working[10], working[14]);
            quarter_round(working[3], working[7], working[11], working[15]);
            quarter_round(working[0], working[5], working[10], working[15]);
            quarter_round(working[1], working[6], working[11], working[12]);
            quarter_round(working[2], working[7], working[8], working[13]);
            quarter_round(working[3], working[4], working[9], working[14]);
        }
        for (uint32_t i = 0; i < 16; ++i) store32(stream + i * 4, working[i] + state[i]);
        uint64_t block = length < 64 ? length : 64;
        for (uint64_t i = 0; i < block; ++i) data[i] ^= stream[i];
        data += block;
        length -= block;
        ++state[12];
    }
}

}
