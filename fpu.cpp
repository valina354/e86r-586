#define _USE_MATH_DEFINES
#include "stdafx.h"
#include "fpu.h"
#include "cpu.h"
#include "modrm.h"
#include "memdescr.h"
#include "interrupts.h"
#include <cmath>

#ifndef M_E
#define M_E        2.71828182845904523536
#endif
#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif
#ifndef M_LN2
#define M_LN2      0.693147180559945309417
#endif

#pragma warning(disable: 4244)

fpu_state_t fpu;

union FloatPun { float f; unsigned int i; };
union DoublePun { double d; unsigned long long i; };

static long double DeserializeLdbl(const unsigned char p[10]) {
	unsigned long long mant = *(unsigned long long*)p;
	unsigned short exp = *(unsigned short*)(p + 8);
	long double res;
	unsigned short* res_p = (unsigned short*)&res;
	res_p[0] = (unsigned short)(mant & 0xFFFF);
	res_p[1] = (unsigned short)((mant >> 16) & 0xFFFF);
	res_p[2] = (unsigned short)((mant >> 32) & 0xFFFF);
	res_p[3] = (unsigned short)((mant >> 48) & 0xFFFF);
	res_p[4] = exp;
	return res;
}

static void SerializeLdbl(unsigned char p[10], long double x) {
	*(unsigned long long*)p = *(unsigned long long*) & x;
	*(unsigned short*)(p + 8) = *(unsigned short*)((char*)&x + 8);
}

static short fpu_get_mem_short() {
	unsigned int val;
	int old_i32 = i32; i32 = 0;
	readmod(&val);
	i32 = old_i32;
	return (short)val;
}

static int fpu_get_mem_int() {
	unsigned int val;
	int old_i32 = i32; i32 = 1;
	readmod(&val);
	i32 = old_i32;
	return (int)val;
}

static long long fpu_get_mem_long() {
	unsigned int lo, hi;
	int old_i32 = i32; i32 = 1;
	readmod(&lo);
	ofs += 4;
	readmod(&hi);
	ofs -= 4;
	i32 = old_i32;
	return ((unsigned long long)hi << 32) | lo;
}

static float fpu_get_mem_float() { union FloatPun u; u.i = fpu_get_mem_int(); return u.f; }
static double fpu_get_mem_double() { union DoublePun u; u.i = fpu_get_mem_long(); return u.d; }
static long double fpu_get_mem_ldbl() {
	unsigned char buf[10];
	for (int i = 0; i < 10; ++i) read8(sel, ofs + i, &buf[i]);
	return DeserializeLdbl(buf);
}

static void fpu_set_mem_short(short val) {
	int old_i32 = i32; i32 = 0;
	writemod(val);
	i32 = old_i32;
}

static void fpu_set_mem_int(int val) {
	int old_i32 = i32; i32 = 1;
	writemod(val);
	i32 = old_i32;
}

static void fpu_set_mem_long(long long val) {
	int old_i32 = i32; i32 = 1;
	writemod((unsigned int)val);
	ofs += 4;
	writemod((unsigned int)(val >> 32));
	ofs -= 4;
	i32 = old_i32;
}

static void fpu_set_mem_float(float val) { union FloatPun u; u.f = val; fpu_set_mem_int(u.i); }
static void fpu_set_mem_double(double val) { union DoublePun u; u.d = val; fpu_set_mem_long(u.i); }
static void fpu_set_mem_ldbl(long double val) {
	unsigned char buf[10];
	SerializeLdbl(buf, val);
	for (int i = 0; i < 10; ++i) write8(sel, ofs + i, buf[i]);
}

static void fpu_on_stack_overflow() { fpu.sw |= kFpuSwIe | kFpuSwC1 | kFpuSwSf; }
static long double fpu_on_stack_underflow() { fpu.sw |= kFpuSwIe | kFpuSwSf; fpu.sw &= ~kFpuSwC1; return -NAN; }

