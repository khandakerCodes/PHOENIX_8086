/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Floppy Disk Controller Driver
 *
 * Drives the PC's floppy controller (NEC µPD765 / Intel 8272 and
 * compatibles) directly, the way DOS-era systems did: commands go to
 * the controller's data port, the sector itself is moved by the DMA
 * controller (channel 2), and the controller raises IRQ6 when a
 * command is done.
 *
 * Unlike a BIOS call, this never stops the kernel. While the drive
 * seeks or reads, the calling thread waits on a semaphore that the
 * IRQ6 handler signals, and other threads run.
 *
 * Read-only, drive A: only, one sector per command.
 */

#include "floppy.h"
#include "interrupts.h"
#include "scheduler.h"
#include "thread.h"
#include "sync.h"
#include "hal.h"

/* ── Controller ports ───────────────────────── */
#define FDC_DOR         0x3F2   /* Digital output: motors, reset, IRQ/DMA enable */
#define FDC_MSR         0x3F4   /* Main status */
#define FDC_DATA        0x3F5   /* Command and result bytes */
#define FDC_CCR         0x3F7   /* Data rate (AT and later; ignored on an XT) */

#define DOR_RESET       0x00    /* Controller held in reset */
#define DOR_ENABLE      0x0C    /* Out of reset, IRQ and DMA enabled, motors off */
#define DOR_MOTOR_A     0x1C    /* The same with drive A: selected and spinning */

#define MSR_RQM         0x80    /* Ready for a byte */
#define MSR_DIO         0x40    /* 1 = controller has a byte for us */

/* ── Commands ───────────────────────────────── */
#define CMD_SPECIFY     0x03
#define CMD_RECALIBRATE 0x07
#define CMD_SENSE_INT   0x08
#define CMD_SEEK        0x0F
#define CMD_READ        0xE6    /* Read data: multi-track, MFM, skip deleted */

#define ST0_ERROR_MASK  0xC0    /* Interrupt code: 00 = normal termination */
#define ST0_SEEK_END    0x20

/* ── DMA controller (8237), channel 2 ───────── */
#define DMA_MASK        0x0A
#define DMA_MODE        0x0B
#define DMA_CLEAR_FF    0x0C
#define DMA_ADDR_2      0x04
#define DMA_COUNT_2     0x05
#define DMA_PAGE_2      0x81

#define DMA_MODE_READ   0x46    /* Single transfer, device to memory, channel 2 */

/* ── Parameters ─────────────────────────────── */
#define SECTOR_SIZE         512
#define SECTORS_PER_TRACK   18
#define GAP_LENGTH          0x1B
#define IRQ_TIMEOUT         (HZ / 2)        /* Ticks to wait for the controller */
#define SPIN_UP_TICKS       (HZ * 3 / 10)   /* Motor spin-up */
#define MOTOR_IDLE_TICKS    (2 * HZ)        /* Motor stays on this long after a read */
#define STATUS_POLLS        20000           /* Polls of the status port per byte */
#define RETRIES             3
#define CYLINDER_UNKNOWN    0xFF

/* ── State ──────────────────────────────────── */
static semaphore_t   irq_sem;       /* Signalled by the IRQ6 handler */
static volatile bool irq_seen;
static bool          motor_running;
static volatile bool busy;          /* A command is in progress: keep the motor on */
static uint32_t      motor_deadline;
static uint8_t       current_cylinder = CYLINDER_UNKNOWN;

extern void floppy_isr(void);

/* ── Waiting ────────────────────────────────── */

/*
 * The boot context (thread 0) is the idle thread and must never be
 * taken off the CPU, so it waits by halting until the next interrupt.
 * Every other thread really sleeps.
 */
static void delay_ticks(uint16_t ticks)
{
    if (thread_current_tid() == 0) {
        uint32_t start = irq_ticks();
        while (irq_ticks() - start < ticks) {
            __asm__ __volatile__("hlt");
        }
    } else {
        thread_sleep(ticks);
    }
}

static void forget_interrupts(void)
{
    while (sem_trywait(&irq_sem)) {
        /* discard interrupts nobody was waiting for */
    }
    irq_seen = false;
}

static bool wait_for_interrupt(void)
{
    bool ok;

    if (thread_current_tid() == 0) {
        uint32_t start = irq_ticks();

        while (!irq_seen && irq_ticks() - start < IRQ_TIMEOUT) {
            __asm__ __volatile__("hlt");
        }
        ok = irq_seen;
        sem_trywait(&irq_sem);
    } else {
        ok = sem_wait_timeout(&irq_sem, IRQ_TIMEOUT);
    }
    irq_seen = false;
    return ok;
}

uint16_t floppy_handler(uint16_t sp)
{
    irq_seen = true;
    sem_signal(&irq_sem);
    irq_eoi(6);

    /* The waiting thread is ready now; let the scheduler decide */
    return sched_switch(sp);
}

/* ── Talking to the controller ──────────────── */

static bool send_byte(uint8_t byte)
{
    uint16_t polls;

    for (polls = 0; polls < STATUS_POLLS; polls++) {
        if ((inb(FDC_MSR) & (MSR_RQM | MSR_DIO)) == MSR_RQM) {
            outb(FDC_DATA, byte);
            return true;
        }
    }
    return false;
}

