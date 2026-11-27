#include "vm.hpp"

namespace tristos::vm {
namespace {

inline constexpr size_t kPml4Shift = 39;
inline constexpr size_t kPdptShift = 30;
inline constexpr size_t kPdShift = 21;
inline constexpr size_t kPtShift = 12;
inline constexpr size_t kIndexMask = 0x1FFULL;

Config g_config{};
bool g_initialized = false;

void zero_bytes(void* dst, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        d[i] = 0;
    }
}

void copy_bytes(void* dst, const void* src, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
}

bool is_page_aligned(uint64_t addr) {
    return (addr & kPageOffsetMask) == 0;
}

size_t pml4_index(uint64_t va) { return static_cast<size_t>((va >> kPml4Shift) & kIndexMask); }
size_t pdpt_index(uint64_t va) { return static_cast<size_t>((va >> kPdptShift) & kIndexMask); }
size_t pd_index(uint64_t va)   { return static_cast<size_t>((va >> kPdShift) & kIndexMask); }
size_t pt_index(uint64_t va)   { return static_cast<size_t>((va >> kPtShift) & kIndexMask); }

PageTable* table_from_phys(uint64_t phys_addr) {
    return static_cast<PageTable*>(phys_to_virt(phys_addr & kPhysAddrMask));
}

const PageTable* const_table_from_phys(uint64_t phys_addr) {
    return static_cast<const PageTable*>(phys_to_virt_const(phys_addr & kPhysAddrMask));
}

uint64_t alloc_zeroed_table() {
    if (g_config.alloc_frame == nullptr) {
        return 0;
    }
    const uint64_t phys = g_config.alloc_frame(g_config.allocator_ctx);
    if (phys == 0 || !is_page_aligned(phys)) {
        return 0;
    }
    PageTable* tbl = table_from_phys(phys);
    zero_bytes(tbl, sizeof(*tbl));
    return phys;
}

void free_frame_if_possible(uint64_t phys_addr) {
    const uint64_t clean = phys_addr & kPhysAddrMask;
    if (clean != 0 && g_config.free_frame != nullptr) {
        g_config.free_frame(g_config.allocator_ctx, clean);
    }
}

uint64_t map_flags_to_pte(tristos::proc::MapFlags flags, uint64_t virt_addr) {
    uint64_t pte = kPtePresent;

    if (tristos::proc::has_map_flag(flags, tristos::proc::MapFlags::Write)) {
        pte |= kPteWritable;
    }

    const bool user_flag =
        tristos::proc::has_map_flag(flags, tristos::proc::MapFlags::UserAccessible) &&
        is_user_address(virt_addr);
    if (user_flag) {
        pte |= kPteUser;
    } else if (!is_user_address(virt_addr)) {
        pte |= kPteGlobal;
    }

    // Enforce W^X: non-executable pages get NX; executable pages drop Writable
    const bool exec = tristos::proc::has_map_flag(flags, tristos::proc::MapFlags::Execute);
    if (!exec) {
        pte |= kPteNoExecute;
    } else {
        pte &= ~kPteWritable;
    }

    return pte;
}

uint64_t intermediate_flags_for_va(uint64_t virt_addr) {
    return is_user_address(virt_addr) ? kIntermediateUserFlags : kIntermediateKernelFlags;
}

Status walk_to_pt(uint64_t cr3_phys,
                   uint64_t virt_addr,
                   bool create_missing,
                   PageTable** out_pt) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (cr3_phys == 0 || !is_page_aligned(cr3_phys) || out_pt == nullptr) {
        return Status::InvalidArg;
    }

    const uint64_t inter_flags = intermediate_flags_for_va(virt_addr);

    PageTable* pml4 = table_from_phys(cr3_phys);
    uint64_t& pml4e = pml4->entries[pml4_index(virt_addr)];
    if ((pml4e & kPtePresent) == 0) {
        if (!create_missing) {
            return Status::NotMapped;
        }
        const uint64_t pdpt_phys = alloc_zeroed_table();
        if (pdpt_phys == 0) {
            return Status::OutOfMemory;
        }
        pml4e = (pdpt_phys & kPhysAddrMask) | inter_flags;
    } else if ((pml4e & kPtePageSize) != 0) {
        return Status::LargePageConflict;
    } else if (is_user_address(virt_addr)) {
        pml4e |= kPteUser | kPteWritable;
    }

