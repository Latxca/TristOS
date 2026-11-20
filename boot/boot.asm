bits 16
org 0x7c00

STAGE2_SEGMENT equ 0x0800
STAGE2_SECTORS equ 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov [boot_drive], dl

    mov si, loading_message
    call print

    mov si, stage2_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc disk_error

    jmp STAGE2_SEGMENT:0x0000

disk_error:
    mov si, error_message
    call print
    cli
.hang:
    hlt
    jmp .hang

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

loading_message db 'TristOS bootloader: loading recovery manager...', 13, 10, 0
error_message db 'Disk read failed.', 13, 10, 0
boot_drive db 0

stage2_packet:
    db 0x10, 0
    dw STAGE2_SECTORS
    dw 0x0000
    dw STAGE2_SEGMENT
    dq 1

times 510-($-$$) db 0
dw 0xaa55
