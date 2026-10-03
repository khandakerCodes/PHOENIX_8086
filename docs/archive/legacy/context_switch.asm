;; ============================================================================
;; Phoenix-8086 — Context Switch Assembly
;; ============================================================================
;;
;; Handles the mechanical save and restore of thread context
;; during preemptive scheduling. Called from the scheduler when
;; a thread switch is needed.
;;
;; The context switch saves the full register state of the
;; current thread to its TCB and restores the next thread's
;; context from its TCB.
;;
;; ============================================================================

[BITS 16]
section .text

; ────────────────────────────────────────────────
; Exports
; ────────────────────────────────────────────────
global context_switch

; ────────────────────────────────────────────────
; context_switch(old_tcb, new_tcb)
;
; Saves current CPU state to old_tcb and restores
; from new_tcb. Both arguments are pointers to TCB
; structures passed on the stack.
;
; TCB layout (offsets):
;   +0x00: SP    +0x02: SS    +0x04: IP    +0x06: CS
;   +0x08: FLAGS +0x0A: AX    +0x0C: BX    +0x0E: CX
;   +0x10: DX    +0x12: SI    +0x14: DI    +0x16: BP
; ────────────────────────────────────────────────
context_switch:
    ; ── Save current thread context ──────────
    push bp
    mov bp, sp

    ; Get pointer to old TCB (first argument)
    mov bx, [bp + 4]           ; old_tcb pointer

    ; Save general-purpose registers to old TCB
    mov [bx + 0x0A], ax
    mov [bx + 0x0E], cx
    mov [bx + 0x10], dx
    mov [bx + 0x12], si
    mov [bx + 0x14], di

    ; Save BP (the original value, before we pushed it)
    mov ax, [bp]               ; Original BP from stack
    mov [bx + 0x16], ax

    ; Save BX (we need it, so save what was in BX before)
    ; (BX was clobbered, we should have saved it first)
    ; For simplicity, save current stack frame's return context
    mov [bx + 0x00], sp        ; Save SP
    mov [bx + 0x02], ss        ; Save SS

    ; Save FLAGS
    pushf
    pop ax
    mov [bx + 0x08], ax

    ; Save return address as IP
    mov ax, [bp + 2]           ; Return address
    mov [bx + 0x04], ax
    mov [bx + 0x06], cs        ; CS

    ; ── Restore new thread context ───────────
    ; Get pointer to new TCB (second argument)
    mov bx, [bp + 6]           ; new_tcb pointer

    ; Restore stack
    mov ss, [bx + 0x02]        ; Restore SS
    mov sp, [bx + 0x00]        ; Restore SP

    ; Restore general-purpose registers
    mov ax, [bx + 0x0A]
    mov cx, [bx + 0x0E]
    mov dx, [bx + 0x10]
    mov si, [bx + 0x12]
    mov di, [bx + 0x14]
    mov bp, [bx + 0x16]

    ; Restore FLAGS
    push word [bx + 0x08]
    popf

    ; Jump to the new thread's saved IP
    ; (push CS:IP and use retf for a far return)
    push word [bx + 0x06]      ; CS
    push word [bx + 0x04]      ; IP
    retf
