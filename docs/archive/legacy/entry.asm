;; ============================================================================
;; Phoenix-8086 — Kernel Entry Point
;; ============================================================================
;;
;; This is the first kernel code that executes after Stage 2 hands off.
;; Loaded at physical address 0x8000.
;;
;; Responsibilities:
;;   1. Set up data/stack segments for the kernel
;;   2. Zero the BSS section
;;   3. Call kernel_main() in C
;;   4. Halt if kernel_main ever returns
;;
;; ============================================================================

[BITS 16]
section .text

; ────────────────────────────────────────────────
; Exports
; ────────────────────────────────────────────────
global kernel_entry
global halt

; ────────────────────────────────────────────────
; Imports from C
; ────────────────────────────────────────────────
extern kernel_main
extern __bss_start
extern __bss_end

; ────────────────────────────────────────────────
; Kernel entry — Stage 2 jumps here
; ────────────────────────────────────────────────
kernel_entry:
    cli

    ; Set up segments — all pointing to 0 for flat real-mode access
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; Set up kernel stack
    mov ss, ax
    mov sp, 0x7C00                      ; Stack below bootloader region

    ; Save boot info passed from Stage 2
    ; DL = boot drive, CX = memory KB
    mov [boot_drive], dl
    mov [mem_kb], cx

    ; ────────────────────────────────────────────
    ; Zero the BSS section
    ; ────────────────────────────────────────────
    mov di, __bss_start
    mov cx, __bss_end
    sub cx, __bss_start
    xor al, al
    rep stosb

    sti

    ; ────────────────────────────────────────────
    ; Call the C kernel main function
    ; ────────────────────────────────────────────
    call kernel_main

    ; If kernel_main returns, halt the CPU
halt:
    cli
    hlt
    jmp halt

; ────────────────────────────────────────────────
; Kernel boot data (accessible from C)
; ────────────────────────────────────────────────
global boot_drive
global mem_kb

boot_drive:    db 0
mem_kb:        dw 0