    PageTable* pdpt = table_from_phys(pml4e);
    uint64_t& pdpte = pdpt->entries[pdpt_index(virt_addr)];
    if ((pdpte & kPtePresent) == 0) {
        if (!create_missing) {
            return Status::NotMapped;
        }
        const uint64_t pd_phys = alloc_zeroed_table();
        if (pd_phys == 0) {
            return Status::OutOfMemory;
        }
        pdpte = (pd_phys & kPhysAddrMask) | inter_flags;
    } else if ((pdpte & kPtePageSize) != 0) {
        return Status::LargePageConflict;
    } else if (is_user_address(virt_addr)) {
        pdpte |= kPteUser | kPteWritable;
    }

    PageTable* pd = table_from_phys(pdpte);
    uint64_t& pde = pd->entries[pd_index(virt_addr)];
    if ((pde & kPtePresent) == 0) {
        if (!create_missing) {
            return Status::NotMapped;
        }
        const uint64_t pt_phys = alloc_zeroed_table();
        if (pt_phys == 0) {
            return Status::OutOfMemory;
        }
        pde = (pt_phys & kPhysAddrMask) | inter_flags;
    } else if ((pde & kPtePageSize) != 0) {
        return Status::LargePageConflict;
    } else if (is_user_address(virt_addr)) {
        pde |= kPteUser | kPteWritable;
    }

    *out_pt = table_from_phys(pde);
    return Status::Ok;
}

void free_pt_level(uint64_t table_phys, int level) {
    const uint64_t clean = table_phys & kPhysAddrMask;
    if (clean == 0) {
        return;
    }
    if (level > 1) {
        const PageTable* tbl = const_table_from_phys(clean);
        for (size_t i = 0; i < kTableEntries; ++i) {
            const uint64_t entry = tbl->entries[i];
            if ((entry & kPtePresent) != 0 && (entry & kPtePageSize) == 0) {
                free_pt_level(entry, level - 1);
            }
        }
    }
    free_frame_if_possible(clean);
}

} // namespace

Status init(const Config& config) {
    if (config.kernel_cr3_phys == 0 || !is_page_aligned(config.kernel_cr3_phys)) {
        return Status::InvalidArg;
    }
    g_config = config;
    g_initialized = true;
    return Status::Ok;
}

bool is_initialized() {
    return g_initialized;
}

const Config& config() {
    return g_config;
}

Status create_address_space(uint64_t* out_cr3_phys) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (out_cr3_phys == nullptr) {
        return Status::InvalidArg;
    }

    const uint64_t new_pml4_phys = alloc_zeroed_table();
    if (new_pml4_phys == 0) {
        return Status::OutOfMemory;
    }

    const PageTable* kernel_pml4 = const_table_from_phys(g_config.kernel_cr3_phys);
    PageTable* user_pml4 = table_from_phys(new_pml4_phys);

    // Clone upper-half / kernel PML4 entries while leaving lower user slots clean for isolation
    for (size_t i = 256; i < kTableEntries; ++i) {
        user_pml4->entries[i] = kernel_pml4->entries[i];
    }

    *out_cr3_phys = new_pml4_phys;
    return Status::Ok;
}

Status destroy_address_space(uint64_t cr3_phys) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    const uint64_t clean = cr3_phys & kPhysAddrMask;
    if (clean == 0 || !is_page_aligned(clean) || clean == (g_config.kernel_cr3_phys & kPhysAddrMask)) {
        return Status::InvalidArg;
    }

    if ((current_cr3() & kPhysAddrMask) == clean) {
        activate_address_space(g_config.kernel_cr3_phys);
    }

    // Free only private lower-half user subtrees (indices 0..255); upper kernel tables are shared
    const PageTable* user_pml4 = const_table_from_phys(clean);
    for (size_t i = 0; i < 256; ++i) {
        const uint64_t entry = user_pml4->entries[i];
        if ((entry & kPtePresent) != 0 && (entry & kPtePageSize) == 0) {
            free_pt_level(entry, 3);
        }
    }

    free_frame_if_possible(clean);
    return Status::Ok;
}

