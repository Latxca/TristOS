#pragma once

#include "../../pcb.hpp"

#include <stddef.h>
#include <stdint.h>

namespace tristos::arch::x86_64 {

// x86-64 segment selectors (low 2 bits encode RPL: 0 = Ring 0, 3 = Ring 3)
inline constexpr uint16_t kKernelCodeSel = 0x08u; // Ring 0 64-bit code segment
inline constexpr uint16_t kKernelDataSel = 0x10u; // Ring 0 data/stack segment
inline constexpr uint16_t kUserDataSel   = 0x23u; // Ring 3 data/stack segment (0x20 | RPL 3)
inline constexpr uint16_t kUserCodeSel   = 0x2Bu; // Ring 3 64-bit code segment (0x28 | RPL 3)
inline constexpr uint16_t kTssSel        = 0x30u; // 64-bit TSS descriptor selector

// RFLAGS bit masks
inline constexpr uint64_t kRflagsReserved1  = 1ULL << 1;
inline constexpr uint64_t kRflagsInterrupts = 1ULL << 9;
inline constexpr uint64_t kDefaultUserRflags = kRflagsReserved1 | kRflagsInterrupts;

// x86-64 64-bit Task State Segment (104 bytes)
struct TaskStateSegment {
    uint32_t reserved0;
    uint64_t rsp0;       // Ring 0 stack pointer loaded on privilege level elevation
    uint64_t rsp1;       // Ring 1 stack pointer (unused in standard 2-ring model)
    uint64_t rsp2;       // Ring 2 stack pointer (unused in standard 2-ring model)
    uint64_t reserved1;
    uint64_t ist[7];     // Interrupt Stack Table pointers (IST1..IST7)
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base; // Offset to I/O permission bitmap (>= sizeof(TSS) disables bitmap)
} __attribute__((packed));

static_assert(sizeof(TaskStateSegment) == 104, "64-bit TSS must be exactly 104 bytes");

struct SwitchConfig {
    uint64_t initial_rsp0; // Initial kernel stack top before first task switch
    bool load_tss;         // Execute LTR with kTssSel during init if true
};

enum class Status : uint8_t {
    Ok = 0,
    InvalidArg,
    NotInitialized,
    InvalidSelector
};

// Initialize 64-bit TSS state and optionally load TR register
Status init_task_switch(const SwitchConfig& config);
bool is_task_switch_initialized();

// Update TSS.RSP0 to the active task's dedicated kernel stack top
void set_kernel_stack_top(uint64_t rsp0);
uint64_t kernel_stack_top();
const TaskStateSegment& tss();

// Switch address space (CR3), update TSS.RSP0, and transfer CPU context from prev to next
Status switch_process(tristos::proc::ProcessTable* table,
                      tristos::proc::Pcb* prev,
                      tristos::proc::Pcb* next);

// Launch a Ring 3 process using its prepared CpuContext (CS/SS/RIP/RSP/RFLAGS)
[[noreturn]] Status launch_user_process(tristos::proc::ProcessTable* table,
                                        tristos::proc::Pcb* proc);

// Low-level assembly helpers (implemented in task_switch_asm.asm or inline/extern linkage)
extern "C" void context_switch_asm(tristos::proc::CpuContext* prev_ctx,
                                   const tristos::proc::CpuContext* next_ctx);

[[noreturn]] extern "C" void enter_user_mode_iretq(uint64_t user_rip,
                                                   uint64_t user_rsp,
                                                   uint64_t user_cs,
                                                   uint64_t user_ss,
                                                   uint64_t rflags);

const char* status_to_string(Status status);

} // namespace tristos::arch::x86_64
