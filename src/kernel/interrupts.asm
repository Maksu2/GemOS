[bits 32]
[extern irq_handler] ; C function
global idt_load

global irq1
global irq12

idt_load:
    mov eax, [esp+4]
    lidt [eax]
    ret

; Common Stub
irq_common_stub:
    pusha           ; Pushes edi,esi,ebp,esp,ebx,edx,ecx,eax

    mov ax, ds      ; Lower 16-bits of eax = ds.
    push eax        ; Save the data segment descriptor

    mov ax, 0x10    ; Load the kernel data segment descriptor
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    call irq_handler

    pop eax         ; Reload the original data segment descriptor
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa            ; Pops edi,esi,ebp...
    add esp, 8      ; Cleans up the pushed error code and ISR number
    iret

; IRQ 1 - Keyboard
irq1:
    push byte 0     ; Dummy error code
    push byte 33    ; IDT Index (32+1)
    jmp irq_common_stub

; IRQ 12 - Mouse
irq12:
    push byte 0
    push byte 44    ; IDT Index (32+12)
    jmp irq_common_stub
