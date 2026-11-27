#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tristos::proc {

inline constexpr uint32_t kInvalidPid = 0;
inline constexpr size_t kMaxProcesses = 32;
inline constexpr size_t kMaxProcNameLen = 32;
inline constexpr size_t kKernelStackBytes = 8192; // 8 KiB per task kernel stack

enum class State : uint8_t {
    Unused = 0,
    Created,
    Ready,
    Running,
    Blocked,
    Zombie
};

enum class Privilege : uint8_t {
    Kernel = 0,        // Ring 0 full kernel privileges
    SystemService = 1, // Ring 0 or privileged service with restricted capabilities
    User = 3           // Ring 3 unprivileged user mode (U/S = 1 pages only)
};

enum class MapFlags : uint32_t {
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Execute = 1u << 2,
    UserAccessible = 1u << 3 // Sets x86-64 U/S bit; clear = kernel-only
};

constexpr MapFlags operator|(MapFlags a, MapFlags b) {
    return static_cast<MapFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr bool has_map_flag(MapFlags flags, MapFlags test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

struct CpuContext {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rip;    // Entry or resume instruction pointer
    uint64_t cs;     // Code segment selector (Ring 0 vs Ring 3)
    uint64_t rflags; // Interrupts/flags state
    uint64_t rsp;    // Active or user stack pointer
    uint64_t ss;     // Stack segment selector
};

struct AddressSpace {
    uint64_t cr3_phys;     // Physical address of PML4 root table
    uint64_t code_base;    // Virtual base of loaded .texe image
    uint64_t code_size;    // Mapped code + data + BSS span
    uint64_t stack_top;    // Top of user (or task) stack virtual address
    uint64_t stack_size;   // Stack allocation size in bytes
    bool isolated;         // True for dedicated per-process page tables
};

struct Pcb {
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t gid;
    int32_t exit_code;
    State state;
    Privilege privilege;
    uint8_t priority;
    bool system_sealed;
    char name[kMaxProcNameLen];

    CpuContext context;
    AddressSpace addr_space;

    uint64_t kernel_stack_top; // Loaded into TSS.RSP0 on switch to this task
    uint8_t kernel_stack[kKernelStackBytes] __attribute__((aligned(16)));
};

class ProcessTable {
public:
    void init();
    Pcb* create_from_image(uint32_t ppid,
                           uint32_t uid,
                           uint32_t gid,
                           const char* name,
                           Privilege privilege,
                           bool system_sealed,
                           uint64_t entry_va,
                           uint64_t code_base,
                           uint64_t code_size,
                           uint64_t stack_top,
                           uint64_t stack_size,
                           uint64_t cr3_phys);
    Pcb* find(uint32_t pid);
    const Pcb* find(uint32_t pid) const;
    bool set_state(uint32_t pid, State next_state);
    bool terminate(uint32_t pid, int32_t exit_code);
    Pcb* current() { return current_; }
    const Pcb* current() const { return current_; }
    void set_current(Pcb* pcb) { current_ = pcb; }

private:
    Pcb entries_[kMaxProcesses];
    Pcb* current_ = nullptr;
    uint32_t next_pid_ = 1;
};

} // namespace tristos::proc

namespace tristos::texe {
struct LoadedImage;
}

Pcb* create_process_from_texe(ProcessTable* table,
                              const tristos::texe::LoadedImage& image,
                              uint32_t ppid,
                              uint32_t uid,
                              uint32_t gid,
                              uint64_t cr3_phys = 0);
