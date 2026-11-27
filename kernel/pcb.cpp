#include "pcb.hpp"
#include "texe.hpp"

namespace tristos::proc {
namespace {

inline constexpr uint64_t kDefaultUserStackSize = 16384; // 16 KiB
inline constexpr uint64_t kDefaultUserStackTop = 0x00007FFFFFFFE000ULL;
inline constexpr uint64_t kDefaultUserCodeBase = 0x0000000000400000ULL;
inline constexpr uint64_t kKernelCodeSelector = 0x08ULL;
inline constexpr uint64_t kKernelDataSelector = 0x10ULL;
inline constexpr uint64_t kUserCodeSelector = 0x1BULL; // Ring 3 code segment (RPL=3)
inline constexpr uint64_t kUserDataSelector = 0x23ULL; // Ring 3 data/stack segment (RPL=3)
inline constexpr uint64_t kDefaultRflags = 0x202ULL;   // Interrupts enabled (IF)

void zero_bytes(void* dst, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        d[i] = 0;
    }
}

void copy_name(char* dst, size_t dst_cap, const char* src) {
    if (dst == nullptr || dst_cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dst_cap && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

uint64_t align_down_16(uint64_t value) {
    return value & ~0xFULL;
}

void init_context_for_privilege(CpuContext* ctx,
                                Privilege privilege,
                                uint64_t entry_va,
                                uint64_t stack_top) {
    if (ctx == nullptr) {
        return;
    }
    zero_bytes(ctx, sizeof(*ctx));
    ctx->rip = entry_va;
    ctx->rsp = align_down_16(stack_top);
    ctx->rbp = ctx->rsp;
    ctx->rflags = kDefaultRflags;

    if (privilege == Privilege::User) {
        ctx->cs = kUserCodeSelector;
        ctx->ss = kUserDataSelector;
    } else {
        ctx->cs = kKernelCodeSelector;
        ctx->ss = kKernelDataSelector;
    }
}

uint8_t default_priority_for(Privilege privilege, bool system_sealed) {
    if (privilege == Privilege::Kernel) {
        return 10;
    }
    if (privilege == Privilege::SystemService || system_sealed) {
        return 20;
    }
    return 40;
}

} // namespace

void ProcessTable::init() {
    for (size_t i = 0; i < kMaxProcesses; ++i) {
        zero_bytes(&entries_[i], sizeof(entries_[i]));
        entries_[i].pid = kInvalidPid;
        entries_[i].state = State::Unused;
        entries_[i].kernel_stack_top =
            reinterpret_cast<uint64_t>(&entries_[i].kernel_stack[kKernelStackBytes]);
    }
    current_ = nullptr;
    next_pid_ = 1;
}

Pcb* ProcessTable::create_from_image(uint32_t ppid,
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
                                     uint64_t cr3_phys) {
    if (entry_va == 0) {
        return nullptr;
    }

    for (size_t i = 0; i < kMaxProcesses; ++i) {
        Pcb& pcb = entries_[i];
        if (pcb.state == State::Unused) {
            zero_bytes(&pcb, sizeof(pcb));
            pcb.pid = next_pid_++;
            if (next_pid_ == kInvalidPid) {
                next_pid_ = 1;
            }
            pcb.ppid = ppid;
            pcb.uid = uid;
            pcb.gid = gid;
            pcb.exit_code = 0;
            pcb.state = State::Ready;
            pcb.privilege = privilege;
            pcb.priority = default_priority_for(privilege, system_sealed);
            pcb.system_sealed = system_sealed;
            copy_name(pcb.name, sizeof(pcb.name), (name != nullptr && name[0] != '\0') ? name : "texe_proc");

            pcb.kernel_stack_top =
                align_down_16(reinterpret_cast<uint64_t>(&pcb.kernel_stack[kKernelStackBytes]));

            const uint64_t effective_stack_size =
                (stack_size != 0) ? stack_size : kDefaultUserStackSize;
            const uint64_t effective_stack_top =
                (stack_top != 0) ? stack_top : kDefaultUserStackTop;
            const uint64_t effective_code_base =
                (code_base != 0) ? code_base : kDefaultUserCodeBase;

            pcb.addr_space.cr3_phys = cr3_phys;
            pcb.addr_space.code_base = effective_code_base;
            pcb.addr_space.code_size = code_size;
            pcb.addr_space.stack_top = effective_stack_top;
            pcb.addr_space.stack_size = effective_stack_size;
            pcb.addr_space.isolated = (cr3_phys != 0) && (privilege == Privilege::User);

            init_context_for_privilege(&pcb.context, privilege, entry_va, effective_stack_top);
            return &pcb;
        }
    }

    return nullptr;
}

Pcb* create_process_from_texe(ProcessTable* table,
                              const tristos::texe::LoadedImage& image,
                              uint32_t ppid,
                              uint32_t uid,
                              uint32_t gid,
                              uint64_t cr3_phys) {
    if (table == nullptr) {
        return nullptr;
    }

    const bool is_sealed =
        texe::has_flag(image.info.flags, texe::ImageFlags::SystemSealed) ||
        image.info.signature_verified;

    Privilege priv = Privilege::User;
    if (image.info.subsystem == texe::Subsystem::KernelService) {
        priv = Privilege::Kernel;
    } else if (image.info.subsystem == texe::Subsystem::SystemDaemon || is_sealed) {
        priv = Privilege::SystemService;
    }

    const uint64_t mapped_span =
        image.info.code_size + image.info.bss_size;
    const uint64_t stack_size =
        (image.info.stack_reserve != 0) ? image.info.stack_reserve : kDefaultUserStackSize;
    const uint64_t code_base = kDefaultUserCodeBase;
    const uint64_t entry_va =
        (image.info.entry_offset >= code_base)
            ? image.info.entry_offset
            : (code_base + image.info.entry_offset);

    return table->create_from_image(ppid,
                                    uid,
                                    gid,
                                    image.info.name,
                                    priv,
                                    is_sealed,
                                    entry_va,
                                    code_base,
                                    mapped_span,
                                    kDefaultUserStackTop,
                                    stack_size,
                                    cr3_phys);
}

Pcb* ProcessTable::find(uint32_t pid) {
    if (pid == kInvalidPid) {
        return nullptr;
    }
    for (size_t i = 0; i < kMaxProcesses; ++i) {
        if (entries_[i].state != State::Unused && entries_[i].pid == pid) {
            return &entries_[i];
        }
    }
    return nullptr;
}

const Pcb* ProcessTable::find(uint32_t pid) const {
    if (pid == kInvalidPid) {
        return nullptr;
    }
    for (size_t i = 0; i < kMaxProcesses; ++i) {
        if (entries_[i].state != State::Unused && entries_[i].pid == pid) {
            return &entries_[i];
        }
    }
    return nullptr;
}

bool ProcessTable::set_state(uint32_t pid, State next_state) {
    Pcb* pcb = find(pid);
    if (pcb == nullptr || next_state == State::Unused) {
        return false;
    }
    pcb->state = next_state;
    if (next_state == State::Running) {
        current_ = pcb;
    } else if (current_ == pcb) {
        current_ = nullptr;
    }
    return true;
}

bool ProcessTable::terminate(uint32_t pid, int32_t exit_code) {
    Pcb* pcb = find(pid);
    if (pcb == nullptr) {
        return false;
    }
    pcb->exit_code = exit_code;
    pcb->state = State::Zombie;
    if (current_ == pcb) {
        current_ = nullptr;
    }
    return true;
}

} // namespace tristos::proc
