bits 64
default rel

section .text

global context_switch_asm
global enter_user_mode_iretq

; void context_switch_asm(tristos::proc::CpuContext* prev_ctx,
;                         const tristos::proc::CpuContext* next_ctx)
;   rdi = prev_ctx (may be NULL)
;   rsi = next_ctx (must be non-NULL)
context_switch_asm:
    test rsi, rsi
    jz .done

    test rdi, rdi
    jz .restore_next

    ; Save general-purpose registers into prev_ctx
    mov [rdi + 0x00], r15
    mov [rdi + 0x08], r14
    mov [rdi + 0x10], r13
    mov [rdi + 0x18], r12
    mov [rdi + 0x20], rbp
    mov [rdi + 0x28], rbx
    mov [rdi + 0x30], r11
    mov [rdi + 0x38], r10
    mov [rdi + 0x40], r9
    mov [rdi + 0x50], r8
    mov [rdi + 0x58], rax
    mov [rdi + 0x60], rcx
    mov [rdi + 0x68], rdx
    ; Save original incoming rsi and rdi values
    mov rax, rsi
    mov [rdi + 0x70], rax
    mov rax, rdi
    mov [rdi + 0x78], rax

    ; Save return RIP (caller's return address on stack) and caller RSP (post-ret)
    mov rax, [rsp]
    mov [rdi + 0x80], rax        ; rip
    lea rax, [rsp + 8]
    mov [rdi + 0x98], rax        ; rsp

    ; Save CS, SS, and RFLAGS
    xor rax, rax
    mov ax, cs
    mov [rdi + 0x88], rax        ; cs
    mov ax, ss
    mov [rdi + 0xA0], rax        ; ss
    pushfq
    pop rax
    mov [rdi + 0x90], rax        ; rflags

.restore_next:
    ; Load next_ctx general-purpose registers except rsi/rdi/rax/rsp/rip
    mov r15, [rsi + 0x00]
    mov r14, [rsi + 0x08]
    mov r13, [rsi + 0x10]
    mov r12, [rsi + 0x18]
    mov rbp, [rsi + 0x20]
    mov rbx, [rsi + 0x28]
    mov r11, [rsi + 0x30]
    mov r10, [rsi + 0x38]
    mov r9,  [rsi + 0x40]
    mov r8,  [rsi + 0x50]
    mov rcx, [rsi + 0x60]
    mov rdx, [rsi + 0x68]

    ; Switch to target stack before pushing resume RIP and RFLAGS
    mov rsp, [rsi + 0x98]
    push qword [rsi + 0x80]      ; resume rip
    push qword [rsi + 0x90]      ; rflags

    ; Restore rax, rsi, rdi last
    mov rax, [rsi + 0x58]
    mov rdi, [rsi + 0x78]
    mov rsi, [rsi + 0x70]

    popfq
    ret

.done:
    ret


; [[noreturn]] void enter_user_mode_iretq(uint64_t user_rip,
;                                         uint64_t user_rsp,
;                                         uint64_t user_cs,
;                                         uint64_t user_ss,
;                                         uint64_t rflags)
;   rdi = user_rip
;   rsi = user_rsp
;   rdx = user_cs
;   rcx = user_ss
;   r8  = rflags
enter_user_mode_iretq:
    cli

    ; Load Ring 3 data segment into DS/ES; clear FS/GS base selectors for clean entry
    mov ax, cx
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; Build 5-qword x86-64 iretq frame (SS, RSP, RFLAGS, CS, RIP)
    push rcx                     ; SS (user data selector, RPL=3)
    push rsi                     ; RSP (user stack pointer)
    push r8                      ; RFLAGS (ensure IF=1 / bit 1=1 set by caller)
    push rdx                     ; CS (user code selector, RPL=3)
    push rdi                     ; RIP (user entry point)

    ; Zero general-purpose registers so kernel pointers/values do not leak to Ring 3
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8,  r8
    xor r9,  r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    iretq
