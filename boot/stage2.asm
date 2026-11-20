bits 16
org 0x8000

KERNEL_LOAD_SEGMENT equ 0x1000
KERNEL_SECTORS equ 128
KERNEL_START_SECTOR equ 17

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov [boot_drive], dl

    call clear_screen
    mov si, title
    call print
    mov si, menu
    call print

.wait_key:
    xor ah, ah
    int 0x16
    cmp al, '1'
    je boot_kernel
    cmp al, '2'
    je diagnostics
    cmp al, '3'
    je halt_system
    jmp .wait_key

diagnostics:
    mov si, diagnostics_message
    call print
    mov si, diagnostics_ok
    call print
    mov si, press_key
    call print
    xor ah, ah
    int 0x16
    call clear_screen
    mov si, title
    call print
    mov si, menu
    call print
    jmp .wait_key

boot_kernel:
    mov si, loading_kernel
    call print
    mov si, kernel_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error

    call enter_long_mode

halt_system:
    mov si, halted_message
    call print
    cli
.hang:
    hlt
    jmp .hang

disk_error:
    mov si, disk_error_message
    call print
    jmp halt_system

clear_screen:
    mov ax, 0x0003
    int 0x10
    ret

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp print
.done:
    ret

enter_long_mode:
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    and eax, 0x7fffffff
    mov cr0, eax

    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, (KERNEL_SECTORS * 512) / 4
    rep movsd

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x08:protected_start

    bits 32
    protected_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov edi, 0xa000
    xor eax, eax
    mov ecx, 0x3000 / 4
    rep stosd
    mov dword [0xa000], 0xb003
    mov dword [0xb000], 0xc003
    mov dword [0xc000], 0x0083
    mov eax, 0xa000
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    jmp 0x18:long_mode_entry

    bits 64
    long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x90000
    jmp 0x100000

bits 16
boot_drive db 0

title db 13, 10, 'TristOS x64 Recovery Manager', 13, 10, 0
menu db 13, 10, '[1] Start TristOS', 13, 10, '[2] Run diagnostics', 13, 10, '[3] Halt system', 13, 10, '> ', 0
diagnostics_message db 13, 10, 'Running memory and disk diagnostics...', 13, 10, 0
diagnostics_ok db 'Diagnostics complete: boot media is readable.', 13, 10, 0
press_key db 'Press any key to return to the menu.', 13, 10, 0
loading_kernel db 13, 10, 'Loading TristOS kernel...', 13, 10, 0
disk_error_message db 13, 10, 'Kernel read failed.', 13, 10, 0
halted_message db 13, 10, 'System halted.', 13, 10, 0

align 8
kernel_packet:
    db 0x10, 0
    dw KERNEL_SECTORS
    dw 0x0000
    dw KERNEL_LOAD_SEGMENT
    dq KERNEL_START_SECTOR

gdt_start:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
    dq 0x00af9a000000ffff
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

    times 8192-($-$$) db 0
