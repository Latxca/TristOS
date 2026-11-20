#include "fs.hpp"

namespace trist::fs {

static bool equal(const char* left, const char* right) {
    while (*left != 0 && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static void copy_name(char* destination, const char* source) {
    uint32_t i = 0;
    for (; i < 31 && source[i] != 0; ++i) destination[i] = source[i];
    destination[i] = 0;
}

void format(Volume* volume) {
    volume->signature = kNtfsSignature;
    volume->version = 1;
    for (uint32_t i = 0; i < kMaxFiles; ++i) volume->files[i].used = 0;
}

bool mount(Volume* volume) {
    return volume != nullptr && volume->signature == kNtfsSignature && volume->version == 1;
}

bool create(Volume* volume, const char* name, uint32_t size, bool encrypted) {
    if (!mount(volume) || find(volume, name) != nullptr) return false;
    for (uint32_t i = 0; i < kMaxFiles; ++i) {
        if (!volume->files[i].used) {
            copy_name(volume->files[i].name, name);
            volume->files[i].size = size;
            volume->files[i].encrypted = encrypted ? 1 : 0;
            volume->files[i].used = 1;
            return true;
        }
    }
    return false;
}

const FileRecord* find(const Volume* volume, const char* name) {
    if (!mount(const_cast<Volume*>(volume))) return nullptr;
    for (uint32_t i = 0; i < kMaxFiles; ++i)
        if (volume->files[i].used && equal(volume->files[i].name, name)) return &volume->files[i];
    return nullptr;
}

}