static int fpu_get_tag(int i) {
	unsigned t = fpu.tw;
	i = (i + FPU_STACK_TOP) & 7;
	return (t >> (i * 2)) & 3;
}
static void fpu_set_tag(int i, int t) {
	i = (i + FPU_STACK_TOP) & 7;
	fpu.tw &= ~(3 << (i * 2));
	fpu.tw |= (t << (i * 2));
}

static void fpu_push(long double x) {
	if (fpu_get_tag(-1) != kFpuTagEmpty) fpu_on_stack_overflow();
	fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw - (1 << 11)) & kFpuSwSp);
	FPU_ST(0) = x;
	fpu_set_tag(0, kFpuTagValid);
}
static long double fpu_pop() {
	long double x;
	if (fpu_get_tag(0) != kFpuTagEmpty) {
		x = FPU_ST(0);
		fpu_set_tag(0, kFpuTagEmpty);
	}
	else {
		x = fpu_on_stack_underflow();
	}
	fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw + (1 << 11)) & kFpuSwSp);
	return x;
}

static long double ST(int i) {
	if (fpu_get_tag(i) == kFpuTagEmpty) fpu_on_stack_underflow();
	return FPU_ST(i);
}
static long double ST0() { return ST(0); }
static long double ST1() { return ST(1); }
static long double ST_RM() { return ST(modrm & 7); }
static void SET_ST0(long double x) { FPU_ST(0) = x; }
static void SET_ST_RM(long double x) { FPU_ST(modrm & 7) = x; }
static void SET_ST_POP(int i, long double x) { FPU_ST(i) = x; fpu_pop(); }
static void SET_ST_RM_POP(long double x) { SET_ST_POP(modrm & 7, x); }

static void fpu_clear_roundup() { fpu.sw &= ~kFpuSwC1; }
static void fpu_clear_oor() { fpu.sw &= ~kFpuSwC2; }

