;; SPDX-License-Identifier: MIT
;; ============================================================================
;; Phoenix-8086 — Stage 1 Bootloader
;; ============================================================================
;;
;; Fits in one 512-byte boot sector.
;; Responsibilities:
;;   1. Set up segment registers and stack
;;   2. Save the boot drive number
;;   3. Load Stage 2 from disk (sectors 2–5 → 0x7E00)
;;
;; The sector starts with a FAT12 BIOS Parameter Block.
;;   4. Far jump to Stage 2 entry
;;
;; Assembled with:  nasm -f bin boot/stage1.asm -o build/stage1.bin
;; ============================================================================

[BITS 16]
[CPU 8086]
[ORG 0x7C00]

; ────────────────────────────────────────────────
; Constants
; ────────────────────────────────────────────────
STAGE2_LOAD_SEG     equ 0x0000
STAGE2_LOAD_OFF     equ 0x7E00
STAGE2_SECTORS      equ 4              ; 4 sectors = 2 KB for Stage 2
STAGE2_START_SECTOR equ 2              ; Stage 2 starts at sector 2

; ────────────────────────────────────────────────
; FAT12 BIOS Parameter Block
;
; The floppy is a valid FAT12 volume, so any operating system can
; copy files onto it. Stage 2 and the kernel live in the reserved
; sectors in front of the first FAT. tools/mkfat12.py reads these
; fields to lay out the image; kernel/fat12.c reads them at mount.
; ────────────────────────────────────────────────
RESERVED_SECTORS    equ 136             ; boot sector + Stage 2 + kernel (LBA 0-135)

    jmp short _start
    nop

    db "PHOENIX "                       ; OEM name
    dw 512                              ; Bytes per sector
    db 1                                ; Sectors per cluster
    dw RESERVED_SECTORS                 ; Reserved sectors
    db 2                                ; Number of FATs
    dw 224                              ; Root directory entries
    dw 2880                             ; Total sectors (1.44 MB)
    db 0xF0                             ; Media descriptor
    dw 9                                ; Sectors per FAT
    dw 18                               ; Sectors per track
    dw 2                                ; Heads
    dd 0                                ; Hidden sectors
    dd 0                                ; Total sectors (32-bit, unused)
    db 0                                ; Drive number
    db 0                                ; Reserved
    db 0x29                             ; Extended boot signature
    dd 0x8086F00D                       ; Volume serial number
    db "PHOENIX8086"                    ; Volume label
    db "FAT12   "                       ; File system type

; ────────────────────────────────────────────────
; Entry point — BIOS jumps here after POST
; ────────────────────────────────────────────────
_start:
    cli                                 ; Disable interrupts during setup
    xor ax, ax
    mov ds, ax                          ; DS = 0
    mov es, ax                          ; ES = 0
    mov ss, ax                          ; SS = 0
    mov sp, 0x7C00                      ; Stack grows down from bootloader
    sti                                 ; Re-enable interrupts

    mov [boot_drive], dl                ; Save boot drive number from BIOS

; ────────────────────────────────────────────────
; Print boot banner
; ────────────────────────────────────────────────
    mov si, msg_boot
    call print_string

; ────────────────────────────────────────────────
; Load Stage 2 from disk using INT 13h
; ────────────────────────────────────────────────
    mov ah, 0x02                        ; BIOS: Read sectors
    mov al, STAGE2_SECTORS              ; Number of sectors to read
    mov ch, 0                           ; Cylinder 0
    mov cl, STAGE2_START_SECTOR         ; Start at sector 2
    mov dh, 0                           ; Head 0
    mov dl, [boot_drive]                ; Drive number
    mov bx, STAGE2_LOAD_OFF            ; ES:BX = destination buffer

    int 0x13                            ; Call BIOS disk service
    jc disk_error                       ; CF set = error

    ; Verify sectors read
    cmp al, STAGE2_SECTORS
    jne disk_error

; ────────────────────────────────────────────────
; Emit telemetry: BOOT_STAGE = 0x01 (Stage 1 complete)
; ────────────────────────────────────────────────
    mov si, msg_stage2
    call print_string

; ────────────────────────────────────────────────
; Jump to Stage 2
; ────────────────────────────────────────────────
    mov dl, [boot_drive]                ; Pass boot drive to Stage 2
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

; ────────────────────────────────────────────────
; Error handler — print error message and halt
; ────────────────────────────────────────────────
disk_error:
    mov si, msg_err
    call print_string
    cli
    hlt

; ────────────────────────────────────────────────
; print_string — Print null-terminated string at DS:SI
; Uses BIOS INT 10h / AH=0Eh (teletype output)
; ────────────────────────────────────────────────
print_string:
    push ax
    push bx
    push si
.loop:
    lodsb                               ; Load byte from DS:SI into AL
    test al, al                         ; Check for null terminator
    jz .done
    mov ah, 0x0E                        ; BIOS teletype output
    mov bh, 0                           ; Page 0
    mov bl, 0x07                        ; Light gray on black
    int 0x10
    jmp .loop
.done:
    pop si
    pop bx
    pop ax
    ret

; ────────────────────────────────────────────────
; Data
; ────────────────────────────────────────────────
boot_drive:     db 0
msg_boot:       db "Phoenix-8086 Stage 1", 0x0D, 0x0A, 0
msg_stage2:     db "Loading Stage 2...", 0x0D, 0x0A, 0
msg_err:        db "DISK ERROR", 0x0D, 0x0A, 0

; ────────────────────────────────────────────────
; Pad to 512 bytes and add boot signature
; ────────────────────────────────────────────────
    times 510 - ($ - $$) db 0
    dw 0xAA55
