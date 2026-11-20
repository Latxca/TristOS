#pragma once

#include <stdint.h>

namespace trist::fs {

constexpr uint32_t kNtfsSignature = 0x5346544e;
constexpr uint32_t kMaxFiles = 32;

struct FileRecord {
    char name[32];
    uint32_t size;
    uint8_t encrypted;
    uint8_t used;
};

struct Volume {
    uint32_t signature;
    uint32_t version;
    FileRecord files[kMaxFiles];
};

void format(Volume* volume);
bool mount(Volume* volume);
bool create(Volume* volume, const char* name, uint32_t size, bool encrypted);
const FileRecord* find(const Volume* volume, const char* name);

}
