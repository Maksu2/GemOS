; =============================================================================
; GemOS Stage 1 Bootloader (MBR)
; =============================================================================
; First sector of the boot disk (floppy or hard disk). Loads Stage 2 (the
; next 32 sectors) to 0x7E00 and jumps to it with the BIOS drive number in DL.
;
; Disk access:
;   - INT 13h extensions (LBA, AH=42h) when the BIOS offers them for the
;     drive (hard disks),
;   - otherwise CHS reads (AH=02h) one sector at a time, with the geometry
;     taken from INT 13h AH=08h (floppies; SeaBIOS has no LBA for them).
; Every read is retried three times after a disk reset.
;
; Memory (real mode):
;   0x0000:0x7C00 - this sector; the stack grows down from 0x7C00
;   0x0000:0x7E00 - Stage 2 (16 KB, up to 0xBE00)
; =============================================================================

[BITS 16]
[ORG 0x7C00]

STAGE2_LOAD_ADDR    equ 0x7E00
STAGE2_LBA          equ 1
STAGE2_SECTORS      equ 32          ; 16 KB, must match Stage 2
READ_RETRIES        equ 3

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov [boot_drive], dl

    mov si, msg_loading
    call print_string

    ; LBA through INT 13h extensions if the BIOS has them for this drive
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .chs
    cmp bx, 0xAA55
    jne .chs
    test cx, 1                  ; fixed disk access subset (AH=42h)
    jz .chs

    mov di, READ_RETRIES
.lba_retry:
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .loaded
    call reset_disk
    dec di
    jnz .lba_retry
    jmp disk_error

.chs:
    ; geometry: CL[5:0] = sectors per track, DH = last head. For floppies
    ; the BIOS also returns its parameter table in ES:DI, so ES is reset.
    mov ah, 0x08
    mov dl, [boot_drive]
    xor di, di                  ; ES:DI = 0 works around some BIOSes
    int 0x13
    mov ax, 0
    mov es, ax
    jc disk_error
    and cx, 0x3F
    jz disk_error
    mov [sectors_per_track], cx
    mov dl, dh
    xor dh, dh
    inc dx
    mov [head_count], dx

    mov ax, STAGE2_LBA
    mov bx, STAGE2_LOAD_ADDR
    mov cx, STAGE2_SECTORS
.chs_next:
    push cx
    push ax
    call read_chs_sector
    pop ax
    pop cx
    inc ax
    add bx, 512
    loop .chs_next

.loaded:
    mov si, msg_done
    call print_string
    mov dl, [boot_drive]
    jmp 0x0000:STAGE2_LOAD_ADDR

; -----------------------------------------------------------------------------
; read_chs_sector - read LBA AX into ES:BX (CHS, with retries)
; -----------------------------------------------------------------------------
read_chs_sector:
    xor dx, dx
    div word [sectors_per_track] ; AX = track, DX = sector - 1
    inc dx
    mov cl, dl                  ; CL[5:0] = sector
    xor dx, dx
    div word [head_count]       ; AX = cylinder, DX = head
    mov ch, al                  ; CH = cylinder bits 0-7
    shl ah, 6
    or cl, ah                   ; CL[7:6] = cylinder bits 8-9
    mov dh, dl                  ; DH = head
    mov dl, [boot_drive]
    mov di, READ_RETRIES
.retry:
    mov ax, 0x0201              ; read 1 sector
    int 0x13
    jnc .done
    call reset_disk
    dec di
    jnz .retry
    jmp disk_error
.done:
    ret

; reset the controller before a retry (keeps all registers but AX)
reset_disk:
    push dx
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    pop dx
    ret

disk_error:
    mov si, msg_disk_error
    call print_string
halt:
    cli
    hlt
    jmp halt

; print_string - print the zero-terminated string at SI
print_string:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    xor bh, bh
    int 0x10
    jmp .loop
.done:
    popa
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
boot_drive:         db 0
sectors_per_track:  dw 0
head_count:         dw 0

; Disk Address Packet for AH=42h
dap:
    db 0x10, 0                  ; size, reserved
    dw STAGE2_SECTORS
    dw STAGE2_LOAD_ADDR, 0      ; buffer offset, segment
    dq STAGE2_LBA

msg_loading:        db 'GemOS Boot...', 0x0D, 0x0A, 0
msg_done:           db 'OK', 0x0D, 0x0A, 0
msg_disk_error:     db 'Disk Error!', 0x0D, 0x0A, 0

times 510 - ($ - $$) db 0
dw 0xAA55