static long double fpu_add(long double x, long double y) {
	if (!isunordered(x, y)) {
		int inf_x = isinf((double)x), inf_y = isinf((double)y);
		switch (inf_y << 1 | inf_x) {
		case 0: return x + y;
		case 1: return x;
		case 2: return y;
		case 3:
			if (signbit((double)x) == signbit((double)y)) return x;
			fpu.sw |= kFpuSwIe; return copysignl(NAN, x);
		}
	}
	return NAN;
}
static long double fpu_sub(long double x, long double y) {
	if (!isunordered(x, y)) {
		int inf_x = isinf((double)x), inf_y = isinf((double)y);
		switch (inf_y << 1 | inf_x) {
		case 0: return x - y;
		case 1: return -x;
		case 2: return y;
		case 3:
			if (signbit((double)x) == signbit((double)y)) { fpu.sw |= kFpuSwIe; return copysignl(NAN, x); }
			return y;
		}
	}
	return NAN;
}
static long double fpu_mul(long double x, long double y) {
	if (!isunordered(x, y)) {
		if (!((isinf((double)x) && !y) || (isinf((double)y) && !x))) return x * y;
		fpu.sw |= kFpuSwIe; return -NAN;
	}
	return NAN;
}
#if (CPU == 586)
static bool IsFdivBugTriggered(double dividend) {
	int exp;
	double mant = frexp(dividend, &exp);

	unsigned int scaled_mant = mant * (1 << 20);

	if (scaled_mant >= 0b11000010000000000000 && scaled_mant <= 0b11000011111111111111) {
		return true;
	}

	if (dividend == 4195835.0 && fpu.st[((1 + FPU_STACK_TOP) & 7)] == 3145727.0) {
		return true;
	}

	return false;
}
#endif
#if (CPU >= 686)
static bool handle_fist_bug(double val, int size_bits) {
	if (CPU != 686) {
		return false;
	}

	unsigned int rounding_mode = (fpu.cw & kFpuCwRc) >> 10;
	if (rounding_mode == 1) {
		return false;
	}

	bool overflow = false;
	if (size_bits == 16) {
		if (val < -32768.0) overflow = true;
	}
	else {
		if (val < -2147483648.0) overflow = true;
	}

	if (!overflow) {
		return false;
	}

	if (size_bits == 16) {
		fpu_set_mem_short(0x8000);
	}
	else {
		fpu_set_mem_int(0x80000000);
	}

	fpu.sw |= kFpuSwPe;

	return true;
}
#endif
static double fpu_div(double x, double y) {
	if (isunordered(x, y)) return NAN;
	if (y == 0.0) {
		if (x == 0.0) {
			fpu.sw |= kFpuSwIe;
			return -NAN;
		}
		fpu.sw |= kFpuSwZe;
		return copysign(INFINITY, x);
	}

#if (CPU == 586)
	if ((r.r32[0] & 0xFFF) == 0x521) {
		if (IsFdivBugTriggered(x)) {
			double buggy_x = x - 1.5e-10 * x;
			return buggy_x / y;
		}
	}
#endif
	return x / y;
}
static long double fpu_round(long double x) {
	switch ((fpu.cw & kFpuCwRc) >> 10) {
	case 0: return rintl(x);
	case 1: return floorl(x);
	case 2: return ceill(x);
	case 3: return truncl(x);
	}
	return x;
}
static void fpu_compare(long double y) {
	long double x = ST0();
	fpu.sw &= ~(kFpuSwC0 | kFpuSwC1 | kFpuSwC2 | kFpuSwC3);
	if (!isunordered(x, y)) {
		if (x < y) fpu.sw |= kFpuSwC0;
		if (x == y) fpu.sw |= kFpuSwC3;
	}
	else {
		fpu.sw |= kFpuSwC0 | kFpuSwC2 | kFpuSwC3 | kFpuSwIe;
	}
}
static long double x87rem(long double x, long double y, unsigned short* sw, long double rem(long double, long double), long double rnd(long double)) {
	unsigned short s = 0;
	long double r = rem(x, y);
	long long q = rnd(x / y);
	s &= ~kFpuSwC2;
	if (q & 1) s |= kFpuSwC1;
	if (q & 2) s |= kFpuSwC3;
	if (q & 4) s |= kFpuSwC0;
	if (sw) *sw = s | (*sw & ~(kFpuSwC0 | kFpuSwC1 | kFpuSwC2 | kFpuSwC3));
	return r;
}
static long double fpu_fprem(long double dividend, long double modulus, unsigned short* sw) { return x87rem(dividend, modulus, sw, fmodl, truncl); }
static long double fpu_fprem1(long double dividend, long double modulus, unsigned short* sw) { return x87rem(dividend, modulus, sw, remainderl, rintl); }

static void fpu_undefined() {
	D("\n!!! Undefined FPU instruction: Opcode=0xD8-0xDF / Mod=%d Reg=%d RM=%d (Full ModR/M=0x%.2X) !!!\n",
		(modrm >> 6), (modrm >> 3) & 7, modrm & 7, modrm);
}

#define DISP(op, ismem, reg) (((op) << 4) | ((ismem) << 3) | (reg))