Status map_page(uint64_t cr3_phys,
                uint64_t virt_addr,
                uint64_t phys_addr,
                tristos::proc::MapFlags flags) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (!is_page_aligned(virt_addr) || !is_page_aligned(phys_addr)) {
        return Status::UnalignedAddress;
    }

    PageTable* pt = nullptr;
    Status st = walk_to_pt(cr3_phys, virt_addr, true, &pt);
    if (st != Status::Ok) {
        return st;
    }

    uint64_t& pte = pt->entries[pt_index(virt_addr)];
    if ((pte & kPtePresent) != 0) {
        return Status::AlreadyMapped;
    }

    pte = (phys_addr & kPhysAddrMask) | map_flags_to_pte(flags, virt_addr);
    if ((current_cr3() & kPhysAddrMask) == (cr3_phys & kPhysAddrMask)) {
        invalidate_page(virt_addr);
    }
    return Status::Ok;
}

Status map_range(uint64_t cr3_phys,
                 uint64_t virt_start,
                 uint64_t phys_start,
                 size_t byte_len,
                 tristos::proc::MapFlags flags) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (byte_len == 0) {
        return Status::Ok;
    }
    if (!is_page_aligned(virt_start) || !is_page_aligned(phys_start)) {
        return Status::UnalignedAddress;
    }
    if (virt_start > UINT64_MAX - byte_len || phys_start > UINT64_MAX - byte_len) {
        return Status::InvalidRange;
    }

    const size_t page_count = (static_cast<uint64_t>(byte_len) + kPageSize - 1ULL) / kPageSize;
    for (size_t i = 0; i < page_count; ++i) {
        const uint64_t offset = static_cast<uint64_t>(i) * kPageSize;
        Status st = map_page(cr3_phys, virt_start + offset, phys_start + offset, flags);
        if (st != Status::Ok) {
            return st;
        }
    }
    return Status::Ok;
}

Status unmap_page(uint64_t cr3_phys, uint64_t virt_addr, bool free_phys_frame) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (!is_page_aligned(virt_addr)) {
        return Status::UnalignedAddress;
    }

    PageTable* pt = nullptr;
    Status st = walk_to_pt(cr3_phys, virt_addr, false, &pt);
    if (st != Status::Ok) {
        return st;
    }

    uint64_t& pte = pt->entries[pt_index(virt_addr)];
    if ((pte & kPtePresent) == 0) {
        return Status::NotMapped;
    }

    const uint64_t mapped_phys = pte & kPhysAddrMask;
    pte = 0;

    if ((current_cr3() & kPhysAddrMask) == (cr3_phys & kPhysAddrMask)) {
        invalidate_page(virt_addr);
    }
    if (free_phys_frame) {
        free_frame_if_possible(mapped_phys);
    }
    return Status::Ok;
}

Status unmap_range(uint64_t cr3_phys,
                 uint64_t virt_start,
                 size_t byte_len,
                 bool free_phys_frames) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (byte_len == 0) {
        return Status::Ok;
    }
    if (!is_page_aligned(virt_start) || virt_start > UINT64_MAX - byte_len) {
        return Status::InvalidRange;
    }

    const size_t page_count = (static_cast<uint64_t>(byte_len) + kPageSize - 1ULL) / kPageSize;
    for (size_t i = 0; i < page_count; ++i) {
        const uint64_t va = virt_start + static_cast<uint64_t>(i) * kPageSize;
        Status st = unmap_page(cr3_phys, va, free_phys_frames);
        if (st != Status::Ok && st != Status::NotMapped) {
            return st;
        }
    }
    return Status::Ok;
}

