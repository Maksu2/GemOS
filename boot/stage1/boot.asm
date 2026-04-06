; =============================================================================
; GemOS Stage 1 Bootloader (MBR)
; =============================================================================
; This is the first code executed when the computer boots. Located at the 
; first sector of the disk (512 bytes), it:
;   1. Sets up segments and stack
;   2. Loads Stage 2 from disk
;   3. Jumps to Stage 2
;
; Memory Layout after boot (Real Mode):
;   0x0000:0x0500 - BIOS Data Area (BDA)
;   0x0000:0x1000 - Kernel Load Buffer (Temporary, 64KB)
;   0x0000:0x7C00 - Stage 1 (MBR, 512 bytes)
;               |   Stack grows down from 0x7C00
;               v
;   0x0000:0x7E00 - Stage 2 (Loader, 16KB)
;   0x0000:0xBE00 - End of Stage 2
; =============================================================================

[BITS 16]
[ORG 0x7C00]

; -----------------------------------------------------------------------------
; Constants
; -----------------------------------------------------------------------------
STAGE2_LOAD_ADDR    equ 0x7E00      ; Where to load Stage 2
STAGE2_SECTORS      equ 32          ; Number of sectors to load (16KB)
; Stage 2 occupies 0x7E00 - 0xBE00 (16KB)
; Stack must be ABOVE this area to avoid being overwritten!
STACK_TOP           equ 0x7C00      ; Stack below Stage 1 (grows down into free low memory)

; -----------------------------------------------------------------------------
; Entry Point
; -----------------------------------------------------------------------------
start:
    ; Clear interrupts during setup
    cli
    
    ; Set up segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP
    
    ; Re-enable interrupts
    sti
    
    ; Save boot drive number (BIOS passes it in DL)
    mov [boot_drive], dl
    
    ; Print loading message
    mov si, msg_loading
    call print_string
    
    ; Load Stage 2 from disk
    call load_stage2
    
    ; Print success message
    mov si, msg_done
    call print_string
    
    ; Pass boot drive number to Stage 2
    ; Stage 2 expects it at STAGE2_LOAD_ADDR + offset (boot_drive_saved)
    ; We'll store it at a fixed offset within Stage 2 data section
    mov al, [boot_drive]
    mov [STAGE2_LOAD_ADDR + 0x3F00], al  ; Store at end of Stage 2 area
    
    ; Jump to Stage 2 with drive number in DL
    mov dl, [boot_drive]
    jmp STAGE2_LOAD_ADDR


; -----------------------------------------------------------------------------
; load_stage2 - Load Stage 2 from disk using BIOS INT 13h
; -----------------------------------------------------------------------------
load_stage2:
    ; 1. Read sectors 2-18 (Head 0) -> 17 sectors
    mov ah, 0x02
    mov al, 17                  ; Read up to end of track (18-2+1=17)
    mov ch, 0                   ; Cylinder 0
    mov cl, 2                   ; Start from sector 2
    mov dh, 0                   ; Head 0
    mov dl, [boot_drive]
    mov bx, STAGE2_LOAD_ADDR
    int 0x13
    jc disk_error
    cmp al, 17
    jne disk_error

    ; 2. Read sectors 1-15 (Head 1) -> 15 sectors
    ; Dest buffer offset: 17 * 512 = 8704 = 0x2200
    mov bx, STAGE2_LOAD_ADDR
    add bx, 0x2200
    
    mov ah, 0x02
    mov al, 15                  ; Remaining sectors (32-17=15)
    mov ch, 0                   ; Cylinder 0
    mov cl, 1                   ; Start from sector 1
    mov dh, 1                   ; Head 1
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    cmp al, 15
    jne disk_error
    
    ret

; -----------------------------------------------------------------------------
; disk_error - Display error and halt
; -----------------------------------------------------------------------------
disk_error:
    mov si, msg_disk_error
    call print_string
    jmp halt

; -----------------------------------------------------------------------------
; halt - Infinite loop
; -----------------------------------------------------------------------------
halt:
    cli
    hlt
    jmp halt

; -----------------------------------------------------------------------------
; print_string - Print null-terminated string
; Input: SI = pointer to string
; -----------------------------------------------------------------------------
print_string:
    pusha
.loop:
    lodsb                       ; Load byte from SI into AL
    or al, al                   ; Check if null terminator
    jz .done
    mov ah, 0x0E                ; BIOS teletype function
    mov bh, 0                   ; Page 0
    int 0x10                    ; BIOS video interrupt
    jmp .loop
.done:
    popa
    ret

; -----------------------------------------------------------------------------
; Data Section
; -----------------------------------------------------------------------------
boot_drive:     db 0
msg_loading:    db 'GemOS Boot...', 0x0D, 0x0A, 0
msg_done:       db 'OK', 0x0D, 0x0A, 0
msg_disk_error: db 'Disk Error!', 0x0D, 0x0A, 0

; -----------------------------------------------------------------------------
; Boot Sector Signature
; -----------------------------------------------------------------------------
times 510 - ($ - $$) db 0       ; Pad with zeros
dw 0xAA55                       ; Boot signature