void fpu_op(unsigned char opcode) {
	unsigned char op = opcode & 7;
	bool ismemory = !modrm_isreg;
	unsigned char reg = (modrm >> 3) & 7;

	fpu.ip = instr_eip;
	fpu.dp = ismemory ? (sel->base + ofs) : 0;
	fpu.op = op << 8 | (!ismemory ? 0xC0 : 0) | reg << 3 | (modrm & 7);

	switch (DISP(op, ismemory, reg)) {
	case DISP(0, 1, 0): SET_ST0(fpu_add(ST0(), fpu_get_mem_float())); break;
	case DISP(0, 1, 1): SET_ST0(fpu_mul(ST0(), fpu_get_mem_float())); break;
	case DISP(0, 1, 2): fpu_compare(fpu_get_mem_float()); break;
	case DISP(0, 1, 3): fpu_compare(fpu_get_mem_float()); fpu_pop(); break;
	case DISP(0, 1, 4): SET_ST0(fpu_sub(ST0(), fpu_get_mem_float())); break;
	case DISP(0, 1, 5): SET_ST0(fpu_sub(fpu_get_mem_float(), ST0())); break;
	case DISP(0, 1, 6): SET_ST0(fpu_div(ST0(), fpu_get_mem_float())); break;
	case DISP(0, 1, 7): SET_ST0(fpu_div(fpu_get_mem_float(), ST0())); break;
	case DISP(0, 0, 0): SET_ST0(fpu_add(ST0(), ST_RM())); break;
	case DISP(0, 0, 1): SET_ST0(fpu_mul(ST0(), ST_RM())); break;
	case DISP(0, 0, 2): fpu_compare(ST_RM()); break;
	case DISP(0, 0, 3): fpu_compare(ST_RM()); fpu_pop(); break;
	case DISP(0, 0, 4): SET_ST0(fpu_sub(ST0(), ST_RM())); break;
	case DISP(0, 0, 5): SET_ST0(fpu_sub(ST_RM(), ST0())); break;
	case DISP(0, 0, 6): SET_ST0(fpu_div(ST0(), ST_RM())); break;
	case DISP(0, 0, 7): SET_ST0(fpu_div(ST_RM(), ST0())); break;
	case DISP(1, 1, 0): fpu_push(fpu_get_mem_float()); break;
	case DISP(1, 1, 2): fpu_set_mem_float(ST0()); break;
	case DISP(1, 1, 3): fpu_set_mem_float(fpu_pop()); break;
	case DISP(1, 1, 4): fpu_init(); break;
	case DISP(1, 1, 5): fpu.cw = fpu_get_mem_short(); break;
	case DISP(1, 1, 6): fpu_set_mem_short(fpu.cw); break;
	case DISP(1, 1, 7): fpu_set_mem_short(fpu.cw); break;
	case DISP(1, 0, 0): fpu_push(ST_RM()); break;
	case DISP(1, 0, 1): { double t = ST_RM(); SET_ST_RM(ST0()); SET_ST0(t); } break;
	case DISP(1, 0, 2): break;
	case DISP(1, 0, 3): SET_ST_RM_POP(ST0()); break;
	case DISP(1, 0, 5):
		switch (modrm & 7) {
		case 0: fpu_push(1.0); break;
		case 1: fpu_push(log10(2.0)); break;
		case 2: fpu_push(log2(M_E)); break;
		case 3: fpu_push(M_PI); break;
		case 4: fpu_push(log2(10.0)); break;
		case 5: fpu_push(M_LN2); break;
		case 6: fpu_push(0.0); break;
		} break;
	case DISP(1, 0, 4):
		switch (modrm & 7) {
		case 0: SET_ST0(-ST0()); break;
		case 1: SET_ST0(fabs(ST0())); break;
		case 4: fpu_compare(0); break;
		case 5:
			fpu.sw &= ~(kFpuSwC0 | kFpuSwC1 | kFpuSwC2 | kFpuSwC3);
			if (signbit(ST0())) fpu.sw |= kFpuSwC1;
			if (fpu_get_tag(0) == kFpuTagEmpty) fpu.sw |= kFpuSwC0 | kFpuSwC3;
			else switch (fpclassify(ST0())) {
			case FP_NAN: fpu.sw |= kFpuSwC0; break;
			case FP_INFINITE: fpu.sw |= kFpuSwC0 | kFpuSwC2; break;
			case FP_ZERO: fpu.sw |= kFpuSwC3; break;
			case FP_SUBNORMAL: fpu.sw |= kFpuSwC2 | kFpuSwC3; break;
			case FP_NORMAL: fpu.sw |= kFpuSwC2; break;
			} break;
		} break;
	case DISP(1, 0, 6):
		switch (modrm & 7) {
		case 0: SET_ST0(exp2(ST0()) - 1); break;
		case 1: SET_ST_POP(1, ST1() * log2(ST0())); break;
		case 2: fpu_clear_oor(); SET_ST0(tan(ST0())); fpu_push(1); break;
		case 3: fpu_clear_roundup(); SET_ST_POP(1, atan2(ST1(), ST0())); break;
		case 4: { double x = ST0(); SET_ST0(logb(x)); fpu_push(ldexp(x, -ilogb(x))); } break;
		case 5: SET_ST0(fpu_fprem1(ST0(), ST1(), &fpu.sw)); break;
		case 6: fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw - (1 << 11)) & kFpuSwSp); break;
		case 7: fpu.sw = (fpu.sw & ~kFpuSwSp) | ((fpu.sw + (1 << 11)) & kFpuSwSp); break;
		} break;
	case DISP(1, 0, 7):
		switch (modrm & 7) {
		case 0: SET_ST0(fpu_fprem(ST0(), ST1(), &fpu.sw)); break;
		case 1: SET_ST_POP(1, ST1() * log2(ST0() + 1)); break;
		case 2: fpu_clear_roundup(); SET_ST0(sqrt(ST0())); break;
		case 3: { double s = sin(ST0()), c = cos(ST0()); SET_ST0(s); fpu_push(c); } break;
		case 4: SET_ST0(fpu_round(ST0())); break;
		case 5: fpu_clear_roundup(); SET_ST0(ldexp(ST0(), ST1())); break;
		case 6: fpu_clear_oor(); SET_ST0(sin(ST0())); break;
		case 7: fpu_clear_oor(); SET_ST0(cos(ST0())); break;
		} break;
