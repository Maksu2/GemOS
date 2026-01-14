[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET equ 0x10000
VESA_INFO_ADDR equ 0x9000
MODE_INFO_ADDR equ 0x9200

start:
    mov [boot_drive], dl    ; Save boot drive

    ; Print 'L' (Loading)
    mov ah, 0x0E
    mov al, 'L'
    int 0x10

    mov bp, 0x7C00
    mov sp, bp

    ; --- Check LBA Extensions ---
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc use_chs_fallback
    
    ; Check if 0x42 supported (CX bit 0)
    test cx, 1
    jz use_chs_fallback

    ; --- LBA Read ---
    ; Print 'l'
    mov ah, 0x0E
    mov al, 'l'
    int 0x10

    mov si, lba_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    jmp kernel_loaded

use_chs_fallback:
    ; Print 'c' (CHS)
    mov ah, 0x0E
    mov al, 'c'
    int 0x10

    ; Fallback: Read only 15 sectors safe
    mov bx, KERNEL_OFFSET
    mov dh, 0
    mov dl, [boot_drive]
    mov ch, 0
    mov cl, 2
    mov al, 15
    mov ah, 0x02
    int 0x13
    jc disk_error

kernel_loaded:
    ; Print 'K' (Kernel Loaded)
    mov ah, 0x0E
    mov al, 'K'
    int 0x10

    ; --- VESA ---
    ; Print 'V' (VESA)
    ; mov ah, 0x0E
    ; mov al, 'V'
    ; int 0x10

    mov di, VESA_INFO_ADDR
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne vesa_error

    ; Get Video Mode List Pointer
    mov ax, [VESA_INFO_ADDR + 14] ; Offset
    mov word [mode_offset], ax
    mov ax, [VESA_INFO_ADDR + 16] ; Segment
    mov word [mode_segment], ax

    ; Loop through modes
    mov fs, word [mode_segment]
    mov si, word [mode_offset]

find_mode_loop:
    mov cx, [fs:si]       ; Get mode number
    add si, 2             ; Next mode in list
    cmp cx, 0xFFFF        ; End of list?
    je use_fallback       ; If no suitable mode found, try fallback

    ; Check Mode Info
    push es
    push si
    mov di, MODE_INFO_ADDR
    mov ax, 0x4F01
    int 0x10
    pop si
    pop es
    
    cmp ax, 0x004F
    jne find_mode_loop

    ; Logic checks...
    mov ax, [MODE_INFO_ADDR]
    and ax, 0x0080
    jz find_mode_loop
    
    mov al, [MODE_INFO_ADDR + 25]
    cmp al, 32
    jne find_mode_loop
    
    mov ax, [MODE_INFO_ADDR + 18]
    cmp ax, 800
    jl find_mode_loop

    ; Found
    mov bx, cx
    or bx, 0x4000
    jmp set_mode

use_fallback:
    ; Print 'F' (Fallback)
    mov ah, 0x0E
    mov al, 'F'
    int 0x10
    mov bx, 0x118 
    or bx, 0x4000

set_mode:
    ; Print 'M' (Mode Set)
    mov ah, 0x0E
    mov al, 'M'
    int 0x10
    
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne vesa_error

    ; Store Info for Kernel
    ; We are at 0x9200 now.
    ; Copy relevant values to a KNOWN location for Kernel to pick up.
    ; Let's assume Kernel looks at 0x9000 for specific stash, OR we just point Kernel to 0x9200?
    ; Original code copied specific fields to 0x9000.
    ; But now VESA_INFO is at 0x9000. Collision.
    ; Let's put the Kernel Stash at 0x8000 (safe now that kernel loaded there? No, kernel is at 0x1000..0x8000+).
    ; Usage:
    ; Kernel: 0x1000 (up to 30KB -> 0x8800).
    ; Stash: 0x9000 (VESA INFO). 0x9200 (MODE INFO).
    ; We can leave them there and tell `video.c` to look at 0x9228 for PhysBase.
    
    ; FOR COMPATIBILITY with my `video.c`, I should copy the critical values to where `video.c` expects them.
    ; Where does `video.c` look? 
    ; Let's check `video.c` later or assume it looks at hardcoded address.
    ; Previously it copied to 0x9000. Now 0x9000 is occupied by VESA INFO block.
    ; I will copy the values to **0xA000** and update `video.c` if needed, OR stick to copying to **0x9000** AFTER we are done with VESA INFO block (we only need Mode Info now).
    ; Since VESA INFO (list of modes) is not needed after mode set, we can overwrite 0x9000!
    
    mov eax, [MODE_INFO_ADDR + 40] ; PhysBasePtr
    mov [0x9000], eax              
    
    mov ax, [MODE_INFO_ADDR + 18]  ; Width
    mov [0x9004], ax
    
    mov ax, [MODE_INFO_ADDR + 20]  ; Height
    mov [0x9006], ax
    
    mov al, [MODE_INFO_ADDR + 25]  ; BPP
    mov [0x9008], al
    
    mov ax, [MODE_INFO_ADDR + 16]  ; Pitch
    mov [0x9009], ax

    ; --- Enable A20 Line ---
    in al, 0x92
    or al, 2
    out 0x92, al

    ; --- Switch to Protected Mode ---
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp CODE_SEG:init_pm

disk_error:
    mov ah, 0x0E
    mov al, 'D' ; Error D
    int 0x10
    jmp $

vesa_error:
    mov ah, 0x0E
    mov al, 'E' ; Error E (Video)
    int 0x10
    jmp $

[BITS 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, 0x90000        ; Stack at top of free memory
    mov esp, ebp

    call KERNEL_OFFSET
    jmp $

; Variables
boot_drive db 0
mode_offset dw 0
mode_segment dw 0

align 4
lba_packet:
    db 0x10         ; Size
    db 0            ; Res
    dw 100          ; Count (100 sectors = 50KB)
    dw 0x0000       ; Offset (0)
    dw 0x1000       ; Segment (0x1000) -> 0x10000 Physical
    dq 1            ; LBA Start

; GDT
gdt_start:
    dq 0x0

gdt_code:
    dw 0xFFFF    ; Limit
    dw 0x0       ; Base (low)
    db 0x0       ; Base (middle)
    db 10011010b ; Access (exec, read)
    db 11001111b ; Granularity
    db 0x0       ; Base (high)

gdt_data:
    dw 0xFFFF
    dw 0x0
    db 0x0
    db 10010010b ; Access (read, write)
    db 11001111b
    db 0x0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; Padding to 512 bytes
times 510-($-$$) db 0
dw 0xaa55
