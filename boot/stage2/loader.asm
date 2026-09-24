; =============================================================================
; GemOS Stage 2 Bootloader
; =============================================================================
;
; Memory Layout (Real Mode):
;   0x0000:0x7E00 - Stage 2 code and data, incl. the boot info block (16KB)
;   0x0000:0xBE00 - End of Stage 2
;   0x2000:0x0000 - Disk read buffer (32KB); every chunk is copied to 1 MB
;                   right away in unreal mode
;
; Memory Layout (Protected Mode):
;   0x00100000    - Kernel Code (1MB)
;   0x0009F000    - Kernel Stack (Top, grows down)
;
; The kernel starts with EBX pointing to the boot info block (boot_info
; below, boot_info_t in kernel/include/boot_info.h).
; =============================================================================

[BITS 16]
[ORG 0x7E00]

; -----------------------------------------------------------------------------
; Constants
; -----------------------------------------------------------------------------
STAGE2_SECTORS      equ 32          ; Size of Stage 2 in sectors (must match Stage 1)
KERNEL_START_SECTOR equ 33          ; Kernel starts after Stage 1 (1) + Stage 2 (32)
KERNEL_MAGIC        equ 0x4B4D4547  ; "GEMK": header in kernel/entry.S
KERNEL_MAX_SECTORS  equ 4096        ; sanity limit for the header (2 MB)
BUFFER_SEG          equ 0x2000      ; read buffer at 0x20000 (64 KB aligned,
                                    ; so no floppy DMA crosses a 64 KB line)
CHUNK_SECTORS       equ 64          ; 32 KB per read + copy
READ_RETRIES        equ 3

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
    mov [bi_boot_drive], dl
    
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
    mov di, bi_e820             ; Destination: boot info entries
    xor ebx, ebx                ; Continuation value

.loop:
    mov dword [di + 20], 1      ; ACPI 3.0 attributes: valid, if not returned
    mov eax, 0xE820             ; Function number
    mov ecx, 24                 ; Buffer size
    mov edx, 0x534D4150         ; 'SMAP' signature
    int 0x15

    jc .done                    ; Error or end
    cmp eax, 0x534D4150         ; Verify signature
    jne .done

    mov eax, [di + 8]           ; skip empty entries
    or eax, [di + 12]
    jz .next

    add di, 24                  ; Next entry
    inc dword [bi_e820_count]
    cmp dword [bi_e820_count], BOOT_INFO_E820_MAX
    je .done

.next:
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
    mov [bi_vbe_mode], cx
    mov bx, cx
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    
    cmp ax, 0x004F
    jne .vbe_error

    ; hand the mode info to the kernel (ES still points at the mode list)
    xor ax, ax
    mov es, ax
    cld
    mov si, vbe_mode_info
    mov di, bi_vbe_mode_info
    mov cx, 256
    rep movsb

    ret
    
.next_mode:
    pop di
    pop es
    jmp .mode_loop
    
.no_mode_found:
    ; 1920x1080x32 with a linear framebuffer is required; there is no
    ; fallback mode
    jmp .vbe_error

.vbe_error:
    mov si, msg_vbe_fail
    call print_string
    jmp halt

; =============================================================================
; Disk access: INT 13h extensions (LBA) if the BIOS has them for the boot
; drive (hard disks), CHS with the geometry from AH=08h otherwise (floppies)
; =============================================================================
disk_init:
    mov byte [disk_use_lba], 0
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive_saved]
    int 0x13
    jc .chs
    cmp bx, 0xAA55
    jne .chs
    test cx, 1                  ; fixed disk access subset (AH=42h)
    jz .chs
    mov byte [disk_use_lba], 1
    ret
.chs:
    push es
    mov ah, 0x08
    mov dl, [boot_drive_saved]
    xor di, di                  ; ES:DI = 0 works around some BIOSes
    mov es, di
    int 0x13
    pop es
    jc disk_error
    and cx, 0x3F                ; CL[5:0] = sectors per track
    jz disk_error
    mov [disk_spt], cx
    mov dl, dh                  ; DH = last head
    xor dh, dh
    inc dx
    mov [disk_heads], dx
    ret

; Read CX sectors (1..CHUNK_SECTORS) starting at LBA EAX into BUFFER_SEG:0.
read_sectors:
    cmp byte [disk_use_lba], 0
    je .chs
    mov [dap_count], cx
    mov word [dap_offset], 0
    mov word [dap_segment], BUFFER_SEG
    mov [dap_lba], eax
    mov dword [dap_lba + 4], 0
    mov di, READ_RETRIES
.lba_retry:
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive_saved]
    int 0x13
    jnc .done
    call reset_disk
    dec di
    jnz .lba_retry
    jmp disk_error
.chs:
    push es
    mov bx, BUFFER_SEG
    mov es, bx
    xor bx, bx
.chs_next:
    push eax
    push cx
    call read_chs_sector
    pop cx
    pop eax
    inc eax
    add bx, 512
    loop .chs_next
    pop es
.done:
    ret

