; =============================================================================
; GemOS Stage 2 Bootloader
; =============================================================================
;
; Memory Layout (Real Mode):
;   0x0000:0x1000 - Kernel Load Buffer (Temporary, 64KB)
;   0x0000:0x7E00 - Stage 2 Code (16KB)
;   0x0000:0x9000 - Boot Info Structure (VBE Info)
;   0x0000:0xBE00 - End of Stage 2
;
; Memory Layout (Protected Mode):
;   0x00100000    - Kernel Code (1MB)
;   0x00090000    - Kernel Stack (Top, grows down)
; =============================================================================

[BITS 16]
[ORG 0x7E00]

; -----------------------------------------------------------------------------
; Constants
; -----------------------------------------------------------------------------
STAGE2_SECTORS      equ 32          ; Size of Stage 2 in sectors (must match Stage 1)
KERNEL_LOAD_SEG     equ 0x1000      ; Segment to load kernel (temporary)
KERNEL_LOAD_OFF     equ 0x0000      ; Offset within segment
KERNEL_SECTORS      equ 1120        ; Sectors to load (~560KB, stays below VGA memory)
KERNEL_START_SECTOR equ 33          ; Kernel starts after Stage 1 (1) + Stage 2 (32)

VBE_MODE            equ 0x4115      ; 800x600x32bpp (safer choice)
; Alternative modes:
; 0x4112 = 640x480x32bpp
; 0x4115 = 800x600x32bpp  
; 0x4118 = 1024x768x32bpp

PROTECTED_MODE_BASE equ 0x100000    ; 1MB - where kernel will be in PM

; -----------------------------------------------------------------------------
; Stage 2 Entry Point
; -----------------------------------------------------------------------------
stage2_start:
    ; Save boot drive number (passed in DL from Stage 1)
    mov [boot_drive_saved], dl
    
    ; Print welcome message
    mov si, msg_stage2
    call print_string
    
    ; Step 1: Enable A20 line
    call enable_a20
    mov si, msg_a20_ok
    call print_string
    
    ; Step 2: Get memory map
    call get_memory_map
    mov si, msg_memmap_ok
    call print_string

    ; Step 3: Load kernel to temporary location
    call load_kernel
    mov si, msg_kernel_ok
    call print_string
    
    ; Step 4: Set up VBE graphics mode
    xor ax, ax
    mov es, ax                  ; Reset ES for VBE calls (ES:DI)
    call setup_vbe
    mov si, msg_vbe_ok
    call print_string
    
    ; Step 5: Switch to Protected Mode
    cli                         ; Disable interrupts
    lgdt [gdt_descriptor]       ; Load GDT
    
    ; Enable Protected Mode (set PE bit in CR0)
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Far jump to flush pipeline and enter 32-bit code
    jmp 0x08:protected_mode_entry

; =============================================================================
; A20 Line Enable
; =============================================================================
enable_a20:
    ; Try keyboard controller method
    call a20_keyboard
    call check_a20
    jnz .done
    
    ; Try Fast A20 (port 0x92)
    call a20_fast
    call check_a20
    jnz .done
    
    ; Try BIOS method
    call a20_bios
    call check_a20
    jnz .done
    
    ; A20 failed
    mov si, msg_a20_fail
    call print_string
    jmp halt

.done:
    ret

; Keyboard controller method
a20_keyboard:
    call a20_wait_input
    mov al, 0xAD                ; Disable keyboard
    out 0x64, al
    
    call a20_wait_input
    mov al, 0xD0                ; Read output port
    out 0x64, al
    
    call a20_wait_output
    in al, 0x60
    push ax
    
    call a20_wait_input
    mov al, 0xD1                ; Write output port
    out 0x64, al
    
    call a20_wait_input
    pop ax
    or al, 2                    ; Set A20 bit
    out 0x60, al
    
    call a20_wait_input
    mov al, 0xAE                ; Enable keyboard
    out 0x64, al
    
    call a20_wait_input
    ret

a20_wait_input:
    in al, 0x64
    test al, 2
    jnz a20_wait_input
    ret

a20_wait_output:
    in al, 0x64
    test al, 1
    jz a20_wait_output
    ret

; Fast A20 method
a20_fast:
    in al, 0x92
    test al, 2
    jnz .done
    or al, 2
    and al, 0xFE                ; Make sure we don't reset
    out 0x92, al
.done:
    ret

; BIOS method
a20_bios:
    mov ax, 0x2401
    int 0x15
    ret