#if (CPU >= 686)
	case DISP(2, 0, 0): if (r.eflags & F_C) SET_ST0(ST_RM()); break;
	case DISP(2, 0, 1): if (r.eflags & F_Z) SET_ST0(ST_RM()); break;
	case DISP(2, 0, 2): if (r.eflags & (F_C | F_Z)) SET_ST0(ST_RM()); break;
	case DISP(2, 0, 3): if (r.eflags & F_P) SET_ST0(ST_RM()); break;
#endif
	case DISP(2, 1, 0): SET_ST0(fpu_add(ST0(), fpu_get_mem_int())); break;
	case DISP(2, 1, 1): SET_ST0(fpu_mul(ST0(), fpu_get_mem_int())); break;
	case DISP(2, 1, 2): fpu_compare(fpu_get_mem_int()); break;
	case DISP(2, 1, 3): fpu_compare(fpu_get_mem_int()); fpu_pop(); break;
	case DISP(2, 1, 4): SET_ST0(fpu_sub(ST0(), fpu_get_mem_int())); break;
	case DISP(2, 1, 5): SET_ST0(fpu_sub(fpu_get_mem_int(), ST0())); break;
	case DISP(2, 1, 6): SET_ST0(fpu_div(ST0(), fpu_get_mem_int())); break;
	case DISP(2, 1, 7): SET_ST0(fpu_div(fpu_get_mem_int(), ST0())); break;
#if (CPU >= 686)
	case DISP(3, 0, 0): if (!(r.eflags & F_C)) SET_ST0(ST_RM()); break;
	case DISP(3, 0, 1): if (!(r.eflags & F_Z)) SET_ST0(ST_RM()); break;
	case DISP(3, 0, 2): if (!(r.eflags & (F_C | F_Z))) SET_ST0(ST_RM()); break;
	case DISP(3, 0, 3): if (!(r.eflags & F_P)) SET_ST0(ST_RM()); break;
	case DISP(3, 0, 5): case DISP(3, 0, 6): { double x = ST0(), y = ST_RM(); r.eflags &= ~(F_Z | F_P | F_C); if (isunordered(x, y)) { r.eflags |= (F_Z | F_P | F_C); } else { if (x < y) r.eflags |= F_C; if (x == y) r.eflags |= F_Z; } } break;
