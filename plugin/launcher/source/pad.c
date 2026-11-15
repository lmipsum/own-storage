// SPDX-License-Identifier: GPL-3.0-or-later
// The VSH's pad poll (0x444194 in the 4.82 vsh.self) calls sys_io's cellPadGetData with a buffer
// whose address it builds with "lis r5; addi r5" at +0x234/+0x244 from the code below; the buffer
// is a CellPadData (+0 len, +8 DIGITAL1, +0xa DIGITAL2). When len is not 0 the poll also keeps
// the buttons held in a word at base + 8, base built with "lis r9; addi r9" at +0xe0/+0xe8.
#include "pad.h"

#include "owns_log.h"

// The VSH's code, where the pad poll's pattern is searched for.
#define VSH_CODE_START 0x10000u
#define VSH_CODE_END 0x700000u
#define PAD_DATA_HI 0x234
#define PAD_DATA_LO 0x244
#define PAD_HELD_HI 0xe0
#define PAD_HELD_LO 0xe8
#define PAD_HELD_OFFSET 8
// DIGITAL2's low byte, and CELL_PAD_CTRL_TRIANGLE in it and in the held word.
#define PAD_DIGITAL2_LOW 0xb
#define PAD_TRIANGLE_DIGITAL2 0x10
#define PAD_TRIANGLE_HELD 0x1000
// The PowerPC opcodes of lis (addis) and addi.
#define OPCODE_ADDIS 15
#define OPCODE_ADDI 14

/** The address built by "lis rX, hi" at hi_at and "addi rX, rX, lo" at lo_at; 0 for other code. */
static uint32_t decode_lis_addi(uint32_t hi_at, uint32_t lo_at)
{
    const uint32_t hi = *(const volatile uint32_t *)(uintptr_t)hi_at;
    const uint32_t lo = *(const volatile uint32_t *)(uintptr_t)lo_at;
    const uint32_t register_hi = (hi >> 21) & 31;
    if ((hi >> 26) != OPCODE_ADDIS || ((hi >> 16) & 31) != 0 || (lo >> 26) != OPCODE_ADDI ||
        ((lo >> 21) & 31) != register_hi || ((lo >> 16) & 31) != register_hi)
    {
        return 0;
    }
    return (hi << 16) + (uint32_t)(int32_t)(int16_t)(lo & 0xFFFF);
}

vsh_pad pad_find(void)
{
    vsh_pad pad = {0, 0};
    for (uint32_t at = VSH_CODE_START; at < VSH_CODE_END; at += 4)
    {
        const volatile uint32_t *code = (const volatile uint32_t *)(uintptr_t)at;
        // li r0, 7; mr r31, r9; mr r12, r11; mtctr r0
        if (code[0] == 0x38000007u && code[1] == 0x7D3F4B78u && code[2] == 0x7D6C5B78u &&
            code[3] == 0x7C0903A6u)
        {
            owns_log_value("pad poll code", at);
            pad.data = decode_lis_addi(at + PAD_DATA_HI, at + PAD_DATA_LO);
            const uint32_t base = decode_lis_addi(at + PAD_HELD_HI, at + PAD_HELD_LO);
            pad.held = base != 0 ? base + PAD_HELD_OFFSET : 0;
            break;
        }
    }
    owns_log_value("pad data", pad.data);
    owns_log_value("pad held", pad.held);
    return pad;
}

int pad_found(const vsh_pad *pad)
{
    return pad->held != 0 || pad->data != 0;
}

/** Whether Triangle is down, by the held word, else by the last data the VSH read. */
static int triangle_down(const vsh_pad *pad)
{
    if (pad->held != 0)
    {
        return (*(const volatile uint32_t *)(uintptr_t)pad->held & PAD_TRIANGLE_HELD) != 0;
    }
    if (pad->data != 0)
    {
        const volatile uint8_t *data = (const volatile uint8_t *)(uintptr_t)pad->data;
        return (data[PAD_DIGITAL2_LOW] & PAD_TRIANGLE_DIGITAL2) != 0;
    }
    return 0;
}

int pad_triangle_pressed(const vsh_pad *pad, int *was_down)
{
    const int down = triangle_down(pad);
    const int pressed = down && !*was_down;
    *was_down = down;
    return pressed;
}
