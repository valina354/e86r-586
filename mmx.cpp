#include "stdafx.h"
#include "mmx.h"
#include "fpu.h"
#include "cpu.h"
#include "modrm.h"
#include "memdescr.h"
#include <cstring>

typedef union {
    unsigned long long u64;
    unsigned int u32[2];
    unsigned short u16[4];
    unsigned char u8[8];
    signed long long s64;
    signed int s32[2];
    signed short s16[4];
    signed char s8[8];
} mmx_reg;

static mmx_reg get_mmx_operand() {
    mmx_reg operand;
    int old_i32 = i32;
    i32 = 1;

    if (modrm_isreg) {
        int reg_idx = (modrm & 7);
        operand.u64 = *(unsigned long long*) & FPU_ST(reg_idx);
    }
    else {
        read32(sel, ofs, &operand.u32[0]);
        if (opcode != 0x7E) {
            read32(sel, ofs + 4, &operand.u32[1]);
        }
    }

    i32 = old_i32;
    return operand;
}

static void set_mmx_result(int dest_reg_idx, mmx_reg result) {
    *(unsigned long long*)& FPU_ST(dest_reg_idx) = result.u64;
}

void mmx_op(unsigned char opcode) {
    fpu_enter_mmx_mode();
    if (!mod(0)) return;

    int dest_reg_idx = (modrm >> 3) & 7;
    mmx_reg dest = { *(unsigned long long*) & FPU_ST(dest_reg_idx) };
    mmx_reg src;

    if (opcode != 0x77) {
        src = get_mmx_operand();
    }

    switch (opcode) {
    case 0xFC:
        for (int i = 0; i < 8; ++i) dest.u8[i] += src.u8[i];
        break;
    case 0xFD:
        for (int i = 0; i < 4; ++i) dest.u16[i] += src.u16[i];
        break;
    case 0xFE:
        for (int i = 0; i < 2; ++i) dest.u32[i] += src.u32[i];
        break;
    case 0xEC:
        for (int i = 0; i < 8; ++i) {
            int sum = dest.s8[i] + src.s8[i];
            if (sum > 127) sum = 127; if (sum < -128) sum = -128;
            dest.s8[i] = sum;
        }
        break;
    case 0xED:
        for (int i = 0; i < 4; ++i) {
            int sum = dest.s16[i] + src.s16[i];
            if (sum > 32767) sum = 32767; if (sum < -32768) sum = -32768;
            dest.s16[i] = sum;
        }
        break;
    case 0xDC:
        for (int i = 0; i < 8; ++i) {
            int sum = dest.u8[i] + src.u8[i];
            if (sum > 255) sum = 255;
            dest.u8[i] = sum;
        }
        break;
    case 0xDD:
        for (int i = 0; i < 4; ++i) {
            int sum = dest.u16[i] + src.u16[i];
            if (sum > 65535) sum = 65535;
            dest.u16[i] = sum;
        }
        break;
    case 0xF8:
        for (int i = 0; i < 8; ++i) dest.u8[i] -= src.u8[i];
        break;
    case 0xF9:
        for (int i = 0; i < 4; ++i) dest.u16[i] -= src.u16[i];
        break;
    case 0xFA:
        for (int i = 0; i < 2; ++i) dest.u32[i] -= src.u32[i];
        break;
    case 0xE8:
        for (int i = 0; i < 8; ++i) {
            int diff = dest.s8[i] - src.s8[i];
            if (diff > 127) diff = 127; if (diff < -128) diff = -128;
            dest.s8[i] = diff;
        }
        break;
    case 0xE9:
        for (int i = 0; i < 4; ++i) {
            int diff = dest.s16[i] - src.s16[i];
            if (diff > 32767) diff = 32767; if (diff < -32768) diff = -32768;
            dest.s16[i] = diff;
        }
        break;
    case 0xD8:
        for (int i = 0; i < 8; ++i) {
            int diff = dest.u8[i] - src.u8[i];
            if (diff < 0) diff = 0;
            dest.u8[i] = diff;
        }
        break;
    case 0xD9:
        for (int i = 0; i < 4; ++i) {
            int diff = dest.u16[i] - src.u16[i];
            if (diff < 0) diff = 0;
            dest.u16[i] = diff;
        }
        break;
    case 0xE5:
        for (int i = 0; i < 4; ++i) {
            int product = (int)dest.s16[i] * (int)src.s16[i];
            dest.s16[i] = (short)(product >> 16);
        }
        break;
    case 0xD5:
        for (int i = 0; i < 4; ++i) dest.u16[i] *= src.u16[i];
        break;
    case 0xF5:
        dest.s32[0] = ((int)dest.s16[0] * (int)src.s16[0]) + ((int)dest.s16[1] * (int)src.s16[1]);
        dest.s32[1] = ((int)dest.s16[2] * (int)src.s16[2]) + ((int)dest.s16[3] * (int)src.s16[3]);
        break;
    case 0x74:
        for (int i = 0; i < 8; ++i) dest.u8[i] = (dest.u8[i] == src.u8[i]) ? 0xFF : 0x00;
        break;
    case 0x75:
        for (int i = 0; i < 4; ++i) dest.u16[i] = (dest.u16[i] == src.u16[i]) ? 0xFFFF : 0x0000;
        break;
    case 0x76:
        for (int i = 0; i < 2; ++i) dest.u32[i] = (dest.u32[i] == src.u32[i]) ? 0xFFFFFFFF : 0x00000000;
        break;
    case 0x64:
        for (int i = 0; i < 8; ++i) dest.u8[i] = (dest.s8[i] > src.s8[i]) ? 0xFF : 0x00;
        break;
    case 0x65:
        for (int i = 0; i < 4; ++i) dest.u16[i] = (dest.s16[i] > src.s16[i]) ? 0xFFFF : 0x0000;
        break;
    case 0x66:
        for (int i = 0; i < 2; ++i) dest.u32[i] = (dest.s32[i] > src.s32[i]) ? 0xFFFFFFFF : 0x00000000;
        break;
    case 0xF1:
        for (int i = 0; i < 4; ++i) dest.u16[i] <<= src.u8[0];
        break;
    case 0xF2:
        for (int i = 0; i < 2; ++i) dest.u32[i] <<= src.u8[0];
        break;
    case 0xF3:
        dest.u64 <<= src.u8[0];
        break;
    case 0xE1:
        for (int i = 0; i < 4; ++i) dest.u16[i] >>= src.u8[0];
        break;
    case 0xE2:
        for (int i = 0; i < 2; ++i) dest.u32[i] >>= src.u8[0];
        break;
    case 0xE3:
        dest.u64 >>= src.u8[0];
        break;
    case 0xD1:
        for (int i = 0; i < 4; ++i) dest.s16[i] >>= src.u8[0];
        break;
    case 0xD2:
        for (int i = 0; i < 2; ++i) dest.s32[i] >>= src.u8[0];
        break;
    case 0xDB:
        dest.u64 &= src.u64;
        break;
    case 0xDF:
        dest.u64 = (~dest.u64) & src.u64;
        break;
    case 0xEB:
        dest.u64 |= src.u64;
        break;
    case 0xEF:
        dest.u64 ^= src.u64;
        break;
    case 0x60: { mmx_reg temp = dest; dest.u8[0] = temp.u8[0]; dest.u8[1] = src.u8[0]; dest.u8[2] = temp.u8[1]; dest.u8[3] = src.u8[1]; dest.u8[4] = temp.u8[2]; dest.u8[5] = src.u8[2]; dest.u8[6] = temp.u8[3]; dest.u8[7] = src.u8[3]; } break;
    case 0x61: { mmx_reg temp = dest; dest.u16[0] = temp.u16[0]; dest.u16[1] = src.u16[0]; dest.u16[2] = temp.u16[1]; dest.u16[3] = src.u16[1]; } break;
    case 0x62: { mmx_reg temp = dest; dest.u32[0] = temp.u32[0]; dest.u32[1] = src.u32[0]; } break;
    case 0x68: { mmx_reg temp = dest; dest.u8[0] = temp.u8[4]; dest.u8[1] = src.u8[4]; dest.u8[2] = temp.u8[5]; dest.u8[3] = src.u8[5]; dest.u8[4] = temp.u8[6]; dest.u8[5] = src.u8[6]; dest.u8[6] = temp.u8[7]; dest.u8[7] = src.u8[7]; } break;
    case 0x69: { mmx_reg temp = dest; dest.u16[0] = temp.u16[2]; dest.u16[1] = src.u16[2]; dest.u16[2] = temp.u16[3]; dest.u16[3] = src.u16[3]; } break;
    case 0x6A: { mmx_reg temp = dest; dest.u32[0] = temp.u32[1]; dest.u32[1] = src.u32[1]; } break;
    case 0x67: { mmx_reg temp_src = src; for (int i = 0; i < 4; ++i) { int val = dest.s16[i]; if (val > 127) val = 127; if (val < -128) val = -128; dest.s8[i] = val; } for (int i = 0; i < 4; ++i) { int val = temp_src.s16[i]; if (val > 127) val = 127; if (val < -128) val = -128; dest.s8[i + 4] = val; } } break;
    case 0x6B: { mmx_reg temp_src = src; for (int i = 0; i < 2; ++i) { int val = dest.s32[i]; if (val > 32767) val = 32767; if (val < -32768) val = -32768; dest.s16[i] = val; } for (int i = 0; i < 2; ++i) { int val = temp_src.s32[i]; if (val > 32767) val = 32767; if (val < -32768) val = -32768; dest.s16[i + 2] = val; } } break;
    case 0x63: { mmx_reg temp_src = src; for (int i = 0; i < 4; ++i) { int val = dest.s16[i]; if (val > 255) val = 255; if (val < 0) val = 0; dest.u8[i] = val; } for (int i = 0; i < 4; ++i) { int val = temp_src.s16[i]; if (val > 255) val = 255; if (val < 0) val = 0; dest.u8[i + 4] = val; } } break;
    case 0x6E: r.r32[modrm & 7] = dest.u32[0]; return;
    case 0x7E: dest.u32[0] = r.r32[modrm & 7]; dest.u32[1] = 0; break;
    case 0x6F: dest.u64 = src.u64; break;
    case 0x7F: { int old_i32 = i32; i32 = 1; writemod(dest.u32[0]); ofs += 4; writemod(dest.u32[1]); ofs -= 4; i32 = old_i32; } return;
    default: D("\n!!! Undefined MMX instruction: Opcode=0F %.2X / ModR/M=%.2X !!!\n", opcode, modrm); return;
    }

    set_mmx_result(dest_reg_idx, dest);
}