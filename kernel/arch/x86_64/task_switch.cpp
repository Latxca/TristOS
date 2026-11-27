#include "task_switch.hpp"
#include "../../vm.hpp"

namespace tristos::arch::x86_64 {
namespace {

TaskStateSegment g_tss{};
bool g_initialized = false;

void zero_bytes(void* dst, size_t count) {
    auto* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < count; ++i) {
        d[i] = 0;
    }
}

void load_tr_register(uint16_t selector) {
    asm volatile("ltr %0" : : "r"(selector) : "memory");
}

void write_tss_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
    asm volatile("" : : "r"(&g_tss.rsp0) : "memory");
}

bool is_ring3_context(const tristos::proc::CpuContext& ctx) {
    return ((ctx.cs & 0x3ULL) == 3ULL) || ((ctx.ss & 0x3ULL) == 3ULL);
}

[[noreturn]] void halt_forever() {
    for (;;) {
        asm volatile("cli; hlt" : : : "memory");
    }
}

} // namespace

Status init_task_switch(const SwitchConfig& config) {
    zero_bytes(&g_tss, sizeof(g_tss));
    g_tss.rsp0 = config.initial_rsp0;
    g_tss.iomap_base = static_cast<uint16_t>(sizeof(TaskStateSegment));

    if (config.load_tss) {
        load_tr_register(kTssSel);
    }

    g_initialized = true;
    return Status::Ok;
}

bool is_task_switch_initialized() {
    return g_initialized;
}

void set_kernel_stack_top(uint64_t rsp0) {
    write_tss_rsp0(rsp0);
}

uint64_t kernel_stack_top() {
    return g_tss.rsp0;
}

const TaskStateSegment& tss() {
    return g_tss;
}

Status switch_process(tristos::proc::ProcessTable* table,
                      tristos::proc::Pcb* prev,
                      tristos::proc::Pcb* next) {
    if (!g_initialized) {
        return Status::NotInitialized;
    }
    if (next == nullptr || next->state == tristos::proc::State::Unused) {
        return Status::InvalidArg;
    }

    if (prev != nullptr && prev != next && prev->state == tristos::proc::State::Running) {
        prev->state = tristos::proc::State::Ready;
    }

    if (next->kernel_stack_top != 0) {
        set_kernel_stack_top(next->kernel_stack_top);
    }

    if (next->addr_space.cr3_phys != 0 && tristos::vm::is_initialized()) {
        tristos::vm::activate_address_space(next->addr_space.cr3_phys);
    }

    next->state = tristos::proc::State::Running;
    if (table != nullptr) {
        table->set_current(next);
    }

    tristos::proc::CpuContext* prev_ctx = (prev != nullptr) ? &prev->context : nullptr;
    context_switch_asm(prev_ctx, &next->context);

    // If next is a fresh Ring 3 process with a user CS/SS frame, enter user mode via iretq
    if (next->privilege == tristos::proc::Privilege::User || is_ring3_context(next->context)) {
        const uint64_t user_cs =
            ((next->context.cs & 0x3ULL) == 3ULL) ? next->context.cs : kUserCodeSel;
        const uint64_t user_ss =
            ((next->context.ss & 0x3ULL) == 3ULL) ? next->context.ss : kUserDataSel;
        const uint64_t rflags =
            (next->context.rflags != 0) ? next->context.rflags : kDefaultUserRflags;

        enter_user_mode_iretq(next->context.rip,
                              next->context.rsp,
                              user_cs,
                              user_ss,
                              rflags);
    }

    return Status::Ok;
}

[[noreturn]] Status launch_user_process(tristos::proc::ProcessTable* table,
                                        tristos::proc::Pcb* proc) {
    if (proc == nullptr || proc->state == tristos::proc::State::Unused) {
        halt_forever();
    }

    if (!g_initialized) {
        SwitchConfig cfg{};
        cfg.initial_rsp0 = proc->kernel_stack_top;
        cfg.load_tss = false;
        init_task_switch(cfg);
    } else if (proc->kernel_stack_top != 0) {
        set_kernel_stack_top(proc->kernel_stack_top);
    }

    if (proc->addr_space.cr3_phys != 0 && tristos::vm::is_initialized()) {
        tristos::vm::activate_address_space(proc->addr_space.cr3_phys);
    }

    proc->state = tristos::proc::State::Running;
    if (table != nullptr) {
        table->set_current(proc);
    }

    const uint64_t user_cs =
        ((proc->context.cs & 0x3ULL) == 3ULL) ? proc->context.cs : kUserCodeSel;
    const uint64_t user_ss =
        ((proc->context.ss & 0x3ULL) == 3ULL) ? proc->context.ss : kUserDataSel;
    const uint64_t rflags =
        (proc->context.rflags != 0) ? proc->context.rflags : kDefaultUserRflags;

    enter_user_mode_iretq(proc->context.rip,
                          proc->context.rsp,
                          user_cs,
                          user_ss,
                          rflags);
}

const char* status_to_string(Status status) {
    switch (status) {
        case Status::Ok:              return "Ok";
        case Status::InvalidArg:      return "InvalidArg";
        case Status::NotInitialized:  return "NotInitialized";
        case Status::InvalidSelector: return "InvalidSelector";
        default:                      return "UnknownStatus";
    }
}

} // namespace tristos::arch::x86_64
