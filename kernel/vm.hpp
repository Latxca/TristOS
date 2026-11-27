#pragma once

#include "pcb.hpp"

#include <stddef.h>
#include <stdint.h>

namespace tristos::vm {

inline constexpr uint64_t kPageSize = 4096ULL;
inline constexpr uint64_t kPageMask = ~(kPageSize - 1ULL);
inline constexpr uint64_t kPageOffsetMask = kPageSize - 1ULL;
inline constexpr size_t kTableEntries = 512;

// Canonical x86-64 user-space upper limit (exclusive): [0x0000000000000000, 0x0000800000000000)
inline constexpr uint64_t kUserSpaceLimit = 0x0000800000000000ULL;

// x86-64 4 KiB page table entry (PTE / PDE / PDPTE / PML4E) flags
inline constexpr uint64_t kPtePresent = 1ULL << 0;
inline constexpr uint64_t kPteWritable = 1ULL << 1;
inline constexpr uint64_t kPteUser = 1ULL << 2;
inline constexpr uint64_t kPteWriteThrough = 1ULL << 3;
inline constexpr uint64_t kPteCacheDisable = 1ULL << 4;
inline constexpr uint64_t kPteAccessed = 1ULL << 5;
inline constexpr uint64_t kPteDirty = 1ULL << 6;
inline constexpr uint64_t kPtePageSize = 1ULL << 7; // Large page bit (PD / PDPT)
inline constexpr uint64_t kPteGlobal = 1ULL << 8;
inline constexpr uint64_t kPteNoExecute = 1ULL << 63;

// Hardware physical frame address mask (bits 12..51)
inline constexpr uint64_t kPhysAddrMask = 0x000FFFFFFFFFF000ULL;

// Intermediate table permissions (allow traversal; leaf permissions enforce R/W/U/NX)
inline constexpr uint64_t kIntermediateKernelFlags = kPtePresent | kPteWritable;
inline constexpr uint64_t kIntermediateUserFlags = kPtePresent | kPteWritable | kPteUser;

enum class Status : uint8_t {
    Ok = 0,
    InvalidArg,
    NotInitialized,
    OutOfMemory,
    NotMapped,
    AlreadyMapped,
    UnalignedAddress,
    InvalidRange,
    LargePageConflict
};

struct PageTable {
    uint64_t entries[kTableEntries];
} __attribute__((aligned(4096)));

static_assert(sizeof(PageTable) == kPageSize, "PageTable must be exactly 4096 bytes");

// Physical frame allocator callbacks (backed by static pool or physical memory manager)
using AllocFrameFn = uint64_t (*)(void* ctx);
using FreeFrameFn = void (*)(void* ctx, uint64_t phys_addr);

struct Config {
    uint64_t kernel_cr3_phys; // Boot/kernel PML4 physical address
    uint64_t hhdm_offset;     // Higher-half direct map base (0 if identity-mapped early)
    AllocFrameFn alloc_frame;
    FreeFrameFn free_frame;
    void* allocator_ctx;
};

// Initialize virtual memory subsystem with kernel PML4 and frame allocator hooks
Status init(const Config& config);
bool is_initialized();
const Config& config();

// Address-space lifecycle (creates a per-process PML4 sharing kernel mappings)
Status create_address_space(uint64_t* out_cr3_phys);
Status destroy_address_space(uint64_t cr3_phys);

// 4 KiB page mapping, unmapping, permission update, and translation
Status map_page(uint64_t cr3_phys,
                uint64_t virt_addr,
                uint64_t phys_addr,
                tristos::proc::MapFlags flags);

Status map_range(uint64_t cr3_phys,
                 uint64_t virt_start,
                 uint64_t phys_start,
                 size_t byte_len,
                 tristos::proc::MapFlags flags);

Status unmap_page(uint64_t cr3_phys, uint64_t virt_addr, bool free_phys_frame);
Status unmap_range(uint64_t cr3_phys, uint64_t virt_start, size_t byte_len, bool free_phys_frames);

Status protect_page(uint64_t cr3_phys, uint64_t virt_addr, tristos::proc::MapFlags flags);

bool translate(uint64_t cr3_phys,
               uint64_t virt_addr,
               uint64_t* out_phys_addr,
               uint64_t* out_pte_flags = nullptr);

// Boundary safety helpers for validating Ring 3 pointers before kernel dereference
bool is_user_address(uint64_t virt_addr);
bool is_user_range_valid(uint64_t cr3_phys,
                         uint64_t user_virt,
                         size_t byte_len,
                         bool need_write);

// TLB and CR3 control helpers
void invalidate_page(uint64_t virt_addr);
void activate_address_space(uint64_t cr3_phys);
uint64_t current_cr3();
uint64_t kernel_cr3();

// Convert physical address to kernel-accessible virtual pointer via HHDM
void* phys_to_virt(uint64_t phys_addr);
const void* phys_to_virt_const(uint64_t phys_addr);

const char* status_to_string(Status status);

} // namespace tristos::vm