Status protect_page(uint64_t cr3_phys, uint64_t virt_addr, tristos::proc::MapFlags flags) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (!is_page_aligned(virt_addr)) {
        return Status::UnalignedAddress;
    }

    PageTable* pt = nullptr;
    Status st = walk_to_pt(cr3_phys, virt_addr, false, &pt);
    if (st != Status::Ok) {
        return st;
    }

    uint64_t& pte = pt->entries[pt_index(virt_addr)];
    if ((pte & kPtePresent) == 0) {
        return Status::NotMapped;
    }

    const uint64_t phys_frame = pte & kPhysAddrMask;
    pte = phys_frame | map_flags_to_pte(flags, virt_addr);

    if ((current_cr3() & kPhysAddrMask) == (cr3_phys & kPhysAddrMask)) {
        invalidate_page(virt_addr);
    }
    return Status::Ok;
}

bool translate(uint64_t cr3_phys,
               uint64_t virt_addr,
               uint64_t* out_phys_addr,
               uint64_t* out_pte_flags) {
    if (!g_initialized || cr3_phys == 0) {
        return false;
    }

    const PageTable* pml4 = const_table_from_phys(cr3_phys);
    const uint64_t pml4e = pml4->entries[pml4_index(virt_addr)];
    if ((pml4e & kPtePresent) == 0) {
        return false;
    }

    const PageTable* pdpt = const_table_from_phys(pml4e);
    const uint64_t pdpte = pdpt->entries[pdpt_index(virt_addr)];
    if ((pdpte & kPtePresent) == 0) {
        return false;
    }
    if ((pdpte & kPtePageSize) != 0) {
        constexpr uint64_t k1GbMask = 0x000FFFFFC0000000ULL;
        if (out_phys_addr != nullptr) {
            *out_phys_addr = (pdpte & k1GbMask) | (virt_addr & ~k1GbMask);
        }
        if (out_pte_flags != nullptr) {
            *out_pte_flags = pdpte & ~k1GbMask;
        }
        return true;
    }

    const PageTable* pd = const_table_from_phys(pdpte);
    const uint64_t pde = pd->entries[pd_index(virt_addr)];
    if ((pde & kPtePresent) == 0) {
        return false;
    }
    if ((pde & kPtePageSize) != 0) {
        constexpr uint64_t k2MbMask = 0x000FFFFFFFE00000ULL;
        if (out_phys_addr != nullptr) {
            *out_phys_addr = (pde & k2MbMask) | (virt_addr & ~k2MbMask);
        }
        if (out_pte_flags != nullptr) {
            *out_pte_flags = pde & ~k2MbMask;
        }
        return true;
    }

    const PageTable* pt = const_table_from_phys(pde);
    const uint64_t pte = pt->entries[pt_index(virt_addr)];
    if ((pte & kPtePresent) == 0) {
        return false;
    }

    if (out_phys_addr != nullptr) {
        *out_phys_addr = (pte & kPhysAddrMask) | (virt_addr & kPageOffsetMask);
    }
    if (out_pte_flags != nullptr) {
        *out_pte_flags = pte & ~kPhysAddrMask;
    }
    return true;
}

bool is_user_address(uint64_t virt_addr) {
    return virt_addr < kUserSpaceLimit;
}

bool is_user_range_valid(uint64_t cr3_phys,
                         uint64_t user_virt,
                         size_t byte_len,
                         bool need_write) {
    if (!g_initialized || cr3_phys == 0) {
        return false;
    }
    if (byte_len == 0) {
        return is_user_address(user_virt);
    }
    if (!is_user_address(user_virt)) {
        return false;
    }
    if (static_cast<uint64_t>(byte_len) > kUserSpaceLimit - user_virt) {
        return false;
    }

    const uint64_t start_page = user_virt & kPageMask;
    const uint64_t end_addr = user_virt + static_cast<uint64_t>(byte_len) - 1ULL;
    const uint64_t end_page = end_addr & kPageMask;

    for (uint64_t page = start_page;; page += kPageSize) {
        uint64_t flags = 0;
        if (!translate(cr3_phys, page, nullptr, &flags)) {
            return false;
        }
        if ((flags & kPtePresent) == 0 || (flags & kPteUser) == 0) {
            return false;
        }
        if (need_write && (flags & kPteWritable) == 0) {
            return false;
        }
        if (page == end_page) {
            break;
        }
    }

    return true;
}

void invalidate_page(uint64_t virt_addr) {
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void activate_address_space(uint64_t cr3_phys) {
    const uint64_t target = cr3_phys & kPhysAddrMask;
    if (target