; Read LBA AX (below 65536) into ES:BX with CHS.
read_chs_sector:
    xor dx, dx
    div word [disk_spt]         ; AX = track, DX = sector - 1
    inc dx
    mov cl, dl                  ; CL[5:0] = sector
    xor dx, dx
    div word [disk_heads]       ; AX = cylinder, DX = head
    mov ch, al                  ; CH = cylinder bits 0-7
    shl ah, 6
    or cl, ah                   ; CL[7:6] = cylinder bits 8-9
    mov dh, dl                  ; DH = head
    mov dl, [boot_drive_saved]
    mov di, READ_RETRIES
.retry:
    mov ax, 0x0201              ; read 1 sector
    int 0x13
    jnc .ok
    call reset_disk
    dec di
    jnz .retry
    jmp disk_error
.ok:
    ret

; Reset the disk controller before a retry (keeps all registers but AX).
reset_disk:
    push dx
    xor ah, ah
    mov dl, [boot_drive_saved]
    int 0x13
    pop dx
    ret

; Copy ECX dwords from linear ESI to linear EDI (above 1 MB) in unreal
; mode: DS/ES get 4 GB limits from a short trip through protected mode.
; Interrupts stay off until the copy is done, so no BIOS code can reload
; the segment limits in between.
copy_high:
    cli
    push ds
    push es
    lgdt [gdt_descriptor]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp .pm
.pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov eax, cr0
    and al, 0xFE
    mov cr0, eax
    jmp .rm
.rm:
    xor ax, ax
    mov ds, ax
    mov es, ax
    cld
    a32 rep movsd
    pop es
    pop ds
    sti
    ret

; =============================================================================
; Load Kernel: header first, then the image in chunks to 1 MB
; =============================================================================
load_kernel:
    call disk_init

    ; kernel header (kernel/entry.S): magic at +4, image bytes at +8
    mov eax, KERNEL_START_SECTOR
    mov cx, 1
    call read_sectors
    push ds
    mov ax, BUFFER_SEG
    mov ds, ax
    mov eax, [4]
    mov ecx, [8]
    pop ds
    cmp eax, KERNEL_MAGIC
    jne .bad_header
    test ecx, ecx
    jz .bad_header
    mov [bi_kernel_bytes], ecx
    add ecx, 511
    shr ecx, 9
    cmp ecx, KERNEL_MAX_SECTORS
    ja .bad_header
    mov [load_left], cx
    mov dword [load_lba], KERNEL_START_SECTOR
    mov dword [load_dest], PROTECTED_MODE_BASE

.chunk:
    mov cx, [load_left]
    cmp cx, CHUNK_SECTORS
    jbe .read
    mov cx, CHUNK_SECTORS
.read:
    mov [load_chunk], cx
    mov eax, [load_lba]
    call read_sectors

    movzx ecx, word [load_chunk]
    shl ecx, 7                  ; sectors -> dwords
    mov esi, BUFFER_SEG * 16
    mov edi, [load_dest]
    call copy_high
    mov [load_dest], edi

    movzx eax, word [load_chunk]
    add [load_lba], eax
    sub [load_left], ax
    jnz .chunk
    ret

.bad_header:
    mov si, msg_bad_header
    call print_string
    jmp halt

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
    
    ; The kernel is already at 1 MB (load_kernel). Stack below the EBDA.
    mov esp, 0x9F000

    ; Jump to kernel! EBX = boot info block
    mov ebx, boot_info
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
msg_bad_header:     db '  Bad kernel header!', 0x0D, 0x0A, 0

; disk access state (disk_init)
disk_use_lba:       db 0
disk_spt:           dw 0
disk_heads:         dw 0

; kernel load progress
load_lba:           dd 0
load_dest:          dd 0
load_left:          dw 0
load_chunk:         dw 0

; Disk Address Packet for AH=42h
align 4
dap:                db 0x10, 0
dap_count:          dw 0
dap_offset:         dw 0
dap_segment:        dw 0
dap_lba:            dq 0

; Align to 16 bytes for VBE structures
align 16
vbe_info:           times 512 db 0
vbe_mode_info:      times 256 db 0

; -----------------------------------------------------------------------------
; Boot info block for the kernel (boot_info_t, kernel/include/boot_info.h)
; -----------------------------------------------------------------------------
BOOT_INFO_MAGIC     equ 0x424D4547  ; "GEMB"
BOOT_INFO_E820_MAX  equ 32

align 16
boot_info:
bi_magic:           dd BOOT_INFO_MAGIC
bi_version:         dd 1
bi_boot_drive:      dd 0            ; BIOS drive number
bi_kernel_bytes:    dd 0            ; bytes copied to 1 MB
bi_vbe_mode:        dd 0            ; VBE mode number that was set
bi_e820_count:      dd 0
bi_e820:            times BOOT_INFO_E820_MAX * 24 db 0
bi_vbe_mode_info:   times 256 db 0  ; ModeInfoBlock of bi_vbe_mode

; Pad Stage 2 to fill allocated sectors
times (STAGE2_SECTORS * 512) - ($ - $$) db 0