#endif
	case DISP(3, 1, 0): fpu_push(fpu_get_mem_int()); break;
	case DISP(3, 1, 1): fpu_set_mem_int(trunc(fpu_pop())); break;
	case DISP(3, 1, 2):
#if (CPU >= 686)
		if (handle_fist_bug(ST0(), 32)) break;
#endif
		fpu_set_mem_int(fpu_round(ST0()));
		break;
	case DISP(3, 1, 3):
#if (CPU >= 686)
		if (handle_fist_bug(ST0(), 32)) { fpu_pop(); break; }
#endif
		fpu_set_mem_int(fpu_round(fpu_pop()));
		break;
	case DISP(3, 1, 5): fpu_push(fpu_get_mem_ldbl()); break;
	case DISP(3, 1, 7): fpu_set_mem_ldbl(fpu_pop()); break;
	case DISP(4, 1, 0): SET_ST0(fpu_add(ST0(), fpu_get_mem_double())); break;
	case DISP(4, 1, 1): SET_ST0(fpu_mul(ST0(), fpu_get_mem_double())); break;
	case DISP(4, 1, 2): fpu_compare(fpu_get_mem_double()); break;
	case DISP(4, 1, 3): fpu_compare(fpu_get_mem_double()); fpu_pop(); break;
	case DISP(4, 1, 4): SET_ST0(fpu_sub(ST0(), fpu_get_mem_double())); break;
	case DISP(4, 1, 5): SET_ST0(fpu_sub(fpu_get_mem_double(), ST0())); break;
	case DISP(4, 1, 6): SET_ST0(fpu_div(ST0(), fpu_get_mem_double())); break;
	case DISP(4, 1, 7): SET_ST0(fpu_div(fpu_get_mem_double(), ST0())); break;
	case DISP(4, 0, 0): SET_ST_RM(fpu_add(ST_RM(), ST0())); break;
	case DISP(4, 0, 1): SET_ST_RM(fpu_mul(ST_RM(), ST0())); break;
	case DISP(4, 0, 4): SET_ST_RM(fpu_sub(ST0(), ST_RM())); break;
	case DISP(4, 0, 5): SET_ST_RM(fpu_sub(ST_RM(), ST0())); break;
	case DISP(4, 0, 6): SET_ST_RM(fpu_div(ST_RM(), ST0())); break;
	case DISP(4, 0, 7): SET_ST_RM(fpu_div(ST0(), ST_RM())); break;
	case DISP(5, 1, 0): fpu_push(fpu_get_mem_double()); break;
	case DISP(5, 1, 1): fpu_set_mem_long(trunc(fpu_pop())); break;
	case DISP(5, 1, 2): fpu_set_mem_double(ST0()); break;
	case DISP(5, 1, 3): fpu_set_mem_double(fpu_pop()); break;
	case DISP(5, 1, 4): fpu_init(); break;
	case DISP(5, 1, 6): fpu_init(); break;
	case DISP(5, 1, 7): fpu_set_mem_short(fpu.sw); break;
	case DISP(5, 0, 0): fpu_set_tag(modrm & 7, kFpuTagEmpty); break;
	case DISP(5, 0, 2): SET_ST_RM(ST0()); break;
	case DISP(5, 0, 3): SET_ST_RM_POP(ST0()); break;
	case DISP(5, 0, 4): fpu_compare(ST_RM()); break;
	case DISP(5, 0, 5): fpu_compare(ST_RM()); fpu_pop(); break;
	case DISP(6, 1, 0): SET_ST0(fpu_add(ST0(), fpu_get_mem_short())); break;
	case DISP(6, 1, 1): SET_ST0(fpu_mul(ST0(), fpu_get_mem_short())); break;
	case DISP(6, 1, 2): fpu_compare(fpu_get_mem_short()); break;
	case DISP(6, 1, 3): fpu_compare(fpu_get_mem_short()); fpu_pop(); break;
	case DISP(6, 1, 4): SET_ST0(fpu_sub(ST0(), fpu_get_mem_short())); break;
	case DISP(6, 1, 5): SET_ST0(fpu_sub(fpu_get_mem_short(), ST0())); break;
	case DISP(6, 1, 6): SET_ST0(fpu_div(ST0(), fpu_get_mem_short())); break;
	case DISP(6, 1, 7): SET_ST0(fpu_div(fpu_get_mem_short(), ST0())); break;
	case DISP(6, 0, 0): SET_ST_RM_POP(fpu_add(ST0(), ST_RM())); break;
	case DISP(6, 0, 1): SET_ST_RM_POP(fpu_mul(ST0(), ST_RM())); break;
	case DISP(6, 0, 2): fpu_compare(ST_RM()); fpu_pop(); break;
	case DISP(6, 0, 3): fpu_compare(ST_RM()); fpu_pop(); fpu_pop(); break;
	case DISP(6, 0, 4): SET_ST_RM_POP(fpu_sub(ST0(), ST_RM())); break;
	case DISP(6, 0, 5): SET_ST_POP(1, fpu_sub(ST_RM(), ST0())); break;
	case DISP(6, 0, 6): SET_ST_RM_POP(fpu_div(ST0(), ST_RM())); break;
	case DISP(6, 0, 7): SET_ST_RM_POP(fpu_div(ST_RM(), ST0())); break;
	case DISP(7, 1, 0): fpu_push(fpu_get_mem_short()); break;
	case DISP(7, 1, 1): fpu_set_mem_short(trunc(fpu_pop())); break;
	case DISP(7, 1, 2):