; Check if A20 is enabled
check_a20:
    pushf
    push ds
    push es
    push di
    push si
    
    xor ax, ax
    mov es, ax
    mov di, 0x0500
    
    mov ax, 0xFFFF
    mov ds, ax
    mov si, 0x0510
    
    mov al, [es:di]
    push ax
    mov al, [ds:si]
    push ax
    
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
    
    cmp byte [es:di], 0xFF
    
    pop ax
    mov [ds:si], al
    pop ax
    mov [es:di], al
    
    pop si
    pop di
    pop es
    pop ds
    popf
    
    ; Return NZ if A20 enabled (different memory locations)
    jne .enabled
    xor ax, ax                  ; ZF=1, A20 disabled
    ret
.enabled:
    mov ax, 1                   ; ZF=0, A20 enabled
    or ax, ax
    ret

; =============================================================================
; Memory Map (E820)
; =============================================================================
get_memory_map:
    mov di, memory_map          ; Destination buffer
    xor ebx, ebx                ; Continuation value
    mov edx, 0x534D4150         ; 'SMAP' signature
    
.loop:
    mov eax, 0xE820             ; Function number
    mov ecx, 24                 ; Buffer size
    int 0x15
    
    jc .done                    ; Error or end
    cmp eax, 0x534D4150         ; Verify signature
    jne .done
    
    add di, 24                  ; Next entry
    inc byte [memory_map_count]
    
    test ebx, ebx               ; Continue if ebx != 0
    jnz .loop
    
.done:
    ret

; =============================================================================
; VBE Graphics Mode Setup
; =============================================================================
setup_vbe:
    ; Get VBE controller info
    mov ax, 0x4F00
    mov di, vbe_info
    int 0x10
    
    cmp ax, 0x004F
    jne .vbe_error
    
    ; Get pointer to mode list
    ; vbe_info + 14 is segment, + 16 is offset? No.
    ; Offset 14: DWORD VideoModePtr (Far Pointer: Offset:Segment)
    mov ax, [vbe_info + 16]     ; Segment
    mov es, ax
    mov di, [vbe_info + 14]     ; Offset
    
.mode_loop:
    mov cx, [es:di]             ; Get mode number
    cmp cx, 0xFFFF              ; End of list?
    je .no_mode_found
    add di, 2                   ; Next entry
    
    ; Get mode info
    push es
    push di
    
    ; Reset ES to our segment for buffer
    push ax
    xor ax, ax
    mov es, ax
    pop ax
    
    mov ax, 0x4F01
    mov di, vbe_mode_info
    int 0x10
    
    cmp ax, 0x004F
    jne .next_mode
    
    ; Check properties
    ; Offset 0: ModeAttributes
    ; Bit 7 = Linear Frame Buffer
    mov ax, [vbe_mode_info]
    test ax, 0x0080
    jz .next_mode
    
    ; Offset 18: XResolution
    mov ax, [vbe_mode_info + 18]
    cmp ax, 1920
    jne .next_mode
    
    ; Offset 20: YResolution
    mov ax, [vbe_mode_info + 20]
    cmp ax, 1080
    jne .next_mode
    
    ; Offset 25: BitsPerPixel
    mov al, [vbe_mode_info + 25]
    cmp al, 32
    jne .next_mode
    
    ; FOUND IT!
    ; CX contains mode number
    pop di
    pop es
    
    ; Set Mode (CX) | LFB (0x4000)
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    
    cmp ax, 0x004F
    jne .vbe_error
    
    ret
    
.next_mode:
    pop di
    pop es
    jmp .mode_loop
    
.no_mode_found:
    ; Fallback to safe mode (800x600) if 1080p fails?
    ; For now, just error out as requested to enforce 1080p
    ; User said: "Docelowa rozdzielczość: 1920x1080"
    jmp .vbe_error

.vbe_error:
    mov si, msg_vbe_fail
    call print_string
    jmp halt

; =============================================================================
; Load Kernel
; =============================================================================
load_kernel:
    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    mov bx, KERNEL_LOAD_OFF
    
    mov cx, KERNEL_SECTORS
    mov ax, KERNEL_START_SECTOR     ; Start LBA

.read_loop:
    push cx             ; Save loop counter
    push ax             ; Save current LBA
    
    mov dl, [boot_drive_saved]  ; Restore correct drive number
    call lba_to_chs     ; Converts AX (LBA) -> CHS
    
    mov ah, 0x02        ; Read sectors
    mov al, 1           ; Read 1 sector
    int 0x13
    jc .disk_read_fail  ; Jump to error handler
    
    pop ax              ; Restore current LBA
    inc ax              ; Next LBA
    
    mov dx, es
    add dx, 32          ; Advance ES by 512 bytes (32 paragraphs)
    mov es, dx
    
    pop cx              ; Restore loop counter
    loop .read_loop
    
    ret

