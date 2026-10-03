;; SPDX-License-Identifier: MIT
;; ============================================================================
;; Phoenix-8086 — Stage 2 Loader
;; ============================================================================
;;
;; Loaded by Stage 1 at 0x7E00.
;; Responsibilities:
;;   1. Display boot progress messages
;;   2. Detect available memory (INT 12h)
;;   3. Read the kernel image header and load exactly the sectors it needs
;;      to 1000:0000
;;   4. Verify the image magic and checksum
;;   5. Far jump to the kernel entry point (DL = boot drive, CX = memory KB)
;;
;; 8086 instructions only. Layout constants mirror include/layout.h.
;; ============================================================================

[BITS 16]
[CPU 8086]
[ORG 0x7E00]

; ────────────────────────────────────────────────
; Constants
; ────────────────────────────────────────────────
KERNEL_LOAD_SEG     equ 0x1000          ; Segment 0x1000 → physical 0x10000
KERNEL_ENTRY_OFF    equ 0x0010          ; Entry point follows the 16-byte header
KERNEL_START_LBA    equ 5               ; Sector 6 = LBA 5 (0-indexed)

HDR_MAGIC_LO        equ 0               ; "PX"
HDR_MAGIC_HI        equ 2               ; "86"
HDR_IMAGE_SIZE      equ 6               ; Image size in bytes
HDR_CHECKSUM        equ 14              ; 16-bit byte sum, field counted as zero

; ────────────────────────────────────────────────
; Entry point — Stage 1 jumps here
; ────────────────────────────────────────────────
stage2_entry:
    mov [boot_drive_s2], dl             ; Save boot drive (passed from Stage 1)

    ; Print banner
    mov si, msg_s2_active
    call s2_print_string

; ────────────────────────────────────────────────
; Detect conventional memory (INT 12h)
; ────────────────────────────────────────────────
    int 0x12                            ; Returns KB of low memory in AX
    mov [mem_kb], ax

    ; Print memory info
    mov si, msg_mem_detect
    call s2_print_string

    ; Convert AX (KB) to decimal and print
    mov ax, [mem_kb]
    call s2_print_dec
    mov si, msg_kb_suffix
    call s2_print_string

; ────────────────────────────────────────────────
; Load the first kernel sector and read the image header
; ────────────────────────────────────────────────
    mov si, msg_loading_kernel
    call s2_print_string

    mov ax, KERNEL_LOAD_SEG
    mov es, ax
    xor bx, bx
    mov ax, KERNEL_START_LBA
    call read_single_sector_lba

    cmp word [es:HDR_MAGIC_LO], 'PX'
    jne bad_image
    cmp word [es:HDR_MAGIC_HI], '86'
    jne bad_image

    ; sectors = (image_size + 511) / 512, using the carry as bit 16
    mov ax, [es:HDR_IMAGE_SIZE]
    add ax, 511
    rcr ax, 1
    mov al, ah
    xor ah, ah
    mov [kernel_sectors], ax

; ────────────────────────────────────────────────
; Load the remaining kernel sectors
; ────────────────────────────────────────────────
    mov cx, ax
    dec cx                              ; First sector is already loaded
    jcxz .loaded
    mov ax, KERNEL_START_LBA + 1
    mov bx, 512

.load_loop:
    call read_single_sector_lba
    add bx, 512                         ; Image is < 64 KB, BX cannot wrap
    inc ax
    loop .load_loop

.loaded:

; ────────────────────────────────────────────────
; Verify checksum: sum every loaded byte, then take the
; checksum field's own two bytes back out
; ────────────────────────────────────────────────
    mov cx, [kernel_sectors]
    xor si, si
    xor dx, dx                          ; DX = running sum
    xor ah, ah

.sum_sector:
    push cx
    mov cx, 512
.sum_byte:
    mov al, [es:si]
    add dx, ax
    inc si
    loop .sum_byte
    pop cx
    loop .sum_sector

    mov bx, [es:HDR_CHECKSUM]
    mov al, bl
    sub dx, ax
    mov al, bh
    sub dx, ax
    cmp dx, bx
    jne bad_image

    mov si, msg_kernel_loaded
    call s2_print_string

