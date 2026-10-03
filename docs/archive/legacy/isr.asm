;; ============================================================================
;; Phoenix-8086 — Interrupt Service Routine Stubs (Assembly)
;; ============================================================================
;;
;; These assembly stubs are installed in the IVT. They save the
;; full register context, call the C handler, restore context,
;; and return via IRET.
;;
;; ============================================================================

[BITS 16]
section .text

; ────────────────────────────────────────────────
; Exports (installed by interrupts.c via idt_install)
; ────────────────────────────────────────────────
global timer_isr
global keyboard_isr
global syscall_isr

; ────────────────────────────────────────────────
; Imports (C handlers)
; ────────────────────────────────────────────────
extern timer_handler
extern keyboard_handler
extern syscall_handler

; ────────────────────────────────────────────────
; Macro: Save all general-purpose registers
; ────────────────────────────────────────────────
%macro SAVE_REGS 0
    pusha               ; Push AX, CX, DX, BX, SP, BP, SI, DI
    push ds
    push es

    ; Ensure DS and ES point to segment 0 (kernel data)
    xor ax, ax
    mov ds, ax
    mov es, ax
%endmacro

; ────────────────────────────────────────────────
; Macro: Restore all general-purpose registers
; ────────────────────────────────────────────────
%macro RESTORE_REGS 0
    pop es
    pop ds
    popa                ; Pop DI, SI, BP, SP, BX, DX, CX, AX
%endmacro

; ────────────────────────────────────────────────
; Timer ISR (IRQ0 — Vector 0x08)
; ────────────────────────────────────────────────
timer_isr:
    SAVE_REGS
    call timer_handler
    RESTORE_REGS
    iret

; ────────────────────────────────────────────────
; Keyboard ISR (IRQ1 — Vector 0x09)
; ────────────────────────────────────────────────
keyboard_isr:
    SAVE_REGS
    call keyboard_handler
    RESTORE_REGS
    iret

; ────────────────────────────────────────────────
; System Call ISR (INT 80h)
; ────────────────────────────────────────────────
syscall_isr:
    SAVE_REGS
    call syscall_handler
    RESTORE_REGS
    iret
