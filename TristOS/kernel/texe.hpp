#pragma once

#include <stdint.h>

namespace trist::texe {

struct Header {
    char magic[8];
    uint32_t version;
    uint32_t type;
    uint64_t entry;
};

bool valid(const Header* header);

}
