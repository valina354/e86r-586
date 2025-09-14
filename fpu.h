#ifndef FPU_H
#define FPU_H

// FPU State Structure
typedef struct {
	long double st[8];    // 80-bit FPU registers ST(0) to ST(7)
	unsigned short cw;    // Control Word
	unsigned short sw;    // Status Word
	unsigned short tw;    // Tag Word
	unsigned int ip;      // FPU Instruction Pointer
	unsigned int dp;      // FPU Data Pointer
	unsigned short op;    // Last FPU Opcode
} fpu_state_t;

// FPU State Global Variable
extern fpu_state_t fpu;

// FPU Constants (from Bink FPU)
#define kFpuTagValid   0
#define kFpuTagZero    1
#define kFpuTagSpecial 2
#define kFpuTagEmpty   3

#define kFpuCwIm 0x0001 /* invalid operation mask */
#define kFpuCwDm 0x0002 /* denormal operand mask */
#define kFpuCwZm 0x0004 /* zero divide mask */
#define kFpuCwOm 0x0008 /* overflow mask */
#define kFpuCwUm 0x0010 /* underflow mask */
#define kFpuCwPm 0x0020 /* precision mask */
#define kFpuCwPc 0x0300 /* precision: 32,?,64,80 */
#define kFpuCwRc 0x0c00 /* rounding: even,?-?,?+?,?0 */

#define kFpuSwIe 0x0001 /* invalid operation */
#define kFpuSwDe 0x0002 /* denormalized operand */
#define kFpuSwZe 0x0004 /* zero divide */
#define kFpuSwOe 0x0008 /* overflow */
#define kFpuSwUe 0x0010 /* underflow */
#define kFpuSwPe 0x0020 /* precision */
#define kFpuSwSf 0x0040 /* stack fault */
#define kFpuSwEs 0x0080 /* exception summary status */
#define kFpuSwC0 0x0100 /* condition 0 */
#define kFpuSwC1 0x0200 /* condition 1 */
#define kFpuSwC2 0x0400 /* condition 2 */
#define kFpuSwSp 0x3800 /* top stack */
#define kFpuSwC3 0x4000 /* condition 3 */
#define kFpuSwBf 0x8000 /* busy flag */

// FPU Helper Macros
#define FPU_STACK_TOP ((fpu.sw & kFpuSwSp) >> 11)
#define FPU_ST(i) (fpu.st[(((i) + FPU_STACK_TOP) & 7)])

// FPU Function Prototypes
void fpu_init();
void fpu_op(unsigned char opcode);
void fpu_wait();

#endif // FPU_H