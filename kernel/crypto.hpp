#pragma once

#include <stdint.h>

namespace trist::crypto {

struct ChaCha20 {
    uint32_t state[16];
    void init(const uint8_t key[32], uint32_t counter, const uint8_t nonce[12]);
    void crypt(uint8_t* data, uint64_t length);
};

}
