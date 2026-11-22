#include "texe.hpp"

namespace trist::texe {

bool valid(const Header* header) {
    return header != nullptr && header->magic[0] == 'T' && header->magic[1] == 'R' &&
           header->magic[2] == 'I' && header->magic[3] == 'S' && header->magic[4] == 'T' &&
           header->magic[5] == 'E' && header->magic[6] == 'X' && header->magic[7] == 'E' &&
           header->version == 1 && header->entry != 0;
}

bool supported_image(const uint8_t* image, uint64_t length) {
    if (image == nullptr || length < 2) return false;
    if (image[0] == 'M' && image[1] == 'Z') return true;
    return length >= sizeof(Header) && valid(reinterpret_cast<const Header*>(image));
}

}