#if (CPU >= 686)
		if (handle_fist_bug(ST0(), 16)) break;
#endif
		fpu_set_mem_short(fpu_round(ST0()));
		break;
	case DISP(7, 1, 3):
#if (CPU >= 686)
		if (handle_fist_bug(ST0(), 16)) { fpu_pop(); break; }
#endif
		fpu_set_mem_short(fpu_round(fpu_pop()));
		break;
	case DISP(7, 1, 5): fpu_push(fpu_get_mem_long()); break;
	case DISP(7, 1, 7): fpu_set_mem_long(fpu_round(fpu_pop())); break;
	case DISP(7, 0, 4):
		r.ax = fpu.sw;
		break;
#if (CPU >= 686)
	case DISP(5, 0, 6): case DISP(5, 0, 7): { double x = ST0(), y = ST_RM(); r.eflags &= ~(F_Z | F_P | F_C); if (isunordered(x, y)) { r.eflags |= (F_Z | F_P | F_C); } else { if (x < y) r.eflags |= F_C; if (x == y) r.eflags |= F_Z; } fpu_pop(); } break;
#endif
	default: fpu_undefined(); break;
	}
}

void fpu_init() {
	fpu.cw = 0x037F;
	fpu.sw = 0;
	fpu.tw = 0xFFFF;
}

void fpu_wait() {
	int sw = fpu.sw;
	int cw = fpu.cw;
	if (((sw & kFpuSwIe) && !(cw & kFpuCwIm)) ||
		((sw & kFpuSwDe) && !(cw & kFpuCwDm)) ||
		((sw & kFpuSwZe) && !(cw & kFpuCwZm)) ||
		((sw & kFpuSwOe) && !(cw & kFpuCwOm)) ||
		((sw & kFpuSwUe) && !(cw & kFpuCwUm)) ||
		((sw & kFpuSwPe) && !(cw & kFpuCwPm)) ||
		((sw & kFpuSwSf) && !(cw & kFpuCwIm))) {
		ex(EX_COPROCESSOR);
	}
}

#if (ENABLE_MMX == 1)
void fpu_enter_mmx_mode() {
	in_mmx_mode = true;
}

void emms() {
	fpu.tw = 0xFFFF;
	in_mmx_mode = false;
}
#endif