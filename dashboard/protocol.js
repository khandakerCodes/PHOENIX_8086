// SPDX-License-Identifier: MIT
/**
 * Phoenix-8086 — Telemetry protocol v1 decoder (JavaScript)
 *
 * The same decoder as bridge/protocol.py, for when the kernel runs
 * inside the page and there is no bridge. It turns the kernel's serial
 * bytes into the messages the bridge would have relayed. See
 * docs/telemetry.md.
 */
(function (root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory();
    } else {
        root.PhoenixProtocol = factory();
    }
}(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    const PROTOCOL_VERSION = 1;
    const DELIMITER = 0x7E;
    const ESCAPE = 0x7D;
    const ESCAPE_XOR = 0x20;
    const HEADER_SIZE = 7;      // version, type, seq, tick[4]
    const CRC_SIZE = 2;

    const TYPE_NAMES = ['HELLO', 'BOOT_STAGE', 'THREAD_CREATE', 'THREAD_EXIT', 'THREAD_STATE',
                        'CONTEXT_SWITCH', 'COUNTERS', 'MEMORY', 'FAULT', 'CONSOLE', 'SYSCALL',
                        'BENCH', 'THREAD_STATS'];
    const THREAD_STATES = ['READY', 'RUNNING', 'BLOCKED', 'SLEEPING', 'TERMINATED'];
    const BENCH_KINDS = ['context_switches', 'heap_pairs'];
    const REGISTERS = ['ip', 'cs', 'flags', 'sp', 'ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp'];

    /* Code page 437, upper half: the PC text-mode character set */
    const CP437_HIGH =
        'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐' +
        '└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';

    function crc16(bytes, length) {
        /* CRC-16/CCITT-FALSE */
        let crc = 0xFFFF;
        for (let i = 0; i < length; i++) {
            crc ^= bytes[i] << 8;
            for (let bit = 0; bit < 8; bit++) {
                crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
            }
        }
        return crc;
    }

    function u16(p, offset) {
        return p[offset] | (p[offset + 1] << 8);
    }

    function u32(p, offset) {
        return (u16(p, offset) | (u16(p, offset + 2) << 16)) >>> 0;
    }

    function text(p, start, end) {
        let result = '';
        for (let i = start; i < end; i++) {
            result += p[i] < 0x80 ? String.fromCharCode(p[i]) : CP437_HIGH[p[i] - 0x80];
        }
        return result;
    }

    function name(p, offset) {
        let end = offset;
        while (end < offset + 12 && p[end] !== 0) end++;
        return text(p, offset, end);
    }

    function state(value) {
        return value < THREAD_STATES.length ? THREAD_STATES[value] : 'UNKNOWN_' + value;
    }

    function registers(p, offset) {
        const regs = {};
        REGISTERS.forEach(function (register, i) { regs[register] = u16(p, offset + i * 2); });
        return regs;
    }

    /* Minimum payload length per type; shorter frames are rejected */
    const MIN_LENGTH = { HELLO: 18, BOOT_STAGE: 1, THREAD_CREATE: 14, THREAD_EXIT: 1, THREAD_STATE: 3,
                         CONTEXT_SWITCH: 24, COUNTERS: 18, MEMORY: 8, FAULT: 23, CONSOLE: 0,
                         SYSCALL: 2, BENCH: 5, THREAD_STATS: 25 };

    function decodePayload(type, p) {
        switch (type) {
        case 'HELLO':
            return { version: p[0], hz: p[1], max_threads: p[2],
                     code_seg: u16(p, 4), data_seg: u16(p, 6),
                     far_start_seg: u16(p, 8), far_end_seg: u16(p, 10),
                     mem_kb: u16(p, 12), heap_start: u16(p, 14), heap_end: u16(p, 16) };
        case 'BOOT_STAGE':
            return { stage: p[0] };
        case 'THREAD_CREATE':
            return { tid: p[0], priority: p[1], name: name(p, 2) };
        case 'THREAD_EXIT':
            return { tid: p[0] };
        case 'THREAD_STATE':
            return { tid: p[0], state: state(p[1]), priority: p[2] };
        case 'CONTEXT_SWITCH':
            return { from_tid: p[0], to_tid: p[1], regs: registers(p, 2) };
        case 'COUNTERS':
            return { timer: u32(p, 0), keyboard: u32(p, 4), syscall: u32(p, 8),
                     context_switches: u32(p, 12), drops: u16(p, 16) };
        case 'MEMORY':
            return { heap_free: u16(p, 0), heap_used: u16(p, 2),
                     far_free_paras: u16(p, 4), far_total_paras: u16(p, 6) };
        case 'FAULT':
            return { tid: p[0], regs: registers(p, 1), reason: text(p, 23, p.length) };
        case 'CONSOLE':
            return { text: text(p, 0, p.length) };
        case 'SYSCALL':
            return { tid: p[0], func: p[1] };
        case 'BENCH':
            return { kind: p[0] < BENCH_KINDS.length ? BENCH_KINDS[p[0]] : 'UNKNOWN_' + p[0],
                     count: u32(p, 1) };
        case 'THREAD_STATS':
            return { tid: p[0], state: state(p[1]), priority: p[2], cpu_ticks: u32(p, 3),
                     sp: u16(p, 7), stack_base: u16(p, 9), stack_size: u16(p, 11), name: name(p, 13) };
        default:
            return { data: Array.from(p) };
        }
    }

    /** Decode one unescaped frame body; returns a message or null if it is damaged. */
    function decodeFrame(body) {
        if (body.length < HEADER_SIZE + CRC_SIZE) return null;
        const contentLength = body.length - CRC_SIZE;
        if (crc16(body, contentLength) !== u16(body, contentLength)) return null;
        if (body[0] !== PROTOCOL_VERSION) return null;

        const typeId = body[1];
        const type = typeId < TYPE_NAMES.length
            ? TYPE_NAMES[typeId]
            : 'UNKNOWN_' + typeId.toString(16).toUpperCase().padStart(2, '0');
        const payload = body.slice(HEADER_SIZE, contentLength);
        if (type in MIN_LENGTH && payload.length < MIN_LENGTH[type]) return null;

        const message = { type: type, seq: body[2], tick: u32(body, 3) };
        const fields = decodePayload(type, payload);
        Object.keys(fields).forEach(function (key) { message[key] = fields[key]; });
        return message;
    }

    /** Incremental stream decoder: feed it bytes, get complete messages back. */
    function Decoder() {
        this.body = [];
        this.escaped = false;
        this.inFrame = false;
        this.lastSeq = null;
        this.frames = 0;
        this.badFrames = 0;
        this.lostFrames = 0;
        this.strayBytes = 0;
    }

    Decoder.prototype.feed = function (bytes) {
        const messages = [];
        for (let i = 0; i < bytes.length; i++) {
            const byte = bytes[i];
            if (byte === DELIMITER) {
                if (this.inFrame && this.body.length) {
                    this.finish(messages);
                }
                this.inFrame = true;
                this.body = [];
                this.escaped = false;
            } else if (!this.inFrame) {
                this.strayBytes++;
            } else if (byte === ESCAPE) {
                this.escaped = true;
            } else {
                this.body.push(this.escaped ? byte ^ ESCAPE_XOR : byte);
                this.escaped = false;
            }
        }
        return messages;
    };

    Decoder.prototype.finish = function (messages) {
        const message = decodeFrame(Uint8Array.from(this.body));
        if (message === null) {
            this.badFrames++;
            return;
        }
        if (this.lastSeq !== null) {
            this.lostFrames += (message.seq - this.lastSeq - 1) & 0xFF;
        }
        this.lastSeq = message.seq;
        this.frames++;
        messages.push(message);
    };

    return { Decoder: Decoder, decodeFrame: decodeFrame, crc16: crc16 };
}));
