#include "texe.hpp"

namespace trist::texe {

bool valid(const Header* header) {
    return header != nullptr && header->magic[0] == 'T' && header->magic[1] == 'R' &&
           header->magic[2] == 'I' && header->magic[3] == 'S' && header->magic[4] == 'T' &&
           header->magic[5] == 'E' && header->magic[6] == 'X' && header->magic[7] == 'E' &&
           header->version == 1 && header->entry != 0;
}

}