static bool read_byte(uint8_t *byte)
{
    uint16_t polls;

    for (polls = 0; polls < STATUS_POLLS; polls++) {
        if ((inb(FDC_MSR) & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) {
            *byte = inb(FDC_DATA);
            return true;
        }
    }
    return false;
}

/* After a seek or reset the controller must be asked what happened */
static bool sense_interrupt(uint8_t *st0, uint8_t *cylinder)
{
    return send_byte(CMD_SENSE_INT) && read_byte(st0) && read_byte(cylinder);
}

/* ── Motor ──────────────────────────────────── */

static void motor_on(void)
{
    if (!motor_running) {
        outb(FDC_DOR, DOR_MOTOR_A);
        delay_ticks(SPIN_UP_TICKS);
        motor_running = true;
    }
}

void floppy_tick(void)
{
    if (motor_running && !busy && (int32_t)(tick_count - motor_deadline) >= 0) {
        outb(FDC_DOR, DOR_ENABLE);
        motor_running = false;
    }
}

/* ── Head positioning ───────────────────────── */

static bool recalibrate(void)
{
    uint8_t st0, cylinder;

    forget_interrupts();
    if (!send_byte(CMD_RECALIBRATE) || !send_byte(0)) return false;
    if (!wait_for_interrupt()) return false;
    if (!sense_interrupt(&st0, &cylinder)) return false;
    if (!(st0 & ST0_SEEK_END) || cylinder != 0) return false;

    current_cylinder = 0;
    return true;
}

static bool seek(uint8_t cylinder, uint8_t head)
{
    uint8_t st0, reached;

    if (current_cylinder == CYLINDER_UNKNOWN && !recalibrate()) return false;
    if (current_cylinder == cylinder) return true;

    forget_interrupts();
    if (!send_byte(CMD_SEEK) || !send_byte(head << 2) || !send_byte(cylinder)) return false;
    if (!wait_for_interrupt()) return false;
    if (!sense_interrupt(&st0, &reached)) return false;
    if (!(st0 & ST0_SEEK_END) || reached != cylinder) {
        current_cylinder = CYLINDER_UNKNOWN;
        return false;
    }

    current_cylinder = cylinder;
    return true;
}

/* ── Data transfer ──────────────────────────── */

/* Point DMA channel 2 at the buffer for one sector, device to memory */
static void dma_prepare(uint8_t *buffer)
{
    uint16_t offset = (uint16_t)buffer;
    uint16_t count = SECTOR_SIZE - 1;

    outb(DMA_MASK, 0x06);                       /* Mask channel 2 while changing it */
    outb(DMA_CLEAR_FF, 0xFF);
    outb(DMA_ADDR_2, (uint8_t)(offset & 0xFF));
    outb(DMA_ADDR_2, (uint8_t)(offset >> 8));
    outb(DMA_PAGE_2, KERNEL_DATA_SEG >> 12);    /* Top four bits of the 20-bit address */
    outb(DMA_CLEAR_FF, 0xFF);
    outb(DMA_COUNT_2, (uint8_t)(count & 0xFF));
    outb(DMA_COUNT_2, (uint8_t)(count >> 8));
    outb(DMA_MODE, DMA_MODE_READ);
    outb(DMA_MASK, 0x02);                       /* Unmask channel 2 */
}

static bool read_once(uint8_t cylinder, uint8_t head, uint8_t sector, uint8_t *buffer)
{
    uint8_t result[7];
    uint8_t i;

    if (!seek(cylinder, head)) return false;

    dma_prepare(buffer);
    forget_interrupts();

    if (!send_byte(CMD_READ) ||
        !send_byte(head << 2) ||            /* Head and drive 0 */
        !send_byte(cylinder) ||
        !send_byte(head) ||
        !send_byte(sector) ||
        !send_byte(2) ||                    /* 512 bytes per sector */
        !send_byte(SECTORS_PER_TRACK) ||    /* Last sector on the track */
        !send_byte(GAP_LENGTH) ||
        !send_byte(0xFF)) {
        return false;
    }

    if (!wait_for_interrupt()) return false;

    for (i = 0; i < sizeof(result); i++) {
        if (!read_byte(&result[i])) return false;
    }

    /* The DMA controller ends the transfer after one sector: normal termination */
    return (result[0] & ST0_ERROR_MASK) == 0;
}

/* ── Public API ─────────────────────────────── */

bool floppy_reset(void)
{
    uint8_t st0, cylinder;
    uint8_t i;

    forget_interrupts();
    current_cylinder = CYLINDER_UNKNOWN;
    motor_running = false;

    outb(FDC_DOR, DOR_RESET);
    delay_ticks(2);
    outb(FDC_DOR, DOR_ENABLE);

    /* A controller that exists interrupts when it comes out of reset */
    if (!wait_for_interrupt()) return false;

    /* One status per drive is waiting to be collected */
    for (i = 0; i < 4; i++) {
        if (!sense_interrupt(&st0, &cylinder)) return false;
    }

    outb(FDC_CCR, 0x00);                    /* 500 kbit/s: 1.44 MB disks */

    /* Step rate 3 ms, head unload 240 ms, head load 16 ms, DMA mode */
    return send_byte(CMD_SPECIFY) && send_byte(0xDF) && send_byte(0x02);
}

bool floppy_init(void)
{
    sem_init(&irq_sem, 0);
    irq_seen = false;
    busy = false;

    irq_claim(6, FLOPPY_VECTOR, floppy_isr);

    if (!floppy_reset()) {
        irq_release(6, FLOPPY_VECTOR);
        return false;
    }
    return true;
}

bool floppy_read_sector(uint8_t cylinder, uint8_t head, uint8_t sector, uint8_t *buffer)
{
    bool ok = false;
    uint8_t attempt;

    busy = true;
    motor_on();

    for (attempt = 0; attempt < RETRIES && !ok; attempt++) {
        ok = read_once(cylinder, head, sector, buffer);
        if (!ok) {
            /* Start over from a known state */
            floppy_reset();
            motor_on();
        }
    }

    motor_deadline = irq_ticks() + MOTOR_IDLE_TICKS;
    busy = false;
    return ok;
}