.disk_read_fail:
    jmp disk_error      ; Jump to global error handler

; Convert LBA (AX) to CHS
; Output: CH=Cylinder, DH=Head, CL=Sector, DL=Drive (preserved)
lba_to_chs:
    push bx
    push dx             ; Save original DX (contains Drive in DL)
    
    xor dx, dx
    mov bx, 18          ; 18 sectors per track
    div bx              ; AX = LBA / 18, DX = LBA % 18
    
    inc dx              ; Sector = (LBA % 18) + 1
    mov cl, dl          ; CL = Sector
    
    xor dx, dx
    mov bx, 2           ; 2 heads
    div bx              ; AX = Cyl, DX = Head
    
    mov ch, al          ; CH = Cylinder
    mov dh, dl          ; DH = Head
    
    pop bx              ; Restore original DX into BX
    mov dl, bl          ; Restore Drive Number only
    
    pop bx
    ret

disk_error:
    mov si, msg_kernel_fail
    call print_string
    jmp halt

; =============================================================================
; Print String (Real Mode)
; =============================================================================
print_string:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp .loop
.done:
    popa
    ret

; =============================================================================
; Halt
; =============================================================================
halt:
    cli
    hlt
    jmp halt

; =============================================================================
; GDT (Global Descriptor Table)
; =============================================================================
gdt_start:

gdt_null:                       ; Null descriptor (required)
    dq 0

gdt_code:                       ; Code segment descriptor
    dw 0xFFFF                   ; Limit (bits 0-15)
    dw 0                        ; Base (bits 0-15)
    db 0                        ; Base (bits 16-23)
    db 10011010b                ; Access: present, ring 0, code, readable
    db 11001111b                ; Flags: 4KB granularity, 32-bit
    db 0                        ; Base (bits 24-31)

gdt_data:                       ; Data segment descriptor
    dw 0xFFFF
    dw 0
    db 0
    db 10010010b                ; Access: present, ring 0, data, writable
    db 11001111b
    db 0

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Size
    dd gdt_start                ; Address

; Segment selectors
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; =============================================================================
; Protected Mode Entry (32-bit)
; =============================================================================
[BITS 32]
protected_mode_entry:
    ; Set up segment registers for Protected Mode
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Copy kernel from temporary location to 1MB
    mov esi, KERNEL_LOAD_SEG * 16 + KERNEL_LOAD_OFF
    mov edi, PROTECTED_MODE_BASE
    mov ecx, KERNEL_SECTORS * 512 / 4   ; Copy dwords
    rep movsd

    ; Set up the protected-mode stack after the copy so the temporary kernel
    ; image can use the full low-memory staging window.
    mov esp, 0x9F000
    
    ; Prepare boot info structure for kernel
    ; Store VBE info at a known location
    mov esi, vbe_mode_info
    mov edi, 0x9000             ; Boot info location
    mov ecx, 256 / 4
    rep movsd
    
    ; Jump to kernel!
    jmp PROTECTED_MODE_BASE

; =============================================================================
; Data Section
; =============================================================================
[BITS 16]

boot_drive_saved:   db 0        ; Will be set by Stage 1 before jump

msg_stage2:         db 'GemOS Stage 2', 0x0D, 0x0A, 0
msg_a20_ok:         db '  A20 enabled', 0x0D, 0x0A, 0
msg_a20_fail:       db '  A20 FAILED!', 0x0D, 0x0A, 0
msg_memmap_ok:      db '  Memory map OK', 0x0D, 0x0A, 0
msg_vbe_ok:         db '  VBE mode set', 0x0D, 0x0A, 0
msg_vbe_fail:       db '  VBE FAILED!', 0x0D, 0x0A, 0
msg_kernel_ok:      db '  Kernel loaded', 0x0D, 0x0A, 0
msg_kernel_fail:    db '  Kernel FAILED!', 0x0D, 0x0A, 0

memory_map_count:   db 0

; Align to 16 bytes for VBE structures
align 16
vbe_info:           times 512 db 0
vbe_mode_info:      times 256 db 0
memory_map:         times 24*32 db 0    ; Space for 32 entries

; Pad Stage 2 to fill allocated sectors
times (STAGE2_SECTORS * 512) - ($ - $$) db 0