; ────────────────────────────────────────────────
; Hand off to the kernel
; ────────────────────────────────────────────────
    mov si, msg_jumping
    call s2_print_string

    cli                                 ; Kernel sets up its own segments and stack

    ; Pass boot drive in DL, memory size in CX
    mov dl, [boot_drive_s2]
    mov cx, [mem_kb]

    jmp KERNEL_LOAD_SEG:KERNEL_ENTRY_OFF

; ────────────────────────────────────────────────
; bad_image — header magic or checksum mismatch
; ────────────────────────────────────────────────
bad_image:
    mov si, msg_bad_image
    call s2_print_string
    cli
    hlt

; ────────────────────────────────────────────────
; read_single_sector_lba
; Input:  AX = LBA, ES:BX = buffer
; Preserves all registers
; ────────────────────────────────────────────────
read_single_sector_lba:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, 3                           ; Retry up to 3 times

.retry:
    push ax
    push di

    ; LBA to CHS conversion for 1.44MB Floppy (18 SPT, 2 Heads)
    ; LBA / 18 -> AX = track_head, DX = sector - 1
    xor dx, dx
    mov si, 18
    div si
    inc dx                              ; Sector number (1-18)
    mov cl, dl                          ; CL = Sector (bits 0-5)

    ; track_head / 2 -> AX = Cylinder, DX = Head
    xor dx, dx
    mov si, 2
    div si
    mov ch, al                          ; CH = Cylinder (0-79)
    mov dh, dl                          ; DH = Head (0-1)
    mov dl, [boot_drive_s2]             ; DL = Boot drive

    mov ax, 0x0201                      ; AH=02 (Read), AL=1 (1 sector)
    int 0x13
    jnc .success                        ; CF = 0 -> Success

    ; Reset disk controller on failure
    xor ax, ax
    mov dl, [boot_drive_s2]
    int 0x13

    pop di
    pop ax
    dec di
    jnz .retry

    ; Error handler if all retries failed
    mov si, msg_s2_err
    call s2_print_string
    cli
    hlt

.success:
    pop di
    pop ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ────────────────────────────────────────────────
; s2_print_string — Print null-terminated string at DS:SI
; ────────────────────────────────────────────────
s2_print_string:
    push ax
    push bx
    push si
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    pop si
    pop bx
    pop ax
    ret

; ────────────────────────────────────────────────
; s2_print_dec — Print unsigned 16-bit integer in AX as decimal
; ────────────────────────────────────────────────
s2_print_dec:
    push ax
    push bx
    push cx
    push dx
    mov cx, 0                           ; Digit counter
    mov bx, 10
.div_loop:
    xor dx, dx
    div bx                              ; AX = AX / 10, DX = remainder
    push dx                             ; Save digit
    inc cx
    test ax, ax
    jnz .div_loop
.print_loop:
    pop dx
    add dl, '0'                         ; Convert to ASCII
    mov ah, 0x0E
    mov al, dl
    mov bh, 0
    int 0x10
    loop .print_loop
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ────────────────────────────────────────────────
; Data
; ────────────────────────────────────────────────
boot_drive_s2:      db 0
mem_kb:             dw 0
kernel_sectors:     dw 0
msg_s2_active:      db "Phoenix-8086 Stage 2 Loader", 0x0D, 0x0A, 0
msg_mem_detect:     db "Memory: ", 0
msg_kb_suffix:      db " KB detected", 0x0D, 0x0A, 0
msg_loading_kernel: db "Loading kernel...", 0x0D, 0x0A, 0
msg_kernel_loaded:  db "Kernel image loaded at 1000:0000, checksum OK", 0x0D, 0x0A, 0
msg_jumping:        db "Jumping to kernel entry...", 0x0D, 0x0A, 0
msg_s2_err:         db "STAGE 2 DISK ERROR", 0x0D, 0x0A, 0
msg_bad_image:      db "BAD KERNEL IMAGE", 0x0D, 0x0A, 0

; ────────────────────────────────────────────────
; Pad Stage 2 to fill exactly 4 sectors (2048 bytes)
; ────────────────────────────────────────────────
    times 2048 - ($ - $$) db 0
