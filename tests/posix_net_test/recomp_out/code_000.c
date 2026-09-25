#include "recomp_runtime.h"

// Chunk 0 (92 functions)

// Forward declarations for functions defined in this chunk
void fn_0x20(GuestContext *__restrict__ ctx);
void fn_0x5a0(GuestContext *__restrict__ ctx);
void fn_0x7e8(GuestContext *__restrict__ ctx);
void fn_0x7f0(GuestContext *__restrict__ ctx);
void fn_0x7f8(GuestContext *__restrict__ ctx);
void fn_0x800(GuestContext *__restrict__ ctx);
void fn_0xd54(GuestContext *__restrict__ ctx);
void fn_0xd60(GuestContext *__restrict__ ctx);
void fn_0xd6c(GuestContext *__restrict__ ctx);
void fn_0xe10(GuestContext *__restrict__ ctx);
void fn_0xfb4(GuestContext *__restrict__ ctx);
void fn_0x104c(GuestContext *__restrict__ ctx);
void fn_0x104f(GuestContext *__restrict__ ctx);
void fn_0x1060(GuestContext *__restrict__ ctx);
void fn_0x10c0(GuestContext *__restrict__ ctx);
void fn_0x1220(GuestContext *__restrict__ ctx);
void fn_0x12d4(GuestContext *__restrict__ ctx);
void fn_0x12dc(GuestContext *__restrict__ ctx);
void fn_0x1444(GuestContext *__restrict__ ctx);
void fn_0x3182(GuestContext *__restrict__ ctx);
void fn_0x3348(GuestContext *__restrict__ ctx);
void fn_0x33cc(GuestContext *__restrict__ ctx);
void fn_0x3439(GuestContext *__restrict__ ctx);
void fn_0x3444(GuestContext *__restrict__ ctx);
void fn_0x3484(GuestContext *__restrict__ ctx);
void fn_0x348c(GuestContext *__restrict__ ctx);
void fn_0x34cd(GuestContext *__restrict__ ctx);
void fn_0x3518(GuestContext *__restrict__ ctx);
void fn_0x352e(GuestContext *__restrict__ ctx);
void fn_0x353c(GuestContext *__restrict__ ctx);
void fn_0x3608(GuestContext *__restrict__ ctx);
void fn_0x3694(GuestContext *__restrict__ ctx);
void fn_0x36a0(GuestContext *__restrict__ ctx);
void fn_0x36ec(GuestContext *__restrict__ ctx);
void fn_0x3784(GuestContext *__restrict__ ctx);
void fn_0x379c(GuestContext *__restrict__ ctx);
void fn_0x3878(GuestContext *__restrict__ ctx);
void fn_0x3896(GuestContext *__restrict__ ctx);
void fn_0x38e0(GuestContext *__restrict__ ctx);
void fn_0x3c10(GuestContext *__restrict__ ctx);
void fn_0x42e0(GuestContext *__restrict__ ctx);
void fn_0x43c0(GuestContext *__restrict__ ctx);
void fn_0x4400(GuestContext *__restrict__ ctx);
void fn_0x4500(GuestContext *__restrict__ ctx);
void fn_0x4530(GuestContext *__restrict__ ctx);
void fn_0x4700(GuestContext *__restrict__ ctx);
void fn_0x4769(GuestContext *__restrict__ ctx);
void fn_0x477c(GuestContext *__restrict__ ctx);
void fn_0x4798(GuestContext *__restrict__ ctx);
void fn_0x47a0(GuestContext *__restrict__ ctx);
void fn_0x47a8(GuestContext *__restrict__ ctx);
void fn_0x47cc(GuestContext *__restrict__ ctx);
void fn_0x47cf(GuestContext *__restrict__ ctx);
void fn_0x47d0(GuestContext *__restrict__ ctx);
void fn_0x4956(GuestContext *__restrict__ ctx);
void fn_0x4989(GuestContext *__restrict__ ctx);
void fn_0x49bf(GuestContext *__restrict__ ctx);
void fn_0x49f0(GuestContext *__restrict__ ctx);
void fn_0x49f1(GuestContext *__restrict__ ctx);
void fn_0x4a2b(GuestContext *__restrict__ ctx);
void fn_0x4a48(GuestContext *__restrict__ ctx);
void fn_0x4a70(GuestContext *__restrict__ ctx);
void fn_0x4a80(GuestContext *__restrict__ ctx);
void fn_0x4a90(GuestContext *__restrict__ ctx);
void fn_0x4aa0(GuestContext *__restrict__ ctx);
void fn_0x4ab0(GuestContext *__restrict__ ctx);
void fn_0x4ac0(GuestContext *__restrict__ ctx);
void fn_0x4ad0(GuestContext *__restrict__ ctx);
void fn_0x4ae0(GuestContext *__restrict__ ctx);
void fn_0x4af0(GuestContext *__restrict__ ctx);
void fn_0x4b00(GuestContext *__restrict__ ctx);
void fn_0x4b10(GuestContext *__restrict__ ctx);
void fn_0x4b20(GuestContext *__restrict__ ctx);
void fn_0x4b30(GuestContext *__restrict__ ctx);
void fn_0x4b40(GuestContext *__restrict__ ctx);
void fn_0x4b50(GuestContext *__restrict__ ctx);
void fn_0x4b60(GuestContext *__restrict__ ctx);
void fn_0x4b70(GuestContext *__restrict__ ctx);
void fn_0x4b80(GuestContext *__restrict__ ctx);
void fn_0x4b90(GuestContext *__restrict__ ctx);
void fn_0x4ba0(GuestContext *__restrict__ ctx);
void fn_0x4bb0(GuestContext *__restrict__ ctx);
void fn_0x4bc0(GuestContext *__restrict__ ctx);
void fn_0x4bd0(GuestContext *__restrict__ ctx);
void fn_0x4be0(GuestContext *__restrict__ ctx);
void fn_0x4bf0(GuestContext *__restrict__ ctx);
void fn_0x4c00(GuestContext *__restrict__ ctx);
void fn_0x4c10(GuestContext *__restrict__ ctx);
void fn_0x4c20(GuestContext *__restrict__ ctx);
void fn_0x4c30(GuestContext *__restrict__ ctx);
void fn_0x4c40(GuestContext *__restrict__ ctx);
void fn_0x4c50(GuestContext *__restrict__ ctx);

// Function main at 0x20
void fn_0x20(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x20ULL) {
        switch (ctx->rip) {
            case 0x67ULL: goto loc_0x67;
            case 0x84ULL: goto loc_0x84;
            case 0xe8ULL: goto loc_0xe8;
            case 0x120ULL: goto loc_0x120;
            case 0x13eULL: goto loc_0x13e;
            case 0x15bULL: goto loc_0x15b;
            case 0x1afULL: goto loc_0x1af;
            case 0x1c4ULL: goto loc_0x1c4;
            case 0x1e5ULL: goto loc_0x1e5;
            case 0x22aULL: goto loc_0x22a;
            case 0x24aULL: goto loc_0x24a;
            case 0x265ULL: goto loc_0x265;
            case 0x285ULL: goto loc_0x285;
            case 0x2a0ULL: goto loc_0x2a0;
            case 0x2c0ULL: goto loc_0x2c0;
            case 0x2dbULL: goto loc_0x2db;
            case 0x2fbULL: goto loc_0x2fb;
            case 0x316ULL: goto loc_0x316;
            case 0x336ULL: goto loc_0x336;
            case 0x380ULL: goto loc_0x380;
            case 0x3a0ULL: goto loc_0x3a0;
            case 0x3caULL: goto loc_0x3ca;
            case 0x3ebULL: goto loc_0x3eb;
            case 0x455ULL: goto loc_0x455;
            case 0x476ULL: goto loc_0x476;
            case 0x4feULL: goto loc_0x4fe;
            case 0x516ULL: goto loc_0x516;
            case 0x534ULL: goto loc_0x534;
            case 0x587ULL: goto loc_0x587;
            default: goto loc_0x20;
        }
    }

loc_0x20:
    ctx->rip = 0x20ULL;
    /* 0x20: PUSH RBP */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbp);
    /* 0x21: MOV RBP, RSP */
ctx->rbp = (uint64_t)(ctx->rsp);
    /* 0x24: SUB RSP, 0x1a0 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x1a0ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x2b: MOV [RBP-0x4], 0x0 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x0ULL);
    /* 0x32: LEA RDI, [RIP+0x5aef] */
    ctx->rdi = (uint64_t)(0x5b28ULL);
    /* 0x39: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x3b: CALL .+3956 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x40ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x40: LEA RAX, [RIP+0x5b8b] */
    ctx->rax = (uint64_t)(0x5bd2ULL);
    /* 0x47: MOV [RBP-0x10], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x10LL)) = (uint64_t)(ctx->rax);
    /* 0x4b: MOV RDI, [RBP-0x10] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x10LL)));
    /* 0x4f: MOV ESI, 0x601 */
ctx->rsi = (uint64_t)(uint32_t)(0x601ULL);
    /* 0x54: MOV EDX, 0x1a4 */
ctx->rdx = (uint64_t)(uint32_t)(0x1a4ULL);
    /* 0x59: CALL .+18962 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x5eULL;
    ctx->rip = 0x4a70ULL;
    fn_0x4a70(ctx);
    /* 0x5e: MOV [RBP-0x14], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x61: CMP [RBP-0x14], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x65: JGE .+29 */
    if (ctx->sf == ctx->of) goto loc_0x84;

loc_0x67:
    ctx->rip = 0x67ULL;
    /* 0x67: MOV ESI, [RBP-0x14] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x6a: LEA RDI, [RIP+0x57ef] */
    ctx->rdi = (uint64_t)(0x5860ULL);
    /* 0x71: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x73: CALL .+3900 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x78ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x78: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x7f: JMP .+1283 */
    goto loc_0x587;

loc_0x84:
    ctx->rip = 0x84ULL;
    /* 0x84: LEA RAX, [RIP+0x597f] */
    ctx->rax = (uint64_t)(0x5a0aULL);
    /* 0x8b: MOV [RBP-0x20], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x20LL)) = (uint64_t)(ctx->rax);
    /* 0x8f: MOV EAX, [RBP-0x14] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x92: MOV [RBP+0xfffffe84], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x17cLL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x98: MOV RAX, [RBP-0x20] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x20LL)));
    /* 0x9c: MOV [RBP+0xfffffe88], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x178LL)) = (uint64_t)(ctx->rax);
    /* 0xa3: MOV RDI, [RBP-0x20] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x20LL)));
    /* 0xa7: CALL .+17236 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0xacULL;
    ctx->rip = 0x4400ULL;
    fn_0x4400(ctx);
    /* 0xac: MOV EDI, [RBP+0xfffffe84] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x17cLL)));
    /* 0xb2: MOV RSI, [RBP+0xfffffe88] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x178LL)));
    /* 0xb9: MOV RDX, RAX */
ctx->rdx = (uint64_t)(ctx->rax);
    /* 0xbc: CALL .+18879 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0xc1ULL;
    ctx->rip = 0x4a80ULL;
    fn_0x4a80(ctx);
    /* 0xc1: MOV [RBP-0x28], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x28LL)) = (uint64_t)(ctx->rax);
    /* 0xc5: MOV RAX, [RBP-0x28] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x28LL)));
    /* 0xc9: MOV [RBP+0xfffffe90], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x170LL)) = (uint64_t)(ctx->rax);
    /* 0xd0: MOV RDI, [RBP-0x20] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x20LL)));
    /* 0xd4: CALL .+17191 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0xd9ULL;
    ctx->rip = 0x4400ULL;
    fn_0x4400(ctx);
    /* 0xd9: MOV RCX, RAX */
ctx->rcx = (uint64_t)(ctx->rax);
    /* 0xdc: MOV RAX, [RBP+0xfffffe90] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x170LL)));
    /* 0xe3: CMP RAX, RCX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0xe6: JE .+56 */
    if (ctx->zf) goto loc_0x120;

loc_0xe8:
    ctx->rip = 0xe8ULL;
    /* 0xe8: MOV RAX, [RBP-0x28] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x28LL)));
    /* 0xec: MOV [RBP+0xfffffe78], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x188LL)) = (uint64_t)(ctx->rax);
    /* 0xf3: MOV RDI, [RBP-0x20] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x20LL)));
    /* 0xf7: CALL .+17156 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0xfcULL;
    ctx->rip = 0x4400ULL;
    fn_0x4400(ctx);
    /* 0xfc: MOV RSI, [RBP+0xfffffe78] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x188LL)));
    /* 0x103: MOV RDX, RAX */
ctx->rdx = (uint64_t)(ctx->rax);
    /* 0x106: LEA RDI, [RIP+0x5929] */
    ctx->rdi = (uint64_t)(0x5a36ULL);
    /* 0x10d: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x10f: CALL .+3744 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x114ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x114: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x11b: JMP .+1127 */
    goto loc_0x587;

loc_0x120:
    ctx->rip = 0x120ULL;
    /* 0x120: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x123: CALL .+18792 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x128ULL;
    ctx->rip = 0x4a90ULL;
    fn_0x4a90(ctx);
    /* 0x128: MOV RDI, [RBP-0x10] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x10LL)));
    /* 0x12c: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x12e: MOV ESI, EDX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x130: CALL .+18747 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x135ULL;
    ctx->rip = 0x4a70ULL;
    fn_0x4a70(ctx);
    /* 0x135: MOV [RBP-0x14], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x138: CMP [RBP-0x14], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x13c: JGE .+29 */
    if (ctx->sf == ctx->of) goto loc_0x15b;

loc_0x13e:
    ctx->rip = 0x13eULL;
    /* 0x13e: MOV ESI, [RBP-0x14] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x141: LEA RDI, [RIP+0x5792] */
    ctx->rdi = (uint64_t)(0x58daULL);
    /* 0x148: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x14a: CALL .+3685 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x14fULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x14f: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x156: JMP .+1068 */
    goto loc_0x587;

loc_0x15b:
    ctx->rip = 0x15bULL;
    /* 0x15b: LEA RDI, [RBP+0xffffff50] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xb0LL));
    /* 0x162: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x164: MOV EDX, 0x80 */
ctx->rdx = (uint64_t)(uint32_t)(0x80ULL);
    /* 0x169: CALL .+16754 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x16eULL;
    ctx->rip = 0x42e0ULL;
    fn_0x42e0(ctx);
    /* 0x16e: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x171: LEA RSI, [RBP+0xffffff50] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0xb0LL));
    /* 0x178: MOV EDX, 0x7f */
ctx->rdx = (uint64_t)(uint32_t)(0x7fULL);
    /* 0x17d: CALL .+18718 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x182ULL;
    ctx->rip = 0x4aa0ULL;
    fn_0x4aa0(ctx);
    /* 0x182: MOV [RBP+0xffffff48], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0xb8LL)) = (uint64_t)(ctx->rax);
    /* 0x189: MOV RAX, [RBP+0xffffff48] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0xb8LL)));
    /* 0x190: MOV [RBP+0xfffffe70], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x190LL)) = (uint64_t)(ctx->rax);
    /* 0x197: MOV RDI, [RBP-0x20] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x20LL)));
    /* 0x19b: CALL .+16992 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1a0ULL;
    ctx->rip = 0x4400ULL;
    fn_0x4400(ctx);
    /* 0x1a0: MOV RCX, RAX */
ctx->rcx = (uint64_t)(ctx->rax);
    /* 0x1a3: MOV RAX, [RBP+0xfffffe70] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x190LL)));
    /* 0x1aa: CMP RAX, RCX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1ad: JNE .+21 */
    if (!ctx->zf) goto loc_0x1c4;

loc_0x1af:
    ctx->rip = 0x1afULL;
    /* 0x1af: LEA RDI, [RBP+0xffffff50] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xb0LL));
    /* 0x1b6: MOV RSI, [RBP-0x20] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x20LL)));
    /* 0x1ba: CALL .+16897 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1bfULL;
    ctx->rip = 0x43c0ULL;
    fn_0x43c0(ctx);
    /* 0x1bf: CMP EAX, 0x0 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1c2: JE .+33 */
    if (ctx->zf) goto loc_0x1e5;

loc_0x1c4:
    ctx->rip = 0x1c4ULL;
    /* 0x1c4: LEA RSI, [RBP+0xffffff50] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0xb0LL));
    /* 0x1cb: LEA RDI, [RIP+0x5a5f] */
    ctx->rdi = (uint64_t)(0x5c31ULL);
    /* 0x1d2: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x1d4: CALL .+3547 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1d9ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x1d9: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x1e0: JMP .+930 */
    goto loc_0x587;

loc_0x1e5:
    ctx->rip = 0x1e5ULL;
    /* 0x1e5: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x1e8: CALL .+18595 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1edULL;
    ctx->rip = 0x4a90ULL;
    fn_0x4a90(ctx);
    /* 0x1ed: MOV RDI, [RBP-0x10] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x10LL)));
    /* 0x1f1: CALL .+18618 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1f6ULL;
    ctx->rip = 0x4ab0ULL;
    fn_0x4ab0(ctx);
    /* 0x1f6: LEA RDI, [RIP+0x5907] */
    ctx->rdi = (uint64_t)(0x5b04ULL);
    /* 0x1fd: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x1ff: CALL .+3504 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x204ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x204: LEA RDI, [RBP+0xffffff10] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xf0LL));
    /* 0x20b: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x20d: MOV ESI, EAX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x20f: LEA RDX, [RIP+0x5ad7] */
    ctx->rdx = (uint64_t)(0x5cedULL);
    /* 0x216: CALL .+18597 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x21bULL;
    ctx->rip = 0x4ac0ULL;
    fn_0x4ac0(ctx);
    /* 0x21b: MOV [RBP+0xffffff0c], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x221: CMP [RBP+0xffffff0c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x228: JE .+32 */
    if (ctx->zf) goto loc_0x24a;

loc_0x22a:
    ctx->rip = 0x22aULL;
    /* 0x22a: MOV ESI, [RBP+0xffffff0c] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)));
    /* 0x230: LEA RDI, [RIP+0x5a8e] */
    ctx->rdi = (uint64_t)(0x5cc5ULL);
    /* 0x237: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x239: CALL .+3446 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x23eULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x23e: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x245: JMP .+829 */
    goto loc_0x587;

loc_0x24a:
    ctx->rip = 0x24aULL;
    /* 0x24a: LEA RDI, [RBP+0xffffff10] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xf0LL));
    /* 0x251: CALL .+18554 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x256ULL;
    ctx->rip = 0x4ad0ULL;
    fn_0x4ad0(ctx);
    /* 0x256: MOV [RBP+0xffffff0c], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x25c: CMP [RBP+0xffffff0c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x263: JE .+32 */
    if (ctx->zf) goto loc_0x285;

loc_0x265:
    ctx->rip = 0x265ULL;
    /* 0x265: MOV ESI, [RBP+0xffffff0c] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)));
    /* 0x26b: LEA RDI, [RIP+0x57fe] */
    ctx->rdi = (uint64_t)(0x5a70ULL);
    /* 0x272: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x274: CALL .+3387 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x279ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x279: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x280: JMP .+770 */
    goto loc_0x587;

loc_0x285:
    ctx->rip = 0x285ULL;
    /* 0x285: LEA RDI, [RBP+0xffffff10] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xf0LL));
    /* 0x28c: CALL .+18511 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x291ULL;
    ctx->rip = 0x4ae0ULL;
    fn_0x4ae0(ctx);
    /* 0x291: MOV [RBP+0xffffff0c], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x297: CMP [RBP+0xffffff0c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x29e: JE .+32 */
    if (ctx->zf) goto loc_0x2c0;

loc_0x2a0:
    ctx->rip = 0x2a0ULL;
    /* 0x2a0: MOV ESI, [RBP+0xffffff0c] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)));
    /* 0x2a6: LEA RDI, [RIP+0x593a] */
    ctx->rdi = (uint64_t)(0x5be7ULL);
    /* 0x2ad: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x2af: CALL .+3328 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2b4ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x2b4: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x2bb: JMP .+711 */
    goto loc_0x587;

loc_0x2c0:
    ctx->rip = 0x2c0ULL;
    /* 0x2c0: LEA RDI, [RBP+0xffffff10] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xf0LL));
    /* 0x2c7: CALL .+18468 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2ccULL;
    ctx->rip = 0x4af0ULL;
    fn_0x4af0(ctx);
    /* 0x2cc: MOV [RBP+0xffffff0c], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x2d2: CMP [RBP+0xffffff0c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2d9: JE .+32 */
    if (ctx->zf) goto loc_0x2fb;

loc_0x2db:
    ctx->rip = 0x2dbULL;
    /* 0x2db: MOV ESI, [RBP+0xffffff0c] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)));
    /* 0x2e1: LEA RDI, [RIP+0x559c] */
    ctx->rdi = (uint64_t)(0x5884ULL);
    /* 0x2e8: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x2ea: CALL .+3269 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2efULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x2ef: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x2f6: JMP .+652 */
    goto loc_0x587;

loc_0x2fb:
    ctx->rip = 0x2fbULL;
    /* 0x2fb: LEA RDI, [RBP+0xffffff10] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xf0LL));
    /* 0x302: CALL .+18393 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x307ULL;
    ctx->rip = 0x4ae0ULL;
    fn_0x4ae0(ctx);
    /* 0x307: MOV [RBP+0xffffff0c], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x30d: CMP [RBP+0xffffff0c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x314: JE .+32 */
    if (ctx->zf) goto loc_0x336;

loc_0x316:
    ctx->rip = 0x316ULL;
    /* 0x316: MOV ESI, [RBP+0xffffff0c] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)));
    /* 0x31c: LEA RDI, [RIP+0x567f] */
    ctx->rdi = (uint64_t)(0x59a2ULL);
    /* 0x323: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x325: CALL .+3210 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x32aULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x32a: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x331: JMP .+593 */
    goto loc_0x587;

loc_0x336:
    ctx->rip = 0x336ULL;
    /* 0x336: LEA RDI, [RBP+0xffffff10] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0xf0LL));
    /* 0x33d: CALL .+18366 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x342ULL;
    ctx->rip = 0x4b00ULL;
    fn_0x4b00(ctx);
    /* 0x342: LEA RDI, [RIP+0x5911] */
    ctx->rdi = (uint64_t)(0x5c5aULL);
    /* 0x349: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x34b: CALL .+3172 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x350ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x350: LEA RDI, [RBP+0xffffff00] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0x100LL));
    /* 0x357: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x359: MOV ECX, EAX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x35b: LEA RDX, [RIP+0x23e] */
    ctx->rdx = (uint64_t)(0x5a0ULL);
    /* 0x362: LEA R8, [RIP+0x55a8] */
    ctx->r8 = (uint64_t)(0x5911ULL);
    /* 0x369: MOV RSI, RCX */
ctx->rsi = (uint64_t)(ctx->rcx);
    /* 0x36c: CALL .+18335 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x371ULL;
    ctx->rip = 0x4b10ULL;
    fn_0x4b10(ctx);
    /* 0x371: MOV [RBP+0xffffff0c], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x377: CMP [RBP+0xffffff0c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x37e: JE .+32 */
    if (ctx->zf) goto loc_0x3a0;

loc_0x380:
    ctx->rip = 0x380ULL;
    /* 0x380: MOV ESI, [RBP+0xffffff0c] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0xf4LL)));
    /* 0x386: LEA RDI, [RIP+0x55f1] */
    ctx->rdi = (uint64_t)(0x597eULL);
    /* 0x38d: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x38f: CALL .+3104 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x394ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x394: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x39b: JMP .+487 */
    goto loc_0x587;

loc_0x3a0:
    ctx->rip = 0x3a0ULL;
    /* 0x3a0: MOV EDI, 0xc350 */
ctx->rdi = (uint64_t)(uint32_t)(0xc350ULL);
    /* 0x3a5: CALL .+18294 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3aaULL;
    ctx->rip = 0x4b20ULL;
    fn_0x4b20(ctx);
    /* 0x3aa: MOV EDI, 0x2 */
ctx->rdi = (uint64_t)(uint32_t)(0x2ULL);
    /* 0x3af: MOV ESI, 0x1 */
ctx->rsi = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x3b4: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x3b6: CALL .+2481 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3bbULL;
    ctx->rip = 0xd6cULL;
    fn_0xd6c(ctx);
    /* 0x3bb: MOV [RBP+0xfffffefc], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x104LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x3c1: CMP [RBP+0xfffffefc], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x104LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3c8: JGE .+33 */
    if (ctx->sf == ctx->of) goto loc_0x3eb;

loc_0x3ca:
    ctx->rip = 0x3caULL;
    /* 0x3ca: CALL .+1049 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3cfULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x3cf: MOV ESI, [RAX] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x3d1: LEA RDI, [RIP+0x56c5] */
    ctx->rdi = (uint64_t)(0x5a9dULL);
    /* 0x3d8: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x3da: CALL .+3029 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3dfULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x3df: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x3e6: JMP .+412 */
    goto loc_0x587;

loc_0x3eb:
    ctx->rip = 0x3ebULL;
    /* 0x3eb: LEA RDI, [RBP+0xfffffeec] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0x114LL));
    /* 0x3f2: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x3f4: MOV EDX, 0x10 */
ctx->rdx = (uint64_t)(uint32_t)(0x10ULL);
    /* 0x3f9: CALL .+16098 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3feULL;
    ctx->rip = 0x42e0ULL;
    fn_0x42e0(ctx);
    /* 0x3fe: MOV [RBP+0xfffffeec], 0x10 */
MEM_U8(ctx->rbp + (uint64_t)(-0x114LL)) = (uint8_t)(0x10ULL);
    /* 0x405: MOV [RBP+0xfffffeed], 0x2 */
MEM_U8(ctx->rbp + (uint64_t)(-0x113LL)) = (uint8_t)(0x2ULL);
    /* 0x40c: MOV EDI, 0x4da4 */
ctx->rdi = (uint64_t)(uint32_t)(0x4da4ULL);
    /* 0x411: CALL .+994 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x416ULL;
    ctx->rip = 0x7f8ULL;
    fn_0x7f8(ctx);
    /* 0x416: MOV [RBP+0xfffffeee], AX */
MEM_U16(ctx->rbp + (uint64_t)(-0x112LL)) = (uint16_t)(((uint16_t)ctx->rax));
    /* 0x41d: LEA RDX, [RBP+0xfffffeec] */
    ctx->rdx = (uint64_t)(ctx->rbp + (uint64_t)(-0x114LL));
    /* 0x424: ADD RDX, 0x4 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x428: MOV EDI, 0x2 */
ctx->rdi = (uint64_t)(uint32_t)(0x2ULL);
    /* 0x42d: LEA RSI, [RIP+0x54d3] */
    ctx->rsi = (uint64_t)(0x5907ULL);
    /* 0x434: CALL .+967 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x439ULL;
    ctx->rip = 0x800ULL;
    fn_0x800(ctx);
    /* 0x439: MOV EDI, [RBP+0xfffffefc] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x104LL)));
    /* 0x43f: LEA RSI, [RBP+0xfffffeec] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x114LL));
    /* 0x446: MOV EDX, 0x10 */
ctx->rdx = (uint64_t)(uint32_t)(0x10ULL);
    /* 0x44b: CALL .+18144 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x450ULL;
    ctx->rip = 0x4b30ULL;
    fn_0x4b30(ctx);
    /* 0x450: CMP EAX, 0x0 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x453: JE .+33 */
    if (ctx->zf) goto loc_0x476;

loc_0x455:
    ctx->rip = 0x455ULL;
    /* 0x455: CALL .+910 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x45aULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x45a: MOV ESI, [RAX] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x45c: LEA RDI, [RIP+0x572f] */
    ctx->rdi = (uint64_t)(0x5b92ULL);
    /* 0x463: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x465: CALL .+2890 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x46aULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x46a: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x471: JMP .+273 */
    goto loc_0x587;

loc_0x476:
    ctx->rip = 0x476ULL;
    /* 0x476: LEA RAX, [RIP+0x56e6] */
    ctx->rax = (uint64_t)(0x5b63ULL);
    /* 0x47d: MOV [RBP+0xfffffee0], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x120LL)) = (uint64_t)(ctx->rax);
    /* 0x484: MOV EAX, [RBP+0xfffffefc] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x104LL)));
    /* 0x48a: MOV [RBP+0xfffffe64], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x19cLL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x490: MOV RAX, [RBP+0xfffffee0] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x120LL)));
    /* 0x497: MOV [RBP+0xfffffe68], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x198LL)) = (uint64_t)(ctx->rax);
    /* 0x49e: MOV RDI, [RBP+0xfffffee0] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x120LL)));
    /* 0x4a5: CALL .+16214 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4aaULL;
    ctx->rip = 0x4400ULL;
    fn_0x4400(ctx);
    /* 0x4aa: MOV EDI, [RBP+0xfffffe64] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x19cLL)));
    /* 0x4b0: MOV RSI, [RBP+0xfffffe68] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x198LL)));
    /* 0x4b7: MOV RDX, RAX */
ctx->rdx = (uint64_t)(ctx->rax);
    /* 0x4ba: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x4bc: CALL .+2207 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4c1ULL;
    ctx->rip = 0xd60ULL;
    fn_0xd60(ctx);
    /* 0x4c1: LEA RDI, [RBP+0xfffffea0] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0x160LL));
    /* 0x4c8: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x4ca: MOV EDX, 0x40 */
ctx->rdx = (uint64_t)(uint32_t)(0x40ULL);
    /* 0x4cf: CALL .+15884 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4d4ULL;
    ctx->rip = 0x42e0ULL;
    fn_0x42e0(ctx);
    /* 0x4d4: MOV EDI, [RBP+0xfffffefc] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x104LL)));
    /* 0x4da: LEA RSI, [RBP+0xfffffea0] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x160LL));
    /* 0x4e1: MOV EDX, 0x3f */
ctx->rdx = (uint64_t)(uint32_t)(0x3fULL);
    /* 0x4e6: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x4e8: CALL .+2151 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4edULL;
    ctx->rip = 0xd54ULL;
    fn_0xd54(ctx);
    /* 0x4ed: MOV [RBP+0xfffffe98], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x168LL)) = (uint64_t)(ctx->rax);
    /* 0x4f4: CMP [RBP+0xfffffe98], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x168LL))); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x4fc: JLE .+24 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x516;

loc_0x4fe:
    ctx->rip = 0x4feULL;
    /* 0x4fe: LEA RDI, [RBP+0xfffffea0] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0x160LL));
    /* 0x505: LEA RSI, [RIP+0x576f] */
    ctx->rsi = (uint64_t)(0x5c7bULL);
    /* 0x50c: CALL .+16047 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x511ULL;
    ctx->rip = 0x43c0ULL;
    fn_0x43c0(ctx);
    /* 0x511: CMP EAX, 0x0 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x514: JE .+30 */
    if (ctx->zf) goto loc_0x534;

loc_0x516:
    ctx->rip = 0x516ULL;
    /* 0x516: LEA RSI, [RBP+0xfffffea0] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x160LL));
    /* 0x51d: LEA RDI, [RIP+0x576f] */
    ctx->rdi = (uint64_t)(0x5c93ULL);
    /* 0x524: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x526: CALL .+2697 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x52bULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x52b: MOV [RBP-0x4], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x1ULL);
    /* 0x532: JMP .+83 */
    goto loc_0x587;

loc_0x534:
    ctx->rip = 0x534ULL;
    /* 0x534: LEA RSI, [RBP+0xfffffea0] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x160LL));
    /* 0x53b: LEA RDI, [RIP+0x53d9] */
    ctx->rdi = (uint64_t)(0x591bULL);
    /* 0x542: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x544: CALL .+2667 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x549ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x549: MOV EDI, [RBP+0xfffffefc] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x104LL)));
    /* 0x54f: CALL .+17900 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x554ULL;
    ctx->rip = 0x4b40ULL;
    fn_0x4b40(ctx);
    /* 0x554: MOV RDI, [RBP+0xffffff00] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x100LL)));
    /* 0x55b: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x55d: MOV ESI, EAX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x55f: CALL .+17900 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x564ULL;
    ctx->rip = 0x4b50ULL;
    fn_0x4b50(ctx);
    /* 0x564: LEA RDI, [RIP+0x5468] */
    ctx->rdi = (uint64_t)(0x59d3ULL);
    /* 0x56b: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x56d: CALL .+2626 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x572ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x572: LEA RDI, [RIP+0x53c3] */
    ctx->rdi = (uint64_t)(0x593cULL);
    /* 0x579: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x57b: CALL .+2612 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x580ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x580: MOV [RBP-0x4], 0x0 */
MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)) = (uint32_t)(0x0ULL);

loc_0x587:
    ctx->rip = 0x587ULL;
    /* 0x587: MOV EAX, [RBP-0x4] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x4LL)));
    /* 0x58a: ADD RSP, 0x1a0 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x1a0ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x591: POP RBP */
    ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x592: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function server_thread_func at 0x5a0
void fn_0x5a0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x5a0ULL) {
        switch (ctx->rip) {
            case 0x5c9ULL: goto loc_0x5c9;
            case 0x5eeULL: goto loc_0x5ee;
            case 0x658ULL: goto loc_0x658;
            case 0x685ULL: goto loc_0x685;
            case 0x697ULL: goto loc_0x697;
            case 0x6c4ULL: goto loc_0x6c4;
            case 0x6f7ULL: goto loc_0x6f7;
            case 0x724ULL: goto loc_0x724;
            case 0x766ULL: goto loc_0x766;
            case 0x7c0ULL: goto loc_0x7c0;
            case 0x7d8ULL: goto loc_0x7d8;
            default: goto loc_0x5a0;
        }
    }

loc_0x5a0:
    ctx->rip = 0x5a0ULL;
    /* 0x5a0: PUSH RBP */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbp);
    /* 0x5a1: MOV RBP, RSP */
ctx->rbp = (uint64_t)(ctx->rsp);
    /* 0x5a4: SUB RSP, 0xa0 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0xa0ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x5ab: MOV [RBP-0x10], RDI */
MEM_U64(ctx->rbp + (uint64_t)(-0x10LL)) = (uint64_t)(ctx->rdi);
    /* 0x5af: MOV EDI, 0x2 */
ctx->rdi = (uint64_t)(uint32_t)(0x2ULL);
    /* 0x5b4: MOV ESI, 0x1 */
ctx->rsi = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x5b9: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x5bb: CALL .+1964 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x5c0ULL;
    ctx->rip = 0xd6cULL;
    fn_0xd6c(ctx);
    /* 0x5c0: MOV [RBP-0x14], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x5c3: CMP [RBP-0x14], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x5c7: JGE .+37 */
    if (ctx->sf == ctx->of) goto loc_0x5ee;

loc_0x5c9:
    ctx->rip = 0x5c9ULL;
    /* 0x5c9: CALL .+538 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x5ceULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x5ce: MOV ESI, [RAX] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x5d0: LEA RDI, [RIP+0x54e9] */
    ctx->rdi = (uint64_t)(0x5ac0ULL);
    /* 0x5d7: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x5d9: CALL .+2518 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x5deULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x5de: MOV RAX, -0x1 */
ctx->rax = (uint64_t)(0xffffffffffffffffULL);
    /* 0x5e5: MOV [RBP-0x8], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x8LL)) = (uint64_t)(ctx->rax);
    /* 0x5e9: JMP .+490 */
    goto loc_0x7d8;

loc_0x5ee:
    ctx->rip = 0x5eeULL;
    /* 0x5ee: MOV [RBP-0x18], 0x1 */
MEM_U32(ctx->rbp + (uint64_t)(-0x18LL)) = (uint32_t)(0x1ULL);
    /* 0x5f5: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x5f8: MOV ESI, 0xffff */
ctx->rsi = (uint64_t)(uint32_t)(0xffffULL);
    /* 0x5fd: MOV R8L, 0x4 */
ctx->r8 = (uint64_t)(uint32_t)(0x4ULL);
    /* 0x603: LEA RCX, [RBP-0x18] */
    ctx->rcx = (uint64_t)(ctx->rbp + (uint64_t)(-0x18LL));
    /* 0x607: MOV EDX, R8L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r8));
    /* 0x60a: CALL .+17745 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x60fULL;
    ctx->rip = 0x4b60ULL;
    fn_0x4b60(ctx);
    /* 0x60f: LEA RDI, [RBP-0x28] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0x28LL));
    /* 0x613: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x615: MOV EDX, 0x10 */
ctx->rdx = (uint64_t)(uint32_t)(0x10ULL);
    /* 0x61a: CALL .+15553 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x61fULL;
    ctx->rip = 0x42e0ULL;
    fn_0x42e0(ctx);
    /* 0x61f: MOV [RBP-0x28], 0x10 */
MEM_U8(ctx->rbp + (uint64_t)(-0x28LL)) = (uint8_t)(0x10ULL);
    /* 0x623: MOV [RBP-0x27], 0x2 */
MEM_U8(ctx->rbp + (uint64_t)(-0x27LL)) = (uint8_t)(0x2ULL);
    /* 0x627: MOV EDI, 0x4da4 */
ctx->rdi = (uint64_t)(uint32_t)(0x4da4ULL);
    /* 0x62c: CALL .+455 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x631ULL;
    ctx->rip = 0x7f8ULL;
    fn_0x7f8(ctx);
    /* 0x631: MOV [RBP-0x26], AX */
MEM_U16(ctx->rbp + (uint64_t)(-0x26LL)) = (uint16_t)(((uint16_t)ctx->rax));
    /* 0x635: MOV EDI, 0x7f000001 */
ctx->rdi = (uint64_t)(uint32_t)(0x7f000001ULL);
    /* 0x63a: CALL .+433 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x63fULL;
    ctx->rip = 0x7f0ULL;
    fn_0x7f0(ctx);
    /* 0x63f: MOV [RBP-0x24], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x24LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x642: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x645: LEA RSI, [RBP-0x28] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x28LL));
    /* 0x649: MOV EDX, 0x10 */
ctx->rdx = (uint64_t)(uint32_t)(0x10ULL);
    /* 0x64e: CALL .+17693 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x653ULL;
    ctx->rip = 0x4b70ULL;
    fn_0x4b70(ctx);
    /* 0x653: CMP EAX, 0x0 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x656: JE .+45 */
    if (ctx->zf) goto loc_0x685;

loc_0x658:
    ctx->rip = 0x658ULL;
    /* 0x658: CALL .+395 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x65dULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x65d: MOV ESI, [RAX] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x65f: LEA RDI, [RIP+0x5550] */
    ctx->rdi = (uint64_t)(0x5bb6ULL);
    /* 0x666: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x668: CALL .+2375 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x66dULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x66d: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x670: CALL .+17611 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x675ULL;
    ctx->rip = 0x4b40ULL;
    fn_0x4b40(ctx);
    /* 0x675: MOV RAX, -0x1 */
ctx->rax = (uint64_t)(0xffffffffffffffffULL);
    /* 0x67c: MOV [RBP-0x8], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x8LL)) = (uint64_t)(ctx->rax);
    /* 0x680: JMP .+339 */
    goto loc_0x7d8;

loc_0x685:
    ctx->rip = 0x685ULL;
    /* 0x685: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x688: MOV ESI, 0x1 */
ctx->rsi = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x68d: CALL .+17646 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x692ULL;
    ctx->rip = 0x4b80ULL;
    fn_0x4b80(ctx);
    /* 0x692: CMP EAX, 0x0 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x695: JE .+45 */
    if (ctx->zf) goto loc_0x6c4;

loc_0x697:
    ctx->rip = 0x697ULL;
    /* 0x697: CALL .+332 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x69cULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x69c: MOV ESI, [RAX] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x69e: LEA RDI, [RIP+0x54cf] */
    ctx->rdi = (uint64_t)(0x5b74ULL);
    /* 0x6a5: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x6a7: CALL .+2312 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x6acULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x6ac: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x6af: CALL .+17548 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x6b4ULL;
    ctx->rip = 0x4b40ULL;
    fn_0x4b40(ctx);
    /* 0x6b4: MOV RAX, -0x1 */
ctx->rax = (uint64_t)(0xffffffffffffffffULL);
    /* 0x6bb: MOV [RBP-0x8], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x8LL)) = (uint64_t)(ctx->rax);
    /* 0x6bf: JMP .+276 */
    goto loc_0x7d8;

loc_0x6c4:
    ctx->rip = 0x6c4ULL;
    /* 0x6c4: LEA RDI, [RIP+0x51e3] */
    ctx->rdi = (uint64_t)(0x58aeULL);
    /* 0x6cb: MOV ESI, 0x4da4 */
ctx->rsi = (uint64_t)(uint32_t)(0x4da4ULL);
    /* 0x6d0: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x6d2: CALL .+2269 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x6d7ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x6d7: MOV [RBP-0x3c], 0x10 */
MEM_U32(ctx->rbp + (uint64_t)(-0x3cLL)) = (uint32_t)(0x10ULL);
    /* 0x6de: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x6e1: LEA RSI, [RBP-0x38] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x38LL));
    /* 0x6e5: LEA RDX, [RBP-0x3c] */
    ctx->rdx = (uint64_t)(ctx->rbp + (uint64_t)(-0x3cLL));
    /* 0x6e9: CALL .+17570 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x6eeULL;
    ctx->rip = 0x4b90ULL;
    fn_0x4b90(ctx);
    /* 0x6ee: MOV [RBP-0x40], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x40LL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x6f1: CMP [RBP-0x40], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x40LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x6f5: JGE .+45 */
    if (ctx->sf == ctx->of) goto loc_0x724;

loc_0x6f7:
    ctx->rip = 0x6f7ULL;
    /* 0x6f7: CALL .+236 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x6fcULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x6fc: MOV ESI, [RAX] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x6fe: LEA RDI, [RIP+0x55f4] */
    ctx->rdi = (uint64_t)(0x5cf9ULL);
    /* 0x705: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x707: CALL .+2216 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x70cULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x70c: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x70f: CALL .+17452 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x714ULL;
    ctx->rip = 0x4b40ULL;
    fn_0x4b40(ctx);
    /* 0x714: MOV RAX, -0x1 */
ctx->rax = (uint64_t)(0xffffffffffffffffULL);
    /* 0x71b: MOV [RBP-0x8], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x8LL)) = (uint64_t)(ctx->rax);
    /* 0x71f: JMP .+180 */
    goto loc_0x7d8;

loc_0x724:
    ctx->rip = 0x724ULL;
    /* 0x724: LEA RDI, [RIP+0x53b3] */
    ctx->rdi = (uint64_t)(0x5adeULL);
    /* 0x72b: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x72d: CALL .+2178 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x732ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x732: LEA RDI, [RBP-0x80] */
    ctx->rdi = (uint64_t)(ctx->rbp + (uint64_t)(-0x80LL));
    /* 0x736: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x738: MOV EDX, 0x40 */
ctx->rdx = (uint64_t)(uint32_t)(0x40ULL);
    /* 0x73d: CALL .+15262 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x742ULL;
    ctx->rip = 0x42e0ULL;
    fn_0x42e0(ctx);
    /* 0x742: MOV EDI, [RBP-0x40] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x40LL)));
    /* 0x745: LEA RSI, [RBP-0x80] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x80LL));
    /* 0x749: MOV EDX, 0x3f */
ctx->rdx = (uint64_t)(uint32_t)(0x3fULL);
    /* 0x74e: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x750: CALL .+1535 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x755ULL;
    ctx->rip = 0xd54ULL;
    fn_0xd54(ctx);
    /* 0x755: MOV [RBP+0xffffff78], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x88LL)) = (uint64_t)(ctx->rax);
    /* 0x75c: CMP [RBP+0xffffff78], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x88LL))); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x764: JLE .+90 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x7c0;

loc_0x766:
    ctx->rip = 0x766ULL;
    /* 0x766: LEA RSI, [RBP-0x80] */
    ctx->rsi = (uint64_t)(ctx->rbp + (uint64_t)(-0x80LL));
    /* 0x76a: LEA RDI, [RIP+0x54a7] */
    ctx->rdi = (uint64_t)(0x5c18ULL);
    /* 0x771: MOV AL, 0x0 */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(0x0ULL));
    /* 0x773: CALL .+2108 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x778ULL;
    ctx->rip = 0xfb4ULL;
    fn_0xfb4(ctx);
    /* 0x778: LEA RAX, [RIP+0x54fc] */
    ctx->rax = (uint64_t)(0x5c7bULL);
    /* 0x77f: MOV [RBP+0xffffff70], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x90LL)) = (uint64_t)(ctx->rax);
    /* 0x786: MOV EAX, [RBP-0x40] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x40LL)));
    /* 0x789: MOV [RBP+0xffffff64], EAX */
MEM_U32(ctx->rbp + (uint64_t)(-0x9cLL)) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x78f: MOV RAX, [RBP+0xffffff70] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x90LL)));
    /* 0x796: MOV [RBP+0xffffff68], RAX */
MEM_U64(ctx->rbp + (uint64_t)(-0x98LL)) = (uint64_t)(ctx->rax);
    /* 0x79d: MOV RDI, [RBP+0xffffff70] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x90LL)));
    /* 0x7a4: CALL .+15447 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x7a9ULL;
    ctx->rip = 0x4400ULL;
    fn_0x4400(ctx);
    /* 0x7a9: MOV EDI, [RBP+0xffffff64] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x9cLL)));
    /* 0x7af: MOV RSI, [RBP+0xffffff68] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x98LL)));
    /* 0x7b6: MOV RDX, RAX */
ctx->rdx = (uint64_t)(ctx->rax);
    /* 0x7b9: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x7bb: CALL .+1440 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x7c0ULL;
    ctx->rip = 0xd60ULL;
    fn_0xd60(ctx);

loc_0x7c0:
    ctx->rip = 0x7c0ULL;
    /* 0x7c0: MOV EDI, [RBP-0x40] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x40LL)));
    /* 0x7c3: CALL .+17272 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x7c8ULL;
    ctx->rip = 0x4b40ULL;
    fn_0x4b40(ctx);
    /* 0x7c8: MOV EDI, [RBP-0x14] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x14LL)));
    /* 0x7cb: CALL .+17264 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x7d0ULL;
    ctx->rip = 0x4b40ULL;
    fn_0x4b40(ctx);
    /* 0x7d0: MOV [RBP-0x8], 0x0 */
MEM_U64(ctx->rbp + (uint64_t)(-0x8LL)) = (uint64_t)(0x0ULL);

loc_0x7d8:
    ctx->rip = 0x7d8ULL;
    /* 0x7d8: MOV RAX, [RBP-0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbp + (uint64_t)(-0x8LL)));
    /* 0x7dc: ADD RSP, 0xa0 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0xa0ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x7e3: POP RBP */
    ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x7e4: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function ___errno_location at 0x7e8
void fn_0x7e8(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x7e8:
    ctx->rip = 0x7e8ULL;
    /* 0x7e8: JMP .+17331 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x4ba0ULL;
    fn_0x4ba0(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function htonl at 0x7f0
void fn_0x7f0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x7f0:
    ctx->rip = 0x7f0ULL;
    /* 0x7f0: MOV EAX, EDI */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));
    /* 0x7f2: BSWAP EAX */
ctx->rax = (uint64_t)(uint32_t)(__builtin_bswap32((uint32_t)(((uint32_t)ctx->rax))));
    /* 0x7f4: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function htons at 0x7f8
void fn_0x7f8(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x7f8:
    ctx->rip = 0x7f8ULL;
    /* 0x7f8: MOV EAX, EDI */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));
    /* 0x7fa: ROL AX, 0x8 */
    { uint16_t a = (uint16_t)(((uint16_t)ctx->rax)); uint8_t count = ((uint8_t)(0x8ULL)) & 15; uint16_t res;
      res = (a << count) | (a >> ((16 - count) & 15));
      ctx->cf = res & 1;
    ctx->rax = (ctx->rax & ~0xffffULL) | ((uint64_t)(uint16_t)(res));
    }
    /* 0x7fe: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function inet_pton at 0x800 (Forwarded directly to host shim shim_inet_pton)
void fn_0x800(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_inet_pton(ctx);
    return;
}

// Function recv at 0xd54 (Forwarded directly to host shim shim_recv)
void fn_0xd54(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_recv(ctx);
    return;
}

// Function send at 0xd60 (Forwarded directly to host shim shim_send)
void fn_0xd60(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_send(ctx);
    return;
}

// Function socket at 0xd6c (Forwarded directly to host shim shim_socket)
void fn_0xd6c(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_socket(ctx);
    return;
}

// Function fcntl at 0xe10 (Forwarded directly to host shim shim_fcntl)
void fn_0xe10(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_fcntl(ctx);
    return;
}

// Function printf at 0xfb4
void fn_0xfb4(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0xfb4ULL) {
        switch (ctx->rip) {
            case 0xfdbULL: goto loc_0xfdb;
            case 0x1012ULL: goto loc_0x1012;
            default: goto loc_0xfb4;
        }
    }

loc_0xfb4:
    ctx->rip = 0xfb4ULL;
    /* 0xfb4: SUB RSP, 0xd8 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0xd8ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0xfbb: MOV R10, RDI */
ctx->r10 = (uint64_t)(ctx->rdi);
    /* 0xfbe: LEA RDI, [RSP+Reg(0)+0x20] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x20ULL);
    /* 0xfc3: MOV [RDI+0x8], RSI */
MEM_U64(ctx->rdi + 0x8ULL) = (uint64_t)(ctx->rsi);
    /* 0xfc7: MOV [RDI+0x10], RDX */
MEM_U64(ctx->rdi + 0x10ULL) = (uint64_t)(ctx->rdx);
    /* 0xfcb: MOV [RDI+0x18], RCX */
MEM_U64(ctx->rdi + 0x18ULL) = (uint64_t)(ctx->rcx);
    /* 0xfcf: MOV [RDI+0x20], R8 */
MEM_U64(ctx->rdi + 0x20ULL) = (uint64_t)(ctx->r8);
    /* 0xfd3: MOV [RDI+0x28], R9 */
MEM_U64(ctx->rdi + 0x28ULL) = (uint64_t)(ctx->r9);
    /* 0xfd7: TEST AL, AL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0xfd9: JE .+55 */
    if (ctx->zf) goto loc_0x1012;

loc_0xfdb:
    ctx->rip = 0xfdbULL;
    /* 0xfdb: MOVAPS [RSP+Reg(0)+0x50], X0 */
    memcpy(ctx->mem_base + (ctx->rsp + 0x50ULL), &ctx->xmm[0], 16);
    /* 0xfe0: MOVAPS [RSP+Reg(0)+0x60], X1 */
    memcpy(ctx->mem_base + (ctx->rsp + 0x60ULL), &ctx->xmm[1], 16);
    /* 0xfe5: MOVAPS [RSP+Reg(0)+0x70], X2 */
    memcpy(ctx->mem_base + (ctx->rsp + 0x70ULL), &ctx->xmm[2], 16);
    /* 0xfea: MOVAPS [RSP+Reg(0)+0x80], X3 */
    memcpy(ctx->mem_base + (ctx->rsp + 0x80ULL), &ctx->xmm[3], 16);
    /* 0xff2: MOVAPS [RSP+Reg(0)+0x90], X4 */
    memcpy(ctx->mem_base + (ctx->rsp + 0x90ULL), &ctx->xmm[4], 16);
    /* 0xffa: MOVAPS [RSP+Reg(0)+0xa0], X5 */
    memcpy(ctx->mem_base + (ctx->rsp + 0xa0ULL), &ctx->xmm[5], 16);
    /* 0x1002: MOVAPS [RSP+Reg(0)+0xb0], X6 */
    memcpy(ctx->mem_base + (ctx->rsp + 0xb0ULL), &ctx->xmm[6], 16);
    /* 0x100a: MOVAPS [RSP+Reg(0)+0xc0], X7 */
    memcpy(ctx->mem_base + (ctx->rsp + 0xc0ULL), &ctx->xmm[7], 16);

loc_0x1012:
    ctx->rip = 0x1012ULL;
    /* 0x1012: MOV RDX, RSP */
ctx->rdx = (uint64_t)(ctx->rsp);
    /* 0x1015: MOV [RDX+0x10], RDI */
MEM_U64(ctx->rdx + 0x10ULL) = (uint64_t)(ctx->rdi);
    /* 0x1019: LEA RAX, [RSP+Reg(0)+0xe0] */
    ctx->rax = (uint64_t)(ctx->rsp + 0xe0ULL);
    /* 0x1021: MOV [RDX+0x8], RAX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rax);
    /* 0x1025: MOV RAX, 0x3000000008 */
ctx->rax = (uint64_t)(0x3000000008ULL);
    /* 0x102f: MOV [RDX], RAX */
MEM_U64(ctx->rdx) = (uint64_t)(ctx->rax);
    /* 0x1032: LEA RDI, [RIP+0x1303f] */
    ctx->rdi = (uint64_t)(0x14078ULL);
    /* 0x1039: MOV RSI, R10 */
ctx->rsi = (uint64_t)(ctx->r10);
    /* 0x103c: CALL .+667 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1041ULL;
    ctx->rip = 0x12dcULL;
    fn_0x12dc(ctx);
    /* 0x1041: ADD RSP, 0xd8 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0xd8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x1048: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function dummy at 0x104c
void fn_0x104c(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x104c:
    ctx->rip = 0x104cULL;
    /* 0x104c: MOV EAX, EDI */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));
    /* 0x104e: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __stdio_close at 0x104f
void fn_0x104f(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x104f:
    ctx->rip = 0x104fULL;
    /* 0x104f: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x1050: MOV EDI, [RDI+0x78] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rdi + 0x78ULL));
    /* 0x1053: CALL .-12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1058ULL;
    ctx->rip = 0x104cULL;
    fn_0x104c(ctx);
    /* 0x1058: MOV EDI, EAX */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x105a: POP RAX */
    ctx->rax = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x105b: JMP .+15072 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x4b40ULL;
    fn_0x4b40(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function __stdout_write at 0x1060
void fn_0x1060(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x1060ULL) {
        switch (ctx->rip) {
            case 0x1082ULL: goto loc_0x1082;
            case 0x109bULL: goto loc_0x109b;
            case 0x10a6ULL: goto loc_0x10a6;
            default: goto loc_0x1060;
        }
    }

loc_0x1060:
    ctx->rip = 0x1060ULL;
    /* 0x1060: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x1062: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x1064: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x1065: SUB RSP, 0x10 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x10ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x1069: MOV RBX, RDX */
ctx->rbx = (uint64_t)(ctx->rdx);
    /* 0x106c: MOV R14, RSI */
ctx->r14 = (uint64_t)(ctx->rsi);
    /* 0x106f: MOV R15, RDI */
ctx->r15 = (uint64_t)(ctx->rdi);
    /* 0x1072: LEA RAX, [RIP+0x47] */
    ctx->rax = (uint64_t)(0x10c0ULL);
    /* 0x1079: MOV [RDI+0x48], RAX */
MEM_U64(ctx->rdi + 0x48ULL) = (uint64_t)(ctx->rax);
    /* 0x107d: TEST [RDI], 0x40 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rdi)); uint8_t b = (uint8_t)(0x40ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1080: JNE .+36 */
    if (!ctx->zf) goto loc_0x10a6;

loc_0x1082:
    ctx->rip = 0x1082ULL;
    /* 0x1082: MOV EDI, [R15+0x78] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->r15 + 0x78ULL));
    /* 0x1086: LEA RDX, [RSP+Reg(0)+0x8] */
    ctx->rdx = (uint64_t)(ctx->rsp + 0x8ULL);
    /* 0x108b: MOV ESI, 0x5413 */
ctx->rsi = (uint64_t)(uint32_t)(0x5413ULL);
    /* 0x1090: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x1092: CALL .+393 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1097ULL;
    ctx->rip = 0x1220ULL;
    fn_0x1220(ctx);
    /* 0x1097: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1099: JE .+11 */
    if (ctx->zf) goto loc_0x10a6;

loc_0x109b:
    ctx->rip = 0x109bULL;
    /* 0x109b: MOV [R15+0x90], -0x1 */
MEM_U32(ctx->r15 + 0x90ULL) = (uint32_t)(0xffffffffffffffffULL);

loc_0x10a6:
    ctx->rip = 0x10a6ULL;
    /* 0x10a6: MOV RDI, R15 */
ctx->rdi = (uint64_t)(ctx->r15);
    /* 0x10a9: MOV RSI, R14 */
ctx->rsi = (uint64_t)(ctx->r14);
    /* 0x10ac: MOV RDX, RBX */
ctx->rdx = (uint64_t)(ctx->rbx);
    /* 0x10af: CALL .+12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x10b4ULL;
    ctx->rip = 0x10c0ULL;
    fn_0x10c0(ctx);
    /* 0x10b4: ADD RSP, 0x10 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x10ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x10b8: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x10b9: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x10bb: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x10bd: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __stdio_write at 0x10c0
void fn_0x10c0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x10c0ULL) {
        switch (ctx->rip) {
            case 0x10ffULL: goto loc_0x10ff;
            case 0x1111ULL: goto loc_0x1111;
            case 0x1128ULL: goto loc_0x1128;
            case 0x112cULL: goto loc_0x112c;
            case 0x1132ULL: goto loc_0x1132;
            case 0x1145ULL: goto loc_0x1145;
            case 0x114eULL: goto loc_0x114e;
            case 0x1169ULL: goto loc_0x1169;
            case 0x116bULL: goto loc_0x116b;
            case 0x1172ULL: goto loc_0x1172;
            case 0x1179ULL: goto loc_0x1179;
            case 0x1183ULL: goto loc_0x1183;
            case 0x118aULL: goto loc_0x118a;
            case 0x11bbULL: goto loc_0x11bb;
            case 0x11c0ULL: goto loc_0x11c0;
            case 0x11c5ULL: goto loc_0x11c5;
            case 0x11e8ULL: goto loc_0x11e8;
            case 0x1204ULL: goto loc_0x1204;
            case 0x1208ULL: goto loc_0x1208;
            case 0x1211ULL: goto loc_0x1211;
            default: goto loc_0x10c0;
        }
    }

loc_0x10c0:
    ctx->rip = 0x10c0ULL;
    /* 0x10c0: PUSH RBP */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbp);
    /* 0x10c1: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x10c3: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x10c5: PUSH R13 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r13);
    /* 0x10c7: PUSH R12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r12);
    /* 0x10c9: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x10ca: SUB RSP, 0x48 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x48ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x10ce: MOV RAX, [RDI+0x38] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdi + 0x38ULL));
    /* 0x10d2: LEA R15, [RSP+Reg(0)+0x20] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x20ULL);
    /* 0x10d7: MOV [R15], RAX */
MEM_U64(ctx->r15) = (uint64_t)(ctx->rax);
    /* 0x10da: MOV [RSP+Reg(0)+0x8], RDI */
MEM_U64(ctx->rsp + 0x8ULL) = (uint64_t)(ctx->rdi);
    /* 0x10df: MOV RDI, [RDI+0x28] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rdi + 0x28ULL));
    /* 0x10e3: SUB RDI, RAX */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x10e6: MOV [R15+0x8], RDI */
MEM_U64(ctx->r15 + 0x8ULL) = (uint64_t)(ctx->rdi);
    /* 0x10ea: MOV [R15+0x10], RSI */
MEM_U64(ctx->r15 + 0x10ULL) = (uint64_t)(ctx->rsi);
    /* 0x10ee: MOV [R15+0x18], RDX */
MEM_U64(ctx->r15 + 0x18ULL) = (uint64_t)(ctx->rdx);
    /* 0x10f2: MOV [RSP+Reg(0)+0x18], RDX */
MEM_U64(ctx->rsp + 0x18ULL) = (uint64_t)(ctx->rdx);
    /* 0x10f7: ADD RDI, RDX */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x10fa: MOV EDX, 0x2 */
ctx->rdx = (uint64_t)(uint32_t)(0x2ULL);

loc_0x10ff:
    ctx->rip = 0x10ffULL;
    /* 0x10ff: MOV RAX, [RSP+Reg(0)+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x8ULL));
    /* 0x1104: MOV R13L, [RAX+0x78] */
ctx->r13 = (uint64_t)(uint32_t)(MEM_U32(ctx->rax + 0x78ULL));
    /* 0x1108: LEA EAX, [R13-0x3] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->r13 + (uint64_t)(-0x3LL));
    /* 0x110c: CMP EAX, -0x3 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xfffffffffffffffdULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x110f: JA .+23 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x1128;

loc_0x1111:
    ctx->rip = 0x1111ULL;
    /* 0x1111: MOV [RSP+Reg(0)+0x10], RDI */
MEM_U64(ctx->rsp + 0x10ULL) = (uint64_t)(ctx->rdi);
    /* 0x1116: MOV EDI, R13L */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1119: MOV RSI, R15 */
ctx->rsi = (uint64_t)(ctx->r15);
    /* 0x111c: MOV EBP, EDX */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x111e: CALL .+15069 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1123ULL;
    ctx->rip = 0x4c00ULL;
    fn_0x4c00(ctx);
    /* 0x1123: MOV R12, RAX */
ctx->r12 = (uint64_t)(ctx->rax);
    /* 0x1126: JMP .+81 */
    goto loc_0x1179;

loc_0x1128:
    ctx->rip = 0x1128ULL;
    /* 0x1128: TEST EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x112a: JS .+63 */
    if (ctx->sf) goto loc_0x116b;

loc_0x112c:
    ctx->rip = 0x112cULL;
    /* 0x112c: JE .+137 */
    if (ctx->zf) goto loc_0x11bb;

loc_0x1132:
    ctx->rip = 0x1132ULL;
    /* 0x1132: MOV [RSP+Reg(0)+0x10], RDI */
MEM_U64(ctx->rsp + 0x10ULL) = (uint64_t)(ctx->rdi);
    /* 0x1137: MOV EBP, EDX */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x1139: MOV R14L, EDX */
ctx->r14 = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x113c: SHL R14, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r14); uint8_t count = ((uint8_t)(0x4ULL)) & 0x3f; uint64_t res;
      res = a << count;
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x1140: XOR EBX, EBX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t res;
      res = a ^ b;
    ctx->rbx = (uint64_t)(uint32_t)(res);
    }
    /* 0x1142: XOR R12L, R12L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(((uint32_t)ctx->r12)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }

loc_0x1145:
    ctx->rip = 0x1145ULL;
    /* 0x1145: MOV RSI, [R15+RBX] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->r15 + ctx->rbx));
    /* 0x1149: TEST RSI, RSI */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(ctx->rsi); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x114c: JE .+36 */
    if (ctx->zf) goto loc_0x1172;

loc_0x114e:
    ctx->rip = 0x114eULL;
    /* 0x114e: MOV RDX, [R15+RBX+0x8] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->r15 + ctx->rbx + 0x8ULL));
    /* 0x1153: MOV EDI, R13L */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1156: CALL .+15029 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x115bULL;
    ctx->rip = 0x4c10ULL;
    fn_0x4c10(ctx);
    /* 0x115b: ADD R12, [R15+RBX+0x8] */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(MEM_U64(ctx->r15 + ctx->rbx + 0x8ULL)); uint64_t res;
      res = a + b;
    ctx->r12 = (uint64_t)(res);
    }
    /* 0x1160: ADD RBX, 0x10 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0x10ULL); uint64_t res;
      res = a + b;
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x1164: CMP R14, RBX */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1167: JNE .-36 */
    if (!ctx->zf) goto loc_0x1145;

loc_0x1169:
    ctx->rip = 0x1169ULL;
    /* 0x1169: JMP .+14 */
    goto loc_0x1179;

loc_0x116b:
    ctx->rip = 0x116bULL;
    /* 0x116b: MOV EBP, EDX */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x116d: MOV [RSP+Reg(0)+0x10], RDI */
MEM_U64(ctx->rsp + 0x10ULL) = (uint64_t)(ctx->rdi);

loc_0x1172:
    ctx->rip = 0x1172ULL;
    /* 0x1172: MOV R12, -0x1 */
ctx->r12 = (uint64_t)(0xffffffffffffffffULL);

loc_0x1179:
    ctx->rip = 0x1179ULL;
    /* 0x1179: MOV RDI, [RSP+Reg(0)+0x10] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0x10ULL));
    /* 0x117e: CMP R12, RDI */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1181: JE .+66 */
    if (ctx->zf) goto loc_0x11c5;

loc_0x1183:
    ctx->rip = 0x1183ULL;
    /* 0x1183: MOV EDX, EBP */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x1185: TEST R12, R12 */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x1188: JS .+94 */
    if (ctx->sf) goto loc_0x11e8;

loc_0x118a:
    ctx->rip = 0x118aULL;
    /* 0x118a: SUB RDI, R12 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x118d: MOV RAX, [R15+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->r15 + 0x8ULL));
    /* 0x1191: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x1193: CMP R12, RAX */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1196: MOV ESI, 0x0 */
ctx->rsi = (uint64_t)(uint32_t)(0x0ULL);
    /* 0x119b: CMOVBE RAX, RSI */
    if ((ctx->cf || ctx->zf)) {
    ctx->rax = (uint64_t)(ctx->rsi);
    }
    /* 0x119f: SETA CL */
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)(((!ctx->cf && !ctx->zf) ? 1 : 0)));
    /* 0x11a2: SUB EDX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x11a4: SHL ECX, 0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint8_t count = ((uint8_t)(0x4ULL)) & 0x3f; uint32_t res;
      res = a << count;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x11a7: SUB R12, RAX */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
    ctx->r12 = (uint64_t)(res);
    }
    /* 0x11aa: ADD [R15+RCX], R12 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->r15 + ctx->rcx)); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a + b;
    MEM_U64(ctx->r15 + ctx->rcx) = (uint64_t)(res);
    }
    /* 0x11ae: SUB [R15+RCX+0x8], R12 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->r15 + ctx->rcx + 0x8ULL)); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
    MEM_U64(ctx->r15 + ctx->rcx + 0x8ULL) = (uint64_t)(res);
    }
    /* 0x11b3: ADD R15, RCX */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x11b6: JMP .-188 */
    goto loc_0x10ff;

loc_0x11bb:
    ctx->rip = 0x11bbULL;
    /* 0x11bb: TEST RDI, RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x11be: JE .+5 */
    if (ctx->zf) goto loc_0x11c5;

loc_0x11c0:
    ctx->rip = 0x11c0ULL;
    /* 0x11c0: XOR R12L, R12L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(((uint32_t)ctx->r12)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }
    /* 0x11c3: JMP .-59 */
    goto loc_0x118a;

loc_0x11c5:
    ctx->rip = 0x11c5ULL;
    /* 0x11c5: MOV RDX, [RSP+Reg(0)+0x8] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x8ULL));
    /* 0x11ca: MOV RAX, [RDX+0x58] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x58ULL));
    /* 0x11ce: MOV RCX, [RDX+0x60] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rdx + 0x60ULL));
    /* 0x11d2: ADD RCX, RAX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x11d5: MOV [RDX+0x20], RCX */
MEM_U64(ctx->rdx + 0x20ULL) = (uint64_t)(ctx->rcx);
    /* 0x11d9: MOV [RDX+0x38], RAX */
MEM_U64(ctx->rdx + 0x38ULL) = (uint64_t)(ctx->rax);
    /* 0x11dd: MOV [RDX+0x28], RAX */
MEM_U64(ctx->rdx + 0x28ULL) = (uint64_t)(ctx->rax);
    /* 0x11e1: MOV RAX, [RSP+Reg(0)+0x18] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x18ULL));
    /* 0x11e6: JMP .+41 */
    goto loc_0x1211;

loc_0x11e8:
    ctx->rip = 0x11e8ULL;
    /* 0x11e8: XORPS X0, X0 */
    memset(&ctx->xmm[0], 0, 16);
    /* 0x11eb: MOV RAX, [RSP+Reg(0)+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x8ULL));
    /* 0x11f0: MOVUPS [RAX+0x20], X0 */
    memcpy(ctx->mem_base + (ctx->rax + 0x20ULL), &ctx->xmm[0], 16);
    /* 0x11f4: MOV [RAX+0x38], 0x0 */
MEM_U64(ctx->rax + 0x38ULL) = (uint64_t)(0x0ULL);
    /* 0x11fc: OR [RAX], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a | b;
    MEM_U8(ctx->rax) = (uint8_t)(res);
    }
    /* 0x11ff: CMP EDX, 0x2 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0x2ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1202: JNE .+4 */
    if (!ctx->zf) goto loc_0x1208;

loc_0x1204:
    ctx->rip = 0x1204ULL;
    /* 0x1204: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x1206: JMP .+9 */
    goto loc_0x1211;

loc_0x1208:
    ctx->rip = 0x1208ULL;
    /* 0x1208: MOV RAX, [RSP+Reg(0)+0x18] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x18ULL));
    /* 0x120d: SUB RAX, [R15+0x8] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->r15 + 0x8ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    ctx->rax = (uint64_t)(res);
    }

loc_0x1211:
    ctx->rip = 0x1211ULL;
    /* 0x1211: ADD RSP, 0x48 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x48ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x1215: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x1216: POP R12 */
    ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x1218: POP R13 */
    ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x121a: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x121c: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x121e: POP RBP */
    ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x121f: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function ioctl at 0x1220 (Forwarded directly to host shim shim_ioctl)
void fn_0x1220(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_ioctl(ctx);
    return;
}

// Function __stdio_seek at 0x12d4
void fn_0x12d4(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x12d4:
    ctx->rip = 0x12d4ULL;
    /* 0x12d4: MOV EDI, [RDI+0x78] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rdi + 0x78ULL));
    /* 0x12d7: JMP .+13500 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x4798ULL;
    fn_0x4798(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function vfprintf at 0x12dc
void fn_0x12dc(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x12dcULL) {
        switch (ctx->rip) {
            case 0x1333ULL: goto loc_0x1333;
            case 0x133cULL: goto loc_0x133c;
            case 0x134cULL: goto loc_0x134c;
            case 0x1357ULL: goto loc_0x1357;
            case 0x135aULL: goto loc_0x135a;
            case 0x1366ULL: goto loc_0x1366;
            case 0x136eULL: goto loc_0x136e;
            case 0x1375ULL: goto loc_0x1375;
            case 0x137fULL: goto loc_0x137f;
            case 0x1381ULL: goto loc_0x1381;
            case 0x13a5ULL: goto loc_0x13a5;
            case 0x13b6ULL: goto loc_0x13b6;
            case 0x13d3ULL: goto loc_0x13d3;
            case 0x13dcULL: goto loc_0x13dc;
            case 0x140aULL: goto loc_0x140a;
            case 0x1427ULL: goto loc_0x1427;
            case 0x142fULL: goto loc_0x142f;
            default: goto loc_0x12dc;
        }
    }

loc_0x12dc:
    ctx->rip = 0x12dcULL;
    /* 0x12dc: PUSH RBP */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbp);
    /* 0x12dd: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x12df: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x12e1: PUSH R13 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r13);
    /* 0x12e3: PUSH R12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r12);
    /* 0x12e5: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x12e6: SUB RSP, 0x148 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x148ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x12ed: MOV R14, RSI */
ctx->r14 = (uint64_t)(ctx->rsi);
    /* 0x12f0: MOV RBX, RDI */
ctx->rbx = (uint64_t)(ctx->rdi);
    /* 0x12f3: XORPS X0, X0 */
    memset(&ctx->xmm[0], 0, 16);
    /* 0x12f6: LEA R8, [RSP+Reg(0)+0x20] */
    ctx->r8 = (uint64_t)(ctx->rsp + 0x20ULL);
    /* 0x12fb: MOVAPS [R8+0x10], X0 */
    memcpy(ctx->mem_base + (ctx->r8 + 0x10ULL), &ctx->xmm[0], 16);
    /* 0x1300: MOVAPS [R8], X0 */
    memcpy(ctx->mem_base + (ctx->r8), &ctx->xmm[0], 16);
    /* 0x1304: MOV [R8+0x20], 0x0 */
MEM_U64(ctx->r8 + 0x20ULL) = (uint64_t)(0x0ULL);
    /* 0x130c: MOVUPS X0, [RDX] */
    memcpy(&ctx->xmm[0], ctx->mem_base + (ctx->rdx), 16);
    /* 0x130f: MOV RAX, RSP */
ctx->rax = (uint64_t)(ctx->rsp);
    /* 0x1312: MOVAPS [RAX], X0 */
    memcpy(ctx->mem_base + (ctx->rax), &ctx->xmm[0], 16);
    /* 0x1315: MOV RCX, [RDX+0x10] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL));
    /* 0x1319: MOV [RAX+0x10], RCX */
MEM_U64(ctx->rax + 0x10ULL) = (uint64_t)(ctx->rcx);
    /* 0x131d: LEA RCX, [RSP+Reg(0)+0xa0] */
    ctx->rcx = (uint64_t)(ctx->rsp + 0xa0ULL);
    /* 0x1325: XOR EDI, EDI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t res;
      res = a ^ b;
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x1327: MOV RDX, RAX */
ctx->rdx = (uint64_t)(ctx->rax);
    /* 0x132a: CALL .+277 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x132fULL;
    ctx->rip = 0x1444ULL;
    fn_0x1444(ctx);
    /* 0x132f: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1331: JS .+25 */
    if (ctx->sf) goto loc_0x134c;

loc_0x1333:
    ctx->rip = 0x1333ULL;
    /* 0x1333: CMP [RBX+0x8c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbx + 0x8cULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x133a: JS .+27 */
    if (ctx->sf) goto loc_0x1357;

loc_0x133c:
    ctx->rip = 0x133cULL;
    /* 0x133c: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x133f: CALL .+8328 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1344ULL;
    ctx->rip = 0x33ccULL;
    fn_0x33cc(ctx);
    /* 0x1344: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1346: SETE R15B */
    ctx->r15 = (ctx->r15 & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x134a: JMP .+14 */
    goto loc_0x135a;

loc_0x134c:
    ctx->rip = 0x134cULL;
    /* 0x134c: MOV R14L, -0x1 */
ctx->r14 = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x1352: JMP .+216 */
    goto loc_0x142f;

loc_0x1357:
    ctx->rip = 0x1357ULL;
    /* 0x1357: MOV R15B, 0x1 */
ctx->r15 = (ctx->r15 & ~0xffULL) | ((uint64_t)(uint8_t)(0x1ULL));

loc_0x135a:
    ctx->rip = 0x135aULL;
    /* 0x135a: MOV R12L, [RBX] */
ctx->r12 = (uint64_t)(uint32_t)(MEM_U32(ctx->rbx));
    /* 0x135d: CMP [RBX+0x88], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbx + 0x88ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1364: JG .+8 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x136e;

loc_0x1366:
    ctx->rip = 0x1366ULL;
    /* 0x1366: MOV EAX, R12L */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x1369: AND EAX, -0x21 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xffffffffffffffdfULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x136c: MOV [RBX], EAX */
MEM_U32(ctx->rbx) = (uint32_t)(((uint32_t)ctx->rax));

loc_0x136e:
    ctx->rip = 0x136eULL;
    /* 0x136e: CMP [RBX+0x60], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rbx + 0x60ULL)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1373: JE .+12 */
    if (ctx->zf) goto loc_0x1381;

loc_0x1375:
    ctx->rip = 0x1375ULL;
    /* 0x1375: XOR R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a ^ b;
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1378: CMP [RBX+0x20], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rbx + 0x20ULL)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x137d: JNE .+55 */
    if (!ctx->zf) goto loc_0x13b6;

loc_0x137f:
    ctx->rip = 0x137fULL;
    /* 0x137f: JMP .+36 */
    goto loc_0x13a5;

loc_0x1381:
    ctx->rip = 0x1381ULL;
    /* 0x1381: MOV R13, [RBX+0x58] */
ctx->r13 = (uint64_t)(MEM_U64(ctx->rbx + 0x58ULL));
    /* 0x1385: LEA RAX, [RSP+Reg(0)+0x50] */
    ctx->rax = (uint64_t)(ctx->rsp + 0x50ULL);
    /* 0x138a: MOV [RBX+0x58], RAX */
MEM_U64(ctx->rbx + 0x58ULL) = (uint64_t)(ctx->rax);
    /* 0x138e: MOV [RBX+0x60], 0x50 */
MEM_U64(ctx->rbx + 0x60ULL) = (uint64_t)(0x50ULL);
    /* 0x1396: XORPS X0, X0 */
    memset(&ctx->xmm[0], 0, 16);
    /* 0x1399: MOVUPS [RBX+0x20], X0 */
    memcpy(ctx->mem_base + (ctx->rbx + 0x20ULL), &ctx->xmm[0], 16);
    /* 0x139d: MOV [RBX+0x38], 0x0 */
MEM_U64(ctx->rbx + 0x38ULL) = (uint64_t)(0x0ULL);

loc_0x13a5:
    ctx->rip = 0x13a5ULL;
    /* 0x13a5: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x13a8: CALL .+8343 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x13adULL;
    ctx->rip = 0x3444ULL;
    fn_0x3444(ctx);
    /* 0x13ad: MOV EBP, -0x1 */
ctx->rbp = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x13b2: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x13b4: JNE .+29 */
    if (!ctx->zf) goto loc_0x13d3;

loc_0x13b6:
    ctx->rip = 0x13b6ULL;
    /* 0x13b6: MOV RDX, RSP */
ctx->rdx = (uint64_t)(ctx->rsp);
    /* 0x13b9: LEA RCX, [RSP+Reg(0)+0xa0] */
    ctx->rcx = (uint64_t)(ctx->rsp + 0xa0ULL);
    /* 0x13c1: LEA R8, [RSP+Reg(0)+0x20] */
    ctx->r8 = (uint64_t)(ctx->rsp + 0x20ULL);
    /* 0x13c6: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x13c9: MOV RSI, R14 */
ctx->rsi = (uint64_t)(ctx->r14);
    /* 0x13cc: CALL .+115 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x13d1ULL;
    ctx->rip = 0x1444ULL;
    fn_0x1444(ctx);
    /* 0x13d1: MOV EBP, EAX */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));

loc_0x13d3:
    ctx->rip = 0x13d3ULL;
    /* 0x13d3: AND R12L, 0x20 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(0x20ULL); uint32_t res;
      res = a & b;
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }
    /* 0x13d7: TEST R13, R13 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x13da: JE .+46 */
    if (ctx->zf) goto loc_0x140a;

loc_0x13dc:
    ctx->rip = 0x13dcULL;
    /* 0x13dc: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x13df: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x13e2: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x13e4: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x13e6: CALL [RBX+0x48] */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x13e9ULL;
    ctx->rip = MEM_U64(ctx->rbx + 0x48ULL);
    recomp_dispatch(ctx, MEM_U64(ctx->rbx + 0x48ULL));
    /* 0x13e9: CMP [RBX+0x28], 0x1 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rbx + 0x28ULL)); uint64_t b = (uint64_t)(0x1ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x13ee: MOV EAX, 0x0 */
ctx->rax = (uint64_t)(uint32_t)(0x0ULL);
    /* 0x13f3: SBB EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)) + (uint32_t)ctx->cf; uint32_t res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x13f5: OR EBP, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a | b;
      set_flags_logic_u32(ctx, res);
    ctx->rbp = (uint64_t)(uint32_t)(res);
    }
    /* 0x13f7: MOV [RBX+0x58], R13 */
MEM_U64(ctx->rbx + 0x58ULL) = (uint64_t)(ctx->r13);
    /* 0x13fb: MOV [RBX+0x60], R14 */
MEM_U64(ctx->rbx + 0x60ULL) = (uint64_t)(ctx->r14);
    /* 0x13ff: XORPS X0, X0 */
    memset(&ctx->xmm[0], 0, 16);
    /* 0x1402: MOVUPS [RBX+0x20], X0 */
    memcpy(ctx->mem_base + (ctx->rbx + 0x20ULL), &ctx->xmm[0], 16);
    /* 0x1406: MOV [RBX+0x38], R14 */
MEM_U64(ctx->rbx + 0x38ULL) = (uint64_t)(ctx->r14);

loc_0x140a:
    ctx->rip = 0x140aULL;
    /* 0x140a: MOV EAX, [RBX] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbx));
    /* 0x140c: MOV ECX, EAX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x140e: AND ECX, 0x20 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x20ULL); uint32_t res;
      res = a & b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x1411: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1414: NEG ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = -a;
      set_flags_sub_u32(ctx, 0, a, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x1416: SBB R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)) + (uint32_t)ctx->cf; uint32_t res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1419: OR R14L, EBP */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t res;
      res = a | b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x141c: OR R12L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a | b;
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }
    /* 0x141f: MOV [RBX], R12L */
MEM_U32(ctx->rbx) = (uint32_t)(((uint32_t)ctx->r12));
    /* 0x1422: TEST R15B, R15B */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r15 & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->r15 & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1425: JNE .+8 */
    if (!ctx->zf) goto loc_0x142f;

loc_0x1427:
    ctx->rip = 0x1427ULL;
    /* 0x1427: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x142a: CALL .+8202 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x142fULL;
    ctx->rip = 0x3439ULL;
    fn_0x3439(ctx);

loc_0x142f:
    ctx->rip = 0x142fULL;
    /* 0x142f: MOV EAX, R14L */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->r14));
    /* 0x1432: ADD RSP, 0x148 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x148ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x1439: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x143a: POP R12 */
    ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x143c: POP R13 */
    ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x143e: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x1440: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x1442: POP RBP */
    ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x1443: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function printf_core at 0x1444
void fn_0x1444(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x1444ULL) {
        switch (ctx->rip) {
            case 0x14aaULL: goto loc_0x14aa;
            case 0x14b3ULL: goto loc_0x14b3;
            case 0x14c4ULL: goto loc_0x14c4;
            case 0x14d9ULL: goto loc_0x14d9;
            case 0x14dcULL: goto loc_0x14dc;
            case 0x14e0ULL: goto loc_0x14e0;
            case 0x14e8ULL: goto loc_0x14e8;
            case 0x14f1ULL: goto loc_0x14f1;
            case 0x14f7ULL: goto loc_0x14f7;
            case 0x14fdULL: goto loc_0x14fd;
            case 0x150dULL: goto loc_0x150d;
            case 0x150fULL: goto loc_0x150f;
            case 0x1514ULL: goto loc_0x1514;
            case 0x1517ULL: goto loc_0x1517;
            case 0x152dULL: goto loc_0x152d;
            case 0x1532ULL: goto loc_0x1532;
            case 0x1538ULL: goto loc_0x1538;
            case 0x1546ULL: goto loc_0x1546;
            case 0x154fULL: goto loc_0x154f;
            case 0x156cULL: goto loc_0x156c;
            case 0x1586ULL: goto loc_0x1586;
            case 0x15a1ULL: goto loc_0x15a1;
            case 0x15b4ULL: goto loc_0x15b4;
            case 0x15bfULL: goto loc_0x15bf;
            case 0x15d5ULL: goto loc_0x15d5;
            case 0x15deULL: goto loc_0x15de;
            case 0x15f9ULL: goto loc_0x15f9;
            case 0x160bULL: goto loc_0x160b;
            case 0x160dULL: goto loc_0x160d;
            case 0x1615ULL: goto loc_0x1615;
            case 0x1620ULL: goto loc_0x1620;
            case 0x1630ULL: goto loc_0x1630;
            case 0x163aULL: goto loc_0x163a;
            case 0x166aULL: goto loc_0x166a;
            case 0x1679ULL: goto loc_0x1679;
            case 0x167cULL: goto loc_0x167c;
            case 0x1685ULL: goto loc_0x1685;
            case 0x169aULL: goto loc_0x169a;
            case 0x169fULL: goto loc_0x169f;
            case 0x16c9ULL: goto loc_0x16c9;
            case 0x16cdULL: goto loc_0x16cd;
            case 0x16d2ULL: goto loc_0x16d2;
            case 0x16ddULL: goto loc_0x16dd;
            case 0x16e5ULL: goto loc_0x16e5;
            case 0x16eeULL: goto loc_0x16ee;
            case 0x16fdULL: goto loc_0x16fd;
            case 0x170aULL: goto loc_0x170a;
            case 0x1716ULL: goto loc_0x1716;
            case 0x1721ULL: goto loc_0x1721;
            case 0x1726ULL: goto loc_0x1726;
            case 0x1731ULL: goto loc_0x1731;
            case 0x173dULL: goto loc_0x173d;
            case 0x1746ULL: goto loc_0x1746;
            case 0x1756ULL: goto loc_0x1756;
            case 0x1760ULL: goto loc_0x1760;
            case 0x178fULL: goto loc_0x178f;
            case 0x1798ULL: goto loc_0x1798;
            case 0x17aeULL: goto loc_0x17ae;
            case 0x17bfULL: goto loc_0x17bf;
            case 0x17cbULL: goto loc_0x17cb;
            case 0x17d9ULL: goto loc_0x17d9;
            case 0x17f1ULL: goto loc_0x17f1;
            case 0x1803ULL: goto loc_0x1803;
            case 0x180cULL: goto loc_0x180c;
            case 0x1817ULL: goto loc_0x1817;
            case 0x1827ULL: goto loc_0x1827;
            case 0x183aULL: goto loc_0x183a;
            case 0x1849ULL: goto loc_0x1849;
            case 0x184cULL: goto loc_0x184c;
            case 0x1858ULL: goto loc_0x1858;
            case 0x1864ULL: goto loc_0x1864;
            case 0x1867ULL: goto loc_0x1867;
            case 0x186eULL: goto loc_0x186e;
            case 0x1871ULL: goto loc_0x1871;
            case 0x1886ULL: goto loc_0x1886;
            case 0x18a5ULL: goto loc_0x18a5;
            case 0x18aaULL: goto loc_0x18aa;
            case 0x18b2ULL: goto loc_0x18b2;
            case 0x18b6ULL: goto loc_0x18b6;
            case 0x18dcULL: goto loc_0x18dc;
            case 0x18e4ULL: goto loc_0x18e4;
            case 0x18efULL: goto loc_0x18ef;
            case 0x18f4ULL: goto loc_0x18f4;
            case 0x1900ULL: goto loc_0x1900;
            case 0x1928ULL: goto loc_0x1928;
            case 0x1964ULL: goto loc_0x1964;
            case 0x1971ULL: goto loc_0x1971;
            case 0x1981ULL: goto loc_0x1981;
            case 0x198aULL: goto loc_0x198a;
            case 0x1993ULL: goto loc_0x1993;
            case 0x1999ULL: goto loc_0x1999;
            case 0x19a3ULL: goto loc_0x19a3;
            case 0x19d3ULL: goto loc_0x19d3;
            case 0x19e2ULL: goto loc_0x19e2;
            case 0x1a07ULL: goto loc_0x1a07;
            case 0x1a3cULL: goto loc_0x1a3c;
            case 0x1a59ULL: goto loc_0x1a59;
            case 0x1a7fULL: goto loc_0x1a7f;
            case 0x1a8dULL: goto loc_0x1a8d;
            case 0x1aa1ULL: goto loc_0x1aa1;
            case 0x1abfULL: goto loc_0x1abf;
            case 0x1adbULL: goto loc_0x1adb;
            case 0x1af3ULL: goto loc_0x1af3;
            case 0x1b2eULL: goto loc_0x1b2e;
            case 0x1b61ULL: goto loc_0x1b61;
            case 0x1b9dULL: goto loc_0x1b9d;
            case 0x1c03ULL: goto loc_0x1c03;
            case 0x1c14ULL: goto loc_0x1c14;
            case 0x1c18ULL: goto loc_0x1c18;
            case 0x1c28ULL: goto loc_0x1c28;
            case 0x1c57ULL: goto loc_0x1c57;
            case 0x1c7dULL: goto loc_0x1c7d;
            case 0x1cbeULL: goto loc_0x1cbe;
            case 0x1cccULL: goto loc_0x1ccc;
            case 0x1cdfULL: goto loc_0x1cdf;
            case 0x1cf6ULL: goto loc_0x1cf6;
            case 0x1d07ULL: goto loc_0x1d07;
            case 0x1d16ULL: goto loc_0x1d16;
            case 0x1d23ULL: goto loc_0x1d23;
            case 0x1d37ULL: goto loc_0x1d37;
            case 0x1d49ULL: goto loc_0x1d49;
            case 0x1d76ULL: goto loc_0x1d76;
            case 0x1d88ULL: goto loc_0x1d88;
            case 0x1d8eULL: goto loc_0x1d8e;
            case 0x1dacULL: goto loc_0x1dac;
            case 0x1dbcULL: goto loc_0x1dbc;
            case 0x1dd3ULL: goto loc_0x1dd3;
            case 0x1ddcULL: goto loc_0x1ddc;
            case 0x1de5ULL: goto loc_0x1de5;
            case 0x1dedULL: goto loc_0x1ded;
            case 0x1df3ULL: goto loc_0x1df3;
            case 0x1dfdULL: goto loc_0x1dfd;
            case 0x1e15ULL: goto loc_0x1e15;
            case 0x1e22ULL: goto loc_0x1e22;
            case 0x1e2eULL: goto loc_0x1e2e;
            case 0x1e40ULL: goto loc_0x1e40;
            case 0x1e68ULL: goto loc_0x1e68;
            case 0x1e6bULL: goto loc_0x1e6b;
            case 0x1e77ULL: goto loc_0x1e77;
            case 0x1e96ULL: goto loc_0x1e96;
            case 0x1e9dULL: goto loc_0x1e9d;
            case 0x1eadULL: goto loc_0x1ead;
            case 0x1eb6ULL: goto loc_0x1eb6;
            case 0x1ebbULL: goto loc_0x1ebb;
            case 0x1ed5ULL: goto loc_0x1ed5;
            case 0x1ee2ULL: goto loc_0x1ee2;
            case 0x1efaULL: goto loc_0x1efa;
            case 0x1f1aULL: goto loc_0x1f1a;
            case 0x1f2dULL: goto loc_0x1f2d;
            case 0x1f39ULL: goto loc_0x1f39;
            case 0x1f43ULL: goto loc_0x1f43;
            case 0x1f4fULL: goto loc_0x1f4f;
            case 0x1f63ULL: goto loc_0x1f63;
            case 0x1f7aULL: goto loc_0x1f7a;
            case 0x1f8dULL: goto loc_0x1f8d;
            case 0x1fb7ULL: goto loc_0x1fb7;
            case 0x1fc0ULL: goto loc_0x1fc0;
            case 0x1fd6ULL: goto loc_0x1fd6;
            case 0x1fefULL: goto loc_0x1fef;
            case 0x2015ULL: goto loc_0x2015;
            case 0x2083ULL: goto loc_0x2083;
            case 0x2097ULL: goto loc_0x2097;
            case 0x209fULL: goto loc_0x209f;
            case 0x20b0ULL: goto loc_0x20b0;
            case 0x20bfULL: goto loc_0x20bf;
            case 0x20eeULL: goto loc_0x20ee;
            case 0x20f2ULL: goto loc_0x20f2;
            case 0x20f9ULL: goto loc_0x20f9;
            case 0x2101ULL: goto loc_0x2101;
            case 0x2109ULL: goto loc_0x2109;
            case 0x2114ULL: goto loc_0x2114;
            case 0x212bULL: goto loc_0x212b;
            case 0x212dULL: goto loc_0x212d;
            case 0x2138ULL: goto loc_0x2138;
            case 0x2149ULL: goto loc_0x2149;
            case 0x2165ULL: goto loc_0x2165;
            case 0x217fULL: goto loc_0x217f;
            case 0x218aULL: goto loc_0x218a;
            case 0x2192ULL: goto loc_0x2192;
            case 0x21a7ULL: goto loc_0x21a7;
            case 0x21b9ULL: goto loc_0x21b9;
            case 0x21d1ULL: goto loc_0x21d1;
            case 0x21f3ULL: goto loc_0x21f3;
            case 0x21faULL: goto loc_0x21fa;
            case 0x2201ULL: goto loc_0x2201;
            case 0x2210ULL: goto loc_0x2210;
            case 0x2212ULL: goto loc_0x2212;
            case 0x224eULL: goto loc_0x224e;
            case 0x2260ULL: goto loc_0x2260;
            case 0x2277ULL: goto loc_0x2277;
            case 0x227cULL: goto loc_0x227c;
            case 0x2288ULL: goto loc_0x2288;
            case 0x22d1ULL: goto loc_0x22d1;
            case 0x2316ULL: goto loc_0x2316;
            case 0x2340ULL: goto loc_0x2340;
            case 0x2372ULL: goto loc_0x2372;
            case 0x23c5ULL: goto loc_0x23c5;
            case 0x23feULL: goto loc_0x23fe;
            case 0x2407ULL: goto loc_0x2407;
            case 0x2419ULL: goto loc_0x2419;
            case 0x2423ULL: goto loc_0x2423;
            case 0x242aULL: goto loc_0x242a;
            case 0x242fULL: goto loc_0x242f;
            case 0x2437ULL: goto loc_0x2437;
            case 0x245fULL: goto loc_0x245f;
            case 0x2466ULL: goto loc_0x2466;
            case 0x2470ULL: goto loc_0x2470;
            case 0x2474ULL: goto loc_0x2474;
            case 0x2479ULL: goto loc_0x2479;
            case 0x2488ULL: goto loc_0x2488;
            case 0x249fULL: goto loc_0x249f;
            case 0x24a4ULL: goto loc_0x24a4;
            case 0x24b4ULL: goto loc_0x24b4;
            case 0x24d4ULL: goto loc_0x24d4;
            case 0x24f2ULL: goto loc_0x24f2;
            case 0x250eULL: goto loc_0x250e;
            case 0x2540ULL: goto loc_0x2540;
            case 0x254fULL: goto loc_0x254f;
            case 0x2590ULL: goto loc_0x2590;
            case 0x259eULL: goto loc_0x259e;
            case 0x25c6ULL: goto loc_0x25c6;
            case 0x25d6ULL: goto loc_0x25d6;
            case 0x25f3ULL: goto loc_0x25f3;
            case 0x25fcULL: goto loc_0x25fc;
            case 0x2601ULL: goto loc_0x2601;
            case 0x260aULL: goto loc_0x260a;
            case 0x261dULL: goto loc_0x261d;
            case 0x262fULL: goto loc_0x262f;
            case 0x264dULL: goto loc_0x264d;
            case 0x265fULL: goto loc_0x265f;
            case 0x2663ULL: goto loc_0x2663;
            case 0x266bULL: goto loc_0x266b;
            case 0x2672ULL: goto loc_0x2672;
            case 0x269cULL: goto loc_0x269c;
            case 0x269eULL: goto loc_0x269e;
            case 0x26aeULL: goto loc_0x26ae;
            case 0x26dbULL: goto loc_0x26db;
            case 0x274fULL: goto loc_0x274f;
            case 0x2766ULL: goto loc_0x2766;
            case 0x276bULL: goto loc_0x276b;
            case 0x2770ULL: goto loc_0x2770;
            case 0x277bULL: goto loc_0x277b;
            case 0x2792ULL: goto loc_0x2792;
            case 0x27baULL: goto loc_0x27ba;
            case 0x2815ULL: goto loc_0x2815;
            case 0x282aULL: goto loc_0x282a;
            case 0x2851ULL: goto loc_0x2851;
            case 0x2861ULL: goto loc_0x2861;
            case 0x2887ULL: goto loc_0x2887;
            case 0x288aULL: goto loc_0x288a;
            case 0x2895ULL: goto loc_0x2895;
            case 0x289dULL: goto loc_0x289d;
            case 0x28a2ULL: goto loc_0x28a2;
            case 0x28c3ULL: goto loc_0x28c3;
            case 0x28f2ULL: goto loc_0x28f2;
            case 0x2900ULL: goto loc_0x2900;
            case 0x2902ULL: goto loc_0x2902;
            case 0x2907ULL: goto loc_0x2907;
            case 0x2922ULL: goto loc_0x2922;
            case 0x2932ULL: goto loc_0x2932;
            case 0x2943ULL: goto loc_0x2943;
            case 0x2953ULL: goto loc_0x2953;
            case 0x296cULL: goto loc_0x296c;
            case 0x297eULL: goto loc_0x297e;
            case 0x298bULL: goto loc_0x298b;
            case 0x29a1ULL: goto loc_0x29a1;
            case 0x29b7ULL: goto loc_0x29b7;
            case 0x29c9ULL: goto loc_0x29c9;
            case 0x29e1ULL: goto loc_0x29e1;
            case 0x29eeULL: goto loc_0x29ee;
            case 0x29f9ULL: goto loc_0x29f9;
            case 0x2a0cULL: goto loc_0x2a0c;
            case 0x2a15ULL: goto loc_0x2a15;
            case 0x2a1dULL: goto loc_0x2a1d;
            case 0x2a32ULL: goto loc_0x2a32;
            case 0x2a43ULL: goto loc_0x2a43;
            case 0x2a4eULL: goto loc_0x2a4e;
            case 0x2a5fULL: goto loc_0x2a5f;
            case 0x2a68ULL: goto loc_0x2a68;
            case 0x2ab8ULL: goto loc_0x2ab8;
            case 0x2ac2ULL: goto loc_0x2ac2;
            case 0x2acbULL: goto loc_0x2acb;
            case 0x2adcULL: goto loc_0x2adc;
            case 0x2ae4ULL: goto loc_0x2ae4;
            case 0x2af8ULL: goto loc_0x2af8;
            case 0x2b16ULL: goto loc_0x2b16;
            case 0x2b35ULL: goto loc_0x2b35;
            case 0x2b40ULL: goto loc_0x2b40;
            case 0x2b5cULL: goto loc_0x2b5c;
            case 0x2b8cULL: goto loc_0x2b8c;
            case 0x2b9cULL: goto loc_0x2b9c;
            case 0x2badULL: goto loc_0x2bad;
            case 0x2bb2ULL: goto loc_0x2bb2;
            case 0x2bc6ULL: goto loc_0x2bc6;
            case 0x2bf3ULL: goto loc_0x2bf3;
            case 0x2bf5ULL: goto loc_0x2bf5;
            case 0x2c00ULL: goto loc_0x2c00;
            case 0x2c14ULL: goto loc_0x2c14;
            case 0x2c1aULL: goto loc_0x2c1a;
            case 0x2c2bULL: goto loc_0x2c2b;
            case 0x2c5dULL: goto loc_0x2c5d;
            case 0x2c73ULL: goto loc_0x2c73;
            case 0x2ca8ULL: goto loc_0x2ca8;
            case 0x2cbbULL: goto loc_0x2cbb;
            case 0x2cecULL: goto loc_0x2cec;
            case 0x2d0aULL: goto loc_0x2d0a;
            case 0x2d13ULL: goto loc_0x2d13;
            case 0x2d22ULL: goto loc_0x2d22;
            case 0x2d4fULL: goto loc_0x2d4f;
            case 0x2d54ULL: goto loc_0x2d54;
            case 0x2d5dULL: goto loc_0x2d5d;
            case 0x2d5fULL: goto loc_0x2d5f;
            case 0x2d6cULL: goto loc_0x2d6c;
            case 0x2d78ULL: goto loc_0x2d78;
            case 0x2d7aULL: goto loc_0x2d7a;
            case 0x2d80ULL: goto loc_0x2d80;
            case 0x2d90ULL: goto loc_0x2d90;
            case 0x2d96ULL: goto loc_0x2d96;
            case 0x2db8ULL: goto loc_0x2db8;
            case 0x2dc7ULL: goto loc_0x2dc7;
            case 0x2dd1ULL: goto loc_0x2dd1;
            case 0x2dd7ULL: goto loc_0x2dd7;
            case 0x2dfaULL: goto loc_0x2dfa;
            case 0x2e03ULL: goto loc_0x2e03;
            case 0x2e0cULL: goto loc_0x2e0c;
            case 0x2e1dULL: goto loc_0x2e1d;
            case 0x2e45ULL: goto loc_0x2e45;
            case 0x2e47ULL: goto loc_0x2e47;
            case 0x2e4eULL: goto loc_0x2e4e;
            case 0x2e53ULL: goto loc_0x2e53;
            case 0x2e59ULL: goto loc_0x2e59;
            case 0x2e7eULL: goto loc_0x2e7e;
            case 0x2e8fULL: goto loc_0x2e8f;
            case 0x2e9cULL: goto loc_0x2e9c;
            case 0x2ea1ULL: goto loc_0x2ea1;
            case 0x2ec9ULL: goto loc_0x2ec9;
            case 0x2eccULL: goto loc_0x2ecc;
            case 0x2ed5ULL: goto loc_0x2ed5;
            case 0x2eddULL: goto loc_0x2edd;
            case 0x2f05ULL: goto loc_0x2f05;
            case 0x2f07ULL: goto loc_0x2f07;
            case 0x2f17ULL: goto loc_0x2f17;
            case 0x2f1cULL: goto loc_0x2f1c;
            case 0x2f21ULL: goto loc_0x2f21;
            case 0x2f2bULL: goto loc_0x2f2b;
            case 0x2f31ULL: goto loc_0x2f31;
            case 0x2f51ULL: goto loc_0x2f51;
            case 0x2f5eULL: goto loc_0x2f5e;
            case 0x2f63ULL: goto loc_0x2f63;
            case 0x2f69ULL: goto loc_0x2f69;
            case 0x2f8dULL: goto loc_0x2f8d;
            case 0x2f9eULL: goto loc_0x2f9e;
            case 0x2fc3ULL: goto loc_0x2fc3;
            case 0x2fd1ULL: goto loc_0x2fd1;
            case 0x2fe2ULL: goto loc_0x2fe2;
            case 0x3004ULL: goto loc_0x3004;
            case 0x3021ULL: goto loc_0x3021;
            case 0x3024ULL: goto loc_0x3024;
            case 0x303cULL: goto loc_0x303c;
            case 0x3041ULL: goto loc_0x3041;
            case 0x307bULL: goto loc_0x307b;
            case 0x307dULL: goto loc_0x307d;
            case 0x30a5ULL: goto loc_0x30a5;
            case 0x30aaULL: goto loc_0x30aa;
            case 0x30c5ULL: goto loc_0x30c5;
            case 0x30caULL: goto loc_0x30ca;
            case 0x30d6ULL: goto loc_0x30d6;
            case 0x30dbULL: goto loc_0x30db;
            case 0x30e0ULL: goto loc_0x30e0;
            case 0x30e7ULL: goto loc_0x30e7;
            case 0x30ecULL: goto loc_0x30ec;
            case 0x3100ULL: goto loc_0x3100;
            case 0x3115ULL: goto loc_0x3115;
            case 0x311cULL: goto loc_0x311c;
            case 0x312dULL: goto loc_0x312d;
            case 0x3135ULL: goto loc_0x3135;
            case 0x314dULL: goto loc_0x314d;
            case 0x3154ULL: goto loc_0x3154;
            case 0x3158ULL: goto loc_0x3158;
            case 0x3163ULL: goto loc_0x3163;
            case 0x316aULL: goto loc_0x316a;
            case 0x3173ULL: goto loc_0x3173;
            case 0x3178ULL: goto loc_0x3178;
            default: goto loc_0x1444;
        }
    }

loc_0x1444:
    ctx->rip = 0x1444ULL;
    /* 0x1444: PUSH RBP */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbp);
    /* 0x1445: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x1447: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x1449: PUSH R13 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r13);
    /* 0x144b: PUSH R12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r12);
    /* 0x144d: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x144e: SUB RSP, 0x1e58 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x1e58ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x1455: MOV [RSP+Reg(0)+0xd0], R8 */
MEM_U64(ctx->rsp + 0xd0ULL) = (uint64_t)(ctx->r8);
    /* 0x145d: MOV [RSP+Reg(0)+0xd8], RDX */
MEM_U64(ctx->rsp + 0xd8ULL) = (uint64_t)(ctx->rdx);
    /* 0x1465: MOV R14, RSI */
ctx->r14 = (uint64_t)(ctx->rsi);
    /* 0x1468: MOV [RSP+Reg(0)+0x48], RDI */
MEM_U64(ctx->rsp + 0x48ULL) = (uint64_t)(ctx->rdi);
    /* 0x146d: MOV [RSP+Reg(0)+0x118], RCX */
MEM_U64(ctx->rsp + 0x118ULL) = (uint64_t)(ctx->rcx);
    /* 0x1475: LEA RAX, [RCX+0xfffffd00] */
    ctx->rax = (uint64_t)(ctx->rcx + (uint64_t)(-0x300LL));
    /* 0x147c: MOV [RSP+Reg(0)+0x110], RAX */
MEM_U64(ctx->rsp + 0x110ULL) = (uint64_t)(ctx->rax);
    /* 0x1484: LEA RAX, [RSP+Reg(0)+0x1a0] */
    ctx->rax = (uint64_t)(ctx->rsp + 0x1a0ULL);
    /* 0x148c: NEG RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t res;
      res = -a;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x148f: MOV [RSP+Reg(0)+0x160], RAX */
MEM_U64(ctx->rsp + 0x160ULL) = (uint64_t)(ctx->rax);
    /* 0x1497: MOV [RSP+Reg(0)+0x44], 0x0 */
MEM_U32(ctx->rsp + 0x44ULL) = (uint32_t)(0x0ULL);
    /* 0x149f: MOV [RSP+Reg(0)+0x34], 0x0 */
MEM_U32(ctx->rsp + 0x34ULL) = (uint32_t)(0x0ULL);
    /* 0x14a7: XOR R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }

loc_0x14aa:
    ctx->rip = 0x14aaULL;
    /* 0x14aa: MOV R15, [RSP+Reg(0)+0x48] */
ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x14af: MOV EBP, [RSP+Reg(0)+0x34] */
ctx->rbp = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x34ULL));

loc_0x14b3:
    ctx->rip = 0x14b3ULL;
    /* 0x14b3: MOV ECX, EBP */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x14b5: XOR ECX, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x14bb: CMP R13L, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x14be: JG .+7191 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30db;

loc_0x14c4:
    ctx->rip = 0x14c4ULL;
    /* 0x14c4: MOV EAX, EBP */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x14c6: MOV RBX, R14 */
ctx->rbx = (uint64_t)(ctx->r14);
    /* 0x14c9: MOV EBP, R13L */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x14cc: ADD EBP, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a + b;
    ctx->rbp = (uint64_t)(uint32_t)(res);
    }
    /* 0x14ce: MOV AL, [R14] */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->r14)));
    /* 0x14d1: TEST AL, AL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x14d3: JE .+7207 */
    if (ctx->zf) goto loc_0x3100;

loc_0x14d9:
    ctx->rip = 0x14d9ULL;
    /* 0x14d9: MOV R14, RBX */
ctx->r14 = (uint64_t)(ctx->rbx);

loc_0x14dc:
    ctx->rip = 0x14dcULL;
    /* 0x14dc: TEST AL, AL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x14de: JE .+47 */
    if (ctx->zf) goto loc_0x150f;

loc_0x14e0:
    ctx->rip = 0x14e0ULL;
    /* 0x14e0: MOVZX EAX, AL */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(((uint8_t)(ctx->rax & 0xff))));
    /* 0x14e3: CMP EAX, 0x25 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x25ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x14e6: JE .+9 */
    if (ctx->zf) goto loc_0x14f1;

loc_0x14e8:
    ctx->rip = 0x14e8ULL;
    /* 0x14e8: MOV AL, [R14+0x1] */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->r14 + 0x1ULL)));
    /* 0x14ec: INC R14 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x14ef: JMP .-21 */
    goto loc_0x14dc;

loc_0x14f1:
    ctx->rip = 0x14f1ULL;
    /* 0x14f1: MOV R13, R14 */
ctx->r13 = (uint64_t)(ctx->r14);
    /* 0x14f4: MOV RAX, R14 */
ctx->rax = (uint64_t)(ctx->r14);

loc_0x14f7:
    ctx->rip = 0x14f7ULL;
    /* 0x14f7: CMP [RAX+0x1], 0x25 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax + 0x1ULL)); uint8_t b = (uint8_t)(0x25ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x14fb: JNE .+23 */
    if (!ctx->zf) goto loc_0x1514;

loc_0x14fd:
    ctx->rip = 0x14fdULL;
    /* 0x14fd: INC R13 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x1500: LEA R14, [RAX+0x2] */
    ctx->r14 = (uint64_t)(ctx->rax + 0x2ULL);
    /* 0x1504: CMP [RAX+0x2], 0x25 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax + 0x2ULL)); uint8_t b = (uint8_t)(0x25ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1508: MOV RAX, R14 */
ctx->rax = (uint64_t)(ctx->r14);
    /* 0x150b: JE .-22 */
    if (ctx->zf) goto loc_0x14f7;

loc_0x150d:
    ctx->rip = 0x150dULL;
    /* 0x150d: JMP .+8 */
    goto loc_0x1517;

loc_0x150f:
    ctx->rip = 0x150fULL;
    /* 0x150f: MOV R13, R14 */
ctx->r13 = (uint64_t)(ctx->r14);
    /* 0x1512: JMP .+3 */
    goto loc_0x1517;

loc_0x1514:
    ctx->rip = 0x1514ULL;
    /* 0x1514: MOV R14, RAX */
ctx->r14 = (uint64_t)(ctx->rax);

loc_0x1517:
    ctx->rip = 0x1517ULL;
    /* 0x1517: SUB R13, RBX */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x151a: MOV R12L, EBP */
ctx->r12 = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x151d: XOR R12L, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1524: CMP R13, R12 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1527: JG .+7086 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30db;

loc_0x152d:
    ctx->rip = 0x152dULL;
    /* 0x152d: TEST R15, R15 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x1530: JE .+20 */
    if (ctx->zf) goto loc_0x1546;

loc_0x1532:
    ctx->rip = 0x1532ULL;
    /* 0x1532: TEST [R15], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->r15)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1536: JNE .+14 */
    if (!ctx->zf) goto loc_0x1546;

loc_0x1538:
    ctx->rip = 0x1538ULL;
    /* 0x1538: MOVSXD RSI, R13L */
    ctx->rsi = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r13)));
    /* 0x153b: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x153e: MOV RDX, R15 */
ctx->rdx = (uint64_t)(ctx->r15);
    /* 0x1541: CALL .+8182 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1546ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x1546:
    ctx->rip = 0x1546ULL;
    /* 0x1546: TEST R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1549: JNE .-156 */
    if (!ctx->zf) goto loc_0x14b3;

loc_0x154f:
    ctx->rip = 0x154fULL;
    /* 0x154f: MOV [RSP+Reg(0)+0x34], EBP */
MEM_U32(ctx->rsp + 0x34ULL) = (uint32_t)(((uint32_t)ctx->rbp));
    /* 0x1553: LEA RDX, [R14+0x1] */
    ctx->rdx = (uint64_t)(ctx->r14 + 0x1ULL);
    /* 0x1557: MOV DIB, [R14+0x1] */
ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->r14 + 0x1ULL)));
    /* 0x155b: MOVSX ESI, DIB */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(((uint8_t)(ctx->rdi & 0xff))));
    /* 0x155f: LEA ECX, [RSI-0x30] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rsi + (uint64_t)(-0x30LL));
    /* 0x1562: MOV EAX, -0x1 */
ctx->rax = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x1567: CMP ECX, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x156a: JA .+53 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x15a1;

loc_0x156c:
    ctx->rip = 0x156cULL;
    /* 0x156c: CMP [R14+0x2], 0x24 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->r14 + 0x2ULL)); uint8_t b = (uint8_t)(0x24ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1571: MOV R11, [RSP+Reg(0)+0xd8] */
ctx->r11 = (uint64_t)(MEM_U64(ctx->rsp + 0xd8ULL));
    /* 0x1579: MOV EBP, 0x12889 */
ctx->rbp = (uint64_t)(uint32_t)(0x12889ULL);
    /* 0x157e: MOV R13L, -0x1 */
ctx->r13 = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x1584: JNE .+46 */
    if (!ctx->zf) goto loc_0x15b4;

loc_0x1586:
    ctx->rip = 0x1586ULL;
    /* 0x1586: MOV DIB, [R14+0x3] */
ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->r14 + 0x3ULL)));
    /* 0x158a: ADD R14, 0x3 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(0x3ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x158e: MOVSX ESI, DIB */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(((uint8_t)(ctx->rdi & 0xff))));
    /* 0x1592: MOV [RSP+Reg(0)+0x44], 0x1 */
MEM_U32(ctx->rsp + 0x44ULL) = (uint32_t)(0x1ULL);
    /* 0x159a: MOV RDX, R14 */
ctx->rdx = (uint64_t)(ctx->r14);
    /* 0x159d: MOV EAX, ECX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x159f: JMP .+19 */
    goto loc_0x15b4;

loc_0x15a1:
    ctx->rip = 0x15a1ULL;
    /* 0x15a1: MOV R11, [RSP+Reg(0)+0xd8] */
ctx->r11 = (uint64_t)(MEM_U64(ctx->rsp + 0xd8ULL));
    /* 0x15a9: MOV EBP, 0x12889 */
ctx->rbp = (uint64_t)(uint32_t)(0x12889ULL);
    /* 0x15ae: MOV R13L, -0x1 */
ctx->r13 = (uint64_t)(uint32_t)(0xffffffffffffffffULL);

loc_0x15b4:
    ctx->rip = 0x15b4ULL;
    /* 0x15b4: LEA ECX, [RSI-0x20] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rsi + (uint64_t)(-0x20LL));
    /* 0x15b7: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x15ba: CMP ECX, 0x1f */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x1fULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x15bd: JA .+78 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x160d;

loc_0x15bf:
    ctx->rip = 0x15bfULL;
    /* 0x15bf: MOV R8L, 0x1 */
ctx->r8 = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x15c5: SHL R8L, CL */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint8_t count = ((uint8_t)(((uint8_t)(ctx->rcx & 0xff)))) & 0x3f; uint32_t res;
      res = a << count;
      set_flags_logic_u32(ctx, res);
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x15c8: BT EBP, ECX */
    {
      uint32_t bit_idx = ((uint32_t)(((uint32_t)ctx->rcx))) & 31;
      uint32_t val = (uint32_t)(((uint32_t)ctx->rbp));
      ctx->cf = (val >> bit_idx) & 1;
    }
    /* 0x15cb: MOV R10, [RSP+Reg(0)+0xd0] */
ctx->r10 = (uint64_t)(MEM_U64(ctx->rsp + 0xd0ULL));
    /* 0x15d3: JAE .+64 */
    if (!ctx->cf) goto loc_0x1615;

loc_0x15d5:
    ctx->rip = 0x15d5ULL;
    /* 0x15d5: INC RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x15d8: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x15db: MOV R9, RDX */
ctx->r9 = (uint64_t)(ctx->rdx);

loc_0x15de:
    ctx->rip = 0x15deULL;
    /* 0x15de: MOV ECX, R14L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r14));
    /* 0x15e1: MOV RDX, R9 */
ctx->rdx = (uint64_t)(ctx->r9);
    /* 0x15e4: MOV R14L, R8L */
ctx->r14 = (uint64_t)(uint32_t)(((uint32_t)ctx->r8));
    /* 0x15e7: OR R14L, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a | b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x15ea: MOV DIB, [R9] */
ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->r9)));
    /* 0x15ed: MOVSX ESI, DIB */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(((uint8_t)(ctx->rdi & 0xff))));
    /* 0x15f1: LEA ECX, [RSI-0x20] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rsi + (uint64_t)(-0x20LL));
    /* 0x15f4: CMP ECX, 0x20 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x20ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x15f7: JAE .+28 */
    if (!ctx->cf) goto loc_0x1615;

loc_0x15f9:
    ctx->rip = 0x15f9ULL;
    /* 0x15f9: MOV R8L, 0x1 */
ctx->r8 = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x15ff: SHL R8L, CL */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint8_t count = ((uint8_t)(((uint8_t)(ctx->rcx & 0xff)))) & 0x3f; uint32_t res;
      res = a << count;
      set_flags_logic_u32(ctx, res);
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1602: LEA R9, [RDX+0x1] */
    ctx->r9 = (uint64_t)(ctx->rdx + 0x1ULL);
    /* 0x1606: BT EBP, ECX */
    {
      uint32_t bit_idx = ((uint32_t)(((uint32_t)ctx->rcx))) & 31;
      uint32_t val = (uint32_t)(((uint32_t)ctx->rbp));
      ctx->cf = (val >> bit_idx) & 1;
    }
    /* 0x1609: JB .-45 */
    if (ctx->cf) goto loc_0x15de;

loc_0x160b:
    ctx->rip = 0x160bULL;
    /* 0x160b: JMP .+8 */
    goto loc_0x1615;

loc_0x160d:
    ctx->rip = 0x160dULL;
    /* 0x160d: MOV R10, [RSP+Reg(0)+0xd0] */
ctx->r10 = (uint64_t)(MEM_U64(ctx->rsp + 0xd0ULL));

loc_0x1615:
    ctx->rip = 0x1615ULL;
    /* 0x1615: CMP DIB, 0x2a */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t b = (uint8_t)(0x2aULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1619: MOV [RSP+Reg(0)+0x1c], R14L */
MEM_U32(ctx->rsp + 0x1cULL) = (uint32_t)(((uint32_t)ctx->r14));
    /* 0x161e: JNE .+74 */
    if (!ctx->zf) goto loc_0x166a;

loc_0x1620:
    ctx->rip = 0x1620ULL;
    /* 0x1620: MOVSX ECX, [RDX+0x1] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x1ULL)));
    /* 0x1624: ADD ECX, -0x30 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xffffffffffffffd0ULL); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x1627: CMP ECX, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x162a: JA .+162 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x16d2;

loc_0x1630:
    ctx->rip = 0x1630ULL;
    /* 0x1630: CMP [RDX+0x2], 0x24 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rdx + 0x2ULL)); uint8_t b = (uint8_t)(0x24ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1634: JNE .+152 */
    if (!ctx->zf) goto loc_0x16d2;

loc_0x163a:
    ctx->rip = 0x163aULL;
    /* 0x163a: MOV ECX, ECX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x163c: MOV [R10+4*RCX], 0xa */
MEM_U32(ctx->r10 + (ctx->rcx * 4ULL)) = (uint32_t)(0xaULL);
    /* 0x1644: MOVSX RCX, [RDX+0x1] */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x1ULL)));
    /* 0x1649: SHL RCX, 0x4 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint8_t count = ((uint8_t)(0x4ULL)) & 0x3f; uint64_t res;
      res = a << count;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x164d: MOV RSI, [RSP+Reg(0)+0x110] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rsp + 0x110ULL));
    /* 0x1655: MOV R14L, [RSI+RCX] */
ctx->r14 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsi + ctx->rcx));
    /* 0x1659: ADD RDX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x3ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x165d: MOV [RSP+Reg(0)+0x44], 0x1 */
MEM_U32(ctx->rsp + 0x44ULL) = (uint32_t)(0x1ULL);
    /* 0x1665: JMP .+183 */
    goto loc_0x1721;

loc_0x166a:
    ctx->rip = 0x166aULL;
    /* 0x166a: ADD ESI, -0x30 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0xffffffffffffffd0ULL); uint32_t res;
      res = a + b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x166d: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1670: CMP ESI, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1673: JA .+184 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x1731;

loc_0x1679:
    ctx->rip = 0x1679ULL;
    /* 0x1679: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }

loc_0x167c:
    ctx->rip = 0x167cULL;
    /* 0x167c: CMP R14L, 0xccccccc */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(0xcccccccULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1683: JBE .+26 */
    if ((ctx->cf || ctx->zf)) goto loc_0x169f;

loc_0x1685:
    ctx->rip = 0x1685ULL;
    /* 0x1685: MOVSX ESI, [RDX+0x1] */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x1ULL)));
    /* 0x1689: INC RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x168c: ADD ESI, -0x30 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0xffffffffffffffd0ULL); uint32_t res;
      res = a + b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x168f: MOV R14L, -0x1 */
ctx->r14 = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x1695: CMP ESI, 0xa */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0xaULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1698: JB .-30 */
    if (ctx->cf) goto loc_0x167c;

loc_0x169a:
    ctx->rip = 0x169aULL;
    /* 0x169a: JMP .+6716 */
    goto loc_0x30db;

loc_0x169f:
    ctx->rip = 0x169fULL;
    /* 0x169f: MOV ECX, ESI */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x16a1: ADD R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a + b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x16a4: LEA R14L, [R14+4*R14] */
    ctx->r14 = (uint64_t)(uint32_t)(ctx->r14 + (ctx->r14 * 4ULL));
    /* 0x16a8: MOV EDI, R14L */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->r14));
    /* 0x16ab: XOR EDI, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x16b1: ADD R14L, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a + b;
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x16b4: CMP ESI, EDI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x16b6: CMOVA R14L, R13L */
    if ((!ctx->cf && !ctx->zf)) {
    ctx->r14 = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    }
    /* 0x16ba: MOVSX ESI, [RDX+0x1] */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x1ULL)));
    /* 0x16be: INC RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x16c1: ADD ESI, -0x30 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0xffffffffffffffd0ULL); uint32_t res;
      res = a + b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x16c4: CMP ESI, 0xa */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0xaULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x16c7: JB .-77 */
    if (ctx->cf) goto loc_0x167c;

loc_0x16c9:
    ctx->rip = 0x16c9ULL;
    /* 0x16c9: CMP ECX, EDI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x16cb: JBE .+100 */
    if ((ctx->cf || ctx->zf)) goto loc_0x1731;

loc_0x16cd:
    ctx->rip = 0x16cdULL;
    /* 0x16cd: JMP .+6665 */
    goto loc_0x30db;

loc_0x16d2:
    ctx->rip = 0x16d2ULL;
    /* 0x16d2: CMP [RSP+Reg(0)+0x44], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rsp + 0x44ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x16d7: JNE .+6811 */
    if (!ctx->zf) goto loc_0x3178;

loc_0x16dd:
    ctx->rip = 0x16ddULL;
    /* 0x16dd: INC RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x16e0: TEST R15, R15 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x16e3: JE .+24 */
    if (ctx->zf) goto loc_0x16fd;

loc_0x16e5:
    ctx->rip = 0x16e5ULL;
    /* 0x16e5: MOV ESI, [R11] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->r11));
    /* 0x16e8: CMP RSI, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x16ec: JA .+28 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x170a;

loc_0x16ee:
    ctx->rip = 0x16eeULL;
    /* 0x16ee: MOV RCX, RSI */
ctx->rcx = (uint64_t)(ctx->rsi);
    /* 0x16f1: ADD RCX, [R11+0x10] */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(MEM_U64(ctx->r11 + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x16f5: ADD ESI, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x16f8: MOV [R11], ESI */
MEM_U32(ctx->r11) = (uint32_t)(((uint32_t)ctx->rsi));
    /* 0x16fb: JMP .+25 */
    goto loc_0x1716;

loc_0x16fd:
    ctx->rip = 0x16fdULL;
    /* 0x16fd: MOV [RSP+Reg(0)+0x44], 0x0 */
MEM_U32(ctx->rsp + 0x44ULL) = (uint32_t)(0x0ULL);
    /* 0x1705: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1708: JMP .+39 */
    goto loc_0x1731;

loc_0x170a:
    ctx->rip = 0x170aULL;
    /* 0x170a: MOV RCX, [R11+0x8] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->r11 + 0x8ULL));
    /* 0x170e: LEA RSI, [RCX+0x8] */
    ctx->rsi = (uint64_t)(ctx->rcx + 0x8ULL);
    /* 0x1712: MOV [R11+0x8], RSI */
MEM_U64(ctx->r11 + 0x8ULL) = (uint64_t)(ctx->rsi);

loc_0x1716:
    ctx->rip = 0x1716ULL;
    /* 0x1716: MOV R14L, [RCX] */
ctx->r14 = (uint64_t)(uint32_t)(MEM_U32(ctx->rcx));
    /* 0x1719: MOV [RSP+Reg(0)+0x44], 0x0 */
MEM_U32(ctx->rsp + 0x44ULL) = (uint32_t)(0x0ULL);

loc_0x1721:
    ctx->rip = 0x1721ULL;
    /* 0x1721: TEST R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1724: JNS .+11 */
    if (!ctx->sf) goto loc_0x1731;

loc_0x1726:
    ctx->rip = 0x1726ULL;
    /* 0x1726: OR [RSP+Reg(0)+0x1c], 0x2000 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rsp + 0x1cULL)); uint32_t b = (uint32_t)(0x2000ULL); uint32_t res;
      res = a | b;
    MEM_U32(ctx->rsp + 0x1cULL) = (uint32_t)(res);
    }
    /* 0x172e: NEG R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = -a;
      set_flags_sub_u32(ctx, 0, a, res);
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }

loc_0x1731:
    ctx->rip = 0x1731ULL;
    /* 0x1731: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x1733: CMP [RDX], 0x2e */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rdx)); uint8_t b = (uint8_t)(0x2eULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1736: MOV [RSP+Reg(0)+0x28], R14 */
MEM_U64(ctx->rsp + 0x28ULL) = (uint64_t)(ctx->r14);
    /* 0x173b: JNE .+82 */
    if (!ctx->zf) goto loc_0x178f;

loc_0x173d:
    ctx->rip = 0x173dULL;
    /* 0x173d: MOVSX ECX, [RDX+0x1] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x1ULL)));
    /* 0x1741: CMP ECX, 0x2a */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x2aULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1744: JNE .+104 */
    if (!ctx->zf) goto loc_0x17ae;

loc_0x1746:
    ctx->rip = 0x1746ULL;
    /* 0x1746: MOVSX ECX, [RDX+0x2] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x2ULL)));
    /* 0x174a: ADD ECX, -0x30 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xffffffffffffffd0ULL); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x174d: CMP ECX, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1750: JA .+182 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x180c;

loc_0x1756:
    ctx->rip = 0x1756ULL;
    /* 0x1756: CMP [RDX+0x3], 0x24 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rdx + 0x3ULL)); uint8_t b = (uint8_t)(0x24ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x175a: JNE .+172 */
    if (!ctx->zf) goto loc_0x180c;

loc_0x1760:
    ctx->rip = 0x1760ULL;
    /* 0x1760: MOV ECX, ECX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x1762: MOV [R10+4*RCX], 0xa */
MEM_U32(ctx->r10 + (ctx->rcx * 4ULL)) = (uint32_t)(0xaULL);
    /* 0x176a: MOVSX RCX, [RDX+0x2] */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x2ULL)));
    /* 0x176f: SHL RCX, 0x4 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint8_t count = ((uint8_t)(0x4ULL)) & 0x3f; uint64_t res;
      res = a << count;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x1773: MOV RDI, [RSP+Reg(0)+0x110] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0x110ULL));
    /* 0x177b: MOV R10L, [RDI+RCX] */
ctx->r10 = (uint64_t)(uint32_t)(MEM_U32(ctx->rdi + ctx->rcx));
    /* 0x177f: ADD RDX, 0x4 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x1783: LEA R8, [RIP+0x4796] */
    ctx->r8 = (uint64_t)(0x5f20ULL);
    /* 0x178a: JMP .+189 */
    goto loc_0x184c;

loc_0x178f:
    ctx->rip = 0x178fULL;
    /* 0x178f: MOV R10L, -0x1 */
ctx->r10 = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x1795: XOR R11L, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }

loc_0x1798:
    ctx->rip = 0x1798ULL;
    /* 0x1798: LEA R8, [RIP+0x4781] */
    ctx->r8 = (uint64_t)(0x5f20ULL);
    /* 0x179f: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x17a9: JMP .+192 */
    goto loc_0x186e;

loc_0x17ae:
    ctx->rip = 0x17aeULL;
    /* 0x17ae: INC RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x17b1: ADD ECX, -0x30 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xffffffffffffffd0ULL); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x17b4: MOV R11B, 0x1 */
ctx->r11 = (ctx->r11 & ~0xffULL) | ((uint64_t)(uint8_t)(0x1ULL));
    /* 0x17b7: XOR R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a ^ b;
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x17ba: CMP ECX, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x17bd: JA .-39 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x1798;

loc_0x17bf:
    ctx->rip = 0x17bfULL;
    /* 0x17bf: XOR EDI, EDI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x17c1: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);

loc_0x17cb:
    ctx->rip = 0x17cbULL;
    /* 0x17cb: MOV R10L, -0x1 */
ctx->r10 = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x17d1: CMP EDI, 0xccccccc */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(0xcccccccULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x17d7: JA .+24 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x17f1;

loc_0x17d9:
    ctx->rip = 0x17d9ULL;
    /* 0x17d9: ADD EDI, EDI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t res;
      res = a + b;
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x17db: LEA R10L, [RDI+4*RDI] */
    ctx->r10 = (uint64_t)(uint32_t)(ctx->rdi + (ctx->rdi * 4ULL));
    /* 0x17df: MOV EDI, R10L */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->r10));
    /* 0x17e2: XOR EDI, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x17e8: ADD R10L, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a + b;
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x17eb: CMP ECX, EDI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x17ed: CMOVA R10L, R13L */
    if ((!ctx->cf && !ctx->zf)) {
    ctx->r10 = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    }

loc_0x17f1:
    ctx->rip = 0x17f1ULL;
    /* 0x17f1: MOVSX ECX, [RDX+0x1] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rdx + 0x1ULL)));
    /* 0x17f5: INC RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x17f8: ADD ECX, -0x30 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xffffffffffffffd0ULL); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x17fb: MOV EDI, R10L */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->r10));
    /* 0x17fe: CMP ECX, 0xa */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xaULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1801: JB .-56 */
    if (ctx->cf) goto loc_0x17cb;

loc_0x1803:
    ctx->rip = 0x1803ULL;
    /* 0x1803: LEA R8, [RIP+0x4716] */
    ctx->r8 = (uint64_t)(0x5f20ULL);
    /* 0x180a: JMP .+98 */
    goto loc_0x186e;

loc_0x180c:
    ctx->rip = 0x180cULL;
    /* 0x180c: CMP [RSP+Reg(0)+0x44], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rsp + 0x44ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1811: JNE .+6497 */
    if (!ctx->zf) goto loc_0x3178;

loc_0x1817:
    ctx->rip = 0x1817ULL;
    /* 0x1817: ADD RDX, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x2ULL); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x181b: TEST R15, R15 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x181e: LEA R8, [RIP+0x46fb] */
    ctx->r8 = (uint64_t)(0x5f20ULL);
    /* 0x1825: JE .+34 */
    if (ctx->zf) goto loc_0x1849;

loc_0x1827:
    ctx->rip = 0x1827ULL;
    /* 0x1827: MOV EDI, [R11] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->r11));
    /* 0x182a: CMP RDI, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x182e: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x1838: JA .+30 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x1858;

loc_0x183a:
    ctx->rip = 0x183aULL;
    /* 0x183a: MOV RCX, RDI */
ctx->rcx = (uint64_t)(ctx->rdi);
    /* 0x183d: ADD RCX, [R11+0x10] */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(MEM_U64(ctx->r11 + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x1841: ADD EDI, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x1844: MOV [R11], EDI */
MEM_U32(ctx->r11) = (uint32_t)(((uint32_t)ctx->rdi));
    /* 0x1847: JMP .+27 */
    goto loc_0x1864;

loc_0x1849:
    ctx->rip = 0x1849ULL;
    /* 0x1849: XOR R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }

loc_0x184c:
    ctx->rip = 0x184cULL;
    /* 0x184c: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x1856: JMP .+15 */
    goto loc_0x1867;

loc_0x1858:
    ctx->rip = 0x1858ULL;
    /* 0x1858: MOV RCX, [R11+0x8] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->r11 + 0x8ULL));
    /* 0x185c: LEA RDI, [RCX+0x8] */
    ctx->rdi = (uint64_t)(ctx->rcx + 0x8ULL);
    /* 0x1860: MOV [R11+0x8], RDI */
MEM_U64(ctx->r11 + 0x8ULL) = (uint64_t)(ctx->rdi);

loc_0x1864:
    ctx->rip = 0x1864ULL;
    /* 0x1864: MOV R10L, [RCX] */
ctx->r10 = (uint64_t)(uint32_t)(MEM_U32(ctx->rcx));

loc_0x1867:
    ctx->rip = 0x1867ULL;
    /* 0x1867: TEST R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x186a: SETNS R11B */
    ctx->r11 = (ctx->r11 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->sf ? 1 : 0)));

loc_0x186e:
    ctx->rip = 0x186eULL;
    /* 0x186e: MOV R14, RDX */
ctx->r14 = (uint64_t)(ctx->rdx);

loc_0x1871:
    ctx->rip = 0x1871ULL;
    /* 0x1871: MOVSX ECX, [R14] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->r14)));
    /* 0x1875: LEA EDX, [RCX-0x7b] */
    ctx->rdx = (uint64_t)(uint32_t)(ctx->rcx + (uint64_t)(-0x7bLL));
    /* 0x1878: MOV EBP, 0x16 */
ctx->rbp = (uint64_t)(uint32_t)(0x16ULL);
    /* 0x187d: CMP EDX, -0x3a */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0xffffffffffffffc6ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1880: JB .+6234 */
    if (ctx->cf) goto loc_0x30e0;

loc_0x1886:
    ctx->rip = 0x1886ULL;
    /* 0x1886: MOV R15L, ESI */
ctx->r15 = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x1889: MOVSXD RCX, ECX */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->rcx)));
    /* 0x188c: MOV EDX, ESI */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x188e: INC R14 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x1891: IMUL RDX, RDX, 0x3a */
    { int64_t a = (int64_t)(ctx->rdx); int64_t b = (int64_t)(0x3aULL); int64_t res = a * b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x1895: ADD RDX, R8 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x1898: MOVZX ESI, [RCX+RDX-0x41] */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(MEM_U8(ctx->rcx + ctx->rdx + (uint64_t)(-0x41LL))));
    /* 0x189d: LEA ECX, [RSI-0x1] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rsi + (uint64_t)(-0x1LL));
    /* 0x18a0: CMP ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x18a3: JB .-52 */
    if (ctx->cf) goto loc_0x1871;

loc_0x18a5:
    ctx->rip = 0x18a5ULL;
    /* 0x18a5: CMP ESI, 0x1b */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x1bULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x18a8: JE .+50 */
    if (ctx->zf) goto loc_0x18dc;

loc_0x18aa:
    ctx->rip = 0x18aaULL;
    /* 0x18aa: TEST ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x18ac: JE .+6190 */
    if (ctx->zf) goto loc_0x30e0;

loc_0x18b2:
    ctx->rip = 0x18b2ULL;
    /* 0x18b2: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x18b4: JS .+62 */
    if (ctx->sf) goto loc_0x18f4;

loc_0x18b6:
    ctx->rip = 0x18b6ULL;
    /* 0x18b6: MOV EAX, EAX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x18b8: MOV RCX, [RSP+Reg(0)+0xd0] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0xd0ULL));
    /* 0x18c0: MOV [RCX+4*RAX], ESI */
MEM_U32(ctx->rcx + (ctx->rax * 4ULL)) = (uint32_t)(((uint32_t)ctx->rsi));
    /* 0x18c3: SHL RAX, 0x4 */
    { uint64_t a = (uint64_t)(ctx->rax); uint8_t count = ((uint8_t)(0x4ULL)) & 0x3f; uint64_t res;
      res = a << count;
      set_flags_logic_u64(ctx, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x18c7: MOV RCX, [RSP+Reg(0)+0x118] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x118ULL));
    /* 0x18cf: MOVDQA X0, [RCX+RAX] */
    memcpy(&ctx->xmm[0], ctx->mem_base + (ctx->rcx + ctx->rax), 16);
    /* 0x18d4: MOVDQA [RSP+Reg(0)+0x60], X0 */
    memcpy(ctx->mem_base + (ctx->rsp + 0x60ULL), &ctx->xmm[0], 16);
    /* 0x18da: JMP .+8 */
    goto loc_0x18e4;

loc_0x18dc:
    ctx->rip = 0x18dcULL;
    /* 0x18dc: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x18de: JNS .+6140 */
    if (!ctx->sf) goto loc_0x30e0;

loc_0x18e4:
    ctx->rip = 0x18e4ULL;
    /* 0x18e4: XOR R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a ^ b;
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x18e7: CMP [RSP+Reg(0)+0x48], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x18ed: JNE .+57 */
    if (!ctx->zf) goto loc_0x1928;

loc_0x18ef:
    ctx->rip = 0x18efULL;
    /* 0x18ef: JMP .-1098 */
    goto loc_0x14aa;

loc_0x18f4:
    ctx->rip = 0x18f4ULL;
    /* 0x18f4: CMP [RSP+Reg(0)+0x48], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x18fa: JE .+6228 */
    if (ctx->zf) goto loc_0x3154;

loc_0x1900:
    ctx->rip = 0x1900ULL;
    /* 0x1900: LEA RDI, [RSP+Reg(0)+0x60] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x60ULL);
    /* 0x1905: MOV RDX, [RSP+Reg(0)+0xd8] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0xd8ULL));
    /* 0x190d: MOV R13, R10 */
ctx->r13 = (uint64_t)(ctx->r10);
    /* 0x1910: MOV RBP, R11 */
ctx->rbp = (uint64_t)(ctx->r11);
    /* 0x1913: CALL .+6250 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1918ULL;
    ctx->rip = 0x3182ULL;
    fn_0x3182(ctx);
    /* 0x1918: MOV R11, RBP */
ctx->r11 = (uint64_t)(ctx->rbp);
    /* 0x191b: MOV R10, R13 */
ctx->r10 = (uint64_t)(ctx->r13);
    /* 0x191e: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);

loc_0x1928:
    ctx->rip = 0x1928ULL;
    /* 0x1928: MOVSX EAX, [R14-0x1] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->r14 + (uint64_t)(-0x1LL))));
    /* 0x192d: MOV ECX, EAX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x192f: AND ECX, 0xf */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xfULL); uint32_t res;
      res = a & b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x1932: MOV R13L, EAX */
ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x1935: AND R13L, -0x2d */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(0xffffffffffffffd3ULL); uint32_t res;
      res = a & b;
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1939: CMP ECX, 0x3 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x3ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x193c: CMOVNE R13L, EAX */
    if (!ctx->zf) {
    ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x1940: TEST R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1943: CMOVE R13L, EAX */
    if (ctx->zf) {
    ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x1947: MOV EAX, [RSP+Reg(0)+0x1c] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x194b: MOV EBP, EAX */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x194d: AND EBP, -0x10001 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(0xfffffffffffeffffULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->rbp = (uint64_t)(uint32_t)(res);
    }
    /* 0x1953: BT EAX, 0xd */
    {
      uint32_t bit_idx = ((uint32_t)(0xdULL)) & 31;
      uint32_t val = (uint32_t)(((uint32_t)ctx->rax));
      ctx->cf = (val >> bit_idx) & 1;
    }
    /* 0x1957: CMOVB EAX, EBP */
    if (ctx->cf) {
    ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    }
    /* 0x195a: MOV [RSP+Reg(0)+0x1c], EAX */
MEM_U32(ctx->rsp + 0x1cULL) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x195e: CMP R13L, 0x52 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(0x52ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1962: JLE .+38 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x198a;

loc_0x1964:
    ctx->rip = 0x1964ULL;
    /* 0x1964: LEA EAX, [R13-0x53] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->r13 + (uint64_t)(-0x53LL));
    /* 0x1968: CMP EAX, 0x25 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x25ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x196b: JA .+1980 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x212d;

loc_0x1971:
    ctx->rip = 0x1971ULL;
    /* 0x1971: LEA RCX, [RIP+0x449c] */
    ctx->rcx = (uint64_t)(0x5e14ULL);
    /* 0x1978: MOVSXD RAX, [RCX+4*RAX] */
    ctx->rax = (uint64_t)((uint64_t)(int64_t)(int32_t)(MEM_U32(ctx->rcx + (ctx->rax * 4ULL))));
    /* 0x197c: ADD RAX, RCX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x197f: JMP RAX */
    RECOMP_POP_UNWIND();
    ctx->rip = ctx->rax;
    recomp_dispatch(ctx, ctx->rax);
    return;

loc_0x1981:
    ctx->rip = 0x1981ULL;
    /* 0x1981: MOV ECX, [RSP+Reg(0)+0x1c] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1985: JMP .+1355 */
    goto loc_0x1ed5;

loc_0x198a:
    ctx->rip = 0x198aULL;
    /* 0x198a: LEA EAX, [R13-0x45] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->r13 + (uint64_t)(-0x45LL));
    /* 0x198e: CMP EAX, 0x3 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x3ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1991: JB .+64 */
    if (ctx->cf) goto loc_0x19d3;

loc_0x1993:
    ctx->rip = 0x1993ULL;
    /* 0x1993: CMP R13L, 0x41 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(0x41ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1997: JE .+58 */
    if (ctx->zf) goto loc_0x19d3;

loc_0x1999:
    ctx->rip = 0x1999ULL;
    /* 0x1999: CMP R13L, 0x43 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(0x43ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x199d: JNE .+1930 */
    if (!ctx->zf) goto loc_0x212d;

loc_0x19a3:
    ctx->rip = 0x19a3ULL;
    /* 0x19a3: MOVZX EAX, [RSP+Reg(0)+0x60] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rsp + 0x60ULL)));
    /* 0x19a8: MOV [RSP+Reg(0)+0xc0], AX */
MEM_U16(ctx->rsp + 0xc0ULL) = (uint16_t)(((uint16_t)ctx->rax));
    /* 0x19b0: MOV [RSP+Reg(0)+0xc2], 0x0 */
MEM_U16(ctx->rsp + 0xc2ULL) = (uint16_t)(0x0ULL);
    /* 0x19ba: LEA RBX, [RSP+Reg(0)+0xc0] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0xc0ULL);
    /* 0x19c2: MOV [RSP+Reg(0)+0x60], RBX */
MEM_U64(ctx->rsp + 0x60ULL) = (uint64_t)(ctx->rbx);
    /* 0x19c7: MOV R15, -0x1 */
ctx->r15 = (uint64_t)(0xffffffffffffffffULL);
    /* 0x19ce: JMP .+1050 */
    goto loc_0x1ded;

loc_0x19d3:
    ctx->rip = 0x19d3ULL;
    /* 0x19d3: TEST R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x19d6: SETS AL */
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->sf ? 1 : 0)));
    /* 0x19d9: TEST R11B, AL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r11 & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x19dc: JNE .+5881 */
    if (!ctx->zf) goto loc_0x30db;

loc_0x19e2:
    ctx->rip = 0x19e2ULL;
    /* 0x19e2: MOV [RSP+Reg(0)+0x20], R10 */
MEM_U64(ctx->rsp + 0x20ULL) = (uint64_t)(ctx->r10);
    /* 0x19e7: FLD [RSP+Reg(0)+0x60] */
    { float f; uint32_t u = MEM_U32(ctx->rsp + 0x60ULL); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x19eb: FWAIT */
    /* fwait no-op */
    /* 0x19ec: MOV [RSP+Reg(0)+0x30], 0x0 */
MEM_U32(ctx->rsp + 0x30ULL) = (uint32_t)(0x0ULL);
    /* 0x19f4: FLD F0 */
    fpu_push(ctx, FPU_ST(0));
    /* 0x19f6: FSTP [RSP+Reg(0)+0x38] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rsp + 0x38ULL) = u; }
    /* 0x19fa: FSTP [RSP+Reg(0)] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rsp) = u; }
    /* 0x19fd: FWAIT */
    /* fwait no-op */
    /* 0x19fe: CALL .+7313 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1a03ULL;
    ctx->rip = 0x3694ULL;
    fn_0x3694(ctx);
    /* 0x1a03: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1a05: JE .+53 */
    if (ctx->zf) goto loc_0x1a3c;

loc_0x1a07:
    ctx->rip = 0x1a07ULL;
    /* 0x1a07: FLD [RSP+Reg(0)+0x38] */
    { float f; uint32_t u = MEM_U32(ctx->rsp + 0x38ULL); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x1a0b: FCHS */
    FPU_ST(0) = -FPU_ST(0);
    /* 0x1a0d: MOV EAX, 0x1 */
ctx->rax = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x1a12: MOV [RSP+Reg(0)+0x78], RAX */
MEM_U64(ctx->rsp + 0x78ULL) = (uint64_t)(ctx->rax);
    /* 0x1a17: MOV [RSP+Reg(0)+0xb8], 0x0 */
MEM_U32(ctx->rsp + 0xb8ULL) = (uint32_t)(0x0ULL);
    /* 0x1a22: LEA RBX, [RIP+0x3fce] */
    ctx->rbx = (uint64_t)(0x59f7ULL);
    /* 0x1a29: LEA R15, [RSP+Reg(0)+0x90] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x1a31: MOV R12L, 0x1d */
ctx->r12 = (uint64_t)(uint32_t)(0x1dULL);
    /* 0x1a37: JMP .+159 */
    goto loc_0x1adb;

loc_0x1a3c:
    ctx->rip = 0x1a3cULL;
    /* 0x1a3c: FLD [RSP+Reg(0)+0x38] */
    { float f; uint32_t u = MEM_U32(ctx->rsp + 0x38ULL); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x1a40: FWAIT */
    /* fwait no-op */
    /* 0x1a41: MOV EAX, [RSP+Reg(0)+0x1c] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1a45: BT EAX, 0xb */
    {
      uint32_t bit_idx = ((uint32_t)(0xbULL)) & 31;
      uint32_t val = (uint32_t)(((uint32_t)ctx->rax));
      ctx->cf = (val >> bit_idx) & 1;
    }
    /* 0x1a49: LEA R15, [RSP+Reg(0)+0x90] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x1a51: MOV R12L, 0x1d */
ctx->r12 = (uint64_t)(uint32_t)(0x1dULL);
    /* 0x1a57: JB .+102 */
    if (ctx->cf) goto loc_0x1abf;

loc_0x1a59:
    ctx->rip = 0x1a59ULL;
    /* 0x1a59: AND EAX, 0x1 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x1ULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x1a5c: MOV [RSP+Reg(0)+0x78], RAX */
MEM_U64(ctx->rsp + 0x78ULL) = (uint64_t)(ctx->rax);
    /* 0x1a61: SETE AL */
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x1a64: MOV [RSP+Reg(0)+0xb8], EAX */
MEM_U32(ctx->rsp + 0xb8ULL) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x1a6b: LEA RBX, [RIP+0x3f8b] */
    ctx->rbx = (uint64_t)(0x59fdULL);
    /* 0x1a72: LEA RAX, [RIP+0x3f7f] */
    ctx->rax = (uint64_t)(0x59f8ULL);
    /* 0x1a79: CMOVE RBX, RAX */
    if (ctx->zf) {
    ctx->rbx = (uint64_t)(ctx->rax);
    }
    /* 0x1a7d: JMP .+92 */
    goto loc_0x1adb;

loc_0x1a7f:
    ctx->rip = 0x1a7fULL;
    /* 0x1a7f: MOV RCX, [RSP+Reg(0)+0x60] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x1a84: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x1a87: JS .+1752 */
    if (ctx->sf) goto loc_0x2165;

loc_0x1a8d:
    ctx->rip = 0x1a8dULL;
    /* 0x1a8d: MOV R15L, 0x1 */
ctx->r15 = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x1a93: MOV EAX, [RSP+Reg(0)+0x1c] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1a97: BT EAX, 0xb */
    {
      uint32_t bit_idx = ((uint32_t)(0xbULL)) & 31;
      uint32_t val = (uint32_t)(((uint32_t)ctx->rax));
      ctx->cf = (val >> bit_idx) & 1;
    }
    /* 0x1a9b: JB .+2434 */
    if (ctx->cf) goto loc_0x2423;

loc_0x1aa1:
    ctx->rip = 0x1aa1ULL;
    /* 0x1aa1: MOV R15L, EAX */
ctx->r15 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x1aa4: AND R15L, 0x1 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(0x1ULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1aa8: LEA RDI, [RIP+0x3e23] */
    ctx->rdi = (uint64_t)(0x58d2ULL);
    /* 0x1aaf: LEA RAX, [RIP+0x3e1a] */
    ctx->rax = (uint64_t)(0x58d0ULL);
    /* 0x1ab6: CMOVE RDI, RAX */
    if (ctx->zf) {
    ctx->rdi = (uint64_t)(ctx->rax);
    }
    /* 0x1aba: JMP .+2411 */
    goto loc_0x242a;

loc_0x1abf:
    ctx->rip = 0x1abfULL;
    /* 0x1abf: MOV EAX, 0x1 */
ctx->rax = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x1ac4: MOV [RSP+Reg(0)+0x78], RAX */
MEM_U64(ctx->rsp + 0x78ULL) = (uint64_t)(ctx->rax);
    /* 0x1ac9: MOV [RSP+Reg(0)+0xb8], 0x0 */
MEM_U32(ctx->rsp + 0xb8ULL) = (uint32_t)(0x0ULL);
    /* 0x1ad4: LEA RBX, [RIP+0x3f1f] */
    ctx->rbx = (uint64_t)(0x59faULL);

loc_0x1adb:
    ctx->rip = 0x1adbULL;
    /* 0x1adb: FLD F0 */
    fpu_push(ctx, FPU_ST(0));
    /* 0x1add: FSTP [RSP+Reg(0)+0x38] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rsp + 0x38ULL) = u; }
    /* 0x1ae1: FSTP [RSP+Reg(0)] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rsp) = u; }
    /* 0x1ae4: FWAIT */
    /* fwait no-op */
    /* 0x1ae5: CALL .+7094 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1aeaULL;
    ctx->rip = 0x36a0ULL;
    fn_0x36a0(ctx);
    /* 0x1aea: CMP EAX, 0x1 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x1ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1aed: JLE .+170 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x1b9d;

loc_0x1af3:
    ctx->rip = 0x1af3ULL;
    /* 0x1af3: FLD [RSP+Reg(0)+0x38] */
    { float f; uint32_t u = MEM_U32(ctx->rsp + 0x38ULL); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x1af7: FSTP [RSP+Reg(0)] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rsp) = u; }
    /* 0x1afa: FWAIT */
    /* fwait no-op */
    /* 0x1afb: LEA RDI, [RSP+Reg(0)+0x30] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x30ULL);
    /* 0x1b00: CALL .+7143 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1b05ULL;
    ctx->rip = 0x36ecULL;
    fn_0x36ec(ctx);
    /* 0x1b05: MOV [RSP+Reg(0)+0x10c], 0x2 */
MEM_U32(ctx->rsp + 0x10cULL) = (uint32_t)(0x2ULL);
    /* 0x1b10: FIMUL [RSP+Reg(0)+0x10c] */
    FPU_ST(0) *= (double)(int32_t)MEM_U32(ctx->rsp + 0x10cULL);
    /* 0x1b17: FLDZ */
    fpu_push(ctx, 0.0);
    /* 0x1b19: FXCH F1 */
    { double tmp = FPU_ST(0); FPU_ST(0) = FPU_ST(1); FPU_ST(1) = tmp; }
    /* 0x1b1b: FUCOMI F0, F1 */
    { double a = FPU_ST(0); double b = FPU_ST(1);
      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }
      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }
      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }
      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }
      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }
    /* 0x1b1d: FSTP F1 */
    FPU_ST(1) = fpu_pop(ctx);
    /* 0x1b1f: FWAIT */
    /* fwait no-op */
    /* 0x1b20: SETNP AL */
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->pf ? 1 : 0)));
    /* 0x1b23: SETE CL */
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x1b26: TEST CL, AL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1b28: JNE .+297 */
    if (!ctx->zf) goto loc_0x1c57;

loc_0x1b2e:
    ctx->rip = 0x1b2eULL;
    /* 0x1b2e: MOV R9L, [RSP+Reg(0)+0x30] */
ctx->r9 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x30ULL));
    /* 0x1b33: LEA EAX, [R9-0x1] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->r9 + (uint64_t)(-0x1LL));
    /* 0x1b37: MOV [RSP+Reg(0)+0x30], EAX */
MEM_U32(ctx->rsp + 0x30ULL) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x1b3b: MOV ESI, R13L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1b3e: OR ESI, 0x20 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x20ULL); uint32_t res;
      res = a | b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x1b41: CMP ESI, 0x61 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x61ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1b44: MOVDQA X4, [RIP+0x4284] */
    memcpy(&ctx->xmm[4], ctx->mem_base + (0x5dd0ULL), 16);
    /* 0x1b4c: MOV R10, -0x3333333333333333 */
ctx->r10 = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x1b56: MOV RBP, [RSP+Reg(0)+0x48] */
ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x1b5b: JE .+284 */
    if (ctx->zf) goto loc_0x1c7d;

loc_0x1b61:
    ctx->rip = 0x1b61ULL;
    /* 0x1b61: MOV RCX, [RSP+Reg(0)+0x20] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x1b66: TEST ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1b68: MOV EAX, 0x6 */
ctx->rax = (uint64_t)(uint32_t)(0x6ULL);
    /* 0x1b6d: CMOVS ECX, EAX */
    if (ctx->sf) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x1b70: MOV [RSP+Reg(0)+0x20], RCX */
MEM_U64(ctx->rsp + 0x20ULL) = (uint64_t)(ctx->rcx);
    /* 0x1b75: MOVQ X0, [RIP+0x4263] */
    memset(&ctx->xmm[0], 0, 16);
    ctx->xmm[0].u64[0] = (uint64_t)(MEM_U64(0x5de0ULL));
    /* 0x1b7d: MOVQ [RSP+Reg(0)+0x198], X0 */
MEM_U64(ctx->rsp + 0x198ULL) = (uint64_t)(ctx->xmm[0].u64[0]);
    /* 0x1b86: FLD [RSP+Reg(0)+0x198] */
    { double d; uint64_t u = MEM_U64(ctx->rsp + 0x198ULL); memcpy(&d, &u, 8); fpu_push(ctx, d); }
    /* 0x1b8d: FMULP F1, F0 */
    FPU_ST(1) *= FPU_ST(0);
    fpu_pop(ctx);
    /* 0x1b8f: ADD R9L, -0x1d */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r9)); uint32_t b = (uint32_t)(0xffffffffffffffe3ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->r9 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1b93: MOV [RSP+Reg(0)+0x30], R9L */
MEM_U32(ctx->rsp + 0x30ULL) = (uint32_t)(((uint32_t)ctx->r9));
    /* 0x1b98: JMP .+1106 */
    goto loc_0x1fef;

loc_0x1b9d:
    ctx->rip = 0x1b9dULL;
    /* 0x1b9d: MOV R12, RBX */
ctx->r12 = (uint64_t)(ctx->rbx);
    /* 0x1ba0: TEST R13B, 0x20 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r13 & 0xff))); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1ba4: LEA R13, [RIP+0x3f79] */
    ctx->r13 = (uint64_t)(0x5b24ULL);
    /* 0x1bab: LEA RAX, [RIP+0x3fad] */
    ctx->rax = (uint64_t)(0x5b5fULL);
    /* 0x1bb2: CMOVE R13, RAX */
    if (ctx->zf) {
    ctx->r13 = (uint64_t)(ctx->rax);
    }
    /* 0x1bb6: LEA RAX, [RIP+0x4104] */
    ctx->rax = (uint64_t)(0x5cc1ULL);
    /* 0x1bbd: LEA RCX, [RIP+0x40f9] */
    ctx->rcx = (uint64_t)(0x5cbdULL);
    /* 0x1bc4: CMOVE RAX, RCX */
    if (ctx->zf) {
    ctx->rax = (uint64_t)(ctx->rcx);
    }
    /* 0x1bc8: FLD [RSP+Reg(0)+0x38] */
    { float f; uint32_t u = MEM_U32(ctx->rsp + 0x38ULL); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x1bcc: FUCOMIP F0, F0 */
    { double a = FPU_ST(0); double b = FPU_ST(0);
      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }
      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }
      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }
      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }
      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }
    fpu_pop(ctx);
    /* 0x1bce: FWAIT */
    /* fwait no-op */
    /* 0x1bcf: CMOVNE R13, RAX */
    if (!ctx->zf) {
    ctx->r13 = (uint64_t)(ctx->rax);
    }
    /* 0x1bd3: CMOVP R13, RAX */
    if (ctx->pf) {
    ctx->r13 = (uint64_t)(ctx->rax);
    }
    /* 0x1bd7: MOV R15, [RSP+Reg(0)+0x78] */
ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp + 0x78ULL));
    /* 0x1bdc: LEA EBX, [R15+0x3] */
    ctx->rbx = (uint64_t)(uint32_t)(ctx->r15 + 0x3ULL);
    /* 0x1be0: MOV RDI, [RSP+Reg(0)+0x48] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x1be5: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x1bea: MOV RDX, [RSP+Reg(0)+0x28] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x1bef: MOV ECX, EBX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    /* 0x1bf1: MOV R8L, EBP */
ctx->r8 = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x1bf4: MOV RBP, RDI */
ctx->rbp = (uint64_t)(ctx->rdi);
    /* 0x1bf7: CALL .+5964 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1bfcULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x1bfc: MOV EAX, [RBP] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp));
    /* 0x1bff: TEST AL, 0x20 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1c01: JNE .+17 */
    if (!ctx->zf) goto loc_0x1c14;

loc_0x1c03:
    ctx->rip = 0x1c03ULL;
    /* 0x1c03: MOV ESI, R15L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r15));
    /* 0x1c06: MOV RDI, R12 */
ctx->rdi = (uint64_t)(ctx->r12);
    /* 0x1c09: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x1c0c: CALL .+6443 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1c11ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x1c11: MOV EAX, [RBP] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp));

loc_0x1c14:
    ctx->rip = 0x1c14ULL;
    /* 0x1c14: TEST AL, 0x20 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1c16: JNE .+16 */
    if (!ctx->zf) goto loc_0x1c28;

loc_0x1c18:
    ctx->rip = 0x1c18ULL;
    /* 0x1c18: MOV ESI, 0x3 */
ctx->rsi = (uint64_t)(uint32_t)(0x3ULL);
    /* 0x1c1d: MOV RDI, R13 */
ctx->rdi = (uint64_t)(ctx->r13);
    /* 0x1c20: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x1c23: CALL .+6420 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1c28ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x1c28:
    ctx->rip = 0x1c28ULL;
    /* 0x1c28: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1c2d: XOR R8L, 0x2000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x2000ULL); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1c34: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x1c37: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x1c3c: MOV R13, [RSP+Reg(0)+0x28] */
ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x1c41: MOV EDX, R13L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1c44: MOV ECX, EBX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    /* 0x1c46: CALL .+5885 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1c4bULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x1c4b: CMP EBX, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1c4e: CMOVG R13L, EBX */
    if (!ctx->zf && ctx->sf == ctx->of) {
    ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    }
    /* 0x1c52: JMP .-1965 */
    goto loc_0x14aa;

loc_0x1c57:
    ctx->rip = 0x1c57ULL;
    /* 0x1c57: MOV ESI, R13L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1c5a: OR ESI, 0x20 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x20ULL); uint32_t res;
      res = a | b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x1c5d: CMP ESI, 0x61 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x61ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1c60: MOVDQA X4, [RIP+0x4168] */
    memcpy(&ctx->xmm[4], ctx->mem_base + (0x5dd0ULL), 16);
    /* 0x1c68: MOV R10, -0x3333333333333333 */
ctx->r10 = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x1c72: MOV RBP, [RSP+Reg(0)+0x48] */
ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x1c77: JNE .+857 */
    if (!ctx->zf) goto loc_0x1fd6;

loc_0x1c7d:
    ctx->rip = 0x1c7dULL;
    /* 0x1c7d: MOVQ X0, [RIP+0x4163] */
    memset(&ctx->xmm[0], 0, 16);
    ctx->xmm[0].u64[0] = (uint64_t)(MEM_U64(0x5de8ULL));
    /* 0x1c85: MOVQ [RSP+Reg(0)+0x168], X0 */
MEM_U64(ctx->rsp + 0x168ULL) = (uint64_t)(ctx->xmm[0].u64[0]);
    /* 0x1c8e: FLD [RSP+Reg(0)+0x168] */
    { double d; uint64_t u = MEM_U64(ctx->rsp + 0x168ULL); memcpy(&d, &u, 8); fpu_push(ctx, d); }
    /* 0x1c95: FWAIT */
    /* fwait no-op */
    /* 0x1c96: MOV ESI, R13L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1c99: AND ESI, 0x20 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x20ULL); uint32_t res;
      res = a & b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x1c9c: MOV EAX, ESI */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x1c9e: SHR EAX, 0x5 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint8_t count = ((uint8_t)(0x5ULL)) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x1ca1: LEA RAX, [RAX+8*RAX] */
    ctx->rax = (uint64_t)(ctx->rax + (ctx->rax * 8ULL));
    /* 0x1ca5: ADD RBX, RAX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a + b;
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x1ca8: MOV RAX, [RSP+Reg(0)+0x20] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x1cad: CMP EAX, 0xe */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xeULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1cb0: MOV [RSP+Reg(0)+0xc8], RBX */
MEM_U64(ctx->rsp + 0xc8ULL) = (uint64_t)(ctx->rbx);
    /* 0x1cb8: JA .+2447 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x264d;

loc_0x1cbe:
    ctx->rip = 0x1cbeULL;
    /* 0x1cbe: XOR EAX, 0xf */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xfULL); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x1cc1: MOV R11, [RSP+Reg(0)+0x78] */
ctx->r11 = (uint64_t)(MEM_U64(ctx->rsp + 0x78ULL));
    /* 0x1cc6: JE .+2298 */
    if (ctx->zf) goto loc_0x25c6;

loc_0x1ccc:
    ctx->rip = 0x1cccULL;
    /* 0x1ccc: MOV [RSP+Reg(0)+0xf8], 0x1 */
MEM_U32(ctx->rsp + 0xf8ULL) = (uint32_t)(0x1ULL);
    /* 0x1cd7: FIMUL [RSP+Reg(0)+0xf8] */
    FPU_ST(0) *= (double)(int32_t)MEM_U32(ctx->rsp + 0xf8ULL);
    /* 0x1cde: FWAIT */
    /* fwait no-op */

loc_0x1cdf:
    ctx->rip = 0x1cdfULL;
    /* 0x1cdf: MOV [RSP+Reg(0)+0xf4], 0x10 */
MEM_U32(ctx->rsp + 0xf4ULL) = (uint32_t)(0x10ULL);
    /* 0x1cea: FIMUL [RSP+Reg(0)+0xf4] */
    FPU_ST(0) *= (double)(int32_t)MEM_U32(ctx->rsp + 0xf4ULL);
    /* 0x1cf1: FWAIT */
    /* fwait no-op */
    /* 0x1cf2: DEC EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - 1;
      set_flags_dec_u32(ctx, a, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x1cf4: JNE .-23 */
    if (!ctx->zf) goto loc_0x1cdf;

loc_0x1cf6:
    ctx->rip = 0x1cf6ULL;
    /* 0x1cf6: CMP [RBX], 0x2d */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbx)); uint8_t b = (uint8_t)(0x2dULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1cf9: LEA RBX, [RSP+Reg(0)+0x90] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x1d01: JNE .+2392 */
    if (!ctx->zf) goto loc_0x265f;

loc_0x1d07:
    ctx->rip = 0x1d07ULL;
    /* 0x1d07: FXCH F1 */
    { double tmp = FPU_ST(0); FPU_ST(0) = FPU_ST(1); FPU_ST(1) = tmp; }
    /* 0x1d09: FCHS */
    FPU_ST(0) = -FPU_ST(0);
    /* 0x1d0b: FSUB F0, F1 */
    FPU_ST(0) -= FPU_ST(1);
    /* 0x1d0d: FADDP F1, F0 */
    FPU_ST(1) += FPU_ST(0);
    fpu_pop(ctx);
    /* 0x1d0f: FCHS */
    FPU_ST(0) = -FPU_ST(0);
    /* 0x1d11: JMP .+2381 */
    goto loc_0x2663;

loc_0x1d16:
    ctx->rip = 0x1d16ULL;
    /* 0x1d16: XOR R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a ^ b;
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1d19: CMP R15B, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r15 & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1d1d: JA .-2169 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x14aa;

loc_0x1d23:
    ctx->rip = 0x1d23ULL;
    /* 0x1d23: MOVZX EAX, R15B */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(((uint8_t)(ctx->r15 & 0xff))));
    /* 0x1d27: LEA RCX, [RIP+0x417e] */
    ctx->rcx = (uint64_t)(0x5eacULL);
    /* 0x1d2e: MOVSXD RAX, [RCX+4*RAX] */
    ctx->rax = (uint64_t)((uint64_t)(int64_t)(int32_t)(MEM_U32(ctx->rcx + (ctx->rax * 4ULL))));
    /* 0x1d32: ADD RAX, RCX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x1d35: JMP RAX */
    RECOMP_POP_UNWIND();
    ctx->rip = ctx->rax;
    recomp_dispatch(ctx, ctx->rax);
    return;

loc_0x1d37:
    ctx->rip = 0x1d37ULL;
    /* 0x1d37: MOVSXD RAX, [RSP+Reg(0)+0x34] */
    ctx->rax = (uint64_t)((uint64_t)(int64_t)(int32_t)(MEM_U32(ctx->rsp + 0x34ULL)));
    /* 0x1d3c: MOV RCX, [RSP+Reg(0)+0x60] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x1d41: MOV [RCX], RAX */
MEM_U64(ctx->rcx) = (uint64_t)(ctx->rax);
    /* 0x1d44: JMP .-2207 */
    goto loc_0x14aa;

loc_0x1d49:
    ctx->rip = 0x1d49ULL;
    /* 0x1d49: MOV AL, [RSP+Reg(0)+0x60] */
ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->rsp + 0x60ULL)));
    /* 0x1d4d: MOV [RSP+Reg(0)+0x14a], AL */
MEM_U8(ctx->rsp + 0x14aULL) = (uint8_t)(((uint8_t)(ctx->rax & 0xff)));
    /* 0x1d54: XOR R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1d57: MOV R10L, 0x1 */
ctx->r10 = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x1d5d: LEA RDX, [RSP+Reg(0)+0x14b] */
    ctx->rdx = (uint64_t)(ctx->rsp + 0x14bULL);
    /* 0x1d65: LEA RBX, [RSP+Reg(0)+0x14a] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x14aULL);
    /* 0x1d6d: MOV [RSP+Reg(0)+0x1c], EBP */
MEM_U32(ctx->rsp + 0x1cULL) = (uint32_t)(((uint32_t)ctx->rbp));
    /* 0x1d71: JMP .+962 */
    goto loc_0x2138;

loc_0x1d76:
    ctx->rip = 0x1d76ULL;
    /* 0x1d76: MOV RAX, [RSP+Reg(0)+0x60] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x1d7b: LEA RBX, [RSP+Reg(0)+0x14b] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x14bULL);
    /* 0x1d83: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x1d86: JE .+36 */
    if (ctx->zf) goto loc_0x1dac;

loc_0x1d88:
    ctx->rip = 0x1d88ULL;
    /* 0x1d88: MOV RDX, RAX */
ctx->rdx = (uint64_t)(ctx->rax);
    /* 0x1d8b: MOV RCX, RAX */
ctx->rcx = (uint64_t)(ctx->rax);

loc_0x1d8e:
    ctx->rip = 0x1d8eULL;
    /* 0x1d8e: MOV ESI, EDX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x1d90: AND SIB, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
    ctx->rsi = (ctx->rsi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x1d94: OR SIB, 0x30 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t b = (uint8_t)(0x30ULL); uint8_t res;
      res = a | b;
    ctx->rsi = (ctx->rsi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x1d98: MOV [RBX-0x1], SIB */
MEM_U8(ctx->rbx + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->rsi & 0xff)));
    /* 0x1d9c: DEC RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x1d9f: SHR RCX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint8_t count = ((uint8_t)(0x3ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x1da3: CMP RDX, 0x7 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x7ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1da7: MOV RDX, RCX */
ctx->rdx = (uint64_t)(ctx->rcx);
    /* 0x1daa: JA .-30 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x1d8e;

loc_0x1dac:
    ctx->rip = 0x1dacULL;
    /* 0x1dac: XOR R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a ^ b;
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1daf: MOV ECX, [RSP+Reg(0)+0x1c] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1db3: TEST CL, 0x8 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t b = (uint8_t)(0x8ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1db6: JE .+391 */
    if (ctx->zf) goto loc_0x1f43;

loc_0x1dbc:
    ctx->rip = 0x1dbcULL;
    /* 0x1dbc: MOVSXD RCX, R10L */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r10)));
    /* 0x1dbf: LEA RDX, [RSP+Reg(0)+0x14b] */
    ctx->rdx = (uint64_t)(ctx->rsp + 0x14bULL);
    /* 0x1dc7: SUB RDX, RBX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x1dca: CMP RDX, RCX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1dcd: JGE .+2847 */
    if (ctx->sf == ctx->of) goto loc_0x28f2;

loc_0x1dd3:
    ctx->rip = 0x1dd3ULL;
    /* 0x1dd3: MOV ECX, [RSP+Reg(0)+0x1c] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1dd7: JMP .+359 */
    goto loc_0x1f43;

loc_0x1ddc:
    ctx->rip = 0x1ddcULL;
    /* 0x1ddc: TEST R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1ddf: JE .+2749 */
    if (ctx->zf) goto loc_0x28a2;

loc_0x1de5:
    ctx->rip = 0x1de5ULL;
    /* 0x1de5: MOV RBX, [RSP+Reg(0)+0x60] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x1dea: MOVSXD R15, R10L */
    ctx->r15 = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r10)));

loc_0x1ded:
    ctx->rip = 0x1dedULL;
    /* 0x1ded: MOV R12, RBX */
ctx->r12 = (uint64_t)(ctx->rbx);
    /* 0x1df0: XOR R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }

loc_0x1df3:
    ctx->rip = 0x1df3ULL;
    /* 0x1df3: MOVZX EAX, [R12+Reg(0)] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->r12)));
    /* 0x1df8: TEST AX, AX */
    { uint16_t a = (uint16_t)(((uint16_t)ctx->rax)); uint16_t b = (uint16_t)(((uint16_t)ctx->rax)); uint16_t res;
      res = a & b;
      set_flags_logic_u16(ctx, res);
    }
    /* 0x1dfb: JE .+49 */
    if (ctx->zf) goto loc_0x1e2e;

loc_0x1dfd:
    ctx->rip = 0x1dfdULL;
    /* 0x1dfd: MOVZX ESI, AX */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(((uint16_t)ctx->rax)));
    /* 0x1e00: LEA RDI, [RSP+Reg(0)+0xc4] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0xc4ULL);
    /* 0x1e08: CALL .+6519 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1e0dULL;
    ctx->rip = 0x3784ULL;
    fn_0x3784(ctx);
    /* 0x1e0d: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1e0f: JS .+4818 */
    if (ctx->sf) goto loc_0x30e7;

loc_0x1e15:
    ctx->rip = 0x1e15ULL;
    /* 0x1e15: MOV EAX, EAX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x1e17: MOV RCX, R15 */
ctx->rcx = (uint64_t)(ctx->r15);
    /* 0x1e1a: SUB RCX, R13 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a - b;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x1e1d: CMP RCX, RAX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1e20: JB .+12 */
    if (ctx->cf) goto loc_0x1e2e;

loc_0x1e22:
    ctx->rip = 0x1e22ULL;
    /* 0x1e22: ADD R12, 0x2 */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(0x2ULL); uint64_t res;
      res = a + b;
    ctx->r12 = (uint64_t)(res);
    }
    /* 0x1e26: ADD R13, RAX */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a + b;
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x1e29: CMP R13, R15 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1e2c: JB .-59 */
    if (ctx->cf) goto loc_0x1df3;

loc_0x1e2e:
    ctx->rip = 0x1e2eULL;
    /* 0x1e2e: MOV EBP, 0x54 */
ctx->rbp = (uint64_t)(uint32_t)(0x54ULL);
    /* 0x1e33: CMP R13, 0x7fffffff */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(0x7fffffffULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1e3a: JA .+4768 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x30e0;

loc_0x1e40:
    ctx->rip = 0x1e40ULL;
    /* 0x1e40: MOV R12, [RSP+Reg(0)+0x48] */
ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x1e45: MOV RDI, R12 */
ctx->rdi = (uint64_t)(ctx->r12);
    /* 0x1e48: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x1e4d: MOV RDX, [RSP+Reg(0)+0x28] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x1e52: MOV ECX, R13L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1e55: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1e5a: CALL .+5353 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1e5fULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x1e5f: TEST R13, R13 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x1e62: JE .+2613 */
    if (ctx->zf) goto loc_0x289d;

loc_0x1e68:
    ctx->rip = 0x1e68ULL;
    /* 0x1e68: XOR R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }

loc_0x1e6b:
    ctx->rip = 0x1e6bULL;
    /* 0x1e6b: MOVZX EAX, [RBX] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rbx)));
    /* 0x1e6e: TEST AX, AX */
    { uint16_t a = (uint16_t)(((uint16_t)ctx->rax)); uint16_t b = (uint16_t)(((uint16_t)ctx->rax)); uint16_t res;
      res = a & b;
      set_flags_logic_u16(ctx, res);
    }
    /* 0x1e71: JE .+2636 */
    if (ctx->zf) goto loc_0x28c3;

loc_0x1e77:
    ctx->rip = 0x1e77ULL;
    /* 0x1e77: MOVZX ESI, AX */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(((uint16_t)ctx->rax)));
    /* 0x1e7a: LEA RDI, [RSP+Reg(0)+0xc4] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0xc4ULL);
    /* 0x1e82: CALL .+6397 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1e87ULL;
    ctx->rip = 0x3784ULL;
    fn_0x3784(ctx);
    /* 0x1e87: MOVSXD RSI, EAX */
    ctx->rsi = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->rax)));
    /* 0x1e8a: ADD R15, RSI */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->rsi); uint64_t res;
      res = a + b;
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x1e8d: CMP R15, R13 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1e90: JA .+2605 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x28c3;

loc_0x1e96:
    ctx->rip = 0x1e96ULL;
    /* 0x1e96: TEST [R12+Reg(0)], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->r12)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1e9b: JNE .+16 */
    if (!ctx->zf) goto loc_0x1ead;

loc_0x1e9d:
    ctx->rip = 0x1e9dULL;
    /* 0x1e9d: LEA RDI, [RSP+Reg(0)+0xc4] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0xc4ULL);
    /* 0x1ea5: MOV RDX, R12 */
ctx->rdx = (uint64_t)(ctx->r12);
    /* 0x1ea8: CALL .+5775 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1eadULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x1ead:
    ctx->rip = 0x1eadULL;
    /* 0x1ead: ADD RBX, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0x2ULL); uint64_t res;
      res = a + b;
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x1eb1: CMP R15, R13 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1eb4: JB .-75 */
    if (ctx->cf) goto loc_0x1e6b;

loc_0x1eb6:
    ctx->rip = 0x1eb6ULL;
    /* 0x1eb6: JMP .+2568 */
    goto loc_0x28c3;

loc_0x1ebb:
    ctx->rip = 0x1ebbULL;
    /* 0x1ebb: CMP R10L, 0x11 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(0x11ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1ebf: MOV EAX, 0x10 */
ctx->rax = (uint64_t)(uint32_t)(0x10ULL);
    /* 0x1ec4: CMOVB R10L, EAX */
    if (ctx->cf) {
    ctx->r10 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x1ec8: MOV ECX, [RSP+Reg(0)+0x1c] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x1ecc: OR ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a | b;
      set_flags_logic_u32(ctx, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x1ecf: MOV R13L, 0x78 */
ctx->r13 = (uint64_t)(uint32_t)(0x78ULL);

loc_0x1ed5:
    ctx->rip = 0x1ed5ULL;
    /* 0x1ed5: MOV RAX, [RSP+Reg(0)+0x60] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x1eda: XOR R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a ^ b;
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1edd: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x1ee0: JE .+87 */
    if (ctx->zf) goto loc_0x1f39;

loc_0x1ee2:
    ctx->rip = 0x1ee2ULL;
    /* 0x1ee2: MOV EDX, R13L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x1ee5: AND DL, 0x20 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x1ee8: LEA RBX, [RSP+Reg(0)+0x14b] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x14bULL);
    /* 0x1ef0: MOV RSI, RAX */
ctx->rsi = (uint64_t)(ctx->rax);
    /* 0x1ef3: LEA R8, [RIP+0x3e36] */
    ctx->r8 = (uint64_t)(0x5d30ULL);

loc_0x1efa:
    ctx->rip = 0x1efaULL;
    /* 0x1efa: MOV EDI, EAX */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x1efc: AND EDI, 0xf */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(0xfULL); uint32_t res;
      res = a & b;
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x1eff: MOV DIB, [RDI+R8] */
ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->rdi + ctx->r8)));
    /* 0x1f03: OR DIB, DL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t res;
      res = a | b;
    ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x1f06: MOV [RBX-0x1], DIB */
MEM_U8(ctx->rbx + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->rdi & 0xff)));
    /* 0x1f0a: DEC RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x1f0d: SHR RSI, 0x4 */
    { uint64_t a = (uint64_t)(ctx->rsi); uint8_t count = ((uint8_t)(0x4ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x1f11: CMP RAX, 0xf */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(0xfULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x1f15: MOV RAX, RSI */
ctx->rax = (uint64_t)(ctx->rsi);
    /* 0x1f18: JA .-32 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x1efa;

loc_0x1f1a:
    ctx->rip = 0x1f1aULL;
    /* 0x1f1a: MOV EAX, 0x1 */
ctx->rax = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x1f1f: TEST CL, 0x8 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t b = (uint8_t)(0x8ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x1f22: MOV RSI, [RSP+Reg(0)+0x28] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x1f27: JNE .+540 */
    if (!ctx->zf) goto loc_0x2149;

loc_0x1f2d:
    ctx->rip = 0x1f2dULL;
    /* 0x1f2d: LEA RDI, [RIP+0x399c] */
    ctx->rdi = (uint64_t)(0x58d0ULL);
    /* 0x1f34: JMP .+1344 */
    goto loc_0x2479;

loc_0x1f39:
    ctx->rip = 0x1f39ULL;
    /* 0x1f39: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x1f3b: LEA RBX, [RSP+Reg(0)+0x14b] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x14bULL);

loc_0x1f43:
    ctx->rip = 0x1f43ULL;
    /* 0x1f43: LEA RDI, [RIP+0x3986] */
    ctx->rdi = (uint64_t)(0x58d0ULL);
    /* 0x1f4a: JMP .+1317 */
    goto loc_0x2474;

loc_0x1f4f:
    ctx->rip = 0x1f4fULL;
    /* 0x1f4f: XOR R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1f52: MOV RCX, [RSP+Reg(0)+0x60] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x1f57: LEA RDI, [RIP+0x3972] */
    ctx->rdi = (uint64_t)(0x58d0ULL);
    /* 0x1f5e: JMP .+1223 */
    goto loc_0x242a;

loc_0x1f63:
    ctx->rip = 0x1f63ULL;
    /* 0x1f63: MOV RBX, R10 */
ctx->rbx = (uint64_t)(ctx->r10);
    /* 0x1f66: CALL .-6019 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1f6bULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x1f6b: MOV EDI, [RAX] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x1f6d: CALL .+6436 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1f72ULL;
    ctx->rip = 0x3896ULL;
    fn_0x3896(ctx);
    /* 0x1f72: MOV R10, RBX */
ctx->r10 = (uint64_t)(ctx->rbx);
    /* 0x1f75: MOV RBX, RAX */
ctx->rbx = (uint64_t)(ctx->rax);
    /* 0x1f78: JMP .+19 */
    goto loc_0x1f8d;

loc_0x1f7a:
    ctx->rip = 0x1f7aULL;
    /* 0x1f7a: MOV RBX, [RSP+Reg(0)+0x60] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x1f7f: TEST RBX, RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x1f82: LEA RAX, [RIP+0x3d8e] */
    ctx->rax = (uint64_t)(0x5d17ULL);
    /* 0x1f89: CMOVE RBX, RAX */
    if (ctx->zf) {
    ctx->rbx = (uint64_t)(ctx->rax);
    }

loc_0x1f8d:
    ctx->rip = 0x1f8dULL;
    /* 0x1f8d: MOV EAX, 0x7fffffff */
ctx->rax = (uint64_t)(uint32_t)(0x7fffffffULL);
    /* 0x1f92: CMP R10L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x1f95: MOV ESI, 0x7fffffff */
ctx->rsi = (uint64_t)(uint32_t)(0x7fffffffULL);
    /* 0x1f9a: CMOVB ESI, R10L */
    if (ctx->cf) {
    ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r10));
    }
    /* 0x1f9e: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x1fa1: MOV R15, R10 */
ctx->r15 = (uint64_t)(ctx->r10);
    /* 0x1fa4: CALL .+9559 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x1fa9ULL;
    ctx->rip = 0x4500ULL;
    fn_0x4500(ctx);
    /* 0x1fa9: LEA RDX, [RBX+RAX] */
    ctx->rdx = (uint64_t)(ctx->rbx + ctx->rax);
    /* 0x1fad: TEST R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1fb0: MOV RSI, [RSP+Reg(0)+0x28] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x1fb5: JNS .+9 */
    if (!ctx->sf) goto loc_0x1fc0;

loc_0x1fb7:
    ctx->rip = 0x1fb7ULL;
    /* 0x1fb7: CMP [RDX], 0x0 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rdx)); uint8_t b = (uint8_t)(0x0ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x1fba: JNE .+4379 */
    if (!ctx->zf) goto loc_0x30db;

loc_0x1fc0:
    ctx->rip = 0x1fc0ULL;
    /* 0x1fc0: XOR R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x1fc3: MOV [RSP+Reg(0)+0x1c], EBP */
MEM_U32(ctx->rsp + 0x1cULL) = (uint32_t)(((uint32_t)ctx->rbp));
    /* 0x1fc7: MOV R10L, EAX */
ctx->r10 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x1fca: LEA RDI, [RIP+0x38ff] */
    ctx->rdi = (uint64_t)(0x58d0ULL);
    /* 0x1fd1: JMP .+1278 */
    goto loc_0x24d4;

loc_0x1fd6:
    ctx->rip = 0x1fd6ULL;
    /* 0x1fd6: MOV RCX, [RSP+Reg(0)+0x20] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x1fdb: TEST ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1fdd: MOV EAX, 0x6 */
ctx->rax = (uint64_t)(uint32_t)(0x6ULL);
    /* 0x1fe2: CMOVS ECX, EAX */
    if (ctx->sf) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x1fe5: MOV [RSP+Reg(0)+0x20], RCX */
MEM_U64(ctx->rsp + 0x20ULL) = (uint64_t)(ctx->rcx);
    /* 0x1fea: MOV R9L, [RSP+Reg(0)+0x30] */
ctx->r9 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x30ULL));

loc_0x1fef:
    ctx->rip = 0x1fefULL;
    /* 0x1fef: TEST R9L, R9L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r9)); uint32_t b = (uint32_t)(((uint32_t)ctx->r9)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x1ff2: MOV ECX, 0x6ea */
ctx->rcx = (uint64_t)(uint32_t)(0x6eaULL);
    /* 0x1ff7: MOV EAX, 0x0 */
ctx->rax = (uint64_t)(uint32_t)(0x0ULL);
    /* 0x1ffc: CMOVS RCX, RAX */
    if (ctx->sf) {
    ctx->rcx = (uint64_t)(ctx->rax);
    }
    /* 0x2000: MOV [RSP+Reg(0)+0x58], RCX */
MEM_U64(ctx->rsp + 0x58ULL) = (uint64_t)(ctx->rcx);
    /* 0x2005: LEA R10, [RSP+4*RCX] */
    ctx->r10 = (uint64_t)(ctx->rsp + (ctx->rcx * 4ULL));
    /* 0x2009: ADD R10, 0x1a0 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0x1a0ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x2010: MOV [RSP+Reg(0)+0x38], R10 */
MEM_U64(ctx->rsp + 0x38ULL) = (uint64_t)(ctx->r10);

loc_0x2015:
    ctx->rip = 0x2015ULL;
    /* 0x2015: FNSTCW [RSP+Reg(0)+0x54] */
    MEM_U16(ctx->rsp + 0x54ULL) = ctx->fpu_cw;
    /* 0x2019: MOVZX EAX, [RSP+Reg(0)+0x54] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rsp + 0x54ULL)));
    /* 0x201e: OR EAX, 0xc00 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xc00ULL); uint32_t res;
      res = a | b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2023: MOV [RSP+Reg(0)+0x80], AX */
MEM_U16(ctx->rsp + 0x80ULL) = (uint16_t)(((uint16_t)ctx->rax));
    /* 0x202b: FLDCW [RSP+Reg(0)+0x80] */
    ctx->fpu_cw = MEM_U16(ctx->rsp + 0x80ULL);
    /* 0x2032: FLD F0 */
    fpu_push(ctx, FPU_ST(0));
    /* 0x2034: FISTP [RSP+Reg(0)+0x190] */
    MEM_U64(ctx->rsp + 0x190ULL) = (uint64_t)(int64_t)round(fpu_pop(ctx));
    /* 0x203b: FLDCW [RSP+Reg(0)+0x54] */
    ctx->fpu_cw = MEM_U16(ctx->rsp + 0x54ULL);
    /* 0x203f: MOV EAX, [RSP+Reg(0)+0x190] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x190ULL));
    /* 0x2046: MOV [R10], EAX */
MEM_U32(ctx->r10) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x2049: MOV [RSP+Reg(0)+0x108], 0x3b9aca00 */
MEM_U32(ctx->rsp + 0x108ULL) = (uint32_t)(0x3b9aca00ULL);
    /* 0x2054: ADD R10, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x2058: MOV [RSP+Reg(0)+0x188], RAX */
MEM_U64(ctx->rsp + 0x188ULL) = (uint64_t)(ctx->rax);
    /* 0x2060: FILD [RSP+Reg(0)+0x188] */
    fpu_push(ctx, (double)(int64_t)MEM_U64(ctx->rsp + 0x188ULL));
    /* 0x2067: FSUBP F1, F0 */
    FPU_ST(1) -= FPU_ST(0);
    fpu_pop(ctx);
    /* 0x2069: FIMUL [RSP+Reg(0)+0x108] */
    FPU_ST(0) *= (double)(int32_t)MEM_U32(ctx->rsp + 0x108ULL);
    /* 0x2070: FLDZ */
    fpu_push(ctx, 0.0);
    /* 0x2072: FXCH F1 */
    { double tmp = FPU_ST(0); FPU_ST(0) = FPU_ST(1); FPU_ST(1) = tmp; }
    /* 0x2074: FUCOMI F0, F1 */
    { double a = FPU_ST(0); double b = FPU_ST(1);
      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }
      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }
      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }
      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }
      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }
    /* 0x2076: FSTP F1 */
    FPU_ST(1) = fpu_pop(ctx);
    /* 0x2078: FWAIT */
    /* fwait no-op */
    /* 0x2079: SETP AL */
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->pf ? 1 : 0)));
    /* 0x207c: SETNE CL */
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x207f: OR CL, AL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t res;
      res = a | b;
      set_flags_logic_u8(ctx, res);
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2081: JNE .-110 */
    if (!ctx->zf) goto loc_0x2015;

loc_0x2083:
    ctx->rip = 0x2083ULL;
    /* 0x2083: FSTP F0 */
    FPU_ST(0) = fpu_pop(ctx);
    /* 0x2085: FWAIT */
    /* fwait no-op */
    /* 0x2086: MOV [RSP+Reg(0)+0xc8], RBX */
MEM_U64(ctx->rsp + 0xc8ULL) = (uint64_t)(ctx->rbx);
    /* 0x208e: TEST R9L, R9L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r9)); uint32_t b = (uint32_t)(((uint32_t)ctx->r9)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2091: JLE .+232 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x217f;

loc_0x2097:
    ctx->rip = 0x2097ULL;
    /* 0x2097: MOV RBX, [RSP+Reg(0)+0x38] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    /* 0x209c: MOV R11L, R9L */
ctx->r11 = (uint64_t)(uint32_t)(((uint32_t)ctx->r9));

loc_0x209f:
    ctx->rip = 0x209fULL;
    /* 0x209f: CMP R11L, 0x1d */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(0x1dULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x20a3: CMOVGE R11L, R12L */
    if (ctx->sf == ctx->of) {
    ctx->r11 = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    }
    /* 0x20a7: LEA R8, [R10-0x4] */
    ctx->r8 = (uint64_t)(ctx->r10 + (uint64_t)(-0x4LL));
    /* 0x20ab: CMP R8, RBX */
    { uint64_t a = (uint64_t)(ctx->r8); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x20ae: JB .+81 */
    if (ctx->cf) goto loc_0x2101;

loc_0x20b0:
    ctx->rip = 0x20b0ULL;
    /* 0x20b0: MOV ECX, R11L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x20b3: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x20b5: MOV RDI, 0x44b82fa09b5a53 */
ctx->rdi = (uint64_t)(0x44b82fa09b5a53ULL);

loc_0x20bf:
    ctx->rip = 0x20bfULL;
    /* 0x20bf: MOV EAX, [R8] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->r8));
    /* 0x20c2: SHL RAX, CL */
    { uint64_t a = (uint64_t)(ctx->rax); uint8_t count = ((uint8_t)(((uint8_t)(ctx->rcx & 0xff)))) & 0x3f; uint64_t res;
      res = a << count;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x20c5: MOV R15L, EDX */
ctx->r15 = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x20c8: ADD R15, RAX */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a + b;
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x20cb: MOV RAX, R15 */
ctx->rax = (uint64_t)(ctx->r15);
    /* 0x20ce: SHR RAX, 0x9 */
    { uint64_t a = (uint64_t)(ctx->rax); uint8_t count = ((uint8_t)(0x9ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x20d2: MUL RDI */
    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(ctx->rdi);
      ctx->rax = (uint64_t)res;
      ctx->rdx = (uint64_t)(res >> 64);
      ctx->cf = ctx->of = (ctx->rdx != 0);
    }
    /* 0x20d5: SHR RDX, 0xb */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0xbULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x20d9: IMUL EAX, EDX, 0x3b9aca00 */
    { int32_t a = (int32_t)(((uint32_t)ctx->rdx)); int32_t b = (int32_t)(0x3b9aca00ULL); int32_t res = a * b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x20df: SUB R15L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x20e2: MOV [R8], R15L */
MEM_U32(ctx->r8) = (uint32_t)(((uint32_t)ctx->r15));
    /* 0x20e5: ADD R8, -0x4 */
    { uint64_t a = (uint64_t)(ctx->r8); uint64_t b = (uint64_t)(0xfffffffffffffffcULL); uint64_t res;
      res = a + b;
    ctx->r8 = (uint64_t)(res);
    }
    /* 0x20e9: CMP R8, RBX */
    { uint64_t a = (uint64_t)(ctx->r8); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x20ec: JAE .-47 */
    if (!ctx->cf) goto loc_0x20bf;

loc_0x20ee:
    ctx->rip = 0x20eeULL;
    /* 0x20ee: TEST EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x20f0: JE .+7 */
    if (ctx->zf) goto loc_0x20f9;

loc_0x20f2:
    ctx->rip = 0x20f2ULL;
    /* 0x20f2: MOV [RBX-0x4], EDX */
MEM_U32(ctx->rbx + (uint64_t)(-0x4LL)) = (uint32_t)(((uint32_t)ctx->rdx));
    /* 0x20f5: ADD RBX, -0x4 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0xfffffffffffffffcULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rbx = (uint64_t)(res);
    }

loc_0x20f9:
    ctx->rip = 0x20f9ULL;
    /* 0x20f9: LEA R15, [RSP+Reg(0)+0x90] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x90ULL);

loc_0x2101:
    ctx->rip = 0x2101ULL;
    /* 0x2101: MOV R8, R10 */
ctx->r8 = (uint64_t)(ctx->r10);
    /* 0x2104: CMP R10, RBX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2107: JBE .+11 */
    if ((ctx->cf || ctx->zf)) goto loc_0x2114;

loc_0x2109:
    ctx->rip = 0x2109ULL;
    /* 0x2109: LEA R10, [R8-0x4] */
    ctx->r10 = (uint64_t)(ctx->r8 + (uint64_t)(-0x4LL));
    /* 0x210d: CMP [R8-0x4], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->r8 + (uint64_t)(-0x4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2112: JE .-19 */
    if (ctx->zf) goto loc_0x2101;

loc_0x2114:
    ctx->rip = 0x2114ULL;
    /* 0x2114: MOV ECX, [RSP+Reg(0)+0x30] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x30ULL));
    /* 0x2118: MOV R10, R8 */
ctx->r10 = (uint64_t)(ctx->r8);
    /* 0x211b: SUB ECX, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x211e: MOV [RSP+Reg(0)+0x30], ECX */
MEM_U32(ctx->rsp + 0x30ULL) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2122: MOV R11L, ECX */
ctx->r11 = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2125: JG .-140 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x209f;

loc_0x212b:
    ctx->rip = 0x212bULL;
    /* 0x212b: JMP .+93 */
    goto loc_0x218a;

loc_0x212d:
    ctx->rip = 0x212dULL;
    /* 0x212d: XOR R15L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r15 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2130: LEA RDX, [RSP+Reg(0)+0x14b] */
    ctx->rdx = (uint64_t)(ctx->rsp + 0x14bULL);

loc_0x2138:
    ctx->rip = 0x2138ULL;
    /* 0x2138: LEA RDI, [RIP+0x3791] */
    ctx->rdi = (uint64_t)(0x58d0ULL);
    /* 0x213f: MOV RSI, [RSP+Reg(0)+0x28] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x2144: JMP .+907 */
    goto loc_0x24d4;

loc_0x2149:
    ctx->rip = 0x2149ULL;
    /* 0x2149: SAR R13L, 0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint8_t count = ((uint8_t)(0x4ULL)) & 0x3f; uint32_t res;
      res = (uint32_t)(((int32_t)a) >> count);
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x214d: MOVSXD RDI, R13L */
    ctx->rdi = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r13)));
    /* 0x2150: LEA RDX, [RIP+0x3779] */
    ctx->rdx = (uint64_t)(0x58d0ULL);
    /* 0x2157: ADD RDI, RDX */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x215a: MOV R15L, 0x2 */
ctx->r15 = (uint64_t)(uint32_t)(0x2ULL);
    /* 0x2160: JMP .+788 */
    goto loc_0x2479;

loc_0x2165:
    ctx->rip = 0x2165ULL;
    /* 0x2165: NEG RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t res;
      res = -a;
      set_flags_sub_u64(ctx, 0, a, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x2168: MOV [RSP+Reg(0)+0x60], RCX */
MEM_U64(ctx->rsp + 0x60ULL) = (uint64_t)(ctx->rcx);
    /* 0x216d: MOV R15L, 0x1 */
ctx->r15 = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x2173: LEA RDI, [RIP+0x3756] */
    ctx->rdi = (uint64_t)(0x58d0ULL);
    /* 0x217a: JMP .+688 */
    goto loc_0x242f;

loc_0x217f:
    ctx->rip = 0x217fULL;
    /* 0x217f: MOV ECX, R9L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r9));
    /* 0x2182: MOV R8, R10 */
ctx->r8 = (uint64_t)(ctx->r10);
    /* 0x2185: MOV RBX, [RSP+Reg(0)+0x38] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));

loc_0x218a:
    ctx->rip = 0x218aULL;
    /* 0x218a: TEST ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x218c: JNS .+188 */
    if (!ctx->sf) goto loc_0x224e;

loc_0x2192:
    ctx->rip = 0x2192ULL;
    /* 0x2192: MOV RAX, [RSP+Reg(0)+0x20] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2197: ADD EAX, 0x1d */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x1dULL); uint32_t res;
      res = a + b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x219a: IMUL RAX, RAX, 0x38e38e39 */
    { int64_t a = (int64_t)(ctx->rax); int64_t b = (int64_t)(0x38e38e39ULL); int64_t res = a * b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x21a1: SHR RAX, 0x21 */
    { uint64_t a = (uint64_t)(ctx->rax); uint8_t count = ((uint8_t)(0x21ULL)) & 0x3f; uint64_t res;
      res = a >> count;
      set_flags_logic_u64(ctx, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x21a5: INC EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }

loc_0x21a7:
    ctx->rip = 0x21a7ULL;
    /* 0x21a7: NEG ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = -a;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x21a9: CMP ECX, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x21ac: MOV EDX, 0x9 */
ctx->rdx = (uint64_t)(uint32_t)(0x9ULL);
    /* 0x21b1: CMOVGE ECX, EDX */
    if (ctx->sf == ctx->of) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    }
    /* 0x21b4: CMP RBX, R8 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x21b7: JAE .+87 */
    if (!ctx->cf) goto loc_0x2210;

loc_0x21b9:
    ctx->rip = 0x21b9ULL;
    /* 0x21b9: MOV EDX, -0x1 */
ctx->rdx = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x21be: SHL EDX, CL */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint8_t count = ((uint8_t)(((uint8_t)(ctx->rcx & 0xff)))) & 0x3f; uint32_t res;
      res = a << count;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x21c0: MOV R10L, 0x3b9aca00 */
ctx->r10 = (uint64_t)(uint32_t)(0x3b9aca00ULL);
    /* 0x21c6: SHR R10L, CL */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint8_t count = ((uint8_t)(((uint8_t)(ctx->rcx & 0xff)))) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x21c9: NOT EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = ~a;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x21cb: XOR R11L, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x21ce: MOV R15, RBX */
ctx->r15 = (uint64_t)(ctx->rbx);

loc_0x21d1:
    ctx->rip = 0x21d1ULL;
    /* 0x21d1: MOV EBP, R11L */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x21d4: MOV R12L, [R15] */
ctx->r12 = (uint64_t)(uint32_t)(MEM_U32(ctx->r15));
    /* 0x21d7: MOV R11L, R12L */
ctx->r11 = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x21da: AND R11L, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x21dd: IMUL R11L, R10L */
    { int32_t a = (int32_t)(((uint32_t)ctx->r11)); int32_t b = (int32_t)(((uint32_t)ctx->r10)); int32_t res = a * b;
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x21e1: SHR R12L, CL */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint8_t count = ((uint8_t)(((uint8_t)(ctx->rcx & 0xff)))) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }
    /* 0x21e4: ADD R12L, EBP */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t res;
      res = a + b;
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }
    /* 0x21e7: MOV [R15], R12L */
MEM_U32(ctx->r15) = (uint32_t)(((uint32_t)ctx->r12));
    /* 0x21ea: ADD R15, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x21ee: CMP R15, R8 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x21f1: JB .-34 */
    if (ctx->cf) goto loc_0x21d1;

loc_0x21f3:
    ctx->rip = 0x21f3ULL;
    /* 0x21f3: MOV EDX, [RBX] */
ctx->rdx = (uint64_t)(uint32_t)(MEM_U32(ctx->rbx));
    /* 0x21f5: TEST R11L, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x21f8: JE .+7 */
    if (ctx->zf) goto loc_0x2201;

loc_0x21fa:
    ctx->rip = 0x21faULL;
    /* 0x21fa: MOV [R8], R11L */
MEM_U32(ctx->r8) = (uint32_t)(((uint32_t)ctx->r11));
    /* 0x21fd: ADD R8, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r8); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r8 = (uint64_t)(res);
    }

loc_0x2201:
    ctx->rip = 0x2201ULL;
    /* 0x2201: MOV RBP, [RSP+Reg(0)+0x48] */
ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x2206: LEA R15, [RSP+Reg(0)+0x90] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x220e: JMP .+2 */
    goto loc_0x2212;

loc_0x2210:
    ctx->rip = 0x2210ULL;
    /* 0x2210: MOV EDX, [RBX] */
ctx->rdx = (uint64_t)(uint32_t)(MEM_U32(ctx->rbx));

loc_0x2212:
    ctx->rip = 0x2212ULL;
    /* 0x2212: XOR R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a ^ b;
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2215: TEST EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2217: SETE R10B */
    ctx->r10 = (ctx->r10 & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x221b: CMP ESI, 0x66 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x66ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x221e: LEA RBX, [RBX+4*R10] */
    ctx->rbx = (uint64_t)(ctx->rbx + (ctx->r10 * 4ULL));
    /* 0x2222: MOV RDX, RBX */
ctx->rdx = (uint64_t)(ctx->rbx);
    /* 0x2225: CMOVE RDX, [RSP+Reg(0)+0x38] */
    if (ctx->zf) {
    ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    }
    /* 0x222b: MOV R10, R8 */
ctx->r10 = (uint64_t)(ctx->r8);
    /* 0x222e: SUB R10, RDX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a - b;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x2231: SAR R10, 0x2 */
    { uint64_t a = (uint64_t)(ctx->r10); uint8_t count = ((uint8_t)(0x2ULL)) & 0x3f; uint64_t res;
      res = (uint64_t)(((int64_t)a) >> count);
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x2235: CMP R10, RAX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2238: LEA RDX, [RDX+4*RAX] */
    ctx->rdx = (uint64_t)(ctx->rdx + (ctx->rax * 4ULL));
    /* 0x223c: CMOVG R8, RDX */
    if (!ctx->zf && ctx->sf == ctx->of) {
    ctx->r8 = (uint64_t)(ctx->rdx);
    }
    /* 0x2240: ADD ECX, [RSP+Reg(0)+0x30] */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(MEM_U32(ctx->rsp + 0x30ULL)); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2244: MOV [RSP+Reg(0)+0x30], ECX */
MEM_U32(ctx->rsp + 0x30ULL) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2248: JS .-167 */
    if (ctx->sf) goto loc_0x21a7;

loc_0x224e:
    ctx->rip = 0x224eULL;
    /* 0x224e: XOR R11L, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a ^ b;
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2251: CMP RBX, R8 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2254: MOV R10, -0x3333333333333333 */
ctx->r10 = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x225e: JAE .+40 */
    if (!ctx->cf) goto loc_0x2288;

loc_0x2260:
    ctx->rip = 0x2260ULL;
    /* 0x2260: MOV RAX, [RSP+Reg(0)+0x38] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    /* 0x2265: SUB RAX, RBX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x2268: SHR RAX, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rax); uint8_t count = ((uint8_t)(0x2ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x226c: LEA R11L, [RAX+8*RAX] */
    ctx->r11 = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 8ULL));
    /* 0x2270: MOV EAX, [RBX] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbx));
    /* 0x2272: CMP EAX, 0xa */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xaULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2275: JB .+17 */
    if (ctx->cf) goto loc_0x2288;

loc_0x2277:
    ctx->rip = 0x2277ULL;
    /* 0x2277: MOV ECX, 0xa */
ctx->rcx = (uint64_t)(uint32_t)(0xaULL);

loc_0x227c:
    ctx->rip = 0x227cULL;
    /* 0x227c: ADD ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x227e: LEA ECX, [RCX+4*RCX] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rcx + (ctx->rcx * 4ULL));
    /* 0x2281: INC R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2284: CMP EAX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2286: JAE .-12 */
    if (!ctx->cf) goto loc_0x227c;

loc_0x2288:
    ctx->rip = 0x2288ULL;
    /* 0x2288: CMP ESI, 0x66 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x66ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x228b: MOV ECX, R11L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x228e: MOV EAX, 0x0 */
ctx->rax = (uint64_t)(uint32_t)(0x0ULL);
    /* 0x2293: CMOVE ECX, EAX */
    if (ctx->zf) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x2296: MOV RDX, [RSP+Reg(0)+0x20] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x229b: MOV EAX, EDX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x229d: SUB EAX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x229f: CMP ESI, 0x67 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x67ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x22a2: SETE CL */
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x22a5: TEST EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x22a7: SETNE DL */
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x22aa: AND DL, CL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t res;
      res = a & b;
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x22ac: MOVZX ECX, DL */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(((uint8_t)(ctx->rdx & 0xff))));
    /* 0x22af: SUB EAX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x22b1: MOVSXD RCX, EAX */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->rax)));
    /* 0x22b4: MOV RDX, R8 */
ctx->rdx = (uint64_t)(ctx->r8);
    /* 0x22b7: SUB RDX, [RSP+Reg(0)+0x38] */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL)); uint64_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x22bc: SAR RDX, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0x2ULL)) & 0x3f; uint64_t res;
      res = (uint64_t)(((int64_t)a) >> count);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x22c0: LEA RDX, [RDX+8*RDX] */
    ctx->rdx = (uint64_t)(ctx->rdx + (ctx->rdx * 8ULL));
    /* 0x22c4: ADD RDX, -0x9 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0xfffffffffffffff7ULL); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x22c8: CMP RDX, RCX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x22cb: JLE .+328 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x2419;

loc_0x22d1:
    ctx->rip = 0x22d1ULL;
    /* 0x22d1: MOV RDI, R11 */
ctx->rdi = (uint64_t)(ctx->r11);
    /* 0x22d4: TEST R9L, R9L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r9)); uint32_t b = (uint32_t)(((uint32_t)ctx->r9)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x22d7: MOV R9L, 0x6eb */
ctx->r9 = (uint64_t)(uint32_t)(0x6ebULL);
    /* 0x22dd: MOV ECX, 0x1 */
ctx->rcx = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x22e2: CMOVS R9, RCX */
    if (ctx->sf) {
    ctx->r9 = (uint64_t)(ctx->rcx);
    }
    /* 0x22e6: ADD EAX, 0x24000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x24000ULL); uint32_t res;
      res = a + b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x22eb: MOVSXD RDX, EAX */
    ctx->rdx = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->rax)));
    /* 0x22ee: IMUL RAX, RDX, 0x38e38e39 */
    { int64_t a = (int64_t)(ctx->rdx); int64_t b = (int64_t)(0x38e38e39ULL); int64_t res = a * b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x22f5: MOV RCX, RAX */
ctx->rcx = (uint64_t)(ctx->rax);
    /* 0x22f8: SHR RCX, 0x3f */
    { uint64_t a = (uint64_t)(ctx->rcx); uint8_t count = ((uint8_t)(0x3fULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x22fc: SAR RAX, 0x21 */
    { uint64_t a = (uint64_t)(ctx->rax); uint8_t count = ((uint8_t)(0x21ULL)) & 0x3f; uint64_t res;
      res = (uint64_t)(((int64_t)a) >> count);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x2300: ADD EAX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a + b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2302: LEA ECX, [RAX+8*RAX] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 8ULL));
    /* 0x2305: SUB EDX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2307: MOV R11L, 0xa */
ctx->r11 = (uint64_t)(uint32_t)(0xaULL);
    /* 0x230d: CMP EDX, 0x7 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0x7ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2310: JG .+175 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x23c5;

loc_0x2316:
    ctx->rip = 0x2316ULL;
    /* 0x2316: MOV ECX, 0xb */
ctx->rcx = (uint64_t)(uint32_t)(0xbULL);
    /* 0x231b: SUB ECX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a - b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x231d: AND ECX, -0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xfffffffffffffffcULL); uint32_t res;
      res = a & b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2320: MOV R10L, 0x7 */
ctx->r10 = (uint64_t)(uint32_t)(0x7ULL);
    /* 0x2326: SUB R10L, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2329: MOVD X0, R10L */
    memset(&ctx->xmm[0], 0, 16);
    ctx->xmm[0].u32[0] = (uint32_t)(((uint32_t)ctx->r10));
    /* 0x232e: PSHUFD X0, X0, 0x0 */
    {
      xmm_reg_t src = ctx->xmm[0];
      ctx->xmm[0].u32[0] = src.u32[0];
      ctx->xmm[0].u32[1] = src.u32[0];
      ctx->xmm[0].u32[2] = src.u32[0];
      ctx->xmm[0].u32[3] = src.u32[0];
    }
    /* 0x2333: MOV EDX, 0x4 */
ctx->rdx = (uint64_t)(uint32_t)(0x4ULL);
    /* 0x2338: MOVDQA X2, [RIP+0x3a00] */
    memcpy(&ctx->xmm[2], ctx->mem_base + (0x5d40ULL), 16);

loc_0x2340:
    ctx->rip = 0x2340ULL;
    /* 0x2340: MOVDQA X1, X2 */
    ctx->xmm[1] = ctx->xmm[2];
    /* 0x2344: PMULUDQ X2, X4 */
    ctx->xmm[2].u64[0] = (uint64_t)ctx->xmm[2].u32[0] * (uint64_t)(ctx->xmm[4].u32[0]);
    ctx->xmm[2].u64[1] = (uint64_t)ctx->xmm[2].u32[2] * (uint64_t)(ctx->xmm[4].u32[2]);
    /* 0x2348: PSHUFD X2, X2, 0xe8 */
    {
      xmm_reg_t src = ctx->xmm[2];
      ctx->xmm[2].u32[0] = src.u32[0];
      ctx->xmm[2].u32[1] = src.u32[2];
      ctx->xmm[2].u32[2] = src.u32[2];
      ctx->xmm[2].u32[3] = src.u32[3];
    }
    /* 0x234d: PSHUFD X3, X1, 0xf5 */
    {
      xmm_reg_t src = ctx->xmm[1];
      ctx->xmm[3].u32[0] = src.u32[1];
      ctx->xmm[3].u32[1] = src.u32[1];
      ctx->xmm[3].u32[2] = src.u32[3];
      ctx->xmm[3].u32[3] = src.u32[3];
    }
    /* 0x2352: PMULUDQ X3, X4 */
    ctx->xmm[3].u64[0] = (uint64_t)ctx->xmm[3].u32[0] * (uint64_t)(ctx->xmm[4].u32[0]);
    ctx->xmm[3].u64[1] = (uint64_t)ctx->xmm[3].u32[2] * (uint64_t)(ctx->xmm[4].u32[2]);
    /* 0x2356: PSHUFD X3, X3, 0xe8 */
    {
      xmm_reg_t src = ctx->xmm[3];
      ctx->xmm[3].u32[0] = src.u32[0];
      ctx->xmm[3].u32[1] = src.u32[2];
      ctx->xmm[3].u32[2] = src.u32[2];
      ctx->xmm[3].u32[3] = src.u32[3];
    }
    /* 0x235b: PUNPCKLDQ X2, X3 */
    { uint32_t d0 = ctx->xmm[2].u32[0]; uint32_t d1 = ctx->xmm[2].u32[1];
      uint32_t s0 = ctx->xmm[3].u32[0]; uint32_t s1 = ctx->xmm[3].u32[1];
      ctx->xmm[2].u32[0] = d0; ctx->xmm[2].u32[1] = s0;
      ctx->xmm[2].u32[2] = d1; ctx->xmm[2].u32[3] = s1; }
    /* 0x235f: MOV R10L, EDX */
ctx->r10 = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x2362: ADD EDX, -0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0xfffffffffffffffcULL); uint32_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2365: ADD R10L, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a + b;
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2368: ADD R10L, -0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(0xfffffffffffffffcULL); uint32_t res;
      res = a + b;
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x236c: CMP R10L, 0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(0x4ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2370: JNE .-50 */
    if (!ctx->zf) goto loc_0x2340;

loc_0x2372:
    ctx->rip = 0x2372ULL;
    /* 0x2372: NEG EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = -a;
      set_flags_sub_u32(ctx, 0, a, res);
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2374: MOVD X3, EDX */
    memset(&ctx->xmm[3], 0, 16);
    ctx->xmm[3].u32[0] = (uint32_t)(((uint32_t)ctx->rdx));
    /* 0x2378: PSHUFD X3, X3, 0x0 */
    {
      xmm_reg_t src = ctx->xmm[3];
      ctx->xmm[3].u32[0] = src.u32[0];
      ctx->xmm[3].u32[1] = src.u32[0];
      ctx->xmm[3].u32[2] = src.u32[0];
      ctx->xmm[3].u32[3] = src.u32[0];
    }
    /* 0x237d: POR X3, [RIP+0x39cb] */
    ctx->xmm[3].u64[0] = ctx->xmm[3].u64[0]  |  MEM_U64(0x5d50ULL);
    ctx->xmm[3].u64[1] = ctx->xmm[3].u64[1]  |  MEM_U64((0x5d50ULL) + 8);
    /* 0x2385: MOVDQA X4, [RIP+0x3a23] */
    memcpy(&ctx->xmm[4], ctx->mem_base + (0x5db0ULL), 16);
    /* 0x238d: PXOR X0, X4 */
    ctx->xmm[0].u64[0] = ctx->xmm[0].u64[0]  ^  ctx->xmm[4].u64[0];
    ctx->xmm[0].u64[1] = ctx->xmm[0].u64[1]  ^  ctx->xmm[4].u64[1];
    /* 0x2391: PXOR X3, X4 */
    ctx->xmm[3].u64[0] = ctx->xmm[3].u64[0]  ^  ctx->xmm[4].u64[0];
    ctx->xmm[3].u64[1] = ctx->xmm[3].u64[1]  ^  ctx->xmm[4].u64[1];
    /* 0x2395: PCMPGTD X3, X0 */
    ctx->xmm[3].u32[0] = ((int32_t)ctx->xmm[3].u32[0] > (int32_t)(ctx->xmm[0].u32[0])) ? 0xFFFFFFFFU : 0;
    ctx->xmm[3].u32[1] = ((int32_t)ctx->xmm[3].u32[1] > (int32_t)(ctx->xmm[0].u32[1])) ? 0xFFFFFFFFU : 0;
    ctx->xmm[3].u32[2] = ((int32_t)ctx->xmm[3].u32[2] > (int32_t)(ctx->xmm[0].u32[2])) ? 0xFFFFFFFFU : 0;
    ctx->xmm[3].u32[3] = ((int32_t)ctx->xmm[3].u32[3] > (int32_t)(ctx->xmm[0].u32[3])) ? 0xFFFFFFFFU : 0;
    /* 0x2399: PAND X1, X3 */
    ctx->xmm[1].u64[0] = ctx->xmm[1].u64[0]  &  ctx->xmm[3].u64[0];
    ctx->xmm[1].u64[1] = ctx->xmm[1].u64[1]  &  ctx->xmm[3].u64[1];
    /* 0x239d: PANDN X3, X2 */
    ctx->xmm[3].u64[0] = (~ctx->xmm[3].u64[0]) & ctx->xmm[2].u64[0];
    ctx->xmm[3].u64[1] = (~ctx->xmm[3].u64[1]) & ctx->xmm[2].u64[1];
    /* 0x23a1: POR X3, X1 */
    ctx->xmm[3].u64[0] = ctx->xmm[3].u64[0]  |  ctx->xmm[1].u64[0];
    ctx->xmm[3].u64[1] = ctx->xmm[3].u64[1]  |  ctx->xmm[1].u64[1];
    /* 0x23a5: PSHUFD X0, X3, 0xee */
    {
      xmm_reg_t src = ctx->xmm[3];
      ctx->xmm[0].u32[0] = src.u32[2];
      ctx->xmm[0].u32[1] = src.u32[3];
      ctx->xmm[0].u32[2] = src.u32[2];
      ctx->xmm[0].u32[3] = src.u32[3];
    }
    /* 0x23aa: PSHUFD X1, X3, 0xff */
    {
      xmm_reg_t src = ctx->xmm[3];
      ctx->xmm[1].u32[0] = src.u32[3];
      ctx->xmm[1].u32[1] = src.u32[3];
      ctx->xmm[1].u32[2] = src.u32[3];
      ctx->xmm[1].u32[3] = src.u32[3];
    }
    /* 0x23af: PSHUFD X2, X3, 0xf5 */
    {
      xmm_reg_t src = ctx->xmm[3];
      ctx->xmm[2].u32[0] = src.u32[1];
      ctx->xmm[2].u32[1] = src.u32[1];
      ctx->xmm[2].u32[2] = src.u32[3];
      ctx->xmm[2].u32[3] = src.u32[3];
    }
    /* 0x23b4: PMULUDQ X2, X1 */
    ctx->xmm[2].u64[0] = (uint64_t)ctx->xmm[2].u32[0] * (uint64_t)(ctx->xmm[1].u32[0]);
    ctx->xmm[2].u64[1] = (uint64_t)ctx->xmm[2].u32[2] * (uint64_t)(ctx->xmm[1].u32[2]);
    /* 0x23b8: PMULUDQ X0, X3 */
    ctx->xmm[0].u64[0] = (uint64_t)ctx->xmm[0].u32[0] * (uint64_t)(ctx->xmm[3].u32[0]);
    ctx->xmm[0].u64[1] = (uint64_t)ctx->xmm[0].u32[2] * (uint64_t)(ctx->xmm[3].u32[2]);
    /* 0x23bc: PMULUDQ X0, X2 */
    ctx->xmm[0].u64[0] = (uint64_t)ctx->xmm[0].u32[0] * (uint64_t)(ctx->xmm[2].u32[0]);
    ctx->xmm[0].u64[1] = (uint64_t)ctx->xmm[0].u32[2] * (uint64_t)(ctx->xmm[2].u32[2]);
    /* 0x23c0: MOVD R11L, X0 */
ctx->r11 = (uint64_t)(uint32_t)(ctx->xmm[0].u32[0]);

loc_0x23c5:
    ctx->rip = 0x23c5ULL;
    /* 0x23c5: LEA R10, [RSP+4*R9] */
    ctx->r10 = (uint64_t)(ctx->rsp + (ctx->r9 * 4ULL));
    /* 0x23c9: ADD R10, 0x1a0 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0x1a0ULL); uint64_t res;
      res = a + b;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x23d0: MOVSXD R12, EAX */
    ctx->r12 = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->rax)));
    /* 0x23d3: LEA RBP, [R10+4*R12] */
    ctx->rbp = (uint64_t)(ctx->r10 + (ctx->r12 * 4ULL));
    /* 0x23d7: MOV ECX, [RBP+0xffff0000] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rbp + (uint64_t)(-0x10000LL)));
    /* 0x23dd: MOV EAX, ECX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x23df: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x23e1: DIV R11L */
    { uint64_t dividend = ((uint64_t)(uint32_t)ctx->rdx << 32) | (uint32_t)ctx->rax;
      uint32_t divisor = (uint32_t)(((uint32_t)ctx->r11));
      if (divisor != 0) {
        ctx->rax = (uint64_t)(uint32_t)(dividend / divisor);
        ctx->rdx = (uint64_t)(uint32_t)(dividend % divisor);
      }
    }
    /* 0x23e4: LEA R10, [R10+4*R12] */
    ctx->r10 = (uint64_t)(ctx->r10 + (ctx->r12 * 4ULL));
    /* 0x23e8: ADD R10, -0x10000 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0xffffffffffff0000ULL); uint64_t res;
      res = a + b;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x23ef: LEA R15, [RBP+0xffff0004] */
    ctx->r15 = (uint64_t)(ctx->rbp + (uint64_t)(-0xfffcLL));
    /* 0x23f6: TEST EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x23f8: JNE .+472 */
    if (!ctx->zf) goto loc_0x25d6;

loc_0x23fe:
    ctx->rip = 0x23feULL;
    /* 0x23fe: CMP R15, R8 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2401: JNE .+463 */
    if (!ctx->zf) goto loc_0x25d6;

loc_0x2407:
    ctx->rip = 0x2407ULL;
    /* 0x2407: MOV RBP, [RSP+Reg(0)+0x48] */
ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x240c: LEA R15, [RSP+Reg(0)+0x90] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x2414: JMP .+1532 */
    goto loc_0x2a15;

loc_0x2419:
    ctx->rip = 0x2419ULL;
    /* 0x2419: MOV RDX, [RSP+Reg(0)+0x58] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x58ULL));
    /* 0x241e: JMP .+1551 */
    goto loc_0x2a32;

loc_0x2423:
    ctx->rip = 0x2423ULL;
    /* 0x2423: LEA RDI, [RIP+0x34a7] */
    ctx->rdi = (uint64_t)(0x58d1ULL);

loc_0x242a:
    ctx->rip = 0x242aULL;
    /* 0x242a: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x242d: JE .+55 */
    if (ctx->zf) goto loc_0x2466;

loc_0x242f:
    ctx->rip = 0x242fULL;
    /* 0x242f: LEA RBX, [RSP+Reg(0)+0x14b] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x14bULL);

loc_0x2437:
    ctx->rip = 0x2437ULL;
    /* 0x2437: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x243a: MUL R9 */
    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(ctx->r9);
      ctx->rax = (uint64_t)res;
      ctx->rdx = (uint64_t)(res >> 64);
      ctx->cf = ctx->of = (ctx->rdx != 0);
    }
    /* 0x243d: SHR RDX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0x3ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2441: LEA EAX, [RDX+RDX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rdx + ctx->rdx);
    /* 0x2444: LEA EAX, [RAX+4*RAX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 4ULL));
    /* 0x2447: MOV ESI, ECX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2449: SUB ESI, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x244b: OR SIB, 0x30 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t b = (uint8_t)(0x30ULL); uint8_t res;
      res = a | b;
    ctx->rsi = (ctx->rsi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x244f: MOV [RBX-0x1], SIB */
MEM_U8(ctx->rbx + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->rsi & 0xff)));
    /* 0x2453: DEC RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x2456: CMP RCX, 0x9 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x9ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x245a: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x245d: JA .-40 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x2437;

loc_0x245f:
    ctx->rip = 0x245fULL;
    /* 0x245f: MOV EAX, 0x1 */
ctx->rax = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x2464: JMP .+10 */
    goto loc_0x2470;

loc_0x2466:
    ctx->rip = 0x2466ULL;
    /* 0x2466: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2468: LEA RBX, [RSP+Reg(0)+0x14b] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x14bULL);

loc_0x2470:
    ctx->rip = 0x2470ULL;
    /* 0x2470: MOV ECX, [RSP+Reg(0)+0x1c] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));

loc_0x2474:
    ctx->rip = 0x2474ULL;
    /* 0x2474: MOV RSI, [RSP+Reg(0)+0x28] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));

loc_0x2479:
    ctx->rip = 0x2479ULL;
    /* 0x2479: TEST R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x247c: SETS DL */
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->sf ? 1 : 0)));
    /* 0x247f: TEST R11B, DL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r11 & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2482: JNE .+3155 */
    if (!ctx->zf) goto loc_0x30db;

loc_0x2488:
    ctx->rip = 0x2488ULL;
    /* 0x2488: MOV EDX, ECX */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x248a: AND EDX, -0x10001 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0xfffffffffffeffffULL); uint32_t res;
      res = a & b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2490: TEST R11B, R11B */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r11 & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->r11 & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2493: CMOVE EDX, ECX */
    if (ctx->zf) {
    ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    }
    /* 0x2496: MOV [RSP+Reg(0)+0x1c], EDX */
MEM_U32(ctx->rsp + 0x1cULL) = (uint32_t)(((uint32_t)ctx->rdx));
    /* 0x249a: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x249d: JNE .+21 */
    if (!ctx->zf) goto loc_0x24b4;

loc_0x249f:
    ctx->rip = 0x249fULL;
    /* 0x249f: TEST R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x24a2: JNE .+16 */
    if (!ctx->zf) goto loc_0x24b4;

loc_0x24a4:
    ctx->rip = 0x24a4ULL;
    /* 0x24a4: XOR R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r10 = (uint64_t)(uint32_t)(res);
    }
    /* 0x24a7: LEA RBX, [RSP+Reg(0)+0x14b] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x14bULL);
    /* 0x24af: MOV RDX, RBX */
ctx->rdx = (uint64_t)(ctx->rbx);
    /* 0x24b2: JMP .+32 */
    goto loc_0x24d4;

loc_0x24b4:
    ctx->rip = 0x24b4ULL;
    /* 0x24b4: MOVSXD RCX, R10L */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r10)));
    /* 0x24b7: LEA RDX, [RSP+Reg(0)+0x14b] */
    ctx->rdx = (uint64_t)(ctx->rsp + 0x14bULL);
    /* 0x24bf: MOV R10, RDX */
ctx->r10 = (uint64_t)(ctx->rdx);
    /* 0x24c2: SUB R10, RBX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x24c5: CMP RAX, 0x1 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(0x1ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x24c9: ADC R10, 0x0 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a + b + (uint64_t)ctx->cf;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x24cd: CMP R10, RCX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x24d0: CMOVLE R10, RCX */
    if (ctx->zf || ctx->sf != ctx->of) {
    ctx->r10 = (uint64_t)(ctx->rcx);
    }

loc_0x24d4:
    ctx->rip = 0x24d4ULL;
    /* 0x24d4: MOVSXD R13, R10L */
    ctx->r13 = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r10)));
    /* 0x24d7: SUB RDX, RBX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x24da: CMP RDX, R13 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x24dd: CMOVG R13L, EDX */
    if (!ctx->zf && ctx->sf == ctx->of) {
    ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    }
    /* 0x24e1: MOV EAX, R15L */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->r15));
    /* 0x24e4: XOR EAX, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x24e9: CMP R13L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x24ec: JG .+3049 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30db;

loc_0x24f2:
    ctx->rip = 0x24f2ULL;
    /* 0x24f2: MOV [RSP+Reg(0)+0x38], RDX */
MEM_U64(ctx->rsp + 0x38ULL) = (uint64_t)(ctx->rdx);
    /* 0x24f7: LEA ECX, [R15+R13] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->r15 + ctx->r13);
    /* 0x24fb: CMP ESI, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x24fd: CMOVLE ESI, ECX */
    if (ctx->zf || ctx->sf != ctx->of) {
    ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    }
    /* 0x2500: MOV EBP, 0x54 */
ctx->rbp = (uint64_t)(uint32_t)(0x54ULL);
    /* 0x2505: CMP ESI, R12L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->r12)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2508: JG .+3026 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30e0;

loc_0x250e:
    ctx->rip = 0x250eULL;
    /* 0x250e: MOV RAX, RSI */
ctx->rax = (uint64_t)(ctx->rsi);
    /* 0x2511: MOV [RSP+Reg(0)+0x20], RDI */
MEM_U64(ctx->rsp + 0x20ULL) = (uint64_t)(ctx->rdi);
    /* 0x2516: MOV RBP, R15 */
ctx->rbp = (uint64_t)(ctx->r15);
    /* 0x2519: MOV R15, [RSP+Reg(0)+0x48] */
ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x251e: MOV RDI, R15 */
ctx->rdi = (uint64_t)(ctx->r15);
    /* 0x2521: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x2526: MOV R12, RAX */
ctx->r12 = (uint64_t)(ctx->rax);
    /* 0x2529: MOV EDX, R12L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x252c: MOV [RSP+Reg(0)+0x28], ECX */
MEM_U32(ctx->rsp + 0x28ULL) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2530: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x2535: CALL .+3598 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x253aULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x253a: TEST [R15], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->r15)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x253e: JNE .+15 */
    if (!ctx->zf) goto loc_0x254f;

loc_0x2540:
    ctx->rip = 0x2540ULL;
    /* 0x2540: MOV ESI, EBP */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x2542: MOV RDI, [RSP+Reg(0)+0x20] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2547: MOV RDX, R15 */
ctx->rdx = (uint64_t)(ctx->r15);
    /* 0x254a: CALL .+4077 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x254fULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x254f:
    ctx->rip = 0x254fULL;
    /* 0x254f: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x2554: XOR R8L, 0x10000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x10000ULL); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x255b: MOV RDI, R15 */
ctx->rdi = (uint64_t)(ctx->r15);
    /* 0x255e: MOV ESI, 0x30 */
ctx->rsi = (uint64_t)(uint32_t)(0x30ULL);
    /* 0x2563: MOV EDX, R12L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x2566: MOV ECX, [RSP+Reg(0)+0x28] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x28ULL));
    /* 0x256a: CALL .+3545 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x256fULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x256f: MOV RDI, R15 */
ctx->rdi = (uint64_t)(ctx->r15);
    /* 0x2572: MOV ESI, 0x30 */
ctx->rsi = (uint64_t)(uint32_t)(0x30ULL);
    /* 0x2577: MOV EDX, R13L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x257a: MOV R13, [RSP+Reg(0)+0x38] */
ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    /* 0x257f: MOV ECX, R13L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x2582: XOR R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2585: CALL .+3518 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x258aULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x258a: TEST [R15], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->r15)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x258e: JNE .+14 */
    if (!ctx->zf) goto loc_0x259e;

loc_0x2590:
    ctx->rip = 0x2590ULL;
    /* 0x2590: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x2593: MOV RSI, R13 */
ctx->rsi = (uint64_t)(ctx->r13);
    /* 0x2596: MOV RDX, R15 */
ctx->rdx = (uint64_t)(ctx->r15);
    /* 0x2599: CALL .+3998 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x259eULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x259e:
    ctx->rip = 0x259eULL;
    /* 0x259e: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x25a3: XOR R8L, 0x2000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x2000ULL); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x25aa: MOV RDI, R15 */
ctx->rdi = (uint64_t)(ctx->r15);
    /* 0x25ad: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x25b2: MOV EDX, R12L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x25b5: MOV ECX, [RSP+Reg(0)+0x28] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x28ULL));
    /* 0x25b9: CALL .+3466 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x25beULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x25be: MOV R13L, R12L */
ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x25c1: JMP .-4380 */
    goto loc_0x14aa;

loc_0x25c6:
    ctx->rip = 0x25c6ULL;
    /* 0x25c6: FSTP F0 */
    FPU_ST(0) = fpu_pop(ctx);
    /* 0x25c8: FWAIT */
    /* fwait no-op */
    /* 0x25c9: LEA RBX, [RSP+Reg(0)+0x90] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x25d1: JMP .+141 */
    goto loc_0x2663;

loc_0x25d6:
    ctx->rip = 0x25d6ULL;
    /* 0x25d6: MOV [RSP+Reg(0)+0x104], 0x2 */
MEM_U32(ctx->rsp + 0x104ULL) = (uint32_t)(0x2ULL);
    /* 0x25e1: FILD [RSP+Reg(0)+0x104] */
    fpu_push(ctx, (double)(int32_t)MEM_U32(ctx->rsp + 0x104ULL));
    /* 0x25e8: FDIV [RIP+0x3822] */
    { float f; uint32_t u = MEM_U32(0x5e10ULL); memcpy(&f, &u, 4); FPU_ST(0) /= (double)f; }
    /* 0x25ee: FWAIT */
    /* fwait no-op */
    /* 0x25ef: TEST AL, 0x1 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x1ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x25f1: JNE .+23 */
    if (!ctx->zf) goto loc_0x260a;

loc_0x25f3:
    ctx->rip = 0x25f3ULL;
    /* 0x25f3: CMP R11L, 0x3b9aca00 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(0x3b9aca00ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x25fa: JNE .+33 */
    if (!ctx->zf) goto loc_0x261d;

loc_0x25fc:
    ctx->rip = 0x25fcULL;
    /* 0x25fc: CMP R10, RBX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x25ff: JBE .+28 */
    if ((ctx->cf || ctx->zf)) goto loc_0x261d;

loc_0x2601:
    ctx->rip = 0x2601ULL;
    /* 0x2601: TEST [RBP+0xfffefffc], 0x1 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp + (uint64_t)(-0x10004LL))); uint8_t b = (uint8_t)(0x1ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2608: JE .+19 */
    if (ctx->zf) goto loc_0x261d;

loc_0x260a:
    ctx->rip = 0x260aULL;
    /* 0x260a: MOV [RSP+Reg(0)+0x100], 0x2 */
MEM_U32(ctx->rsp + 0x100ULL) = (uint32_t)(0x2ULL);
    /* 0x2615: FIADD [RSP+Reg(0)+0x100] */
    FPU_ST(0) += (double)(int32_t)MEM_U32(ctx->rsp + 0x100ULL);
    /* 0x261c: FWAIT */
    /* fwait no-op */

loc_0x261d:
    ctx->rip = 0x261dULL;
    /* 0x261d: MOV EAX, R11L */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x2620: SHR EAX, 0x1 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint8_t count = ((uint8_t)(0x1ULL)) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2622: CMP EDX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2624: MOV RBP, [RSP+Reg(0)+0x48] */
ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x2629: JAE .+721 */
    if (!ctx->cf) goto loc_0x2900;

loc_0x262f:
    ctx->rip = 0x262fULL;
    /* 0x262f: MOVQ X0, [RIP+0x37c1] */
    memset(&ctx->xmm[0], 0, 16);
    ctx->xmm[0].u64[0] = (uint64_t)(MEM_U64(0x5df8ULL));
    /* 0x2637: MOVQ [RSP+Reg(0)+0x170], X0 */
MEM_U64(ctx->rsp + 0x170ULL) = (uint64_t)(ctx->xmm[0].u64[0]);
    /* 0x2640: FLD [RSP+Reg(0)+0x170] */
    { double d; uint64_t u = MEM_U64(ctx->rsp + 0x170ULL); memcpy(&d, &u, 8); fpu_push(ctx, d); }
    /* 0x2647: FWAIT */
    /* fwait no-op */
    /* 0x2648: JMP .+799 */
    goto loc_0x296c;

loc_0x264d:
    ctx->rip = 0x264dULL;
    /* 0x264d: FSTP F0 */
    FPU_ST(0) = fpu_pop(ctx);
    /* 0x264f: FWAIT */
    /* fwait no-op */
    /* 0x2650: LEA RBX, [RSP+Reg(0)+0x90] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x2658: MOV R11, [RSP+Reg(0)+0x78] */
ctx->r11 = (uint64_t)(MEM_U64(ctx->rsp + 0x78ULL));
    /* 0x265d: JMP .+4 */
    goto loc_0x2663;

loc_0x265f:
    ctx->rip = 0x265fULL;
    /* 0x265f: FADD F1, F0 */
    FPU_ST(1) += FPU_ST(0);
    /* 0x2661: FSUBP F1, F0 */
    FPU_ST(1) -= FPU_ST(0);
    fpu_pop(ctx);

loc_0x2663:
    ctx->rip = 0x2663ULL;
    /* 0x2663: MOV EDI, [RSP+Reg(0)+0x30] */
ctx->rdi = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x30ULL));
    /* 0x2667: TEST EDI, EDI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2669: JE .+51 */
    if (ctx->zf) goto loc_0x269e;

loc_0x266b:
    ctx->rip = 0x266bULL;
    /* 0x266b: MOV ECX, EDI */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));
    /* 0x266d: NEG ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = -a;
      set_flags_sub_u32(ctx, 0, a, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x266f: CMOVS ECX, EDI */
    if (ctx->sf) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));
    }

loc_0x2672:
    ctx->rip = 0x2672ULL;
    /* 0x2672: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x2675: MUL R10 */
    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(ctx->r10);
      ctx->rax = (uint64_t)res;
      ctx->rdx = (uint64_t)(res >> 64);
      ctx->cf = ctx->of = (ctx->rdx != 0);
    }
    /* 0x2678: SHR RDX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0x3ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x267c: LEA EAX, [RDX+RDX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rdx + ctx->rdx);
    /* 0x267f: LEA EAX, [RAX+4*RAX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 4ULL));
    /* 0x2682: MOV R8L, ECX */
ctx->r8 = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2685: SUB R8L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2688: OR R8B, 0x30 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r8 & 0xff))); uint8_t b = (uint8_t)(0x30ULL); uint8_t res;
      res = a | b;
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x268c: MOV [RBX-0x1], R8B */
MEM_U8(ctx->rbx + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->r8 & 0xff)));
    /* 0x2690: DEC RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x2693: CMP RCX, 0xa */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xaULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2697: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x269a: JAE .-42 */
    if (!ctx->cf) goto loc_0x2672;

loc_0x269c:
    ctx->rip = 0x269cULL;
    /* 0x269c: JMP .+16 */
    goto loc_0x26ae;

loc_0x269e:
    ctx->rip = 0x269eULL;
    /* 0x269e: MOV [RSP+Reg(0)+0x8f], 0x30 */
MEM_U8(ctx->rsp + 0x8fULL) = (uint8_t)(0x30ULL);
    /* 0x26a6: LEA RBX, [RSP+Reg(0)+0x8f] */
    ctx->rbx = (uint64_t)(ctx->rsp + 0x8fULL);

loc_0x26ae:
    ctx->rip = 0x26aeULL;
    /* 0x26ae: OR R11L, 0x2 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(0x2ULL); uint32_t res;
      res = a | b;
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x26b2: SHR EDI, 0x1f */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint8_t count = ((uint8_t)(0x1fULL)) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->rdi = (uint64_t)(uint32_t)(res);
    }
    /* 0x26b5: ADD DIB, DIB */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t res;
      res = a + b;
    ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x26b8: ADD DIB, 0x2b */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t b = (uint8_t)(0x2bULL); uint8_t res;
      res = a + b;
    ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x26bc: MOV [RBX-0x1], DIB */
MEM_U8(ctx->rbx + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->rdi & 0xff)));
    /* 0x26c0: ADD R13B, 0xf */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r13 & 0xff))); uint8_t b = (uint8_t)(0xfULL); uint8_t res;
      res = a + b;
    ctx->r13 = (ctx->r13 & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x26c4: MOV [RBX-0x2], R13B */
MEM_U8(ctx->rbx + (uint64_t)(-0x2LL)) = (uint8_t)(((uint8_t)(ctx->r13 & 0xff)));
    /* 0x26c8: ADD RBX, -0x2 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0xfffffffffffffffeULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x26cc: MOV RAX, R15 */
ctx->rax = (uint64_t)(ctx->r15);
    /* 0x26cf: LEA R8, [RIP+0x365a] */
    ctx->r8 = (uint64_t)(0x5d30ULL);
    /* 0x26d6: MOV R9, [RSP+Reg(0)+0x20] */
ctx->r9 = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));

loc_0x26db:
    ctx->rip = 0x26dbULL;
    /* 0x26db: FNSTCW [RSP+Reg(0)+0x56] */
    MEM_U16(ctx->rsp + 0x56ULL) = ctx->fpu_cw;
    /* 0x26df: MOVZX ECX, [RSP+Reg(0)+0x56] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rsp + 0x56ULL)));
    /* 0x26e4: OR ECX, 0xc00 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xc00ULL); uint32_t res;
      res = a | b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x26ea: MOV [RSP+Reg(0)+0x82], CX */
MEM_U16(ctx->rsp + 0x82ULL) = (uint16_t)(((uint16_t)ctx->rcx));
    /* 0x26f2: FLDCW [RSP+Reg(0)+0x82] */
    ctx->fpu_cw = MEM_U16(ctx->rsp + 0x82ULL);
    /* 0x26f9: FIST [RSP+Reg(0)+0xe8] */
    MEM_U32(ctx->rsp + 0xe8ULL) = (uint32_t)(int32_t)round(FPU_ST(0));
    /* 0x2700: FLDCW [RSP+Reg(0)+0x56] */
    ctx->fpu_cw = MEM_U16(ctx->rsp + 0x56ULL);
    /* 0x2704: MOV RCX, RAX */
ctx->rcx = (uint64_t)(ctx->rax);
    /* 0x2707: MOVSXD RDX, [RSP+Reg(0)+0xe8] */
    ctx->rdx = (uint64_t)((uint64_t)(int64_t)(int32_t)(MEM_U32(ctx->rsp + 0xe8ULL)));
    /* 0x270f: MOV DIB, [RDX+R8] */
ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(MEM_U8(ctx->rdx + ctx->r8)));
    /* 0x2713: OR DIB, SIB */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a | b;
    ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2716: INC RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x2719: MOV [RCX], DIB */
MEM_U8(ctx->rcx) = (uint8_t)(((uint8_t)(ctx->rdi & 0xff)));
    /* 0x271c: MOV [RSP+Reg(0)+0xf0], 0x10 */
MEM_U32(ctx->rsp + 0xf0ULL) = (uint32_t)(0x10ULL);
    /* 0x2727: MOV [RSP+Reg(0)+0xec], EDX */
MEM_U32(ctx->rsp + 0xecULL) = (uint32_t)(((uint32_t)ctx->rdx));
    /* 0x272e: FISUB [RSP+Reg(0)+0xec] */
    FPU_ST(0) -= (double)(int32_t)MEM_U32(ctx->rsp + 0xecULL);
    /* 0x2735: FIMUL [RSP+Reg(0)+0xf0] */
    FPU_ST(0) *= (double)(int32_t)MEM_U32(ctx->rsp + 0xf0ULL);
    /* 0x273c: FWAIT */
    /* fwait no-op */
    /* 0x273d: MOV RDX, R15 */
ctx->rdx = (uint64_t)(ctx->r15);
    /* 0x2740: NEG RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = -a;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2743: ADD RDX, RCX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2746: INC RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2749: CMP RDX, 0x1 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x1ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x274d: JNE .+44 */
    if (!ctx->zf) goto loc_0x277b;

loc_0x274f:
    ctx->rip = 0x274fULL;
    /* 0x274f: FLDZ */
    fpu_push(ctx, 0.0);
    /* 0x2751: FXCH F1 */
    { double tmp = FPU_ST(0); FPU_ST(0) = FPU_ST(1); FPU_ST(1) = tmp; }
    /* 0x2753: FUCOMI F0, F1 */
    { double a = FPU_ST(0); double b = FPU_ST(1);
      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }
      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }
      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }
      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }
      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }
    /* 0x2755: FSTP F1 */
    FPU_ST(1) = fpu_pop(ctx);
    /* 0x2757: FWAIT */
    /* fwait no-op */
    /* 0x2758: SETP DL */
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->pf ? 1 : 0)));
    /* 0x275b: SETNE DIB */
    ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x275f: TEST [RSP+Reg(0)+0x1c], 0x8 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rsp + 0x1cULL)); uint8_t b = (uint8_t)(0x8ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2764: JNE .+10 */
    if (!ctx->zf) goto loc_0x2770;

loc_0x2766:
    ctx->rip = 0x2766ULL;
    /* 0x2766: TEST R9L, R9L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r9)); uint32_t b = (uint32_t)(((uint32_t)ctx->r9)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2769: JG .+5 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x2770;

loc_0x276b:
    ctx->rip = 0x276bULL;
    /* 0x276b: OR DIB, DL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t res;
      res = a | b;
      set_flags_logic_u8(ctx, res);
    ctx->rdi = (ctx->rdi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x276e: JE .+11 */
    if (ctx->zf) goto loc_0x277b;

loc_0x2770:
    ctx->rip = 0x2770ULL;
    /* 0x2770: MOV [RCX+0x1], 0x2e */
MEM_U8(ctx->rcx + 0x1ULL) = (uint8_t)(0x2eULL);
    /* 0x2774: ADD RCX, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x2ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x2778: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);

loc_0x277b:
    ctx->rip = 0x277bULL;
    /* 0x277b: FLDZ */
    fpu_push(ctx, 0.0);
    /* 0x277d: FXCH F1 */
    { double tmp = FPU_ST(0); FPU_ST(0) = FPU_ST(1); FPU_ST(1) = tmp; }
    /* 0x277f: FUCOMI F0, F1 */
    { double a = FPU_ST(0); double b = FPU_ST(1);
      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }
      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }
      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }
      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }
      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }
    /* 0x2781: FSTP F1 */
    FPU_ST(1) = fpu_pop(ctx);
    /* 0x2783: FWAIT */
    /* fwait no-op */
    /* 0x2784: SETP CL */
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->pf ? 1 : 0)));
    /* 0x2787: SETNE DL */
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x278a: OR DL, CL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t res;
      res = a | b;
      set_flags_logic_u8(ctx, res);
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x278c: JNE .-183 */
    if (!ctx->zf) goto loc_0x26db;

loc_0x2792:
    ctx->rip = 0x2792ULL;
    /* 0x2792: FSTP F0 */
    FPU_ST(0) = fpu_pop(ctx);
    /* 0x2794: FWAIT */
    /* fwait no-op */
    /* 0x2795: MOVSXD RCX, R9L */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r9)));
    /* 0x2798: LEA R13, [RSP+Reg(0)+0x90] */
    ctx->r13 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x27a0: SUB R13, RBX */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x27a3: MOV EDI, R11L */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x27a6: MOV ESI, 0x7ffffffd */
ctx->rsi = (uint64_t)(uint32_t)(0x7ffffffdULL);
    /* 0x27ab: SUB RSI, R13 */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a - b;
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x27ae: SUB RSI, RDI */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - b;
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x27b1: CMP RSI, RCX */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x27b4: JL .+2337 */
    if (ctx->sf != ctx->of) goto loc_0x30db;

loc_0x27ba:
    ctx->rip = 0x27baULL;
    /* 0x27ba: MOV R12, R11 */
ctx->r12 = (uint64_t)(ctx->r11);
    /* 0x27bd: MOV [RSP+Reg(0)+0x78], RDI */
MEM_U64(ctx->rsp + 0x78ULL) = (uint64_t)(ctx->rdi);
    /* 0x27c2: MOV RDX, RAX */
ctx->rdx = (uint64_t)(ctx->rax);
    /* 0x27c5: SUB RDX, R15 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x27c8: MOV [RSP+Reg(0)+0x38], RDX */
MEM_U64(ctx->rsp + 0x38ULL) = (uint64_t)(ctx->rdx);
    /* 0x27cd: ADD RDX, -0x2 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0xfffffffffffffffeULL); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x27d1: MOV ESI, R13L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x27d4: SUB ESI, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a - b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x27d7: ADD EAX, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a + b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x27d9: CMP RDX, RCX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x27dc: MOV RCX, [RSP+Reg(0)+0x20] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x27e1: LEA EDX, [RCX+R13+0x2] */
    ctx->rdx = (uint64_t)(uint32_t)(ctx->rcx + ctx->r13 + 0x2ULL);
    /* 0x27e6: CMOVGE EDX, EAX */
    if (ctx->sf == ctx->of) {
    ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x27e9: TEST ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x27eb: CMOVE EDX, EAX */
    if (ctx->zf) {
    ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x27ee: MOV [RSP+Reg(0)+0x20], EDX */
MEM_U32(ctx->rsp + 0x20ULL) = (uint32_t)(((uint32_t)ctx->rdx));
    /* 0x27f2: ADD R12L, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a + b;
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }
    /* 0x27f5: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x27f8: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x27fd: MOV RDX, [RSP+Reg(0)+0x28] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x2802: MOV ECX, R12L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x2805: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x280a: CALL .+2873 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x280fULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x280f: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2813: JNE .+21 */
    if (!ctx->zf) goto loc_0x282a;

loc_0x2815:
    ctx->rip = 0x2815ULL;
    /* 0x2815: MOV RDI, [RSP+Reg(0)+0xc8] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0xc8ULL));
    /* 0x281d: MOV RSI, [RSP+Reg(0)+0x78] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rsp + 0x78ULL));
    /* 0x2822: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2825: CALL .+3346 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x282aULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x282a:
    ctx->rip = 0x282aULL;
    /* 0x282a: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x282f: XOR R8L, 0x10000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x10000ULL); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2836: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x2839: MOV ESI, 0x30 */
ctx->rsi = (uint64_t)(uint32_t)(0x30ULL);
    /* 0x283e: MOV RDX, [RSP+Reg(0)+0x28] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x2843: MOV ECX, R12L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x2846: CALL .+2813 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x284bULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x284b: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x284f: JNE .+16 */
    if (!ctx->zf) goto loc_0x2861;

loc_0x2851:
    ctx->rip = 0x2851ULL;
    /* 0x2851: MOV RDI, R15 */
ctx->rdi = (uint64_t)(ctx->r15);
    /* 0x2854: MOV RSI, [RSP+Reg(0)+0x38] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    /* 0x2859: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x285c: CALL .+3291 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2861ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x2861:
    ctx->rip = 0x2861ULL;
    /* 0x2861: MOV RAX, [RSP+Reg(0)+0x38] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    /* 0x2866: ADD EAX, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a + b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2869: MOV EDX, [RSP+Reg(0)+0x20] */
ctx->rdx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x20ULL));
    /* 0x286d: SUB EDX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x286f: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x2872: MOV ESI, 0x30 */
ctx->rsi = (uint64_t)(uint32_t)(0x30ULL);
    /* 0x2877: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2879: XOR R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x287c: CALL .+2759 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2881ULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x2881: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2885: JNE .+14 */
    if (!ctx->zf) goto loc_0x2895;

loc_0x2887:
    ctx->rip = 0x2887ULL;
    /* 0x2887: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);

loc_0x288a:
    ctx->rip = 0x288aULL;
    /* 0x288a: MOV RSI, R13 */
ctx->rsi = (uint64_t)(ctx->r13);
    /* 0x288d: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2890: CALL .+3239 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2895ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x2895:
    ctx->rip = 0x2895ULL;
    /* 0x2895: MOV ECX, R12L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r12));
    /* 0x2898: JMP .+1956 */
    goto loc_0x3041;

loc_0x289d:
    ctx->rip = 0x289dULL;
    /* 0x289d: XOR R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x28a0: JMP .+33 */
    goto loc_0x28c3;

loc_0x28a2:
    ctx->rip = 0x28a2ULL;
    /* 0x28a2: XOR R13L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a ^ b;
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x28a5: MOV R12, [RSP+Reg(0)+0x48] */
ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp + 0x48ULL));
    /* 0x28aa: MOV RDI, R12 */
ctx->rdi = (uint64_t)(ctx->r12);
    /* 0x28ad: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x28b2: MOV RDX, [RSP+Reg(0)+0x28] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x28b7: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x28b9: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x28be: CALL .+2693 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x28c3ULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);

loc_0x28c3:
    ctx->rip = 0x28c3ULL;
    /* 0x28c3: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x28c8: XOR R8L, 0x2000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x2000ULL); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x28cf: MOV RDI, R12 */
ctx->rdi = (uint64_t)(ctx->r12);
    /* 0x28d2: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x28d7: MOV RBX, [RSP+Reg(0)+0x28] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x28dc: MOV EDX, EBX */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    /* 0x28de: MOV ECX, R13L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x28e1: CALL .+2658 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x28e6ULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x28e6: CMP EBX, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x28e9: CMOVG R13L, EBX */
    if (!ctx->zf && ctx->sf == ctx->of) {
    ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    }
    /* 0x28ed: JMP .-5192 */
    goto loc_0x14aa;

loc_0x28f2:
    ctx->rip = 0x28f2ULL;
    /* 0x28f2: INC EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x28f4: MOV ECX, [RSP+Reg(0)+0x1c] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x28f8: MOV R10L, EDX */
ctx->r10 = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x28fb: JMP .-2493 */
    goto loc_0x1f43;

loc_0x2900:
    ctx->rip = 0x2900ULL;
    /* 0x2900: JNE .+81 */
    if (!ctx->zf) goto loc_0x2953;

loc_0x2902:
    ctx->rip = 0x2902ULL;
    /* 0x2902: CMP R15, R8 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2905: JNE .+76 */
    if (!ctx->zf) goto loc_0x2953;

loc_0x2907:
    ctx->rip = 0x2907ULL;
    /* 0x2907: MOVQ X0, [RIP+0x34e1] */
    memset(&ctx->xmm[0], 0, 16);
    ctx->xmm[0].u64[0] = (uint64_t)(MEM_U64(0x5df0ULL));
    /* 0x290f: MOVQ [RSP+Reg(0)+0x178], X0 */
MEM_U64(ctx->rsp + 0x178ULL) = (uint64_t)(ctx->xmm[0].u64[0]);
    /* 0x2918: FLD [RSP+Reg(0)+0x178] */
    { double d; uint64_t u = MEM_U64(ctx->rsp + 0x178ULL); memcpy(&d, &u, 8); fpu_push(ctx, d); }
    /* 0x291f: FWAIT */
    /* fwait no-op */
    /* 0x2920: JMP .+74 */
    goto loc_0x296c;

loc_0x2922:
    ctx->rip = 0x2922ULL;
    /* 0x2922: MOV RAX, [RSP+Reg(0)+0x60] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x2927: MOV ECX, [RSP+Reg(0)+0x34] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x34ULL));
    /* 0x292b: MOV [RAX], ECX */
MEM_U32(ctx->rax) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x292d: JMP .-5256 */
    goto loc_0x14aa;

loc_0x2932:
    ctx->rip = 0x2932ULL;
    /* 0x2932: MOV RAX, [RSP+Reg(0)+0x60] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x2937: MOV ECX, [RSP+Reg(0)+0x34] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x34ULL));
    /* 0x293b: MOV [RAX], CX */
MEM_U16(ctx->rax) = (uint16_t)(((uint16_t)ctx->rcx));
    /* 0x293e: JMP .-5273 */
    goto loc_0x14aa;

loc_0x2943:
    ctx->rip = 0x2943ULL;
    /* 0x2943: MOV RAX, [RSP+Reg(0)+0x60] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x60ULL));
    /* 0x2948: MOV ECX, [RSP+Reg(0)+0x34] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x34ULL));
    /* 0x294c: MOV [RAX], CL */
MEM_U8(ctx->rax) = (uint8_t)(((uint8_t)(ctx->rcx & 0xff)));
    /* 0x294e: JMP .-5289 */
    goto loc_0x14aa;

loc_0x2953:
    ctx->rip = 0x2953ULL;
    /* 0x2953: MOVQ X0, [RIP+0x34a5] */
    memset(&ctx->xmm[0], 0, 16);
    ctx->xmm[0].u64[0] = (uint64_t)(MEM_U64(0x5e00ULL));
    /* 0x295b: MOVQ [RSP+Reg(0)+0x180], X0 */
MEM_U64(ctx->rsp + 0x180ULL) = (uint64_t)(ctx->xmm[0].u64[0]);
    /* 0x2964: FLD [RSP+Reg(0)+0x180] */
    { double d; uint64_t u = MEM_U64(ctx->rsp + 0x180ULL); memcpy(&d, &u, 8); fpu_push(ctx, d); }
    /* 0x296b: FWAIT */
    /* fwait no-op */

loc_0x296c:
    ctx->rip = 0x296cULL;
    /* 0x296c: CMP [RSP+Reg(0)+0xb8], 0x0 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rsp + 0xb8ULL)); uint8_t b = (uint8_t)(0x0ULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x2974: LEA R15, [RSP+Reg(0)+0x90] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x297c: JNE .+35 */
    if (!ctx->zf) goto loc_0x29a1;

loc_0x297e:
    ctx->rip = 0x297eULL;
    /* 0x297e: MOV RAX, [RSP+Reg(0)+0xc8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0xc8ULL));
    /* 0x2986: CMP [RAX], 0x2d */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(0x2dULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x2989: JNE .+22 */
    if (!ctx->zf) goto loc_0x29a1;

loc_0x298b:
    ctx->rip = 0x298bULL;
    /* 0x298b: MOV [RSP+Reg(0)+0xfc], -0x1 */
MEM_U32(ctx->rsp + 0xfcULL) = (uint32_t)(0xffffffffffffffffULL);
    /* 0x2996: FILD [RSP+Reg(0)+0xfc] */
    fpu_push(ctx, (double)(int32_t)MEM_U32(ctx->rsp + 0xfcULL));
    /* 0x299d: FMUL F2, F0 */
    FPU_ST(2) *= FPU_ST(0);
    /* 0x299f: FMULP F1, F0 */
    FPU_ST(1) *= FPU_ST(0);
    fpu_pop(ctx);

loc_0x29a1:
    ctx->rip = 0x29a1ULL;
    /* 0x29a1: SUB ECX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x29a3: MOV [R10], ECX */
MEM_U32(ctx->r10) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x29a6: FADD F0, F1 */
    FPU_ST(0) += FPU_ST(0);
    /* 0x29a8: FUCOMIP F0, F1 */
    { double a = FPU_ST(0); double b = FPU_ST(1);
      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }
      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }
      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }
      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }
      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }
    fpu_pop(ctx);
    /* 0x29aa: FSTP F0 */
    FPU_ST(0) = fpu_pop(ctx);
    /* 0x29ac: FWAIT */
    /* fwait no-op */
    /* 0x29ad: SETNP AL */
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->pf ? 1 : 0)));
    /* 0x29b0: SETE DL */
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x29b3: TEST DL, AL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x29b5: JNE .+94 */
    if (!ctx->zf) goto loc_0x2a15;

loc_0x29b7:
    ctx->rip = 0x29b7ULL;
    /* 0x29b7: ADD ECX, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x29ba: MOV [R10], ECX */
MEM_U32(ctx->r10) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x29bd: CMP ECX, 0x3b9aca00 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x3b9aca00ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x29c3: JB .+1756 */
    if (ctx->cf) goto loc_0x30a5;

loc_0x29c9:
    ctx->rip = 0x29c9ULL;
    /* 0x29c9: SHL R12, 0x2 */
    { uint64_t a = (uint64_t)(ctx->r12); uint8_t count = ((uint8_t)(0x2ULL)) & 0x3f; uint64_t res;
      res = a << count;
    ctx->r12 = (uint64_t)(res);
    }
    /* 0x29cd: LEA R10, [R12+4*R9] */
    ctx->r10 = (uint64_t)(ctx->r12 + (ctx->r9 * 4ULL));
    /* 0x29d1: LEA RAX, [RSP+Reg(0)+0xffff019c] */
    ctx->rax = (uint64_t)(ctx->rsp + (uint64_t)(-0xfe64LL));
    /* 0x29d9: ADD R10, RAX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x29dc: MOV RDX, [RSP+Reg(0)+0x58] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x58ULL));

loc_0x29e1:
    ctx->rip = 0x29e1ULL;
    /* 0x29e1: MOV [R10+0x4], 0x0 */
MEM_U32(ctx->r10 + 0x4ULL) = (uint32_t)(0x0ULL);
    /* 0x29e9: CMP R10, RBX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x29ec: JAE .+11 */
    if (!ctx->cf) goto loc_0x29f9;

loc_0x29ee:
    ctx->rip = 0x29eeULL;
    /* 0x29ee: MOV [RBX-0x4], 0x0 */
MEM_U32(ctx->rbx + (uint64_t)(-0x4LL)) = (uint32_t)(0x0ULL);
    /* 0x29f5: ADD RBX, -0x4 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0xfffffffffffffffcULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rbx = (uint64_t)(res);
    }

loc_0x29f9:
    ctx->rip = 0x29f9ULL;
    /* 0x29f9: MOV EAX, [R10] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->r10));
    /* 0x29fc: INC EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x29fe: MOV [R10], EAX */
MEM_U32(ctx->r10) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x2a01: ADD R10, -0x4 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0xfffffffffffffffcULL); uint64_t res;
      res = a + b;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x2a05: CMP EAX, 0x3b9ac9ff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x3b9ac9ffULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2a0a: JA .-43 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x29e1;

loc_0x2a0c:
    ctx->rip = 0x2a0cULL;
    /* 0x2a0c: ADD R10, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x2a10: JMP .+1685 */
    goto loc_0x30aa;

loc_0x2a15:
    ctx->rip = 0x2a15ULL;
    /* 0x2a15: MOV R11, RDI */
ctx->r11 = (uint64_t)(ctx->rdi);
    /* 0x2a18: MOV RDX, [RSP+Reg(0)+0x58] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x58ULL));

loc_0x2a1d:
    ctx->rip = 0x2a1dULL;
    /* 0x2a1d: ADD R10, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x2a21: CMP R8, R10 */
    { uint64_t a = (uint64_t)(ctx->r8); uint64_t b = (uint64_t)(ctx->r10); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2a24: CMOVA R8, R10 */
    if ((!ctx->cf && !ctx->zf)) {
    ctx->r8 = (uint64_t)(ctx->r10);
    }
    /* 0x2a28: MOV R10, -0x3333333333333333 */
ctx->r10 = (uint64_t)(0xcccccccccccccccdULL);

loc_0x2a32:
    ctx->rip = 0x2a32ULL;
    /* 0x2a32: MOV RAX, [RSP+Reg(0)+0x160] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x160ULL));
    /* 0x2a3a: ADD RAX, R8 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x2a3d: SHL EDX, 0x2 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint8_t count = ((uint8_t)(0x2ULL)) & 0x3f; uint32_t res;
      res = a << count;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2a40: SUB RAX, RDX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    ctx->rax = (uint64_t)(res);
    }

loc_0x2a43:
    ctx->rip = 0x2a43ULL;
    /* 0x2a43: MOV R12, R8 */
ctx->r12 = (uint64_t)(ctx->r8);
    /* 0x2a46: MOV RDI, RAX */
ctx->rdi = (uint64_t)(ctx->rax);
    /* 0x2a49: CMP R8, RBX */
    { uint64_t a = (uint64_t)(ctx->r8); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2a4c: JBE .+17 */
    if ((ctx->cf || ctx->zf)) goto loc_0x2a5f;

loc_0x2a4e:
    ctx->rip = 0x2a4eULL;
    /* 0x2a4e: LEA R8, [R12+Reg(0)-0x4] */
    ctx->r8 = (uint64_t)(ctx->r12 + (uint64_t)(-0x4LL));
    /* 0x2a53: LEA RAX, [RDI-0x4] */
    ctx->rax = (uint64_t)(ctx->rdi + (uint64_t)(-0x4LL));
    /* 0x2a57: CMP [R12+Reg(0)-0x4], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->r12 + (uint64_t)(-0x4LL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2a5d: JE .-28 */
    if (ctx->zf) goto loc_0x2a43;

loc_0x2a5f:
    ctx->rip = 0x2a5fULL;
    /* 0x2a5f: CMP ESI, 0x67 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x67ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2a62: JNE .+205 */
    if (!ctx->zf) goto loc_0x2b35;

loc_0x2a68:
    ctx->rip = 0x2a68ULL;
    /* 0x2a68: MOV RAX, [RSP+Reg(0)+0x20] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2a6d: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2a6f: MOV ECX, 0x1 */
ctx->rcx = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x2a74: CMOVE EAX, ECX */
    if (ctx->zf) {
    ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    }
    /* 0x2a77: CMP EAX, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2a7a: SETG CL */
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)(((!ctx->zf && (ctx->sf == ctx->of)) ? 1 : 0)));
    /* 0x2a7d: CMP R11L, -0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(0xfffffffffffffffcULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2a81: SETGE DL */
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)(((ctx->sf == ctx->of) ? 1 : 0)));
    /* 0x2a84: AND DL, CL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t res;
      res = a & b;
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2a86: MOV ESI, R11L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x2a89: NOT ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = ~a;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2a8b: MOVZX ECX, DL */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(((uint8_t)(ctx->rdx & 0xff))));
    /* 0x2a8e: MOV EDX, ECX */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2a90: OR EDX, -0x2 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0xfffffffffffffffeULL); uint32_t res;
      res = a | b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2a93: ADD R13L, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r13)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a + b;
    ctx->r13 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2a96: TEST CL, CL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2a98: MOV ECX, -0x1 */
ctx->rcx = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x2a9d: CMOVE ESI, ECX */
    if (ctx->zf) {
    ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    }
    /* 0x2aa0: ADD ESI, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a + b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2aa2: MOV [RSP+Reg(0)+0x20], RSI */
MEM_U64(ctx->rsp + 0x20ULL) = (uint64_t)(ctx->rsi);
    /* 0x2aa7: MOV EAX, [RSP+Reg(0)+0x1c] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x2aab: MOV R8L, EAX */
ctx->r8 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x2aae: AND R8L, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ab2: JNE .+136 */
    if (!ctx->zf) goto loc_0x2b40;

loc_0x2ab8:
    ctx->rip = 0x2ab8ULL;
    /* 0x2ab8: MOV ESI, 0x9 */
ctx->rsi = (uint64_t)(uint32_t)(0x9ULL);
    /* 0x2abd: CMP R12, RBX */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2ac0: JBE .+54 */
    if ((ctx->cf || ctx->zf)) goto loc_0x2af8;

loc_0x2ac2:
    ctx->rip = 0x2ac2ULL;
    /* 0x2ac2: MOV ECX, [R12+Reg(0)-0x4] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->r12 + (uint64_t)(-0x4LL)));
    /* 0x2ac7: TEST ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2ac9: JE .+45 */
    if (ctx->zf) goto loc_0x2af8;

loc_0x2acb:
    ctx->rip = 0x2acbULL;
    /* 0x2acb: IMUL EAX, ECX, -0x33333333 */
    { int32_t a = (int32_t)(((uint32_t)ctx->rcx)); int32_t b = (int32_t)(0xffffffffcccccccdULL); int32_t res = a * b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ad1: ROR EAX, 0x1 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint8_t count = ((uint8_t)(0x1ULL)) & 31; uint32_t res;
      res = (a >> count) | (a << ((32 - count) & 31));
      ctx->cf = (res >> 31) & 1;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ad3: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ad5: CMP EAX, 0x19999999 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x19999999ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2ada: JA .+28 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x2af8;

loc_0x2adc:
    ctx->rip = 0x2adcULL;
    /* 0x2adc: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ade: MOV R8L, 0xa */
ctx->r8 = (uint64_t)(uint32_t)(0xaULL);

loc_0x2ae4:
    ctx->rip = 0x2ae4ULL;
    /* 0x2ae4: ADD R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a + b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ae7: LEA R8L, [R8+4*R8] */
    ctx->r8 = (uint64_t)(uint32_t)(ctx->r8 + (ctx->r8 * 4ULL));
    /* 0x2aeb: INC ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2aed: MOV EAX, ECX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2aef: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2af1: DIV R8L */
    { uint64_t dividend = ((uint64_t)(uint32_t)ctx->rdx << 32) | (uint32_t)ctx->rax;
      uint32_t divisor = (uint32_t)(((uint32_t)ctx->r8));
      if (divisor != 0) {
        ctx->rax = (uint64_t)(uint32_t)(dividend / divisor);
        ctx->rdx = (uint64_t)(uint32_t)(dividend % divisor);
      }
    }
    /* 0x2af4: TEST EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2af6: JE .-20 */
    if (ctx->zf) goto loc_0x2ae4;

loc_0x2af8:
    ctx->rip = 0x2af8ULL;
    /* 0x2af8: MOV EDX, R13L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x2afb: AND EDX, -0x21 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0xffffffffffffffdfULL); uint32_t res;
      res = a & b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2afe: SAR RDI, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint8_t count = ((uint8_t)(0x2ULL)) & 0x3f; uint64_t res;
      res = (uint64_t)(((int64_t)a) >> count);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x2b02: MOVSXD RAX, [RSP+Reg(0)+0x20] */
    ctx->rax = (uint64_t)((uint64_t)(int64_t)(int32_t)(MEM_U32(ctx->rsp + 0x20ULL)));
    /* 0x2b07: LEA RDI, [RDI+8*RDI] */
    ctx->rdi = (uint64_t)(ctx->rdi + (ctx->rdi * 8ULL));
    /* 0x2b0b: MOV ECX, ESI */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x2b0d: CMP EDX, 0x46 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0x46ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2b10: JNE .+1383 */
    if (!ctx->zf) goto loc_0x307d;

loc_0x2b16:
    ctx->rip = 0x2b16ULL;
    /* 0x2b16: SUB RDI, RCX */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x2b19: ADD RDI, -0x9 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(0xfffffffffffffff7ULL); uint64_t res;
      res = a + b;
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x2b1d: XOR R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b20: TEST RDI, RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x2b23: CMOVLE RDI, R8 */
    if (ctx->zf || ctx->sf != ctx->of) {
    ctx->rdi = (uint64_t)(ctx->r8);
    }
    /* 0x2b27: CMP RDI, RAX */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2b2a: CMOVGE RDI, RAX */
    if (ctx->sf == ctx->of) {
    ctx->rdi = (uint64_t)(ctx->rax);
    }
    /* 0x2b2e: MOV [RSP+Reg(0)+0x20], RDI */
MEM_U64(ctx->rsp + 0x20ULL) = (uint64_t)(ctx->rdi);
    /* 0x2b33: JMP .+11 */
    goto loc_0x2b40;

loc_0x2b35:
    ctx->rip = 0x2b35ULL;
    /* 0x2b35: MOV EAX, [RSP+Reg(0)+0x1c] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x2b39: MOV R8L, EAX */
ctx->r8 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x2b3c: AND R8L, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }

loc_0x2b40:
    ctx->rip = 0x2b40ULL;
    /* 0x2b40: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b42: MOV RCX, [RSP+Reg(0)+0x20] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2b47: MOV EDX, ECX */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2b49: OR EDX, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a | b;
      set_flags_logic_u32(ctx, res);
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b4c: SETE AL */
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x2b4f: ADD EAX, 0x7ffffffd */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7ffffffdULL); uint32_t res;
      res = a + b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b54: CMP ECX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2b56: JG .+1407 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30db;

loc_0x2b5c:
    ctx->rip = 0x2b5cULL;
    /* 0x2b5c: CMP EDX, 0x1 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0x1ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2b5f: MOV RAX, [RSP+Reg(0)+0x20] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2b64: MOV ESI, EAX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x2b66: SBB ESI, -0x1 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0xffffffffffffffffULL) + (uint32_t)ctx->cf; uint32_t res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b69: INC ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b6b: MOV EAX, R13L */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x2b6e: AND EAX, -0x21 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xffffffffffffffdfULL); uint32_t res;
      res = a & b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b71: MOV [RSP+Reg(0)+0xe4], EAX */
MEM_U32(ctx->rsp + 0xe4ULL) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x2b78: CMP EAX, 0x46 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x46ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2b7b: MOV [RSP+Reg(0)+0xb8], R8 */
MEM_U64(ctx->rsp + 0xb8ULL) = (uint64_t)(ctx->r8);
    /* 0x2b83: MOV [RSP+Reg(0)+0xe0], EDX */
MEM_U32(ctx->rsp + 0xe0ULL) = (uint32_t)(((uint32_t)ctx->rdx));
    /* 0x2b8a: JNE .+33 */
    if (!ctx->zf) goto loc_0x2bad;

loc_0x2b8c:
    ctx->rip = 0x2b8cULL;
    /* 0x2b8c: MOV EAX, ESI */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x2b8e: XOR EAX, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2b93: CMP R11L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2b96: JG .+1343 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30db;

loc_0x2b9c:
    ctx->rip = 0x2b9cULL;
    /* 0x2b9c: TEST R11L, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2b9f: MOV EAX, 0x0 */
ctx->rax = (uint64_t)(uint32_t)(0x0ULL);
    /* 0x2ba4: CMOVLE R11L, EAX */
    if (ctx->zf || ctx->sf != ctx->of) {
    ctx->r11 = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x2ba8: JMP .+176 */
    goto loc_0x2c5d;

loc_0x2bad:
    ctx->rip = 0x2badULL;
    /* 0x2bad: TEST R11L, R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2bb0: JE .+67 */
    if (ctx->zf) goto loc_0x2bf5;

loc_0x2bb2:
    ctx->rip = 0x2bb2ULL;
    /* 0x2bb2: MOV ECX, R11L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x2bb5: NEG ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = -a;
      set_flags_sub_u32(ctx, 0, a, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x2bb7: CMOVS ECX, R11L */
    if (ctx->sf) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    }
    /* 0x2bbb: LEA R9, [RSP+Reg(0)+0x90] */
    ctx->r9 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x2bc3: MOV RDI, R9 */
ctx->rdi = (uint64_t)(ctx->r9);

loc_0x2bc6:
    ctx->rip = 0x2bc6ULL;
    /* 0x2bc6: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x2bc9: MUL R10 */
    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(ctx->r10);
      ctx->rax = (uint64_t)res;
      ctx->rdx = (uint64_t)(res >> 64);
      ctx->cf = ctx->of = (ctx->rdx != 0);
    }
    /* 0x2bcc: SHR RDX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0x3ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2bd0: LEA EAX, [RDX+RDX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rdx + ctx->rdx);
    /* 0x2bd3: LEA EAX, [RAX+4*RAX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 4ULL));
    /* 0x2bd6: MOV R8L, ECX */
ctx->r8 = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2bd9: SUB R8L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2bdc: OR R8B, 0x30 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r8 & 0xff))); uint8_t b = (uint8_t)(0x30ULL); uint8_t res;
      res = a | b;
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2be0: MOV [R9-0x1], R8B */
MEM_U8(ctx->r9 + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->r8 & 0xff)));
    /* 0x2be4: DEC R9 */
    { uint64_t a = (uint64_t)(ctx->r9); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->r9 = (uint64_t)(res);
    }
    /* 0x2be7: DEC RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x2bea: CMP RCX, 0x9 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x9ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2bee: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x2bf1: JA .-45 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x2bc6;

loc_0x2bf3:
    ctx->rip = 0x2bf3ULL;
    /* 0x2bf3: JMP .+11 */
    goto loc_0x2c00;

loc_0x2bf5:
    ctx->rip = 0x2bf5ULL;
    /* 0x2bf5: LEA R9, [RSP+Reg(0)+0x90] */
    ctx->r9 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x2bfd: MOV RDI, R9 */
ctx->rdi = (uint64_t)(ctx->r9);

loc_0x2c00:
    ctx->rip = 0x2c00ULL;
    /* 0x2c00: LEA RCX, [RSP+Reg(0)+0x90] */
    ctx->rcx = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x2c08: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x2c0b: SUB RAX, RDI */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x2c0e: CMP RAX, 0x1 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(0x1ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2c12: JG .+23 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x2c2b;

loc_0x2c14:
    ctx->rip = 0x2c14ULL;
    /* 0x2c14: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x2c17: SUB RAX, R9 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->r9); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    ctx->rax = (uint64_t)(res);
    }

loc_0x2c1a:
    ctx->rip = 0x2c1aULL;
    /* 0x2c1a: MOV [R9-0x1], 0x30 */
MEM_U8(ctx->r9 + (uint64_t)(-0x1LL)) = (uint8_t)(0x30ULL);
    /* 0x2c1f: DEC R9 */
    { uint64_t a = (uint64_t)(ctx->r9); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->r9 = (uint64_t)(res);
    }
    /* 0x2c22: INC RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x2c25: CMP RAX, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(0x2ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2c29: JL .-17 */
    if (ctx->sf != ctx->of) goto loc_0x2c1a;

loc_0x2c2b:
    ctx->rip = 0x2c2bULL;
    /* 0x2c2b: SHR R11L, 0x1f */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint8_t count = ((uint8_t)(0x1fULL)) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2c2f: ADD R11B, R11B */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r11 & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->r11 & 0xff))); uint8_t res;
      res = a + b;
    ctx->r11 = (ctx->r11 & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2c32: ADD R11B, 0x2b */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r11 & 0xff))); uint8_t b = (uint8_t)(0x2bULL); uint8_t res;
      res = a + b;
    ctx->r11 = (ctx->r11 & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2c36: MOV [R9-0x1], R11B */
MEM_U8(ctx->r9 + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->r11 & 0xff)));
    /* 0x2c3a: MOV [R9-0x2], R13B */
MEM_U8(ctx->r9 + (uint64_t)(-0x2LL)) = (uint8_t)(((uint8_t)(ctx->r13 & 0xff)));
    /* 0x2c3e: ADD R9, -0x2 */
    { uint64_t a = (uint64_t)(ctx->r9); uint64_t b = (uint64_t)(0xfffffffffffffffeULL); uint64_t res;
      res = a + b;
    ctx->r9 = (uint64_t)(res);
    }
    /* 0x2c42: LEA R11, [RSP+Reg(0)+0x90] */
    ctx->r11 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x2c4a: SUB R11, R9 */
    { uint64_t a = (uint64_t)(ctx->r11); uint64_t b = (uint64_t)(ctx->r9); uint64_t res;
      res = a - b;
    ctx->r11 = (uint64_t)(res);
    }
    /* 0x2c4d: MOV EAX, ESI */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x2c4f: XOR EAX, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2c54: CMP R11, RAX */
    { uint64_t a = (uint64_t)(ctx->r11); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2c57: JG .+1150 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30db;

loc_0x2c5d:
    ctx->rip = 0x2c5dULL;
    /* 0x2c5d: ADD R11L, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a + b;
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2c60: MOV RAX, [RSP+Reg(0)+0x78] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x78ULL));
    /* 0x2c65: XOR EAX, 0x7fffffff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7fffffffULL); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x2c6a: CMP R11L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2c6d: JG .+1128 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x30db;

loc_0x2c73:
    ctx->rip = 0x2c73ULL;
    /* 0x2c73: MOV [RSP+Reg(0)+0x158], R9 */
MEM_U64(ctx->rsp + 0x158ULL) = (uint64_t)(ctx->r9);
    /* 0x2c7b: MOV R13, [RSP+Reg(0)+0x78] */
ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp + 0x78ULL));
    /* 0x2c80: ADD R11L, R13L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t b = (uint32_t)(((uint32_t)ctx->r13)); uint32_t res;
      res = a + b;
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2c83: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x2c86: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x2c8b: MOV RDX, [RSP+Reg(0)+0x28] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x2c90: MOV [RSP+Reg(0)+0x58], R11 */
MEM_U64(ctx->rsp + 0x58ULL) = (uint64_t)(ctx->r11);
    /* 0x2c95: MOV ECX, R11L */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->r11));
    /* 0x2c98: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x2c9d: CALL .+1702 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2ca2ULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x2ca2: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2ca6: JNE .+19 */
    if (!ctx->zf) goto loc_0x2cbb;

loc_0x2ca8:
    ctx->rip = 0x2ca8ULL;
    /* 0x2ca8: MOV ESI, R13L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r13));
    /* 0x2cab: MOV RDI, [RSP+Reg(0)+0xc8] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0xc8ULL));
    /* 0x2cb3: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2cb6: CALL .+2177 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2cbbULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x2cbb:
    ctx->rip = 0x2cbbULL;
    /* 0x2cbb: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x2cc0: XOR R8L, 0x10000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x10000ULL); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2cc7: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x2cca: MOV ESI, 0x30 */
ctx->rsi = (uint64_t)(uint32_t)(0x30ULL);
    /* 0x2ccf: MOV RDX, [RSP+Reg(0)+0x28] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x2cd4: MOV RCX, [RSP+Reg(0)+0x58] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x58ULL));
    /* 0x2cd9: CALL .+1642 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2cdeULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x2cde: CMP [RSP+Reg(0)+0xe4], 0x46 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rsp + 0xe4ULL)); uint32_t b = (uint32_t)(0x46ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2ce6: JNE .+437 */
    if (!ctx->zf) goto loc_0x2ea1;

loc_0x2cec:
    ctx->rip = 0x2cecULL;
    /* 0x2cec: MOV RAX, [RSP+Reg(0)+0x38] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    /* 0x2cf1: CMP RBX, RAX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2cf4: CMOVA RBX, RAX */
    if ((!ctx->cf && !ctx->zf)) {
    ctx->rbx = (uint64_t)(ctx->rax);
    }
    /* 0x2cf8: MOV R13, RBX */
ctx->r13 = (uint64_t)(ctx->rbx);
    /* 0x2cfb: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x2d05: MOV R10, [RSP+Reg(0)+0x20] */
ctx->r10 = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));

loc_0x2d0a:
    ctx->rip = 0x2d0aULL;
    /* 0x2d0a: MOV ECX, [R13] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->r13));
    /* 0x2d0e: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x2d11: JE .+76 */
    if (ctx->zf) goto loc_0x2d5f;

loc_0x2d13:
    ctx->rip = 0x2d13ULL;
    /* 0x2d13: MOV RSI, -0xa */
ctx->rsi = (uint64_t)(0xfffffffffffffff6ULL);
    /* 0x2d1a: LEA RDI, [RSP+Reg(0)+0x99] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x99ULL);

loc_0x2d22:
    ctx->rip = 0x2d22ULL;
    /* 0x2d22: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x2d25: MUL R9 */
    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(ctx->r9);
      ctx->rax = (uint64_t)res;
      ctx->rdx = (uint64_t)(res >> 64);
      ctx->cf = ctx->of = (ctx->rdx != 0);
    }
    /* 0x2d28: SHR RDX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0x3ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2d2c: LEA EAX, [RDX+RDX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rdx + ctx->rdx);
    /* 0x2d2f: LEA EAX, [RAX+4*RAX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 4ULL));
    /* 0x2d32: MOV R8L, ECX */
ctx->r8 = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2d35: SUB R8L, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2d38: OR R8B, 0x30 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r8 & 0xff))); uint8_t b = (uint8_t)(0x30ULL); uint8_t res;
      res = a | b;
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2d3c: MOV [RDI-0x1], R8B */
MEM_U8(ctx->rdi + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->r8 & 0xff)));
    /* 0x2d40: DEC RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x2d43: INC RSI */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x2d46: CMP RCX, 0x9 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x9ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2d4a: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x2d4d: JA .-45 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x2d22;

loc_0x2d4f:
    ctx->rip = 0x2d4fULL;
    /* 0x2d4f: CMP R13, RBX */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2d52: JE .+38 */
    if (ctx->zf) goto loc_0x2d7a;

loc_0x2d54:
    ctx->rip = 0x2d54ULL;
    /* 0x2d54: NEG RSI */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t res;
      res = -a;
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x2d57: CMP RSI, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(0x2ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2d5b: JGE .+15 */
    if (ctx->sf == ctx->of) goto loc_0x2d6c;

loc_0x2d5d:
    ctx->rip = 0x2d5dULL;
    /* 0x2d5d: JMP .+49 */
    goto loc_0x2d90;

loc_0x2d5f:
    ctx->rip = 0x2d5fULL;
    /* 0x2d5f: LEA RDI, [RSP+Reg(0)+0x99] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x99ULL);
    /* 0x2d67: CMP R13, RBX */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2d6a: JE .+20 */
    if (ctx->zf) goto loc_0x2d80;

loc_0x2d6c:
    ctx->rip = 0x2d6cULL;
    /* 0x2d6c: MOV [RDI-0x1], 0x30 */
MEM_U8(ctx->rdi + (uint64_t)(-0x1LL)) = (uint8_t)(0x30ULL);
    /* 0x2d70: DEC RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x2d73: CMP RDI, R15 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2d76: JA .-12 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x2d6c;

loc_0x2d78:
    ctx->rip = 0x2d78ULL;
    /* 0x2d78: JMP .+22 */
    goto loc_0x2d90;

loc_0x2d7a:
    ctx->rip = 0x2d7aULL;
    /* 0x2d7a: CMP RSI, -0xa */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(0xfffffffffffffff6ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2d7e: JNE .+16 */
    if (!ctx->zf) goto loc_0x2d90;

loc_0x2d80:
    ctx->rip = 0x2d80ULL;
    /* 0x2d80: MOV [RSP+Reg(0)+0x98], 0x30 */
MEM_U8(ctx->rsp + 0x98ULL) = (uint8_t)(0x30ULL);
    /* 0x2d88: LEA RDI, [RSP+Reg(0)+0x98] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x98ULL);

loc_0x2d90:
    ctx->rip = 0x2d90ULL;
    /* 0x2d90: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2d94: JNE .+34 */
    if (!ctx->zf) goto loc_0x2db8;

loc_0x2d96:
    ctx->rip = 0x2d96ULL;
    /* 0x2d96: LEA RSI, [RSP+Reg(0)+0x99] */
    ctx->rsi = (uint64_t)(ctx->rsp + 0x99ULL);
    /* 0x2d9e: SUB RSI, RDI */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x2da1: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2da4: CALL .+1939 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2da9ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x2da9: MOV R10, [RSP+Reg(0)+0x20] */
ctx->r10 = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2dae: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);

loc_0x2db8:
    ctx->rip = 0x2db8ULL;
    /* 0x2db8: ADD R13, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x2dbc: CMP R13, [RSP+Reg(0)+0x38] */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2dc1: JBE .-189 */
    if ((ctx->cf || ctx->zf)) goto loc_0x2d0a;

loc_0x2dc7:
    ctx->rip = 0x2dc7ULL;
    /* 0x2dc7: CMP [RSP+Reg(0)+0xe0], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rsp + 0xe0ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2dcf: JE .+41 */
    if (ctx->zf) goto loc_0x2dfa;

loc_0x2dd1:
    ctx->rip = 0x2dd1ULL;
    /* 0x2dd1: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2dd5: JNE .+35 */
    if (!ctx->zf) goto loc_0x2dfa;

loc_0x2dd7:
    ctx->rip = 0x2dd7ULL;
    /* 0x2dd7: MOV ESI, 0x1 */
ctx->rsi = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x2ddc: LEA RDI, [RIP+0x2cb8] */
    ctx->rdi = (uint64_t)(0x5a9bULL);
    /* 0x2de3: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2de6: CALL .+1873 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2debULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x2deb: MOV R10, [RSP+Reg(0)+0x20] */
ctx->r10 = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2df0: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);

loc_0x2dfa:
    ctx->rip = 0x2dfaULL;
    /* 0x2dfa: TEST R10L, R10L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(((uint32_t)ctx->r10)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2dfd: JLE .+542 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x3021;

loc_0x2e03:
    ctx->rip = 0x2e03ULL;
    /* 0x2e03: CMP R13, R12 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2e06: JAE .+533 */
    if (!ctx->cf) goto loc_0x3021;

loc_0x2e0c:
    ctx->rip = 0x2e0cULL;
    /* 0x2e0c: MOV ECX, [R13] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->r13));
    /* 0x2e10: LEA RDI, [RSP+Reg(0)+0x99] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x99ULL);
    /* 0x2e18: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x2e1b: JE .+42 */
    if (ctx->zf) goto loc_0x2e47;

loc_0x2e1d:
    ctx->rip = 0x2e1dULL;
    /* 0x2e1d: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x2e20: MUL R9 */
    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(ctx->r9);
      ctx->rax = (uint64_t)res;
      ctx->rdx = (uint64_t)(res >> 64);
      ctx->cf = ctx->of = (ctx->rdx != 0);
    }
    /* 0x2e23: SHR RDX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0x3ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2e27: LEA EAX, [RDX+RDX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rdx + ctx->rdx);
    /* 0x2e2a: LEA EAX, [RAX+4*RAX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 4ULL));
    /* 0x2e2d: MOV ESI, ECX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2e2f: SUB ESI, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2e31: OR SIB, 0x30 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t b = (uint8_t)(0x30ULL); uint8_t res;
      res = a | b;
    ctx->rsi = (ctx->rsi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2e35: MOV [RDI-0x1], SIB */
MEM_U8(ctx->rdi + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->rsi & 0xff)));
    /* 0x2e39: DEC RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x2e3c: CMP RCX, 0x9 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x9ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2e40: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x2e43: JA .-40 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x2e1d;

loc_0x2e45:
    ctx->rip = 0x2e45ULL;
    /* 0x2e45: JMP .+7 */
    goto loc_0x2e4e;

loc_0x2e47:
    ctx->rip = 0x2e47ULL;
    /* 0x2e47: MOV [RDI-0x1], 0x30 */
MEM_U8(ctx->rdi + (uint64_t)(-0x1LL)) = (uint8_t)(0x30ULL);
    /* 0x2e4b: DEC RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rdi = (uint64_t)(res);
    }

loc_0x2e4e:
    ctx->rip = 0x2e4eULL;
    /* 0x2e4e: CMP RDI, R15 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2e51: JA .-12 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x2e47;

loc_0x2e53:
    ctx->rip = 0x2e53ULL;
    /* 0x2e53: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2e57: JNE .+37 */
    if (!ctx->zf) goto loc_0x2e7e;

loc_0x2e59:
    ctx->rip = 0x2e59ULL;
    /* 0x2e59: CMP R10L, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2e5d: MOV ESI, 0x9 */
ctx->rsi = (uint64_t)(uint32_t)(0x9ULL);
    /* 0x2e62: CMOVL ESI, R10L */
    if (ctx->sf != ctx->of) {
    ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r10));
    }
    /* 0x2e66: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2e69: MOV RBX, R10 */
ctx->rbx = (uint64_t)(ctx->r10);
    /* 0x2e6c: CALL .+1739 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2e71ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x2e71: MOV R10, RBX */
ctx->r10 = (uint64_t)(ctx->rbx);
    /* 0x2e74: MOV R9, -0x3333333333333333 */
ctx->r9 = (uint64_t)(0xcccccccccccccccdULL);

loc_0x2e7e:
    ctx->rip = 0x2e7eULL;
    /* 0x2e7e: ADD R13, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x2e82: LEA EDX, [R10-0x9] */
    ctx->rdx = (uint64_t)(uint32_t)(ctx->r10 + (uint64_t)(-0x9LL));
    /* 0x2e86: CMP R13, R12 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2e89: JAE .+405 */
    if (!ctx->cf) goto loc_0x3024;

loc_0x2e8f:
    ctx->rip = 0x2e8fULL;
    /* 0x2e8f: CMP R10L, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r10)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2e93: MOV R10L, EDX */
ctx->r10 = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x2e96: JG .-144 */
    if (!ctx->zf && ctx->sf == ctx->of) goto loc_0x2e0c;

loc_0x2e9c:
    ctx->rip = 0x2e9cULL;
    /* 0x2e9c: JMP .+387 */
    goto loc_0x3024;

loc_0x2ea1:
    ctx->rip = 0x2ea1ULL;
    /* 0x2ea1: LEA RAX, [RBX+0x4] */
    ctx->rax = (uint64_t)(ctx->rbx + 0x4ULL);
    /* 0x2ea5: CMP R12, RBX */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2ea8: CMOVA RAX, R12 */
    if ((!ctx->cf && !ctx->zf)) {
    ctx->rax = (uint64_t)(ctx->r12);
    }
    /* 0x2eac: MOV [RSP+Reg(0)+0x38], RAX */
MEM_U64(ctx->rsp + 0x38ULL) = (uint64_t)(ctx->rax);
    /* 0x2eb1: MOV R8, [RSP+Reg(0)+0x20] */
ctx->r8 = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x2eb6: TEST R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2eb9: MOV RDI, -0x3333333333333333 */
ctx->rdi = (uint64_t)(0xcccccccccccccccdULL);
    /* 0x2ec3: JS .+281 */
    if (ctx->sf) goto loc_0x2fe2;

loc_0x2ec9:
    ctx->rip = 0x2ec9ULL;
    /* 0x2ec9: MOV R12, RBX */
ctx->r12 = (uint64_t)(ctx->rbx);

loc_0x2ecc:
    ctx->rip = 0x2eccULL;
    /* 0x2ecc: MOV ECX, [R12+Reg(0)] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->r12));
    /* 0x2ed0: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x2ed3: JE .+50 */
    if (ctx->zf) goto loc_0x2f07;

loc_0x2ed5:
    ctx->rip = 0x2ed5ULL;
    /* 0x2ed5: LEA R13, [RSP+Reg(0)+0x99] */
    ctx->r13 = (uint64_t)(ctx->rsp + 0x99ULL);

loc_0x2edd:
    ctx->rip = 0x2eddULL;
    /* 0x2edd: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x2ee0: MUL RDI */
    { unsigned __int128 res = (unsigned __int128)ctx->rax * (uint64_t)(ctx->rdi);
      ctx->rax = (uint64_t)res;
      ctx->rdx = (uint64_t)(res >> 64);
      ctx->cf = ctx->of = (ctx->rdx != 0);
    }
    /* 0x2ee3: SHR RDX, 0x3 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint8_t count = ((uint8_t)(0x3ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x2ee7: LEA EAX, [RDX+RDX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rdx + ctx->rdx);
    /* 0x2eea: LEA EAX, [RAX+4*RAX] */
    ctx->rax = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 4ULL));
    /* 0x2eed: MOV ESI, ECX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x2eef: SUB ESI, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a - b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ef1: OR SIB, 0x30 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t b = (uint8_t)(0x30ULL); uint8_t res;
      res = a | b;
    ctx->rsi = (ctx->rsi & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x2ef5: MOV [R13-0x1], SIB */
MEM_U8(ctx->r13 + (uint64_t)(-0x1LL)) = (uint8_t)(((uint8_t)(ctx->rsi & 0xff)));
    /* 0x2ef9: DEC R13 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x2efc: CMP RCX, 0xa */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xaULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2f00: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x2f03: JAE .-40 */
    if (!ctx->cf) goto loc_0x2edd;

loc_0x2f05:
    ctx->rip = 0x2f05ULL;
    /* 0x2f05: JMP .+16 */
    goto loc_0x2f17;

loc_0x2f07:
    ctx->rip = 0x2f07ULL;
    /* 0x2f07: MOV [RSP+Reg(0)+0x98], 0x30 */
MEM_U8(ctx->rsp + 0x98ULL) = (uint8_t)(0x30ULL);
    /* 0x2f0f: LEA R13, [RSP+Reg(0)+0x98] */
    ctx->r13 = (uint64_t)(ctx->rsp + 0x98ULL);

loc_0x2f17:
    ctx->rip = 0x2f17ULL;
    /* 0x2f17: CMP R12, RBX */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2f1a: JE .+15 */
    if (ctx->zf) goto loc_0x2f2b;

loc_0x2f1c:
    ctx->rip = 0x2f1cULL;
    /* 0x2f1c: CMP R13, R15 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2f1f: JBE .+108 */
    if ((ctx->cf || ctx->zf)) goto loc_0x2f8d;

loc_0x2f21:
    ctx->rip = 0x2f21ULL;
    /* 0x2f21: MOV [R13-0x1], 0x30 */
MEM_U8(ctx->r13 + (uint64_t)(-0x1LL)) = (uint8_t)(0x30ULL);
    /* 0x2f26: DEC R13 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x2f29: JMP .-15 */
    goto loc_0x2f1c;

loc_0x2f2b:
    ctx->rip = 0x2f2bULL;
    /* 0x2f2b: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2f2f: JNE .+32 */
    if (!ctx->zf) goto loc_0x2f51;

loc_0x2f31:
    ctx->rip = 0x2f31ULL;
    /* 0x2f31: MOV ESI, 0x1 */
ctx->rsi = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x2f36: MOV RDI, R13 */
ctx->rdi = (uint64_t)(ctx->r13);
    /* 0x2f39: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2f3c: MOV R15, R8 */
ctx->r15 = (uint64_t)(ctx->r8);
    /* 0x2f3f: CALL .+1528 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2f44ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x2f44: MOV R8, R15 */
ctx->r8 = (uint64_t)(ctx->r15);
    /* 0x2f47: MOV RDI, -0x3333333333333333 */
ctx->rdi = (uint64_t)(0xcccccccccccccccdULL);

loc_0x2f51:
    ctx->rip = 0x2f51ULL;
    /* 0x2f51: INC R13 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x2f54: CMP [RSP+Reg(0)+0xb8], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rsp + 0xb8ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x2f5c: JNE .+5 */
    if (!ctx->zf) goto loc_0x2f63;

loc_0x2f5e:
    ctx->rip = 0x2f5eULL;
    /* 0x2f5e: TEST R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2f61: JLE .+42 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x2f8d;

loc_0x2f63:
    ctx->rip = 0x2f63ULL;
    /* 0x2f63: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2f67: JNE .+36 */
    if (!ctx->zf) goto loc_0x2f8d;

loc_0x2f69:
    ctx->rip = 0x2f69ULL;
    /* 0x2f69: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2f6c: MOV ESI, 0x1 */
ctx->rsi = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x2f71: LEA RDI, [RIP+0x2b23] */
    ctx->rdi = (uint64_t)(0x5a9bULL);
    /* 0x2f78: MOV R15, R8 */
ctx->r15 = (uint64_t)(ctx->r8);
    /* 0x2f7b: CALL .+1468 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2f80ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x2f80: MOV R8, R15 */
ctx->r8 = (uint64_t)(ctx->r15);
    /* 0x2f83: MOV RDI, -0x3333333333333333 */
ctx->rdi = (uint64_t)(0xcccccccccccccccdULL);

loc_0x2f8d:
    ctx->rip = 0x2f8dULL;
    /* 0x2f8d: LEA R15, [RSP+Reg(0)+0x99] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x99ULL);
    /* 0x2f95: SUB R15, R13 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r13); uint64_t res;
      res = a - b;
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x2f98: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x2f9c: JNE .+37 */
    if (!ctx->zf) goto loc_0x2fc3;

loc_0x2f9e:
    ctx->rip = 0x2f9eULL;
    /* 0x2f9e: MOV ESI, R8L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r8));
    /* 0x2fa1: CMP R15, RSI */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->rsi); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2fa4: CMOVL RSI, R15 */
    if (ctx->sf != ctx->of) {
    ctx->rsi = (uint64_t)(ctx->r15);
    }
    /* 0x2fa8: MOV RDI, R13 */
ctx->rdi = (uint64_t)(ctx->r13);
    /* 0x2fab: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x2fae: MOV R13, R8 */
ctx->r13 = (uint64_t)(ctx->r8);
    /* 0x2fb1: CALL .+1414 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2fb6ULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x2fb6: MOV R8, R13 */
ctx->r8 = (uint64_t)(ctx->r13);
    /* 0x2fb9: MOV RDI, -0x3333333333333333 */
ctx->rdi = (uint64_t)(0xcccccccccccccccdULL);

loc_0x2fc3:
    ctx->rip = 0x2fc3ULL;
    /* 0x2fc3: SUB R8L, R15L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r15)); uint32_t res;
      res = a - b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2fc6: ADD R12, 0x4 */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(0x4ULL); uint64_t res;
      res = a + b;
    ctx->r12 = (uint64_t)(res);
    }
    /* 0x2fca: CMP R12, [RSP+Reg(0)+0x38] */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x2fcf: JAE .+17 */
    if (!ctx->cf) goto loc_0x2fe2;

loc_0x2fd1:
    ctx->rip = 0x2fd1ULL;
    /* 0x2fd1: TEST R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x2fd4: LEA R15, [RSP+Reg(0)+0x90] */
    ctx->r15 = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x2fdc: JNS .-278 */
    if (!ctx->sf) goto loc_0x2ecc;

loc_0x2fe2:
    ctx->rip = 0x2fe2ULL;
    /* 0x2fe2: ADD R8L, 0x12 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x12ULL); uint32_t res;
      res = a + b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2fe6: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x2fe9: MOV ESI, 0x30 */
ctx->rsi = (uint64_t)(uint32_t)(0x30ULL);
    /* 0x2fee: MOV EDX, R8L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r8));
    /* 0x2ff1: MOV ECX, 0x12 */
ctx->rcx = (uint64_t)(uint32_t)(0x12ULL);
    /* 0x2ff6: XOR R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x2ff9: CALL .+842 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x2ffeULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x2ffe: TEST [RBP], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbp)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x3002: JNE .+56 */
    if (!ctx->zf) goto loc_0x303c;

loc_0x3004:
    ctx->rip = 0x3004ULL;
    /* 0x3004: LEA RSI, [RSP+Reg(0)+0x90] */
    ctx->rsi = (uint64_t)(ctx->rsp + 0x90ULL);
    /* 0x300c: MOV RDI, [RSP+Reg(0)+0x158] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0x158ULL));
    /* 0x3014: SUB RSI, RDI */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x3017: MOV RDX, RBP */
ctx->rdx = (uint64_t)(ctx->rbp);
    /* 0x301a: CALL .+1309 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x301fULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x301f: JMP .+27 */
    goto loc_0x303c;

loc_0x3021:
    ctx->rip = 0x3021ULL;
    /* 0x3021: MOV EDX, R10L */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->r10));

loc_0x3024:
    ctx->rip = 0x3024ULL;
    /* 0x3024: ADD EDX, 0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0x9ULL); uint32_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x3027: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x302a: MOV ESI, 0x30 */
ctx->rsi = (uint64_t)(uint32_t)(0x30ULL);
    /* 0x302f: MOV ECX, 0x9 */
ctx->rcx = (uint64_t)(uint32_t)(0x9ULL);
    /* 0x3034: XOR R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x3037: CALL .+780 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x303cULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);

loc_0x303c:
    ctx->rip = 0x303cULL;
    /* 0x303c: MOV RCX, [RSP+Reg(0)+0x58] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x58ULL));

loc_0x3041:
    ctx->rip = 0x3041ULL;
    /* 0x3041: MOV R8L, [RSP+Reg(0)+0x1c] */
ctx->r8 = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x1cULL));
    /* 0x3046: XOR R8L, 0x2000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x2000ULL); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x304d: MOV RDI, RBP */
ctx->rdi = (uint64_t)(ctx->rbp);
    /* 0x3050: MOV ESI, 0x20 */
ctx->rsi = (uint64_t)(uint32_t)(0x20ULL);
    /* 0x3055: MOV RBX, [RSP+Reg(0)+0x28] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp + 0x28ULL));
    /* 0x305a: MOV EDX, EBX */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    /* 0x305c: MOV R15, RCX */
ctx->r15 = (uint64_t)(ctx->rcx);
    /* 0x305f: CALL .+740 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3064ULL;
    ctx->rip = 0x3348ULL;
    fn_0x3348(ctx);
    /* 0x3064: CMP R15L, EBX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r15)); uint32_t b = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3067: CMOVG EBX, R15L */
    if (!ctx->zf && ctx->sf == ctx->of) {
    ctx->rbx = (uint64_t)(uint32_t)(((uint32_t)ctx->r15));
    }
    /* 0x306b: MOV R13L, EBX */
ctx->r13 = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    /* 0x306e: MOV EBP, 0x54 */
ctx->rbp = (uint64_t)(uint32_t)(0x54ULL);
    /* 0x3073: TEST EBX, EBX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x3075: JNS .-7121 */
    if (!ctx->sf) goto loc_0x14aa;

loc_0x307b:
    ctx->rip = 0x307bULL;
    /* 0x307b: JMP .+99 */
    goto loc_0x30e0;

loc_0x307d:
    ctx->rip = 0x307dULL;
    /* 0x307d: MOVSXD RDX, R11L */
    ctx->rdx = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->r11)));
    /* 0x3080: ADD RDX, RDI */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x3083: SUB RDX, RCX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x3086: ADD RDX, -0x9 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0xfffffffffffffff7ULL); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x308a: XOR R8L, R8L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(((uint32_t)ctx->r8)); uint32_t res;
      res = a ^ b;
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x308d: TEST RDX, RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x3090: CMOVLE RDX, R8 */
    if (ctx->zf || ctx->sf != ctx->of) {
    ctx->rdx = (uint64_t)(ctx->r8);
    }
    /* 0x3094: CMP RDX, RAX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3097: CMOVGE RDX, RAX */
    if (ctx->sf == ctx->of) {
    ctx->rdx = (uint64_t)(ctx->rax);
    }
    /* 0x309b: MOV [RSP+Reg(0)+0x20], RDX */
MEM_U64(ctx->rsp + 0x20ULL) = (uint64_t)(ctx->rdx);
    /* 0x30a0: JMP .-1381 */
    goto loc_0x2b40;

loc_0x30a5:
    ctx->rip = 0x30a5ULL;
    /* 0x30a5: MOV RDX, [RSP+Reg(0)+0x58] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x58ULL));

loc_0x30aa:
    ctx->rip = 0x30aaULL;
    /* 0x30aa: MOV RAX, [RSP+Reg(0)+0x38] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x38ULL));
    /* 0x30af: SUB RAX, RBX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x30b2: SHR RAX, 0x2 */
    { uint64_t a = (uint64_t)(ctx->rax); uint8_t count = ((uint8_t)(0x2ULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x30b6: LEA R11L, [RAX+8*RAX] */
    ctx->r11 = (uint64_t)(uint32_t)(ctx->rax + (ctx->rax * 8ULL));
    /* 0x30ba: MOV EAX, [RBX] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbx));
    /* 0x30bc: CMP EAX, 0xa */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xaULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x30bf: JB .-1704 */
    if (ctx->cf) goto loc_0x2a1d;

loc_0x30c5:
    ctx->rip = 0x30c5ULL;
    /* 0x30c5: MOV ECX, 0xa */
ctx->rcx = (uint64_t)(uint32_t)(0xaULL);

loc_0x30ca:
    ctx->rip = 0x30caULL;
    /* 0x30ca: ADD ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x30cc: LEA ECX, [RCX+4*RCX] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rcx + (ctx->rcx * 4ULL));
    /* 0x30cf: INC R11L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r11)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->r11 = (uint64_t)(uint32_t)(res);
    }
    /* 0x30d2: CMP EAX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x30d4: JAE .-12 */
    if (!ctx->cf) goto loc_0x30ca;

loc_0x30d6:
    ctx->rip = 0x30d6ULL;
    /* 0x30d6: JMP .-1726 */
    goto loc_0x2a1d;

loc_0x30db:
    ctx->rip = 0x30dbULL;
    /* 0x30db: MOV EBP, 0x54 */
ctx->rbp = (uint64_t)(uint32_t)(0x54ULL);

loc_0x30e0:
    ctx->rip = 0x30e0ULL;
    /* 0x30e0: CALL .-10493 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x30e5ULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x30e5: MOV [RAX], EBP */
MEM_U32(ctx->rax) = (uint32_t)(((uint32_t)ctx->rbp));

loc_0x30e7:
    ctx->rip = 0x30e7ULL;
    /* 0x30e7: MOV EBP, -0x1 */
ctx->rbp = (uint64_t)(uint32_t)(0xffffffffffffffffULL);

loc_0x30ec:
    ctx->rip = 0x30ecULL;
    /* 0x30ec: MOV EAX, EBP */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x30ee: ADD RSP, 0x1e58 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x1e58ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x30f5: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x30f6: POP R12 */
    ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x30f8: POP R13 */
    ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x30fa: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x30fc: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x30fe: POP RBP */
    ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x30ff: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x3100:
    ctx->rip = 0x3100ULL;
    /* 0x3100: TEST R15, R15 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x3103: MOV R15, [RSP+Reg(0)+0x118] */
ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp + 0x118ULL));
    /* 0x310b: MOV R12, [RSP+Reg(0)+0xd8] */
ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp + 0xd8ULL));
    /* 0x3113: JNE .-41 */
    if (!ctx->zf) goto loc_0x30ec;

loc_0x3115:
    ctx->rip = 0x3115ULL;
    /* 0x3115: CMP [RSP+Reg(0)+0x44], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rsp + 0x44ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x311a: JE .+56 */
    if (ctx->zf) goto loc_0x3154;

loc_0x311c:
    ctx->rip = 0x311cULL;
    /* 0x311c: ADD R15, 0x10 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(0x10ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x3120: MOV EBX, 0x1 */
ctx->rbx = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x3125: MOV R14, [RSP+Reg(0)+0xd0] */
ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp + 0xd0ULL));

loc_0x312d:
    ctx->rip = 0x312dULL;
    /* 0x312d: MOV ESI, [R14+4*RBX] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->r14 + (ctx->rbx * 4ULL)));
    /* 0x3131: TEST ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x3133: JE .+35 */
    if (ctx->zf) goto loc_0x3158;

loc_0x3135:
    ctx->rip = 0x3135ULL;
    /* 0x3135: MOV RDI, R15 */
ctx->rdi = (uint64_t)(ctx->r15);
    /* 0x3138: MOV RDX, R12 */
ctx->rdx = (uint64_t)(ctx->r12);
    /* 0x313b: CALL .+66 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3140ULL;
    ctx->rip = 0x3182ULL;
    fn_0x3182(ctx);
    /* 0x3140: INC RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x3143: ADD R15, 0x10 */
    { uint64_t a = (uint64_t)(ctx->r15); uint64_t b = (uint64_t)(0x10ULL); uint64_t res;
      res = a + b;
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x3147: CMP RBX, 0xa */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0xaULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x314b: JNE .-32 */
    if (!ctx->zf) goto loc_0x312d;

loc_0x314d:
    ctx->rip = 0x314dULL;
    /* 0x314d: MOV EBP, 0x1 */
ctx->rbp = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x3152: JMP .-104 */
    goto loc_0x30ec;

loc_0x3154:
    ctx->rip = 0x3154ULL;
    /* 0x3154: XOR EBP, EBP */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rbp = (uint64_t)(uint32_t)(res);
    }
    /* 0x3156: JMP .-108 */
    goto loc_0x30ec;

loc_0x3158:
    ctx->rip = 0x3158ULL;
    /* 0x3158: MOV EBP, 0x1 */
ctx->rbp = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x315d: CMP RBX, 0x9 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0x9ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3161: JA .-119 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x30ec;

loc_0x3163:
    ctx->rip = 0x3163ULL;
    /* 0x3163: CMP [R14+4*RBX], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->r14 + (ctx->rbx * 4ULL))); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3168: JNE .+14 */
    if (!ctx->zf) goto loc_0x3178;

loc_0x316a:
    ctx->rip = 0x316aULL;
    /* 0x316a: INC RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x316d: CMP RBX, 0xa */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0xaULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3171: JNE .-16 */
    if (!ctx->zf) goto loc_0x3163;

loc_0x3173:
    ctx->rip = 0x3173ULL;
    /* 0x3173: JMP .-140 */
    goto loc_0x30ec;

loc_0x3178:
    ctx->rip = 0x3178ULL;
    /* 0x3178: MOV EBP, 0x16 */
ctx->rbp = (uint64_t)(uint32_t)(0x16ULL);
    /* 0x317d: JMP .-162 */
    goto loc_0x30e0;
    RECOMP_POP_UNWIND();
}

// Function pop_arg at 0x3182
void fn_0x3182(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x3182ULL) {
        switch (ctx->rip) {
            case 0x318eULL: goto loc_0x318e;
            case 0x319eULL: goto loc_0x319e;
            case 0x31a6ULL: goto loc_0x31a6;
            case 0x31b4ULL: goto loc_0x31b4;
            case 0x31c0ULL: goto loc_0x31c0;
            case 0x31c3ULL: goto loc_0x31c3;
            case 0x31c7ULL: goto loc_0x31c7;
            case 0x31d7ULL: goto loc_0x31d7;
            case 0x31e9ULL: goto loc_0x31e9;
            case 0x31f5ULL: goto loc_0x31f5;
            case 0x3206ULL: goto loc_0x3206;
            case 0x3212ULL: goto loc_0x3212;
            case 0x3223ULL: goto loc_0x3223;
            case 0x322fULL: goto loc_0x322f;
            case 0x3240ULL: goto loc_0x3240;
            case 0x324cULL: goto loc_0x324c;
            case 0x325dULL: goto loc_0x325d;
            case 0x3276ULL: goto loc_0x3276;
            case 0x3282ULL: goto loc_0x3282;
            case 0x3293ULL: goto loc_0x3293;
            case 0x329fULL: goto loc_0x329f;
            case 0x32b0ULL: goto loc_0x32b0;
            case 0x32bcULL: goto loc_0x32bc;
            case 0x32cbULL: goto loc_0x32cb;
            case 0x32ceULL: goto loc_0x32ce;
            case 0x32cfULL: goto loc_0x32cf;
            case 0x32dbULL: goto loc_0x32db;
            case 0x32e4ULL: goto loc_0x32e4;
            case 0x32f0ULL: goto loc_0x32f0;
            case 0x32f7ULL: goto loc_0x32f7;
            case 0x3303ULL: goto loc_0x3303;
            case 0x330cULL: goto loc_0x330c;
            case 0x3318ULL: goto loc_0x3318;
            case 0x3320ULL: goto loc_0x3320;
            case 0x332cULL: goto loc_0x332c;
            case 0x3334ULL: goto loc_0x3334;
            case 0x3340ULL: goto loc_0x3340;
            default: goto loc_0x3182;
        }
    }

loc_0x3182:
    ctx->rip = 0x3182ULL;
    /* 0x3182: ADD ESI, -0x9 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0xfffffffffffffff7ULL); uint32_t res;
      res = a + b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x3185: CMP ESI, 0x11 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x11ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3188: JA .+320 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x32ce;

loc_0x318e:
    ctx->rip = 0x318eULL;
    /* 0x318e: LEA RAX, [RIP+0x2d37] */
    ctx->rax = (uint64_t)(0x5eccULL);
    /* 0x3195: MOVSXD RCX, [RAX+4*RSI] */
    ctx->rcx = (uint64_t)((uint64_t)(int64_t)(int32_t)(MEM_U32(ctx->rax + (ctx->rsi * 4ULL))));
    /* 0x3199: ADD RCX, RAX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x319c: JMP RCX */
    RECOMP_POP_UNWIND();
    ctx->rip = ctx->rcx;
    recomp_dispatch(ctx, ctx->rcx);
    return;

loc_0x319e:
    ctx->rip = 0x319eULL;
    /* 0x319e: MOV ECX, [RDX] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx));
    /* 0x31a0: CMP RCX, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x31a4: JA .+14 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x31b4;

loc_0x31a6:
    ctx->rip = 0x31a6ULL;
    /* 0x31a6: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x31a9: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x31ad: ADD ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x31b0: MOV [RDX], ECX */
MEM_U32(ctx->rdx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x31b2: JMP .+12 */
    goto loc_0x31c0;

loc_0x31b4:
    ctx->rip = 0x31b4ULL;
    /* 0x31b4: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x31b8: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x31bc: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x31c0:
    ctx->rip = 0x31c0ULL;
    /* 0x31c0: MOV RAX, [RAX] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rax));

loc_0x31c3:
    ctx->rip = 0x31c3ULL;
    /* 0x31c3: MOV [RDI], RAX */
MEM_U64(ctx->rdi) = (uint64_t)(ctx->rax);
    /* 0x31c6: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x31c7:
    ctx->rip = 0x31c7ULL;
    /* 0x31c7: MOV ECX, [RDX+0x4] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx + 0x4ULL));
    /* 0x31ca: CMP RCX, 0xa0 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xa0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x31d1: JA .+217 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x32b0;

loc_0x31d7:
    ctx->rip = 0x31d7ULL;
    /* 0x31d7: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x31da: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x31de: ADD ECX, 0x10 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x10ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x31e1: MOV [RDX+0x4], ECX */
MEM_U32(ctx->rdx + 0x4ULL) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x31e4: JMP .+211 */
    goto loc_0x32bc;

loc_0x31e9:
    ctx->rip = 0x31e9ULL;
    /* 0x31e9: MOV ECX, [RDX] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx));
    /* 0x31eb: CMP RCX, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x31ef: JA .+218 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x32cf;

loc_0x31f5:
    ctx->rip = 0x31f5ULL;
    /* 0x31f5: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x31f8: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x31fc: ADD ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x31ff: MOV [RDX], ECX */
MEM_U32(ctx->rdx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x3201: JMP .+213 */
    goto loc_0x32db;

loc_0x3206:
    ctx->rip = 0x3206ULL;
    /* 0x3206: MOV ECX, [RDX] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx));
    /* 0x3208: CMP RCX, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x320c: JA .+210 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x32e4;

loc_0x3212:
    ctx->rip = 0x3212ULL;
    /* 0x3212: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x3215: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3219: ADD ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x321c: MOV [RDX], ECX */
MEM_U32(ctx->rdx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x321e: JMP .+205 */
    goto loc_0x32f0;

loc_0x3223:
    ctx->rip = 0x3223ULL;
    /* 0x3223: MOV ECX, [RDX] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx));
    /* 0x3225: CMP RCX, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3229: JA .+200 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x32f7;

loc_0x322f:
    ctx->rip = 0x322fULL;
    /* 0x322f: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x3232: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3236: ADD ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x3239: MOV [RDX], ECX */
MEM_U32(ctx->rdx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x323b: JMP .+195 */
    goto loc_0x3303;

loc_0x3240:
    ctx->rip = 0x3240ULL;
    /* 0x3240: MOV ECX, [RDX] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx));
    /* 0x3242: CMP RCX, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3246: JA .+192 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x330c;

loc_0x324c:
    ctx->rip = 0x324cULL;
    /* 0x324c: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x324f: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3253: ADD ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x3256: MOV [RDX], ECX */
MEM_U32(ctx->rdx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x3258: JMP .+187 */
    goto loc_0x3318;

loc_0x325d:
    ctx->rip = 0x325dULL;
    /* 0x325d: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x3261: ADD RAX, 0xf */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(0xfULL); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3265: AND RAX, -0x10 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(0xfffffffffffffff0ULL); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3269: LEA RCX, [RAX+0x10] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x10ULL);
    /* 0x326d: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);
    /* 0x3271: FLD [RAX] */
    { float f; uint32_t u = MEM_U32(ctx->rax); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x3273: FWAIT */
    /* fwait no-op */
    /* 0x3274: JMP .+85 */
    goto loc_0x32cb;

loc_0x3276:
    ctx->rip = 0x3276ULL;
    /* 0x3276: MOV ECX, [RDX] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx));
    /* 0x3278: CMP RCX, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x327c: JA .+158 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x3320;

loc_0x3282:
    ctx->rip = 0x3282ULL;
    /* 0x3282: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x3285: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3289: ADD ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x328c: MOV [RDX], ECX */
MEM_U32(ctx->rdx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x328e: JMP .+153 */
    goto loc_0x332c;

loc_0x3293:
    ctx->rip = 0x3293ULL;
    /* 0x3293: MOV ECX, [RDX] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdx));
    /* 0x3295: CMP RCX, 0x28 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x28ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3299: JA .+149 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x3334;

loc_0x329f:
    ctx->rip = 0x329fULL;
    /* 0x329f: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x32a2: ADD RAX, [RDX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdx + 0x10ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x32a6: ADD ECX, 0x8 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0x8ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x32a9: MOV [RDX], ECX */
MEM_U32(ctx->rdx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x32ab: JMP .+144 */
    goto loc_0x3340;

loc_0x32b0:
    ctx->rip = 0x32b0ULL;
    /* 0x32b0: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x32b4: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x32b8: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x32bc:
    ctx->rip = 0x32bcULL;
    /* 0x32bc: MOVSD_XMM X0, [RAX] */
    ctx->xmm[0].u64[0] = MEM_U64(ctx->rax);
    /* 0x32c0: MOVSD_XMM [RSP+Reg(0)-0x8], X0 */
    MEM_U64(ctx->rsp + (uint64_t)(-0x8LL)) = ctx->xmm[0].u64[0];
    /* 0x32c6: FLD [RSP+Reg(0)-0x8] */
    { double d; uint64_t u = MEM_U64(ctx->rsp + (uint64_t)(-0x8LL)); memcpy(&d, &u, 8); fpu_push(ctx, d); }
    /* 0x32ca: FWAIT */
    /* fwait no-op */

loc_0x32cb:
    ctx->rip = 0x32cbULL;
    /* 0x32cb: FSTP [RDI] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rdi) = u; }
    /* 0x32cd: FWAIT */
    /* fwait no-op */

loc_0x32ce:
    ctx->rip = 0x32ceULL;
    /* 0x32ce: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x32cf:
    ctx->rip = 0x32cfULL;
    /* 0x32cf: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x32d3: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x32d7: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x32db:
    ctx->rip = 0x32dbULL;
    /* 0x32db: MOVSX RAX, [RAX] */
    ctx->rax = (uint64_t)((uint64_t)(int64_t)(int8_t)(MEM_U8(ctx->rax)));
    /* 0x32df: JMP .-289 */
    goto loc_0x31c3;

loc_0x32e4:
    ctx->rip = 0x32e4ULL;
    /* 0x32e4: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x32e8: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x32ec: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x32f0:
    ctx->rip = 0x32f0ULL;
    /* 0x32f0: MOV EAX, [RAX] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rax));
    /* 0x32f2: JMP .-308 */
    goto loc_0x31c3;

loc_0x32f7:
    ctx->rip = 0x32f7ULL;
    /* 0x32f7: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x32fb: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x32ff: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x3303:
    ctx->rip = 0x3303ULL;
    /* 0x3303: MOVSX RAX, [RAX] */
    ctx->rax = (uint64_t)((uint64_t)(int64_t)(int16_t)(MEM_U16(ctx->rax)));
    /* 0x3307: JMP .-329 */
    goto loc_0x31c3;

loc_0x330c:
    ctx->rip = 0x330cULL;
    /* 0x330c: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x3310: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x3314: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x3318:
    ctx->rip = 0x3318ULL;
    /* 0x3318: MOVZX EAX, [RAX] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(MEM_U8(ctx->rax)));
    /* 0x331b: JMP .-349 */
    goto loc_0x31c3;

loc_0x3320:
    ctx->rip = 0x3320ULL;
    /* 0x3320: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x3324: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x3328: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x332c:
    ctx->rip = 0x332cULL;
    /* 0x332c: MOVZX EAX, [RAX] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rax)));
    /* 0x332f: JMP .-369 */
    goto loc_0x31c3;

loc_0x3334:
    ctx->rip = 0x3334ULL;
    /* 0x3334: MOV RAX, [RDX+0x8] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x8ULL));
    /* 0x3338: LEA RCX, [RAX+0x8] */
    ctx->rcx = (uint64_t)(ctx->rax + 0x8ULL);
    /* 0x333c: MOV [RDX+0x8], RCX */
MEM_U64(ctx->rdx + 0x8ULL) = (uint64_t)(ctx->rcx);

loc_0x3340:
    ctx->rip = 0x3340ULL;
    /* 0x3340: MOVSXD RAX, [RAX] */
    ctx->rax = (uint64_t)((uint64_t)(int64_t)(int32_t)(MEM_U32(ctx->rax)));
    /* 0x3343: JMP .-389 */
    goto loc_0x31c3;
    RECOMP_POP_UNWIND();
}

// Function pad at 0x3348
void fn_0x3348(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x3348ULL) {
        switch (ctx->rip) {
            case 0x3359ULL: goto loc_0x3359;
            case 0x3362ULL: goto loc_0x3362;
            case 0x3387ULL: goto loc_0x3387;
            case 0x338aULL: goto loc_0x338a;
            case 0x338fULL: goto loc_0x338f;
            case 0x339fULL: goto loc_0x339f;
            case 0x33adULL: goto loc_0x33ad;
            case 0x33b2ULL: goto loc_0x33b2;
            case 0x33bfULL: goto loc_0x33bf;
            default: goto loc_0x3348;
        }
    }

loc_0x3348:
    ctx->rip = 0x3348ULL;
    /* 0x3348: PUSH RBP */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbp);
    /* 0x3349: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x334b: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x334c: SUB RSP, 0x100 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x100ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x3353: MOV EBP, EDX */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->rdx));
    /* 0x3355: SUB EBP, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->rbp = (uint64_t)(uint32_t)(res);
    }
    /* 0x3357: JLE .+102 */
    if (ctx->zf || ctx->sf != ctx->of) goto loc_0x33bf;

loc_0x3359:
    ctx->rip = 0x3359ULL;
    /* 0x3359: AND R8L, 0x12000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r8)); uint32_t b = (uint32_t)(0x12000ULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->r8 = (uint64_t)(uint32_t)(res);
    }
    /* 0x3360: JNE .+93 */
    if (!ctx->zf) goto loc_0x33bf;

loc_0x3362:
    ctx->rip = 0x3362ULL;
    /* 0x3362: MOV RBX, RDI */
ctx->rbx = (uint64_t)(ctx->rdi);
    /* 0x3365: MOVZX ESI, SIB */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(((uint8_t)(ctx->rsi & 0xff))));
    /* 0x3369: MOV R14L, 0x100 */
ctx->r14 = (uint64_t)(uint32_t)(0x100ULL);
    /* 0x336f: CMP EBP, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3372: MOV EDX, 0x100 */
ctx->rdx = (uint64_t)(uint32_t)(0x100ULL);
    /* 0x3377: CMOVB EDX, EBP */
    if (ctx->cf) {
    ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    }
    /* 0x337a: MOV RDI, RSP */
ctx->rdi = (uint64_t)(ctx->rsp);
    /* 0x337d: CALL .+3934 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3382ULL;
    ctx->rip = 0x42e0ULL;
    fn_0x42e0(ctx);
    /* 0x3382: CMP EBP, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3385: JB .+38 */
    if (ctx->cf) goto loc_0x33ad;

loc_0x3387:
    ctx->rip = 0x3387ULL;
    /* 0x3387: MOV R14, RSP */
ctx->r14 = (uint64_t)(ctx->rsp);

loc_0x338a:
    ctx->rip = 0x338aULL;
    /* 0x338a: TEST [RBX], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbx)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x338d: JNE .+16 */
    if (!ctx->zf) goto loc_0x339f;

loc_0x338f:
    ctx->rip = 0x338fULL;
    /* 0x338f: MOV ESI, 0x100 */
ctx->rsi = (uint64_t)(uint32_t)(0x100ULL);
    /* 0x3394: MOV RDI, R14 */
ctx->rdi = (uint64_t)(ctx->r14);
    /* 0x3397: MOV RDX, RBX */
ctx->rdx = (uint64_t)(ctx->rbx);
    /* 0x339a: CALL .+413 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x339fULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x339f:
    ctx->rip = 0x339fULL;
    /* 0x339f: ADD EBP, -0x100 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(0xffffffffffffff00ULL); uint32_t res;
      res = a + b;
    ctx->rbp = (uint64_t)(uint32_t)(res);
    }
    /* 0x33a5: CMP EBP, 0xff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(0xffULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x33ab: JA .-35 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x338a;

loc_0x33ad:
    ctx->rip = 0x33adULL;
    /* 0x33ad: TEST [RBX], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rbx)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x33b0: JNE .+13 */
    if (!ctx->zf) goto loc_0x33bf;

loc_0x33b2:
    ctx->rip = 0x33b2ULL;
    /* 0x33b2: MOV ESI, EBP */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rbp));
    /* 0x33b4: MOV RDI, RSP */
ctx->rdi = (uint64_t)(ctx->rsp);
    /* 0x33b7: MOV RDX, RBX */
ctx->rdx = (uint64_t)(ctx->rbx);
    /* 0x33ba: CALL .+381 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x33bfULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);

loc_0x33bf:
    ctx->rip = 0x33bfULL;
    /* 0x33bf: ADD RSP, 0x100 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x100ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x33c6: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x33c7: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x33c9: POP RBP */
    ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x33ca: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __lockfile at 0x33cc
void fn_0x33cc(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x33ccULL) {
        switch (ctx->rip) {
            case 0x33e9ULL: goto loc_0x33e9;
            case 0x33fdULL: goto loc_0x33fd;
            case 0x340eULL: goto loc_0x340e;
            case 0x3412ULL: goto loc_0x3412;
            case 0x3418ULL: goto loc_0x3418;
            case 0x3429ULL: goto loc_0x3429;
            case 0x3436ULL: goto loc_0x3436;
            default: goto loc_0x33cc;
        }
    }

loc_0x33cc:
    ctx->rip = 0x33ccULL;
    /* 0x33cc: MOV EAX, -0x40000001 */
ctx->rax = (uint64_t)(uint32_t)(0xffffffffbfffffffULL);
    /* 0x33d1: AND EAX, [RDI+0x8c] */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(MEM_U32(ctx->rdi + 0x8cULL)); uint32_t res;
      res = a & b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x33d7: MOV RCX, [Reg(0)] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->fs_base));
    /* 0x33e0: MOV EDX, [RCX+0x38] */
ctx->rdx = (uint64_t)(uint32_t)(MEM_U32(ctx->rcx + 0x38ULL));
    /* 0x33e3: XOR ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a ^ b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x33e5: CMP EAX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x33e7: JE .+77 */
    if (ctx->zf) goto loc_0x3436;

loc_0x33e9:
    ctx->rip = 0x33e9ULL;
    /* 0x33e9: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x33eb: LOCK CMPXCHG [RDI+0x8c], EDX */
    { uint32_t src = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t acc = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t prev = __sync_val_compare_and_swap((uint32_t *)(ctx->mem_base + (ctx->rdi + 0x8cULL)), acc, src);
      set_flags_sub_u32(ctx, acc, prev, acc - prev);
      if (acc != prev) {
      ctx->rax = (uint64_t)(uint32_t)(prev);
      }
    }
    /* 0x33f3: FWAIT */
    /* fwait no-op */
    /* 0x33f4: MOV ECX, 0x1 */
ctx->rcx = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x33f9: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x33fb: JE .+57 */
    if (ctx->zf) goto loc_0x3436;

loc_0x33fd:
    ctx->rip = 0x33fdULL;
    /* 0x33fd: OR EDX, 0x40000000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0x40000000ULL); uint32_t res;
      res = a | b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x3403: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x3405: LOCK CMPXCHG [RDI+0x8c], EDX */
    { uint32_t src = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t acc = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t prev = __sync_val_compare_and_swap((uint32_t *)(ctx->mem_base + (ctx->rdi + 0x8cULL)), acc, src);
      set_flags_sub_u32(ctx, acc, prev, acc - prev);
      if (acc != prev) {
      ctx->rax = (uint64_t)(uint32_t)(prev);
      }
    }
    /* 0x340d: FWAIT */
    /* fwait no-op */

loc_0x340e:
    ctx->rip = 0x340eULL;
    /* 0x340e: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x3410: JE .+36 */
    if (ctx->zf) goto loc_0x3436;

loc_0x3412:
    ctx->rip = 0x3412ULL;
    /* 0x3412: BT EAX, 0x1e */
    {
      uint32_t bit_idx = ((uint32_t)(0x1eULL)) & 31;
      uint32_t val = (uint32_t)(((uint32_t)ctx->rax));
      ctx->cf = (val >> bit_idx) & 1;
    }
    /* 0x3416: JB .+17 */
    if (ctx->cf) goto loc_0x3429;

loc_0x3418:
    ctx->rip = 0x3418ULL;
    /* 0x3418: MOV ESI, EAX */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x341a: OR ESI, 0x40000000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(0x40000000ULL); uint32_t res;
      res = a | b;
      set_flags_logic_u32(ctx, res);
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x3420: LOCK CMPXCHG [RDI+0x8c], ESI */
    { uint32_t src = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t acc = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t prev = __sync_val_compare_and_swap((uint32_t *)(ctx->mem_base + (ctx->rdi + 0x8cULL)), acc, src);
      set_flags_sub_u32(ctx, acc, prev, acc - prev);
      if (acc != prev) {
      ctx->rax = (uint64_t)(uint32_t)(prev);
      }
    }
    /* 0x3428: FWAIT */
    /* fwait no-op */

loc_0x3429:
    ctx->rip = 0x3429ULL;
    /* 0x3429: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x342b: LOCK CMPXCHG [RDI+0x8c], EDX */
    { uint32_t src = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t acc = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t prev = __sync_val_compare_and_swap((uint32_t *)(ctx->mem_base + (ctx->rdi + 0x8cULL)), acc, src);
      set_flags_sub_u32(ctx, acc, prev, acc - prev);
      if (acc != prev) {
      ctx->rax = (uint64_t)(uint32_t)(prev);
      }
    }
    /* 0x3433: FWAIT */
    /* fwait no-op */
    /* 0x3434: JMP .-40 */
    goto loc_0x340e;

loc_0x3436:
    ctx->rip = 0x3436ULL;
    /* 0x3436: MOV EAX, ECX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x3438: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __unlockfile at 0x3439
void fn_0x3439(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x3439:
    ctx->rip = 0x3439ULL;
    /* 0x3439: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x343b: XCHG [RDI+0x8c], EAX */
    { uint32_t tmp1 = (uint32_t)(MEM_U32(ctx->rdi + 0x8cULL)); uint32_t tmp2 = (uint32_t)(((uint32_t)ctx->rax));
    MEM_U32(ctx->rdi + 0x8cULL) = (uint32_t)(tmp2);
    ctx->rax = (uint64_t)(uint32_t)(tmp1);
    }
    /* 0x3441: FWAIT */
    /* fwait no-op */
    /* 0x3442: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __towrite at 0x3444
void fn_0x3444(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x3444ULL) {
        switch (ctx->rip) {
            case 0x345bULL: goto loc_0x345b;
            case 0x3479ULL: goto loc_0x3479;
            default: goto loc_0x3444;
        }
    }

loc_0x3444:
    ctx->rip = 0x3444ULL;
    /* 0x3444: MOV EAX, [RDI] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rdi));
    /* 0x3446: MOV ECX, [RDI+0x88] */
ctx->rcx = (uint64_t)(uint32_t)(MEM_U32(ctx->rdi + 0x88ULL));
    /* 0x344c: LEA EDX, [RCX-0x1] */
    ctx->rdx = (uint64_t)(uint32_t)(ctx->rcx + (uint64_t)(-0x1LL));
    /* 0x344f: OR EDX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a | b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x3451: MOV [RDI+0x88], EDX */
MEM_U32(ctx->rdi + 0x88ULL) = (uint32_t)(((uint32_t)ctx->rdx));
    /* 0x3457: TEST AL, 0x8 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x8ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x3459: JNE .+30 */
    if (!ctx->zf) goto loc_0x3479;

loc_0x345b:
    ctx->rip = 0x345bULL;
    /* 0x345b: XORPS X0, X0 */
    memset(&ctx->xmm[0], 0, 16);
    /* 0x345e: MOVUPS [RDI+0x8], X0 */
    memcpy(ctx->mem_base + (ctx->rdi + 0x8ULL), &ctx->xmm[0], 16);
    /* 0x3462: MOV RAX, [RDI+0x58] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdi + 0x58ULL));
    /* 0x3466: MOV [RDI+0x38], RAX */
MEM_U64(ctx->rdi + 0x38ULL) = (uint64_t)(ctx->rax);
    /* 0x346a: MOV [RDI+0x28], RAX */
MEM_U64(ctx->rdi + 0x28ULL) = (uint64_t)(ctx->rax);
    /* 0x346e: ADD RAX, [RDI+0x60] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rdi + 0x60ULL)); uint64_t res;
      res = a + b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3472: MOV [RDI+0x20], RAX */
MEM_U64(ctx->rdi + 0x20ULL) = (uint64_t)(ctx->rax);
    /* 0x3476: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x3478: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x3479:
    ctx->rip = 0x3479ULL;
    /* 0x3479: OR EAX, 0x20 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x20ULL); uint32_t res;
      res = a | b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x347c: MOV [RDI], EAX */
MEM_U32(ctx->rdi) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x347e: MOV EAX, -0x1 */
ctx->rax = (uint64_t)(uint32_t)(0xffffffffffffffffULL);
    /* 0x3483: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __towrite_needs_stdio_exit at 0x3484
void fn_0x3484(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x3484:
    ctx->rip = 0x3484ULL;
    /* 0x3484: JMP .+3 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x348cULL;
    fn_0x348c(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function __stdio_exit at 0x348c
void fn_0x348c(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x348cULL) {
        switch (ctx->rip) {
            case 0x3495ULL: goto loc_0x3495;
            case 0x349aULL: goto loc_0x349a;
            case 0x34a8ULL: goto loc_0x34a8;
            default: goto loc_0x348c;
        }
    }

loc_0x348c:
    ctx->rip = 0x348cULL;
    /* 0x348c: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x348d: CALL .+134 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3492ULL;
    ctx->rip = 0x3518ULL;
    fn_0x3518(ctx);
    /* 0x3492: MOV RBX, [RAX] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rax));

loc_0x3495:
    ctx->rip = 0x3495ULL;
    /* 0x3495: TEST RBX, RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x3498: JE .+14 */
    if (ctx->zf) goto loc_0x34a8;

loc_0x349a:
    ctx->rip = 0x349aULL;
    /* 0x349a: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x349d: CALL .+43 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x34a2ULL;
    ctx->rip = 0x34cdULL;
    fn_0x34cd(ctx);
    /* 0x34a2: MOV RBX, [RBX+0x70] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rbx + 0x70ULL));
    /* 0x34a6: JMP .-19 */
    goto loc_0x3495;

loc_0x34a8:
    ctx->rip = 0x34a8ULL;
    /* 0x34a8: MOV RDI, [RIP+0x11139] */
ctx->rdi = (uint64_t)(MEM_U64(0x145e8ULL));
    /* 0x34af: CALL .+25 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x34b4ULL;
    ctx->rip = 0x34cdULL;
    fn_0x34cd(ctx);
    /* 0x34b4: MOV RDI, [RIP+0x10ca5] */
ctx->rdi = (uint64_t)(MEM_U64(0x14160ULL));
    /* 0x34bb: CALL .+13 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x34c0ULL;
    ctx->rip = 0x34cdULL;
    fn_0x34cd(ctx);
    /* 0x34c0: MOV RDI, [RIP+0x11121] */
ctx->rdi = (uint64_t)(MEM_U64(0x145e8ULL));
    /* 0x34c7: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x34c8: JMP .+0 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x34cdULL;
    fn_0x34cd(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function close_file at 0x34cd
void fn_0x34cd(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x34cdULL) {
        switch (ctx->rip) {
            case 0x34d3ULL: goto loc_0x34d3;
            case 0x34dfULL: goto loc_0x34df;
            case 0x34e7ULL: goto loc_0x34e7;
            case 0x34f1ULL: goto loc_0x34f1;
            case 0x34fbULL: goto loc_0x34fb;
            case 0x3505ULL: goto loc_0x3505;
            case 0x3514ULL: goto loc_0x3514;
            default: goto loc_0x34cd;
        }
    }

loc_0x34cd:
    ctx->rip = 0x34cdULL;
    /* 0x34cd: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x34ce: TEST RDI, RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x34d1: JE .+65 */
    if (ctx->zf) goto loc_0x3514;

loc_0x34d3:
    ctx->rip = 0x34d3ULL;
    /* 0x34d3: MOV RBX, RDI */
ctx->rbx = (uint64_t)(ctx->rdi);
    /* 0x34d6: CMP [RDI+0x8c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rdi + 0x8cULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x34dd: JS .+8 */
    if (ctx->sf) goto loc_0x34e7;

loc_0x34df:
    ctx->rip = 0x34dfULL;
    /* 0x34df: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x34e2: CALL .-283 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x34e7ULL;
    ctx->rip = 0x33ccULL;
    fn_0x33cc(ctx);

loc_0x34e7:
    ctx->rip = 0x34e7ULL;
    /* 0x34e7: MOV RAX, [RBX+0x28] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbx + 0x28ULL));
    /* 0x34eb: CMP RAX, [RBX+0x38] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rbx + 0x38ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x34ef: JE .+10 */
    if (ctx->zf) goto loc_0x34fb;

loc_0x34f1:
    ctx->rip = 0x34f1ULL;
    /* 0x34f1: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x34f4: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x34f6: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x34f8: CALL [RBX+0x48] */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x34fbULL;
    ctx->rip = MEM_U64(ctx->rbx + 0x48ULL);
    recomp_dispatch(ctx, MEM_U64(ctx->rbx + 0x48ULL));

loc_0x34fb:
    ctx->rip = 0x34fbULL;
    /* 0x34fb: MOV RSI, [RBX+0x8] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rbx + 0x8ULL));
    /* 0x34ff: SUB RSI, [RBX+0x10] */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(MEM_U64(ctx->rbx + 0x10ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    ctx->rsi = (uint64_t)(res);
    }
    /* 0x3503: JE .+15 */
    if (ctx->zf) goto loc_0x3514;

loc_0x3505:
    ctx->rip = 0x3505ULL;
    /* 0x3505: MOV RAX, [RBX+0x50] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbx + 0x50ULL));
    /* 0x3509: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x350c: MOV EDX, 0x1 */
ctx->rdx = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x3511: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3512: JMP RAX */
    RECOMP_POP_UNWIND();
    ctx->rip = ctx->rax;
    recomp_dispatch(ctx, ctx->rax);
    return;

loc_0x3514:
    ctx->rip = 0x3514ULL;
    /* 0x3514: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3515: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __ofl_lock at 0x3518
void fn_0x3518(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x3518:
    ctx->rip = 0x3518ULL;
    /* 0x3518: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x3519: LEA RDI, [RIP+0x110d0] */
    ctx->rdi = (uint64_t)(0x145f0ULL);
    /* 0x3520: CALL .+4571 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3525ULL;
    ctx->rip = 0x4700ULL;
    fn_0x4700(ctx);
    /* 0x3525: LEA RAX, [RIP+0x110cc] */
    ctx->rax = (uint64_t)(0x145f8ULL);
    /* 0x352c: POP RCX */
    ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x352d: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __ofl_unlock at 0x352e
void fn_0x352e(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x352e:
    ctx->rip = 0x352eULL;
    /* 0x352e: LEA RDI, [RIP+0x110bb] */
    ctx->rdi = (uint64_t)(0x145f0ULL);
    /* 0x3535: JMP .+4655 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x4769ULL;
    fn_0x4769(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function __fwritex at 0x353c
void fn_0x353c(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x353cULL) {
        switch (ctx->rip) {
            case 0x3557ULL: goto loc_0x3557;
            case 0x3563ULL: goto loc_0x3563;
            case 0x357bULL: goto loc_0x357b;
            case 0x3584ULL: goto loc_0x3584;
            case 0x3589ULL: goto loc_0x3589;
            case 0x3597ULL: goto loc_0x3597;
            case 0x359fULL: goto loc_0x359f;
            case 0x35aaULL: goto loc_0x35aa;
            case 0x35b0ULL: goto loc_0x35b0;
            case 0x35c7ULL: goto loc_0x35c7;
            case 0x35d3ULL: goto loc_0x35d3;
            case 0x35d5ULL: goto loc_0x35d5;
            case 0x35dfULL: goto loc_0x35df;
            case 0x35e8ULL: goto loc_0x35e8;
            case 0x35fcULL: goto loc_0x35fc;
            default: goto loc_0x353c;
        }
    }

loc_0x353c:
    ctx->rip = 0x353cULL;
    /* 0x353c: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x353e: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x3540: PUSH R13 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r13);
    /* 0x3542: PUSH R12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r12);
    /* 0x3544: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x3545: MOV RBX, RDX */
ctx->rbx = (uint64_t)(ctx->rdx);
    /* 0x3548: MOV R14, RSI */
ctx->r14 = (uint64_t)(ctx->rsi);
    /* 0x354b: MOV R15, RDI */
ctx->r15 = (uint64_t)(ctx->rdi);
    /* 0x354e: MOV RAX, [RDX+0x20] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rdx + 0x20ULL));
    /* 0x3552: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x3555: JE .+112 */
    if (ctx->zf) goto loc_0x35c7;

loc_0x3557:
    ctx->rip = 0x3557ULL;
    /* 0x3557: MOV RDI, [RBX+0x28] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbx + 0x28ULL));
    /* 0x355b: SUB RAX, RDI */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a - b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x355e: CMP RAX, R14 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->r14); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3561: JAE .+24 */
    if (!ctx->cf) goto loc_0x357b;

loc_0x3563:
    ctx->rip = 0x3563ULL;
    /* 0x3563: MOV RAX, [RBX+0x48] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbx + 0x48ULL));
    /* 0x3567: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x356a: MOV RSI, R15 */
ctx->rsi = (uint64_t)(ctx->r15);
    /* 0x356d: MOV RDX, R14 */
ctx->rdx = (uint64_t)(ctx->r14);
    /* 0x3570: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3571: POP R12 */
    ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3573: POP R13 */
    ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3575: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3577: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3579: JMP RAX */
    RECOMP_POP_UNWIND();
    ctx->rip = ctx->rax;
    recomp_dispatch(ctx, ctx->rax);
    return;

loc_0x357b:
    ctx->rip = 0x357bULL;
    /* 0x357b: CMP [RBX+0x90], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rbx + 0x90ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3582: JS .+38 */
    if (ctx->sf) goto loc_0x35aa;

loc_0x3584:
    ctx->rip = 0x3584ULL;
    /* 0x3584: TEST R14, R14 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(ctx->r14); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x3587: JE .+33 */
    if (ctx->zf) goto loc_0x35aa;

loc_0x3589:
    ctx->rip = 0x3589ULL;
    /* 0x3589: LEA R13, [R15+R14] */
    ctx->r13 = (uint64_t)(ctx->r15 + ctx->r14);
    /* 0x358d: MOV RAX, -0x1 */
ctx->rax = (uint64_t)(0xffffffffffffffffULL);
    /* 0x3594: XOR R12L, R12L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r12)); uint32_t b = (uint32_t)(((uint32_t)ctx->r12)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r12 = (uint64_t)(uint32_t)(res);
    }

loc_0x3597:
    ctx->rip = 0x3597ULL;
    /* 0x3597: CMP [R13+RAX], 0xa */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->r13 + ctx->rax)); uint8_t b = (uint8_t)(0xaULL); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x359d: JE .+73 */
    if (ctx->zf) goto loc_0x35e8;

loc_0x359f:
    ctx->rip = 0x359fULL;
    /* 0x359f: INC R12 */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->r12 = (uint64_t)(res);
    }
    /* 0x35a2: DEC RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x35a5: CMP R14, R12 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x35a8: JNE .-19 */
    if (!ctx->zf) goto loc_0x3597;

loc_0x35aa:
    ctx->rip = 0x35aaULL;
    /* 0x35aa: MOV R12, R14 */
ctx->r12 = (uint64_t)(ctx->r14);
    /* 0x35ad: XOR R14L, R14L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r14)); uint32_t b = (uint32_t)(((uint32_t)ctx->r14)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r14 = (uint64_t)(uint32_t)(res);
    }

loc_0x35b0:
    ctx->rip = 0x35b0ULL;
    /* 0x35b0: MOV RSI, R15 */
ctx->rsi = (uint64_t)(ctx->r15);
    /* 0x35b3: MOV RDX, R12 */
ctx->rdx = (uint64_t)(ctx->r12);
    /* 0x35b6: CALL .+805 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x35bbULL;
    ctx->rip = 0x38e0ULL;
    fn_0x38e0(ctx);
    /* 0x35bb: ADD [RBX+0x28], R12 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rbx + 0x28ULL)); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a + b;
    MEM_U64(ctx->rbx + 0x28ULL) = (uint64_t)(res);
    }
    /* 0x35bf: ADD R14, R12 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x35c2: MOV RAX, R14 */
ctx->rax = (uint64_t)(ctx->r14);
    /* 0x35c5: JMP .+14 */
    goto loc_0x35d5;

loc_0x35c7:
    ctx->rip = 0x35c7ULL;
    /* 0x35c7: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x35ca: CALL .-395 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x35cfULL;
    ctx->rip = 0x3444ULL;
    fn_0x3444(ctx);
    /* 0x35cf: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x35d1: JE .+12 */
    if (ctx->zf) goto loc_0x35df;

loc_0x35d3:
    ctx->rip = 0x35d3ULL;
    /* 0x35d3: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }

loc_0x35d5:
    ctx->rip = 0x35d5ULL;
    /* 0x35d5: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x35d6: POP R12 */
    ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x35d8: POP R13 */
    ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x35da: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x35dc: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x35de: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x35df:
    ctx->rip = 0x35dfULL;
    /* 0x35df: MOV RAX, [RBX+0x20] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rbx + 0x20ULL));
    /* 0x35e3: JMP .-145 */
    goto loc_0x3557;

loc_0x35e8:
    ctx->rip = 0x35e8ULL;
    /* 0x35e8: SUB R14, R12 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x35eb: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x35ee: MOV RSI, R15 */
ctx->rsi = (uint64_t)(ctx->r15);
    /* 0x35f1: MOV RDX, R14 */
ctx->rdx = (uint64_t)(ctx->r14);
    /* 0x35f4: CALL [RBX+0x48] */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x35f7ULL;
    ctx->rip = MEM_U64(ctx->rbx + 0x48ULL);
    recomp_dispatch(ctx, MEM_U64(ctx->rbx + 0x48ULL));
    /* 0x35f7: CMP RAX, R14 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->r14); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x35fa: JB .-39 */
    if (ctx->cf) goto loc_0x35d5;

loc_0x35fc:
    ctx->rip = 0x35fcULL;
    /* 0x35fc: SUB R13, R12 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r12); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    ctx->r13 = (uint64_t)(res);
    }
    /* 0x35ff: MOV RDI, [RBX+0x28] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rbx + 0x28ULL));
    /* 0x3603: MOV R15, R13 */
ctx->r15 = (uint64_t)(ctx->r13);
    /* 0x3606: JMP .-88 */
    goto loc_0x35b0;
    RECOMP_POP_UNWIND();
}

// Function fwrite at 0x3608
void fn_0x3608(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x3608ULL) {
        switch (ctx->rip) {
            case 0x3636ULL: goto loc_0x3636;
            case 0x3655ULL: goto loc_0x3655;
            case 0x365fULL: goto loc_0x365f;
            case 0x3670ULL: goto loc_0x3670;
            case 0x3675ULL: goto loc_0x3675;
            case 0x3680ULL: goto loc_0x3680;
            default: goto loc_0x3608;
        }
    }

loc_0x3608:
    ctx->rip = 0x3608ULL;
    /* 0x3608: PUSH RBP */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbp);
    /* 0x3609: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x360b: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x360d: PUSH R13 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r13);
    /* 0x360f: PUSH R12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r12);
    /* 0x3611: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x3612: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x3613: MOV R12, RCX */
ctx->r12 = (uint64_t)(ctx->rcx);
    /* 0x3616: MOV RBX, RDX */
ctx->rbx = (uint64_t)(ctx->rdx);
    /* 0x3619: MOV R14, RSI */
ctx->r14 = (uint64_t)(ctx->rsi);
    /* 0x361c: MOV R13, RDI */
ctx->r13 = (uint64_t)(ctx->rdi);
    /* 0x361f: MOV R15, RDX */
ctx->r15 = (uint64_t)(ctx->rdx);
    /* 0x3622: IMUL R15, RSI */
    { int64_t a = (int64_t)(ctx->r15); int64_t b = (int64_t)(ctx->rsi); int64_t res = a * b;
    ctx->r15 = (uint64_t)(res);
    }
    /* 0x3626: TEST RSI, RSI */
    { uint64_t a = (uint64_t)(ctx->rsi); uint64_t b = (uint64_t)(ctx->rsi); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x3629: CMOVE RBX, RSI */
    if (ctx->zf) {
    ctx->rbx = (uint64_t)(ctx->rsi);
    }
    /* 0x362d: CMP [RCX+0x8c], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rcx + 0x8cULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3634: JS .+41 */
    if (ctx->sf) goto loc_0x365f;

loc_0x3636:
    ctx->rip = 0x3636ULL;
    /* 0x3636: MOV RDI, R12 */
ctx->rdi = (uint64_t)(ctx->r12);
    /* 0x3639: CALL .-626 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x363eULL;
    ctx->rip = 0x33ccULL;
    fn_0x33cc(ctx);
    /* 0x363e: MOV EBP, EAX */
ctx->rbp = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x3640: MOV RDI, R13 */
ctx->rdi = (uint64_t)(ctx->r13);
    /* 0x3643: MOV RSI, R15 */
ctx->rsi = (uint64_t)(ctx->r15);
    /* 0x3646: MOV RDX, R12 */
ctx->rdx = (uint64_t)(ctx->r12);
    /* 0x3649: CALL .-274 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x364eULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x364e: MOV R13, RAX */
ctx->r13 = (uint64_t)(ctx->rax);
    /* 0x3651: TEST EBP, EBP */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t b = (uint32_t)(((uint32_t)ctx->rbp)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x3653: JE .+27 */
    if (ctx->zf) goto loc_0x3670;

loc_0x3655:
    ctx->rip = 0x3655ULL;
    /* 0x3655: MOV RDI, R12 */
ctx->rdi = (uint64_t)(ctx->r12);
    /* 0x3658: CALL .-548 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x365dULL;
    ctx->rip = 0x3439ULL;
    fn_0x3439(ctx);
    /* 0x365d: JMP .+17 */
    goto loc_0x3670;

loc_0x365f:
    ctx->rip = 0x365fULL;
    /* 0x365f: MOV RDI, R13 */
ctx->rdi = (uint64_t)(ctx->r13);
    /* 0x3662: MOV RSI, R15 */
ctx->rsi = (uint64_t)(ctx->r15);
    /* 0x3665: MOV RDX, R12 */
ctx->rdx = (uint64_t)(ctx->r12);
    /* 0x3668: CALL .-305 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x366dULL;
    ctx->rip = 0x353cULL;
    fn_0x353c(ctx);
    /* 0x366d: MOV R13, RAX */
ctx->r13 = (uint64_t)(ctx->rax);

loc_0x3670:
    ctx->rip = 0x3670ULL;
    /* 0x3670: CMP R13, R15 */
    { uint64_t a = (uint64_t)(ctx->r13); uint64_t b = (uint64_t)(ctx->r15); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x3673: JE .+11 */
    if (ctx->zf) goto loc_0x3680;

loc_0x3675:
    ctx->rip = 0x3675ULL;
    /* 0x3675: MOV RAX, R13 */
ctx->rax = (uint64_t)(ctx->r13);
    /* 0x3678: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x367a: DIV R14 */
    { unsigned __int128 dividend = ((unsigned __int128)ctx->rdx << 64) | ctx->rax;
      uint64_t divisor = (uint64_t)(ctx->r14);
      if (divisor != 0) {
        ctx->rax = (uint64_t)(dividend / divisor);
        ctx->rdx = (uint64_t)(dividend % divisor);
      }
    }
    /* 0x367d: MOV RBX, RAX */
ctx->rbx = (uint64_t)(ctx->rax);

loc_0x3680:
    ctx->rip = 0x3680ULL;
    /* 0x3680: MOV RAX, RBX */
ctx->rax = (uint64_t)(ctx->rbx);
    /* 0x3683: ADD RSP, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x3687: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3688: POP R12 */
    ctx->r12 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x368a: POP R13 */
    ctx->r13 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x368c: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x368e: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3690: POP RBP */
    ctx->rbp = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3691: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __signbitl at 0x3694
void fn_0x3694(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x3694:
    ctx->rip = 0x3694ULL;
    /* 0x3694: MOVZX EAX, [RSP+Reg(0)+0x10] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rsp + 0x10ULL)));
    /* 0x3699: SHR EAX, 0xf */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint8_t count = ((uint8_t)(0xfULL)) & 0x3f; uint32_t res;
      res = a >> count;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x369c: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __fpclassifyl at 0x36a0
void fn_0x36a0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x36a0ULL) {
        switch (ctx->rip) {
            case 0x36b3ULL: goto loc_0x36b3;
            case 0x36b7ULL: goto loc_0x36b7;
            case 0x36c4ULL: goto loc_0x36c4;
            case 0x36cbULL: goto loc_0x36cb;
            case 0x36d2ULL: goto loc_0x36d2;
            case 0x36d3ULL: goto loc_0x36d3;
            case 0x36ddULL: goto loc_0x36dd;
            default: goto loc_0x36a0;
        }
    }

loc_0x36a0:
    ctx->rip = 0x36a0ULL;
    /* 0x36a0: MOV EAX, [RSP+Reg(0)+0x10] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rsp + 0x10ULL));
    /* 0x36a4: MOV RCX, [RSP+Reg(0)+0x8] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0x8ULL));
    /* 0x36a9: AND EAX, 0x7fff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7fffULL); uint32_t res;
      res = a & b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x36ae: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x36b1: JS .+17 */
    if (ctx->sf) goto loc_0x36c4;

loc_0x36b3:
    ctx->rip = 0x36b3ULL;
    /* 0x36b3: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x36b5: JNE .+13 */
    if (!ctx->zf) goto loc_0x36c4;

loc_0x36b7:
    ctx->rip = 0x36b7ULL;
    /* 0x36b7: CMP RCX, 0x1 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0x1ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x36bb: MOV EAX, 0x3 */
ctx->rax = (uint64_t)(uint32_t)(0x3ULL);
    /* 0x36c0: SBB EAX, 0x0 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x0ULL) + (uint32_t)ctx->cf; uint32_t res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x36c3: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x36c4:
    ctx->rip = 0x36c4ULL;
    /* 0x36c4: CMP EAX, 0x7fff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7fffULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x36c9: JNE .+8 */
    if (!ctx->zf) goto loc_0x36d3;

loc_0x36cb:
    ctx->rip = 0x36cbULL;
    /* 0x36cb: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x36cd: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x36d0: JS .+11 */
    if (ctx->sf) goto loc_0x36dd;

loc_0x36d2:
    ctx->rip = 0x36d2ULL;
    /* 0x36d2: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x36d3:
    ctx->rip = 0x36d3ULL;
    /* 0x36d3: SHR RCX, 0x3d */
    { uint64_t a = (uint64_t)(ctx->rcx); uint8_t count = ((uint8_t)(0x3dULL)) & 0x3f; uint64_t res;
      res = a >> count;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x36d7: AND ECX, -0x4 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xfffffffffffffffcULL); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x36da: MOV EAX, ECX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x36dc: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x36dd:
    ctx->rip = 0x36ddULL;
    /* 0x36dd: SHL RCX, 0x1 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint8_t count = ((uint8_t)(0x1ULL)) & 0x3f; uint64_t res;
      res = a << count;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x36e0: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x36e2: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x36e5: SETE AL */
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x36e8: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function frexpl at 0x36ec
void fn_0x36ec(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x36ecULL) {
        switch (ctx->rip) {
            case 0x370eULL: goto loc_0x370e;
            case 0x3715ULL: goto loc_0x3715;
            case 0x372aULL: goto loc_0x372a;
            case 0x374fULL: goto loc_0x374f;
            case 0x3753ULL: goto loc_0x3753;
            case 0x377cULL: goto loc_0x377c;
            default: goto loc_0x36ec;
        }
    }

loc_0x36ec:
    ctx->rip = 0x36ecULL;
    /* 0x36ec: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x36ed: SUB RSP, 0x40 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x40ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x36f1: FLD [RSP+Reg(0)+0x50] */
    { float f; uint32_t u = MEM_U32(ctx->rsp + 0x50ULL); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x36f5: FLD F0 */
    fpu_push(ctx, FPU_ST(0));
    /* 0x36f7: FSTP [RSP+Reg(0)+0x20] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rsp + 0x20ULL) = u; }
    /* 0x36fb: FWAIT */
    /* fwait no-op */
    /* 0x36fc: MOVZX EAX, [RSP+Reg(0)+0x28] */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rsp + 0x28ULL)));
    /* 0x3701: MOV EDX, 0x7fff */
ctx->rdx = (uint64_t)(uint32_t)(0x7fffULL);
    /* 0x3706: MOV ECX, EAX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x3708: AND ECX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x370a: CMP ECX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x370c: JE .+110 */
    if (ctx->zf) goto loc_0x377c;

loc_0x370e:
    ctx->rip = 0x370eULL;
    /* 0x370e: MOV RBX, RDI */
ctx->rbx = (uint64_t)(ctx->rdi);
    /* 0x3711: TEST ECX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x3713: JNE .+62 */
    if (!ctx->zf) goto loc_0x3753;

loc_0x3715:
    ctx->rip = 0x3715ULL;
    /* 0x3715: FLDZ */
    fpu_push(ctx, 0.0);
    /* 0x3717: FXCH F1 */
    { double tmp = FPU_ST(0); FPU_ST(0) = FPU_ST(1); FPU_ST(1) = tmp; }
    /* 0x3719: FUCOMI F0, F1 */
    { double a = FPU_ST(0); double b = FPU_ST(1);
      if (isnan(a) || isnan(b)) { ctx->zf = 1; ctx->pf = 1; ctx->cf = 1; }
      else if (a > b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 0; }
      else if (a < b) { ctx->zf = 0; ctx->pf = 0; ctx->cf = 1; }
      else { ctx->zf = 1; ctx->pf = 0; ctx->cf = 0; }
      ctx->of = 0; ctx->sf = 0; ctx->af = 0; }
    /* 0x371b: FSTP F1 */
    FPU_ST(1) = fpu_pop(ctx);
    /* 0x371d: FWAIT */
    /* fwait no-op */
    /* 0x371e: SETNP CL */
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->pf ? 1 : 0)));
    /* 0x3721: SETE DL */
    ctx->rdx = (ctx->rdx & ~0xffULL) | ((uint64_t)(uint8_t)((ctx->zf ? 1 : 0)));
    /* 0x3724: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x3726: TEST DL, CL */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdx & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x3728: JNE .+37 */
    if (!ctx->zf) goto loc_0x374f;

loc_0x372a:
    ctx->rip = 0x372aULL;
    /* 0x372a: MOVSD_XMM X0, [RIP+0x26d6] */
    ctx->xmm[0].u64[0] = MEM_U64(0x5e08ULL);
    /* 0x3732: MOVSD_XMM [RSP+Reg(0)+0x38], X0 */
    MEM_U64(ctx->rsp + 0x38ULL) = ctx->xmm[0].u64[0];
    /* 0x3738: FLD [RSP+Reg(0)+0x38] */
    { double d; uint64_t u = MEM_U64(ctx->rsp + 0x38ULL); memcpy(&d, &u, 8); fpu_push(ctx, d); }
    /* 0x373c: FMULP F1, F0 */
    FPU_ST(1) *= FPU_ST(0);
    fpu_pop(ctx);
    /* 0x373e: FSTP [RSP+Reg(0)] */
    { float f = (float)fpu_pop(ctx); uint32_t u; memcpy(&u, &f, 4); MEM_U32(ctx->rsp) = u; }
    /* 0x3741: FWAIT */
    /* fwait no-op */
    /* 0x3742: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x3745: CALL .-94 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x374aULL;
    ctx->rip = 0x36ecULL;
    fn_0x36ec(ctx);
    /* 0x374a: MOV EAX, [RBX] */
ctx->rax = (uint64_t)(uint32_t)(MEM_U32(ctx->rbx));
    /* 0x374c: ADD EAX, -0x78 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xffffffffffffff88ULL); uint32_t res;
      res = a + b;
      set_flags_add_u32(ctx, a, b, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }

loc_0x374f:
    ctx->rip = 0x374fULL;
    /* 0x374f: MOV [RBX], EAX */
MEM_U32(ctx->rbx) = (uint32_t)(((uint32_t)ctx->rax));
    /* 0x3751: JMP .+41 */
    goto loc_0x377c;

loc_0x3753:
    ctx->rip = 0x3753ULL;
    /* 0x3753: FSTP F0 */
    FPU_ST(0) = fpu_pop(ctx);
    /* 0x3755: FWAIT */
    /* fwait no-op */
    /* 0x3756: MOV RDX, [RSP+Reg(0)+0x20] */
ctx->rdx = (uint64_t)(MEM_U64(ctx->rsp + 0x20ULL));
    /* 0x375b: ADD ECX, -0x3ffe */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xffffffffffffc002ULL); uint32_t res;
      res = a + b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x3761: MOV [RBX], ECX */
MEM_U32(ctx->rbx) = (uint32_t)(((uint32_t)ctx->rcx));
    /* 0x3763: AND EAX, 0x8000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x8000ULL); uint32_t res;
      res = a & b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x3768: OR EAX, 0x3ffe */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x3ffeULL); uint32_t res;
      res = a | b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x376d: MOV [RSP+Reg(0)+0x10], RDX */
MEM_U64(ctx->rsp + 0x10ULL) = (uint64_t)(ctx->rdx);
    /* 0x3772: MOV [RSP+Reg(0)+0x18], AX */
MEM_U16(ctx->rsp + 0x18ULL) = (uint16_t)(((uint16_t)ctx->rax));
    /* 0x3777: FLD [RSP+Reg(0)+0x10] */
    { float f; uint32_t u = MEM_U32(ctx->rsp + 0x10ULL); memcpy(&f, &u, 4); fpu_push(ctx, (double)f); }
    /* 0x377b: FWAIT */
    /* fwait no-op */

loc_0x377c:
    ctx->rip = 0x377cULL;
    /* 0x377c: ADD RSP, 0x40 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x40ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x3780: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3781: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function wctomb at 0x3784
void fn_0x3784(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x3784ULL) {
        switch (ctx->rip) {
            case 0x3789ULL: goto loc_0x3789;
            case 0x3799ULL: goto loc_0x3799;
            default: goto loc_0x3784;
        }
    }

loc_0x3784:
    ctx->rip = 0x3784ULL;
    /* 0x3784: TEST RDI, RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x3787: JE .+16 */
    if (ctx->zf) goto loc_0x3799;

loc_0x3789:
    ctx->rip = 0x3789ULL;
    /* 0x3789: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x378a: MOVZX ESI, SI */
    ctx->rsi = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(((uint16_t)ctx->rsi)));
    /* 0x378d: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x378f: CALL .+8 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3794ULL;
    ctx->rip = 0x379cULL;
    fn_0x379c(ctx);
    /* 0x3794: ADD RSP, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x3798: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x3799:
    ctx->rip = 0x3799ULL;
    /* 0x3799: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x379b: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function wcrtomb at 0x379c
void fn_0x379c(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x379cULL) {
        switch (ctx->rip) {
            case 0x37b0ULL: goto loc_0x37b0;
            case 0x37bbULL: goto loc_0x37bb;
            case 0x37c3ULL: goto loc_0x37c3;
            case 0x37dbULL: goto loc_0x37db;
            case 0x37e5ULL: goto loc_0x37e5;
            case 0x37eeULL: goto loc_0x37ee;
            case 0x37f5ULL: goto loc_0x37f5;
            case 0x3811ULL: goto loc_0x3811;
            case 0x381bULL: goto loc_0x381b;
            case 0x381dULL: goto loc_0x381d;
            case 0x3824ULL: goto loc_0x3824;
            case 0x3834ULL: goto loc_0x3834;
            case 0x3848ULL: goto loc_0x3848;
            case 0x386eULL: goto loc_0x386e;
            default: goto loc_0x379c;
        }
    }

loc_0x379c:
    ctx->rip = 0x379cULL;
    /* 0x379c: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x379e: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x37a0: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x37a1: MOV R15L, 0x1 */
ctx->r15 = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x37a7: TEST RDI, RDI */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(ctx->rdi); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x37aa: JE .+190 */
    if (ctx->zf) goto loc_0x386e;

loc_0x37b0:
    ctx->rip = 0x37b0ULL;
    /* 0x37b0: MOV EBX, ESI */
ctx->rbx = (uint64_t)(uint32_t)(((uint32_t)ctx->rsi));
    /* 0x37b2: MOV R14, RDI */
ctx->r14 = (uint64_t)(ctx->rdi);
    /* 0x37b5: CMP BX, 0x7f */
    { uint16_t a = (uint16_t)(((uint16_t)ctx->rbx)); uint16_t b = (uint16_t)(0x7fULL); uint16_t res;
      res = a - b;
      set_flags_sub_u16(ctx, a, b, res);
    }
    /* 0x37b9: JA .+8 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x37c3;

loc_0x37bb:
    ctx->rip = 0x37bbULL;
    /* 0x37bb: MOV [R14], BL */
MEM_U8(ctx->r14) = (uint8_t)(((uint8_t)(ctx->rbx & 0xff)));
    /* 0x37be: JMP .+171 */
    goto loc_0x386e;

loc_0x37c3:
    ctx->rip = 0x37c3ULL;
    /* 0x37c3: MOV RAX, [Reg(0)] */
ctx->rax = (uint64_t)(MEM_U64(ctx->fs_base));
    /* 0x37cc: LEA RAX, [RAX+0xfffffff8] */
    ctx->rax = (uint64_t)(ctx->rax + (uint64_t)(-0x8LL));
    /* 0x37d3: MOV RCX, [RAX] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rax));
    /* 0x37d6: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x37d9: JNE .+10 */
    if (!ctx->zf) goto loc_0x37e5;

loc_0x37db:
    ctx->rip = 0x37dbULL;
    /* 0x37db: LEA RCX, [RIP+0x109c6] */
    ctx->rcx = (uint64_t)(0x141a8ULL);
    /* 0x37e2: MOV [RAX], RCX */
MEM_U64(ctx->rax) = (uint64_t)(ctx->rcx);

loc_0x37e5:
    ctx->rip = 0x37e5ULL;
    /* 0x37e5: MOVZX EAX, BX */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(((uint16_t)ctx->rbx)));
    /* 0x37e8: CMP [RCX], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rcx)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x37ec: JE .+35 */
    if (ctx->zf) goto loc_0x3811;

loc_0x37ee:
    ctx->rip = 0x37eeULL;
    /* 0x37ee: CMP EAX, 0x7ff */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0x7ffULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x37f3: JA .+40 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x381d;

loc_0x37f5:
    ctx->rip = 0x37f5ULL;
    /* 0x37f5: MOV EAX, EBX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    /* 0x37f7: SHR EAX, 0x6 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint8_t count = ((uint8_t)(0x6ULL)) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x37fa: OR AL, 0xc0 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0xc0ULL); uint8_t res;
      res = a | b;
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x37fc: MOV [R14], AL */
MEM_U8(ctx->r14) = (uint8_t)(((uint8_t)(ctx->rax & 0xff)));
    /* 0x37ff: AND BL, 0x3f */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rbx & 0xff))); uint8_t b = (uint8_t)(0x3fULL); uint8_t res;
      res = a & b;
    ctx->rbx = (ctx->rbx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x3802: OR BL, 0x80 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rbx & 0xff))); uint8_t b = (uint8_t)(0x80ULL); uint8_t res;
      res = a | b;
      set_flags_logic_u8(ctx, res);
    ctx->rbx = (ctx->rbx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x3805: MOV [R14+0x1], BL */
MEM_U8(ctx->r14 + 0x1ULL) = (uint8_t)(((uint8_t)(ctx->rbx & 0xff)));
    /* 0x3809: MOV R15L, 0x2 */
ctx->r15 = (uint64_t)(uint32_t)(0x2ULL);
    /* 0x380f: JMP .+93 */
    goto loc_0x386e;

loc_0x3811:
    ctx->rip = 0x3811ULL;
    /* 0x3811: AND EAX, -0x80 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xffffffffffffff80ULL); uint32_t res;
      res = a & b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x3814: CMP EAX, 0xdf80 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xdf80ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3819: JE .-96 */
    if (ctx->zf) goto loc_0x37bb;

loc_0x381b:
    ctx->rip = 0x381bULL;
    /* 0x381b: JMP .+23 */
    goto loc_0x3834;

loc_0x381d:
    ctx->rip = 0x381dULL;
    /* 0x381d: CMP EAX, 0xd800 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(0xd800ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3822: JB .+36 */
    if (ctx->cf) goto loc_0x3848;

loc_0x3824:
    ctx->rip = 0x3824ULL;
    /* 0x3824: MOV ECX, EAX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x3826: AND ECX, 0xe000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xe000ULL); uint32_t res;
      res = a & b;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x382c: CMP ECX, 0xe000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t b = (uint32_t)(0xe000ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x3832: JE .+20 */
    if (ctx->zf) goto loc_0x3848;

loc_0x3834:
    ctx->rip = 0x3834ULL;
    /* 0x3834: CALL .-12369 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x3839ULL;
    ctx->rip = 0x7e8ULL;
    fn_0x7e8(ctx);
    /* 0x3839: MOV [RAX], 0x56 */
MEM_U32(ctx->rax) = (uint32_t)(0x56ULL);
    /* 0x383f: MOV R15, -0x1 */
ctx->r15 = (uint64_t)(0xffffffffffffffffULL);
    /* 0x3846: JMP .+38 */
    goto loc_0x386e;

loc_0x3848:
    ctx->rip = 0x3848ULL;
    /* 0x3848: MOV ECX, EAX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x384a: SHR ECX, 0xc */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rcx)); uint8_t count = ((uint8_t)(0xcULL)) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->rcx = (uint64_t)(uint32_t)(res);
    }
    /* 0x384d: OR CL, 0xe0 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rcx & 0xff))); uint8_t b = (uint8_t)(0xe0ULL); uint8_t res;
      res = a | b;
    ctx->rcx = (ctx->rcx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x3850: MOV [R14], CL */
MEM_U8(ctx->r14) = (uint8_t)(((uint8_t)(ctx->rcx & 0xff)));
    /* 0x3853: SHR EAX, 0x6 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint8_t count = ((uint8_t)(0x6ULL)) & 0x3f; uint32_t res;
      res = a >> count;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x3856: AND AL, 0x3f */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x3fULL); uint8_t res;
      res = a & b;
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x3858: OR AL, 0x80 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x80ULL); uint8_t res;
      res = a | b;
    ctx->rax = (ctx->rax & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x385a: MOV [R14+0x1], AL */
MEM_U8(ctx->r14 + 0x1ULL) = (uint8_t)(((uint8_t)(ctx->rax & 0xff)));
    /* 0x385e: AND BL, 0x3f */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rbx & 0xff))); uint8_t b = (uint8_t)(0x3fULL); uint8_t res;
      res = a & b;
    ctx->rbx = (ctx->rbx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x3861: OR BL, 0x80 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rbx & 0xff))); uint8_t b = (uint8_t)(0x80ULL); uint8_t res;
      res = a | b;
      set_flags_logic_u8(ctx, res);
    ctx->rbx = (ctx->rbx & ~0xffULL) | ((uint64_t)(uint8_t)(res));
    }
    /* 0x3864: MOV [R14+0x2], BL */
MEM_U8(ctx->r14 + 0x2ULL) = (uint8_t)(((uint8_t)(ctx->rbx & 0xff)));
    /* 0x3868: MOV R15L, 0x3 */
ctx->r15 = (uint64_t)(uint32_t)(0x3ULL);

loc_0x386e:
    ctx->rip = 0x386eULL;
    /* 0x386e: MOV RAX, R15 */
ctx->rax = (uint64_t)(ctx->r15);
    /* 0x3871: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3872: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3874: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x3876: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __strerror_l at 0x3878
void fn_0x3878(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x3878:
    ctx->rip = 0x3878ULL;
    /* 0x3878: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x387a: CMP EDI, 0x61 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdi)); uint32_t b = (uint32_t)(0x61ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x387d: CMOVB EAX, EDI */
    if (ctx->cf) {
    ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));
    }
    /* 0x3880: LEA RCX, [RIP+0x2ee9] */
    ctx->rcx = (uint64_t)(0x6770ULL);
    /* 0x3887: MOVZX ECX, [RCX+2*RAX] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rcx + (ctx->rax * 2ULL))));
    /* 0x388b: LEA RAX, [RIP+0x285e] */
    ctx->rax = (uint64_t)(0x60f0ULL);
    /* 0x3892: ADD RAX, RCX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x3895: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function strerror at 0x3896
void fn_0x3896(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x3896ULL) {
        switch (ctx->rip) {
            case 0x38afULL: goto loc_0x38af;
            case 0x38b9ULL: goto loc_0x38b9;
            default: goto loc_0x3896;
        }
    }

loc_0x3896:
    ctx->rip = 0x3896ULL;
    /* 0x3896: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x3897: MOV EBX, EDI */
ctx->rbx = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));
    /* 0x3899: MOV RAX, [Reg(0)] */
ctx->rax = (uint64_t)(MEM_U64(ctx->fs_base));
    /* 0x38a2: LEA RAX, [RAX+0xfffffff8] */
    ctx->rax = (uint64_t)(ctx->rax + (uint64_t)(-0x8LL));
    /* 0x38a9: CMP [RAX], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rax)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x38ad: JNE .+10 */
    if (!ctx->zf) goto loc_0x38b9;

loc_0x38af:
    ctx->rip = 0x38afULL;
    /* 0x38af: LEA RCX, [RIP+0x108f2] */
    ctx->rcx = (uint64_t)(0x141a8ULL);
    /* 0x38b6: MOV [RAX], RCX */
MEM_U64(ctx->rax) = (uint64_t)(ctx->rcx);

loc_0x38b9:
    ctx->rip = 0x38b9ULL;
    /* 0x38b9: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x38bb: CMP EBX, 0x61 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rbx)); uint32_t b = (uint32_t)(0x61ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x38be: CMOVB EAX, EBX */
    if (ctx->cf) {
    ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    }
    /* 0x38c1: LEA RCX, [RIP+0x2ea8] */
    ctx->rcx = (uint64_t)(0x6770ULL);
    /* 0x38c8: MOVZX ECX, [RCX+2*RAX] */
    ctx->rcx = (uint64_t)(uint32_t)((uint64_t)(uint16_t)(MEM_U16(ctx->rcx + (ctx->rax * 2ULL))));
    /* 0x38cc: LEA RAX, [RIP+0x281d] */
    ctx->rax = (uint64_t)(0x60f0ULL);
    /* 0x38d3: ADD RAX, RCX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x38d6: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x38d7: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function memcpy at 0x38e0 (Forwarded directly to host shim shim_memcpy)
void fn_0x38e0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_memcpy(ctx);
    return;
}

// Function memmove at 0x3c10 (Forwarded directly to host shim shim_memmove)
void fn_0x3c10(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_memmove(ctx);
    return;
}

// Function memset at 0x42e0 (Forwarded directly to host shim shim_memset)
void fn_0x42e0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_memset(ctx);
    return;
}

// Function strcmp at 0x43c0 (Forwarded directly to host shim shim_strcmp)
void fn_0x43c0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_strcmp(ctx);
    return;
}

// Function strlen at 0x4400 (Forwarded directly to host shim shim_strlen)
void fn_0x4400(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_strlen(ctx);
    return;
}

// Function strnlen at 0x4500
void fn_0x4500(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x4500:
    ctx->rip = 0x4500ULL;
    /* 0x4500: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x4502: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x4503: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x4504: MOV RBX, RSI */
ctx->rbx = (uint64_t)(ctx->rsi);
    /* 0x4507: MOV R14, RDI */
ctx->r14 = (uint64_t)(ctx->rdi);
    /* 0x450a: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x450c: MOV RDX, RBX */
ctx->rdx = (uint64_t)(ctx->rbx);
    /* 0x450f: CALL .+28 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4514ULL;
    ctx->rip = 0x4530ULL;
    fn_0x4530(ctx);
    /* 0x4514: MOV RCX, RAX */
ctx->rcx = (uint64_t)(ctx->rax);
    /* 0x4517: SUB RCX, R14 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->r14); uint64_t res;
      res = a - b;
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x451a: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x451d: CMOVE RCX, RBX */
    if (ctx->zf) {
    ctx->rcx = (uint64_t)(ctx->rbx);
    }
    /* 0x4521: MOV RAX, RCX */
ctx->rax = (uint64_t)(ctx->rcx);
    /* 0x4524: ADD RSP, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x4528: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x4529: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x452b: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function memchr at 0x4530
void fn_0x4530(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x4530ULL) {
        switch (ctx->rip) {
            case 0x4541ULL: goto loc_0x4541;
            case 0x454aULL: goto loc_0x454a;
            case 0x454fULL: goto loc_0x454f;
            case 0x455aULL: goto loc_0x455a;
            case 0x4570ULL: goto loc_0x4570;
            case 0x4579ULL: goto loc_0x4579;
            case 0x4582ULL: goto loc_0x4582;
            case 0x4599ULL: goto loc_0x4599;
            case 0x45a2ULL: goto loc_0x45a2;
            case 0x45abULL: goto loc_0x45ab;
            case 0x45c2ULL: goto loc_0x45c2;
            case 0x45cbULL: goto loc_0x45cb;
            case 0x45d4ULL: goto loc_0x45d4;
            case 0x45e7ULL: goto loc_0x45e7;
            case 0x45ecULL: goto loc_0x45ec;
            case 0x45f5ULL: goto loc_0x45f5;
            case 0x4608ULL: goto loc_0x4608;
            case 0x460dULL: goto loc_0x460d;
            case 0x4616ULL: goto loc_0x4616;
            case 0x4629ULL: goto loc_0x4629;
            case 0x462eULL: goto loc_0x462e;
            case 0x4637ULL: goto loc_0x4637;
            case 0x464aULL: goto loc_0x464a;
            case 0x464fULL: goto loc_0x464f;
            case 0x4658ULL: goto loc_0x4658;
            case 0x4666ULL: goto loc_0x4666;
            case 0x466cULL: goto loc_0x466c;
            case 0x4675ULL: goto loc_0x4675;
            case 0x467aULL: goto loc_0x467a;
            case 0x4680ULL: goto loc_0x4680;
            case 0x46b0ULL: goto loc_0x46b0;
            case 0x46cbULL: goto loc_0x46cb;
            case 0x46d9ULL: goto loc_0x46d9;
            case 0x46e4ULL: goto loc_0x46e4;
            case 0x46f0ULL: goto loc_0x46f0;
            case 0x46f5ULL: goto loc_0x46f5;
            case 0x46fdULL: goto loc_0x46fd;
            case 0x46ffULL: goto loc_0x46ff;
            default: goto loc_0x4530;
        }
    }

loc_0x4530:
    ctx->rip = 0x4530ULL;
    /* 0x4530: TEST RDX, RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x4533: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x4537: TEST DIB, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rdi & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x453b: JE .+299 */
    if (ctx->zf) goto loc_0x466c;

loc_0x4541:
    ctx->rip = 0x4541ULL;
    /* 0x4541: TEST RDX, RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x4544: JE .+290 */
    if (ctx->zf) goto loc_0x466c;

loc_0x454a:
    ctx->rip = 0x454aULL;
    /* 0x454a: CMP [RDI], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rdi)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x454d: JNE .+11 */
    if (!ctx->zf) goto loc_0x455a;

loc_0x454f:
    ctx->rip = 0x454fULL;
    /* 0x454f: MOV RAX, RDI */
ctx->rax = (uint64_t)(ctx->rdi);
    /* 0x4552: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x4555: JMP .+406 */
    goto loc_0x46f0;

loc_0x455a:
    ctx->rip = 0x455aULL;
    /* 0x455a: LEA RAX, [RDI+0x1] */
    ctx->rax = (uint64_t)(ctx->rdi + 0x1ULL);
    /* 0x455e: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x4561: DEC RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x4564: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x4568: TEST AL, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x456a: JE .+246 */
    if (ctx->zf) goto loc_0x4666;

loc_0x4570:
    ctx->rip = 0x4570ULL;
    /* 0x4570: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x4573: JE .+237 */
    if (ctx->zf) goto loc_0x4666;

loc_0x4579:
    ctx->rip = 0x4579ULL;
    /* 0x4579: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x457c: JE .+366 */
    if (ctx->zf) goto loc_0x46f0;

loc_0x4582:
    ctx->rip = 0x4582ULL;
    /* 0x4582: LEA RAX, [RDI+0x2] */
    ctx->rax = (uint64_t)(ctx->rdi + 0x2ULL);
    /* 0x4586: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x4589: ADD RCX, -0x2 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xfffffffffffffffeULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x458d: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x4591: TEST AL, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x4593: JE .+205 */
    if (ctx->zf) goto loc_0x4666;

loc_0x4599:
    ctx->rip = 0x4599ULL;
    /* 0x4599: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x459c: JE .+196 */
    if (ctx->zf) goto loc_0x4666;

loc_0x45a2:
    ctx->rip = 0x45a2ULL;
    /* 0x45a2: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x45a5: JE .+325 */
    if (ctx->zf) goto loc_0x46f0;

loc_0x45ab:
    ctx->rip = 0x45abULL;
    /* 0x45ab: LEA RAX, [RDI+0x3] */
    ctx->rax = (uint64_t)(ctx->rdi + 0x3ULL);
    /* 0x45af: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x45b2: ADD RCX, -0x3 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xfffffffffffffffdULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x45b6: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x45ba: TEST AL, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x45bc: JE .+164 */
    if (ctx->zf) goto loc_0x4666;

loc_0x45c2:
    ctx->rip = 0x45c2ULL;
    /* 0x45c2: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x45c5: JE .+155 */
    if (ctx->zf) goto loc_0x4666;

loc_0x45cb:
    ctx->rip = 0x45cbULL;
    /* 0x45cb: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x45ce: JE .+284 */
    if (ctx->zf) goto loc_0x46f0;

loc_0x45d4:
    ctx->rip = 0x45d4ULL;
    /* 0x45d4: LEA RAX, [RDI+0x4] */
    ctx->rax = (uint64_t)(ctx->rdi + 0x4ULL);
    /* 0x45d8: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x45db: ADD RCX, -0x4 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xfffffffffffffffcULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x45df: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x45e3: TEST AL, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x45e5: JE .+127 */
    if (ctx->zf) goto loc_0x4666;

loc_0x45e7:
    ctx->rip = 0x45e7ULL;
    /* 0x45e7: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x45ea: JE .+122 */
    if (ctx->zf) goto loc_0x4666;

loc_0x45ec:
    ctx->rip = 0x45ecULL;
    /* 0x45ec: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x45ef: JE .+251 */
    if (ctx->zf) goto loc_0x46f0;

loc_0x45f5:
    ctx->rip = 0x45f5ULL;
    /* 0x45f5: LEA RAX, [RDI+0x5] */
    ctx->rax = (uint64_t)(ctx->rdi + 0x5ULL);
    /* 0x45f9: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x45fc: ADD RCX, -0x5 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xfffffffffffffffbULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x4600: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x4604: TEST AL, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x4606: JE .+94 */
    if (ctx->zf) goto loc_0x4666;

loc_0x4608:
    ctx->rip = 0x4608ULL;
    /* 0x4608: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x460b: JE .+89 */
    if (ctx->zf) goto loc_0x4666;

loc_0x460d:
    ctx->rip = 0x460dULL;
    /* 0x460d: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x4610: JE .+218 */
    if (ctx->zf) goto loc_0x46f0;

loc_0x4616:
    ctx->rip = 0x4616ULL;
    /* 0x4616: LEA RAX, [RDI+0x6] */
    ctx->rax = (uint64_t)(ctx->rdi + 0x6ULL);
    /* 0x461a: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x461d: ADD RCX, -0x6 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xfffffffffffffffaULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x4621: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x4625: TEST AL, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x4627: JE .+61 */
    if (ctx->zf) goto loc_0x4666;

loc_0x4629:
    ctx->rip = 0x4629ULL;
    /* 0x4629: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x462c: JE .+56 */
    if (ctx->zf) goto loc_0x4666;

loc_0x462e:
    ctx->rip = 0x462eULL;
    /* 0x462e: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x4631: JE .+185 */
    if (ctx->zf) goto loc_0x46f0;

loc_0x4637:
    ctx->rip = 0x4637ULL;
    /* 0x4637: LEA RAX, [RDI+0x7] */
    ctx->rax = (uint64_t)(ctx->rdi + 0x7ULL);
    /* 0x463b: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x463e: ADD RCX, -0x7 */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(0xfffffffffffffff9ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x4642: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x4646: TEST AL, 0x7 */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->rax & 0xff))); uint8_t b = (uint8_t)(0x7ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x4648: JE .+28 */
    if (ctx->zf) goto loc_0x4666;

loc_0x464a:
    ctx->rip = 0x464aULL;
    /* 0x464a: TEST RCX, RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x464d: JE .+23 */
    if (ctx->zf) goto loc_0x4666;

loc_0x464f:
    ctx->rip = 0x464fULL;
    /* 0x464f: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x4652: JE .+152 */
    if (ctx->zf) goto loc_0x46f0;

loc_0x4658:
    ctx->rip = 0x4658ULL;
    /* 0x4658: ADD RDI, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x465c: ADD RDX, -0x8 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0xfffffffffffffff8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x4660: SETNE R8B */
    ctx->r8 = (ctx->r8 & ~0xffULL) | ((uint64_t)(uint8_t)((!ctx->zf ? 1 : 0)));
    /* 0x4664: JMP .+6 */
    goto loc_0x466c;

loc_0x4666:
    ctx->rip = 0x4666ULL;
    /* 0x4666: MOV RDX, RCX */
ctx->rdx = (uint64_t)(ctx->rcx);
    /* 0x4669: MOV RDI, RAX */
ctx->rdi = (uint64_t)(ctx->rax);

loc_0x466c:
    ctx->rip = 0x466cULL;
    /* 0x466c: TEST R8B, R8B */
    { uint8_t a = (uint8_t)(((uint8_t)(ctx->r8 & 0xff))); uint8_t b = (uint8_t)(((uint8_t)(ctx->r8 & 0xff))); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x466f: JE .+136 */
    if (ctx->zf) goto loc_0x46fd;

loc_0x4675:
    ctx->rip = 0x4675ULL;
    /* 0x4675: CMP [RDI], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rdi)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x4678: JE .+95 */
    if (ctx->zf) goto loc_0x46d9;

loc_0x467a:
    ctx->rip = 0x467aULL;
    /* 0x467a: CMP RDX, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x467e: JB .+89 */
    if (ctx->cf) goto loc_0x46d9;

loc_0x4680:
    ctx->rip = 0x4680ULL;
    /* 0x4680: MOVZX EAX, SIB */
    ctx->rax = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(((uint8_t)(ctx->rsi & 0xff))));
    /* 0x4684: MOV ECX, EAX */
ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x4686: MOV RAX, 0x101010101010101 */
ctx->rax = (uint64_t)(0x101010101010101ULL);
    /* 0x4690: IMUL RAX, RCX */
    { int64_t a = (int64_t)(ctx->rax); int64_t b = (int64_t)(ctx->rcx); int64_t res = a * b;
    ctx->rax = (uint64_t)(res);
    }
    /* 0x4694: MOV RCX, -0x7f7f7f7f7f7f7f80 */
ctx->rcx = (uint64_t)(0x8080808080808080ULL);
    /* 0x469e: MOV R8, -0x101010101010101 */
ctx->r8 = (uint64_t)(0xfefefefefefefeffULL);
    /* 0x46a8: NOP [RAX+RAX] */

loc_0x46b0:
    ctx->rip = 0x46b0ULL;
    /* 0x46b0: MOV R9, [RDI] */
ctx->r9 = (uint64_t)(MEM_U64(ctx->rdi));
    /* 0x46b3: XOR R9, RAX */
    { uint64_t a = (uint64_t)(ctx->r9); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a ^ b;
    ctx->r9 = (uint64_t)(res);
    }
    /* 0x46b6: MOV R10, R9 */
ctx->r10 = (uint64_t)(ctx->r9);
    /* 0x46b9: NOT R10 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t res;
      res = ~a;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x46bc: AND R10, RCX */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->rcx); uint64_t res;
      res = a & b;
    ctx->r10 = (uint64_t)(res);
    }
    /* 0x46bf: ADD R9, R8 */
    { uint64_t a = (uint64_t)(ctx->r9); uint64_t b = (uint64_t)(ctx->r8); uint64_t res;
      res = a + b;
    ctx->r9 = (uint64_t)(res);
    }
    /* 0x46c2: TEST R10, R9 */
    { uint64_t a = (uint64_t)(ctx->r10); uint64_t b = (uint64_t)(ctx->r9); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x46c5: JNE .-380 */
    if (!ctx->zf) goto loc_0x454f;

loc_0x46cb:
    ctx->rip = 0x46cbULL;
    /* 0x46cb: ADD RDI, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x46cf: ADD RDX, -0x8 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0xfffffffffffffff8ULL); uint64_t res;
      res = a + b;
    ctx->rdx = (uint64_t)(res);
    }
    /* 0x46d3: CMP RDX, 0x7 */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(0x7ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x46d7: JA .-41 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x46b0;

loc_0x46d9:
    ctx->rip = 0x46d9ULL;
    /* 0x46d9: MOV RAX, RDI */
ctx->rax = (uint64_t)(ctx->rdi);
    /* 0x46dc: MOV RCX, RDX */
ctx->rcx = (uint64_t)(ctx->rdx);
    /* 0x46df: TEST RDX, RDX */
    { uint64_t a = (uint64_t)(ctx->rdx); uint64_t b = (uint64_t)(ctx->rdx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x46e2: JE .+25 */
    if (ctx->zf) goto loc_0x46fd;

loc_0x46e4:
    ctx->rip = 0x46e4ULL;
    /* 0x46e4: DATA16 DATA16 CS NOP [RAX+RAX] */

loc_0x46f0:
    ctx->rip = 0x46f0ULL;
    /* 0x46f0: CMP [RAX], SIB */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rax)); uint8_t b = (uint8_t)(((uint8_t)(ctx->rsi & 0xff))); uint8_t res;
      res = a - b;
      set_flags_sub_u8(ctx, a, b, res);
    }
    /* 0x46f3: JE .+10 */
    if (ctx->zf) goto loc_0x46ff;

loc_0x46f5:
    ctx->rip = 0x46f5ULL;
    /* 0x46f5: INC RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x46f8: DEC RCX */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t res;
      res = a - 1;
      set_flags_dec_u64(ctx, a, res);
    ctx->rcx = (uint64_t)(res);
    }
    /* 0x46fb: JNE .-13 */
    if (!ctx->zf) goto loc_0x46f0;

loc_0x46fd:
    ctx->rip = 0x46fdULL;
    /* 0x46fd: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }

loc_0x46ff:
    ctx->rip = 0x46ffULL;
    /* 0x46ff: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __lock at 0x4700
void fn_0x4700(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x4700ULL) {
        switch (ctx->rip) {
            case 0x4709ULL: goto loc_0x4709;
            case 0x4719ULL: goto loc_0x4719;
            case 0x471eULL: goto loc_0x471e;
            case 0x473aULL: goto loc_0x473a;
            case 0x473eULL: goto loc_0x473e;
            case 0x474aULL: goto loc_0x474a;
            case 0x4768ULL: goto loc_0x4768;
            default: goto loc_0x4700;
        }
    }

loc_0x4700:
    ctx->rip = 0x4700ULL;
    /* 0x4700: CMP [RIP+0xfa6d], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(0x14174ULL)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x4707: JE .+95 */
    if (ctx->zf) goto loc_0x4768;

loc_0x4709:
    ctx->rip = 0x4709ULL;
    /* 0x4709: MOV ECX, -0x7fffffff */
ctx->rcx = (uint64_t)(uint32_t)(0xffffffff80000001ULL);
    /* 0x470e: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x4710: LOCK CMPXCHG [RDI], ECX */
    { uint32_t src = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t acc = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t prev = __sync_val_compare_and_swap((uint32_t *)(ctx->mem_base + (ctx->rdi)), acc, src);
      set_flags_sub_u32(ctx, acc, prev, acc - prev);
      if (acc != prev) {
      ctx->rax = (uint64_t)(uint32_t)(prev);
      }
    }
    /* 0x4714: FWAIT */
    /* fwait no-op */
    /* 0x4715: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x4717: JE .+79 */
    if (ctx->zf) goto loc_0x4768;

loc_0x4719:
    ctx->rip = 0x4719ULL;
    /* 0x4719: MOV EDX, 0xa */
ctx->rdx = (uint64_t)(uint32_t)(0xaULL);

loc_0x471e:
    ctx->rip = 0x471eULL;
    /* 0x471e: LEA ECX, [RAX+0x7fffffff] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rax + 0x7fffffffULL);
    /* 0x4724: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x4726: CMOVNS ECX, EAX */
    if (!ctx->sf) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x4729: LEA ESI, [RCX+0x80000001] */
    ctx->rsi = (uint64_t)(uint32_t)(ctx->rcx + (uint64_t)(-0x7fffffffLL));
    /* 0x472f: MOV EAX, ECX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x4731: LOCK CMPXCHG [RDI], ESI */
    { uint32_t src = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t acc = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t prev = __sync_val_compare_and_swap((uint32_t *)(ctx->mem_base + (ctx->rdi)), acc, src);
      set_flags_sub_u32(ctx, acc, prev, acc - prev);
      if (acc != prev) {
      ctx->rax = (uint64_t)(uint32_t)(prev);
      }
    }
    /* 0x4735: FWAIT */
    /* fwait no-op */
    /* 0x4736: CMP EAX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x4738: JE .+46 */
    if (ctx->zf) goto loc_0x4768;

loc_0x473a:
    ctx->rip = 0x473aULL;
    /* 0x473a: DEC EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a - 1;
      set_flags_dec_u32(ctx, a, res);
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x473c: JNE .-32 */
    if (!ctx->zf) goto loc_0x471e;

loc_0x473e:
    ctx->rip = 0x473eULL;
    /* 0x473e: MOV EAX, 0x1 */
ctx->rax = (uint64_t)(uint32_t)(0x1ULL);
    /* 0x4743: LOCK XADD [RDI], EAX */
    { uint32_t orig_src = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t orig_dst = __sync_fetch_and_add((uint32_t *)(ctx->mem_base + (ctx->rdi)), orig_src);
      uint32_t res = orig_dst + orig_src;
      set_flags_add_u32(ctx, orig_dst, orig_src, res);
    ctx->rax = (uint64_t)(uint32_t)(orig_dst);
    }
    /* 0x4747: FWAIT */
    /* fwait no-op */
    /* 0x4748: INC EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a + 1;
      set_flags_inc_u32(ctx, a, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }

loc_0x474a:
    ctx->rip = 0x474aULL;
    /* 0x474a: LEA ECX, [RAX+0x7fffffff] */
    ctx->rcx = (uint64_t)(uint32_t)(ctx->rax + 0x7fffffffULL);
    /* 0x4750: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x4752: CMOVNS ECX, EAX */
    if (!ctx->sf) {
    ctx->rcx = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    }
    /* 0x4755: MOV EDX, ECX */
ctx->rdx = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x4757: OR EDX, -0x80000000 */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0xffffffff80000000ULL); uint32_t res;
      res = a | b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x475d: MOV EAX, ECX */
ctx->rax = (uint64_t)(uint32_t)(((uint32_t)ctx->rcx));
    /* 0x475f: LOCK CMPXCHG [RDI], EDX */
    { uint32_t src = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t acc = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t prev = __sync_val_compare_and_swap((uint32_t *)(ctx->mem_base + (ctx->rdi)), acc, src);
      set_flags_sub_u32(ctx, acc, prev, acc - prev);
      if (acc != prev) {
      ctx->rax = (uint64_t)(uint32_t)(prev);
      }
    }
    /* 0x4763: FWAIT */
    /* fwait no-op */
    /* 0x4764: CMP EAX, ECX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rcx)); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x4766: JNE .-30 */
    if (!ctx->zf) goto loc_0x474a;

loc_0x4768:
    ctx->rip = 0x4768ULL;
    /* 0x4768: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __unlock at 0x4769
void fn_0x4769(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x4769ULL) {
        switch (ctx->rip) {
            case 0x476eULL: goto loc_0x476e;
            case 0x476fULL: goto loc_0x476f;
            default: goto loc_0x4769;
        }
    }

loc_0x4769:
    ctx->rip = 0x4769ULL;
    /* 0x4769: CMP [RDI], 0x0 */
    { uint32_t a = (uint32_t)(MEM_U32(ctx->rdi)); uint32_t b = (uint32_t)(0x0ULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x476c: JS .+1 */
    if (ctx->sf) goto loc_0x476f;

loc_0x476e:
    ctx->rip = 0x476eULL;
    /* 0x476e: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;

loc_0x476f:
    ctx->rip = 0x476fULL;
    /* 0x476f: MOV EAX, 0x7fffffff */
ctx->rax = (uint64_t)(uint32_t)(0x7fffffffULL);
    /* 0x4774: LOCK XADD [RDI], EAX */
    { uint32_t orig_src = (uint32_t)(((uint32_t)ctx->rax));
      uint32_t orig_dst = __sync_fetch_and_add((uint32_t *)(ctx->mem_base + (ctx->rdi)), orig_src);
      uint32_t res = orig_dst + orig_src;
      set_flags_add_u32(ctx, orig_dst, orig_src, res);
    ctx->rax = (uint64_t)(uint32_t)(orig_dst);
    }
    /* 0x4778: FWAIT */
    /* fwait no-op */
    /* 0x4779: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __tls_get_addr at 0x477c (Forwarded directly to host shim shim___tls_get_addr)
void fn_0x477c(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim___tls_get_addr(ctx);
    return;
}

// Function __lseek at 0x4798
void fn_0x4798(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x4798:
    ctx->rip = 0x4798ULL;
    /* 0x4798: JMP .+1171 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x4c30ULL;
    fn_0x4c30(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function _start at 0x47a0
void fn_0x47a0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x47a0:
    ctx->rip = 0x47a0ULL;
    /* 0x47a0: JMP .+3 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x47a8ULL;
    fn_0x47a8(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function _start_ps4_c at 0x47a8
void fn_0x47a8(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x47a8:
    ctx->rip = 0x47a8ULL;
    /* 0x47a8: MOV ESI, [RDI] */
ctx->rsi = (uint64_t)(uint32_t)(MEM_U32(ctx->rdi));
    /* 0x47aa: LEA RDX, [RDI+0x8] */
    ctx->rdx = (uint64_t)(ctx->rdi + 0x8ULL);
    /* 0x47ae: LEA RDI, [RIP+0xffffb86b] */
    ctx->rdi = (uint64_t)(0x20ULL);
    /* 0x47b5: LEA RCX, [RIP+0x10] */
    ctx->rcx = (uint64_t)(0x47ccULL);
    /* 0x47bc: LEA R8, [RIP+0x9] */
    ctx->r8 = (uint64_t)(0x47ccULL);
    /* 0x47c3: XOR R9L, R9L */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->r9)); uint32_t b = (uint32_t)(((uint32_t)ctx->r9)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->r9 = (uint64_t)(uint32_t)(res);
    }
    /* 0x47c6: JMP .+446 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x4989ULL;
    fn_0x4989(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function dummy at 0x47cc
void fn_0x47cc(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x47cc:
    ctx->rip = 0x47ccULL;
    /* 0x47cc: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x47ce: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function dummy1 at 0x47cf
void fn_0x47cf(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x47cf:
    ctx->rip = 0x47cfULL;
    /* 0x47cf: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __init_libc at 0x47d0
void fn_0x47d0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x47d0ULL) {
        switch (ctx->rip) {
            case 0x47fbULL: goto loc_0x47fb;
            case 0x4805ULL: goto loc_0x4805;
            case 0x4814ULL: goto loc_0x4814;
            case 0x4818ULL: goto loc_0x4818;
            case 0x481eULL: goto loc_0x481e;
            case 0x4827ULL: goto loc_0x4827;
            case 0x4833ULL: goto loc_0x4833;
            case 0x484fULL: goto loc_0x484f;
            case 0x4858ULL: goto loc_0x4858;
            case 0x4863ULL: goto loc_0x4863;
            case 0x4874ULL: goto loc_0x4874;
            case 0x487cULL: goto loc_0x487c;
            case 0x489bULL: goto loc_0x489b;
            case 0x489eULL: goto loc_0x489e;
            case 0x48aaULL: goto loc_0x48aa;
            case 0x48aeULL: goto loc_0x48ae;
            case 0x48caULL: goto loc_0x48ca;
            case 0x48dcULL: goto loc_0x48dc;
            case 0x48e7ULL: goto loc_0x48e7;
            case 0x490cULL: goto loc_0x490c;
            case 0x490eULL: goto loc_0x490e;
            case 0x491bULL: goto loc_0x491b;
            case 0x4922ULL: goto loc_0x4922;
            case 0x4935ULL: goto loc_0x4935;
            case 0x4937ULL: goto loc_0x4937;
            case 0x4941ULL: goto loc_0x4941;
            case 0x494bULL: goto loc_0x494b;
            default: goto loc_0x47d0;
        }
    }

loc_0x47d0:
    ctx->rip = 0x47d0ULL;
    /* 0x47d0: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x47d2: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x47d3: SUB RSP, 0x158 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x158ULL); uint64_t res;
      res = a - b;
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x47da: MOV RBX, RSI */
ctx->rbx = (uint64_t)(ctx->rsi);
    /* 0x47dd: MOV R14, RDI */
ctx->r14 = (uint64_t)(ctx->rdi);
    /* 0x47e0: LEA RDI, [RSP+Reg(0)+0x20] */
    ctx->rdi = (uint64_t)(ctx->rsp + 0x20ULL);
    /* 0x47e5: MOV EDX, 0x130 */
ctx->rdx = (uint64_t)(uint32_t)(0x130ULL);
    /* 0x47ea: XOR ESI, ESI */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t b = (uint32_t)(((uint32_t)ctx->rsi)); uint32_t res;
      res = a ^ b;
      set_flags_logic_u32(ctx, res);
    ctx->rsi = (uint64_t)(uint32_t)(res);
    }
    /* 0x47ec: CALL .-1297 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x47f1ULL;
    ctx->rip = 0x42e0ULL;
    fn_0x42e0(ctx);
    /* 0x47f1: LEA RAX, [RIP+0xfe48] */
    ctx->rax = (uint64_t)(0x14640ULL);
    /* 0x47f8: MOV [RAX], R14 */
MEM_U64(ctx->rax) = (uint64_t)(ctx->r14);

loc_0x47fb:
    ctx->rip = 0x47fbULL;
    /* 0x47fb: CMP [R14], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->r14)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x47ff: LEA R14, [R14+0x8] */
    ctx->r14 = (uint64_t)(ctx->r14 + 0x8ULL);
    /* 0x4803: JNE .-10 */
    if (!ctx->zf) goto loc_0x47fb;

loc_0x4805:
    ctx->rip = 0x4805ULL;
    /* 0x4805: MOV [RIP+0xf96c], R14 */
MEM_U64(0x14178ULL) = (uint64_t)(ctx->r14);
    /* 0x480c: MOV RAX, [R14] */
ctx->rax = (uint64_t)(MEM_U64(ctx->r14));
    /* 0x480f: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x4812: JE .+68 */
    if (ctx->zf) goto loc_0x4858;

loc_0x4814:
    ctx->rip = 0x4814ULL;
    /* 0x4814: ADD R14, 0x10 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(0x10ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r14 = (uint64_t)(res);
    }

loc_0x4818:
    ctx->rip = 0x4818ULL;
    /* 0x4818: CMP RAX, 0x25 */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(0x25ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x481c: JA .+9 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x4827;

loc_0x481e:
    ctx->rip = 0x481eULL;
    /* 0x481e: MOV RCX, [R14-0x8] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->r14 + (uint64_t)(-0x8LL)));
    /* 0x4822: MOV [RSP+8*RAX+0x20], RCX */
MEM_U64(ctx->rsp + (ctx->rax * 8ULL) + 0x20ULL) = (uint64_t)(ctx->rcx);

loc_0x4827:
    ctx->rip = 0x4827ULL;
    /* 0x4827: MOV RAX, [R14] */
ctx->rax = (uint64_t)(MEM_U64(ctx->r14));
    /* 0x482a: ADD R14, 0x10 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(0x10ULL); uint64_t res;
      res = a + b;
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x482e: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x4831: JNE .-27 */
    if (!ctx->zf) goto loc_0x4818;

loc_0x4833:
    ctx->rip = 0x4833ULL;
    /* 0x4833: MOV RCX, [RSP+Reg(0)+0xa0] */
ctx->rcx = (uint64_t)(MEM_U64(ctx->rsp + 0xa0ULL));
    /* 0x483b: MOV RAX, [RSP+Reg(0)+0x120] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x120ULL));
    /* 0x4843: MOV [RIP+0xfdc6], RCX */
MEM_U64(0x14610ULL) = (uint64_t)(ctx->rcx);
    /* 0x484a: TEST RAX, RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(ctx->rax); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x484d: JE .+20 */
    if (ctx->zf) goto loc_0x4863;

loc_0x484f:
    ctx->rip = 0x484fULL;
    /* 0x484f: MOV [RIP+0xfdf2], RAX */
MEM_U64(0x14648ULL) = (uint64_t)(ctx->rax);
    /* 0x4856: JMP .+11 */
    goto loc_0x4863;

loc_0x4858:
    ctx->rip = 0x4858ULL;
    /* 0x4858: MOV [RIP+0xfdad], 0x0 */
MEM_U64(0x14610ULL) = (uint64_t)(0x0ULL);

loc_0x4863:
    ctx->rip = 0x4863ULL;
    /* 0x4863: MOV RAX, [RSP+Reg(0)+0x50] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x50ULL));
    /* 0x4868: MOV [RIP+0xf931], RAX */
MEM_U64(0x141a0ULL) = (uint64_t)(ctx->rax);
    /* 0x486f: TEST RBX, RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x4872: JNE .+8 */
    if (!ctx->zf) goto loc_0x487c;

loc_0x4874:
    ctx->rip = 0x4874ULL;
    /* 0x4874: MOV RBX, [RSP+Reg(0)+0x118] */
ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp + 0x118ULL));

loc_0x487c:
    ctx->rip = 0x487cULL;
    /* 0x487c: TEST RBX, RBX */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a & b;
      set_flags_logic_u64(ctx, res);
    }
    /* 0x487f: LEA RAX, [RIP+0x1214] */
    ctx->rax = (uint64_t)(0x5a9aULL);
    /* 0x4886: CMOVNE RAX, RBX */
    if (!ctx->zf) {
    ctx->rax = (uint64_t)(ctx->rbx);
    }
    /* 0x488a: LEA RCX, [RIP+0xfd77] */
    ctx->rcx = (uint64_t)(0x14608ULL);
    /* 0x4891: MOV [RCX], RAX */
MEM_U64(ctx->rcx) = (uint64_t)(ctx->rax);
    /* 0x4894: LEA RCX, [RIP+0xfd65] */
    ctx->rcx = (uint64_t)(0x14600ULL);

loc_0x489b:
    ctx->rip = 0x489bULL;
    /* 0x489b: MOV [RCX], RAX */
MEM_U64(ctx->rcx) = (uint64_t)(ctx->rax);

loc_0x489e:
    ctx->rip = 0x489eULL;
    /* 0x489e: INC RAX */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t res;
      res = a + 1;
      set_flags_inc_u64(ctx, a, res);
    ctx->rax = (uint64_t)(res);
    }
    /* 0x48a1: MOVZX EDX, [RAX-0x1] */
    ctx->rdx = (uint64_t)(uint32_t)((uint64_t)(uint8_t)(MEM_U8(ctx->rax + (uint64_t)(-0x1LL))));
    /* 0x48a5: CMP EDX, 0x2f */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(0x2fULL); uint32_t res;
      res = a - b;
      set_flags_sub_u32(ctx, a, b, res);
    }
    /* 0x48a8: JE .-15 */
    if (ctx->zf) goto loc_0x489b;

loc_0x48aa:
    ctx->rip = 0x48aaULL;
    /* 0x48aa: TEST EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x48ac: JNE .-16 */
    if (!ctx->zf) goto loc_0x489e;

loc_0x48ae:
    ctx->rip = 0x48aeULL;
    /* 0x48ae: MOV RDI, [RSP+Reg(0)+0xe8] */
ctx->rdi = (uint64_t)(MEM_U64(ctx->rsp + 0xe8ULL));
    /* 0x48b6: CALL .-236 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x48bbULL;
    ctx->rip = 0x47cfULL;
    fn_0x47cf(ctx);
    /* 0x48bb: MOV RAX, [RSP+Reg(0)+0x78] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x78ULL));
    /* 0x48c0: CMP RAX, [RSP+Reg(0)+0x80] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rsp + 0x80ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x48c8: JNE .+29 */
    if (!ctx->zf) goto loc_0x48e7;

loc_0x48ca:
    ctx->rip = 0x48caULL;
    /* 0x48ca: MOV RAX, [RSP+Reg(0)+0x88] */
ctx->rax = (uint64_t)(MEM_U64(ctx->rsp + 0x88ULL));
    /* 0x48d2: CMP RAX, [RSP+Reg(0)+0x90] */
    { uint64_t a = (uint64_t)(ctx->rax); uint64_t b = (uint64_t)(MEM_U64(ctx->rsp + 0x90ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x48da: JNE .+11 */
    if (!ctx->zf) goto loc_0x48e7;

loc_0x48dc:
    ctx->rip = 0x48dcULL;
    /* 0x48dc: CMP [RSP+Reg(0)+0xd8], 0x0 */
    { uint64_t a = (uint64_t)(MEM_U64(ctx->rsp + 0xd8ULL)); uint64_t b = (uint64_t)(0x0ULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x48e5: JE .+100 */
    if (ctx->zf) goto loc_0x494b;

loc_0x48e7:
    ctx->rip = 0x48e7ULL;
    /* 0x48e7: MOVAPS X0, [RIP+0x1f52] */
    memcpy(&ctx->xmm[0], ctx->mem_base + (0x6840ULL), 16);
    /* 0x48ee: MOV RDI, RSP */
ctx->rdi = (uint64_t)(ctx->rsp);
    /* 0x48f1: MOVAPS [RDI], X0 */
    memcpy(ctx->mem_base + (ctx->rdi), &ctx->xmm[0], 16);
    /* 0x48f4: MOV [RDI+0x10], 0x2 */
MEM_U64(ctx->rdi + 0x10ULL) = (uint64_t)(0x2ULL);
    /* 0x48fc: MOV ESI, 0x3 */
ctx->rsi = (uint64_t)(uint32_t)(0x3ULL);
    /* 0x4901: XOR EDX, EDX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t b = (uint32_t)(((uint32_t)ctx->rdx)); uint32_t res;
      res = a ^ b;
    ctx->rdx = (uint64_t)(uint32_t)(res);
    }
    /* 0x4903: CALL .+840 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4908ULL;
    ctx->rip = 0x4c50ULL;
    fn_0x4c50(ctx);
    /* 0x4908: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x490a: JNS .+2 */
    if (!ctx->sf) goto loc_0x490e;

loc_0x490c:
    ctx->rip = 0x490cULL;
    /* 0x490c: HLT */
    exit(0);
    /* 0x490d: FWAIT */
    /* fwait no-op */

loc_0x490e:
    ctx->rip = 0x490eULL;
    /* 0x490e: MOV R14L, 0x6 */
ctx->r14 = (uint64_t)(uint32_t)(0x6ULL);
    /* 0x4914: LEA RBX, [RIP+0x1403] */
    ctx->rbx = (uint64_t)(0x5d1eULL);

loc_0x491b:
    ctx->rip = 0x491bULL;
    /* 0x491b: TEST [RSP+R14], 0x20 */
    { uint8_t a = (uint8_t)(MEM_U8(ctx->rsp + ctx->r14)); uint8_t b = (uint8_t)(0x20ULL); uint8_t res;
      res = a & b;
      set_flags_logic_u8(ctx, res);
    }
    /* 0x4920: JE .+21 */
    if (ctx->zf) goto loc_0x4937;

loc_0x4922:
    ctx->rip = 0x4922ULL;
    /* 0x4922: MOV RDI, RBX */
ctx->rdi = (uint64_t)(ctx->rbx);
    /* 0x4925: MOV ESI, 0x2 */
ctx->rsi = (uint64_t)(uint32_t)(0x2ULL);
    /* 0x492a: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x492c: CALL .+639 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4931ULL;
    ctx->rip = 0x4bb0ULL;
    fn_0x4bb0(ctx);
    /* 0x4931: TEST EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a & b;
      set_flags_logic_u32(ctx, res);
    }
    /* 0x4933: JNS .+2 */
    if (!ctx->sf) goto loc_0x4937;

loc_0x4935:
    ctx->rip = 0x4935ULL;
    /* 0x4935: HLT */
    exit(0);
    /* 0x4936: FWAIT */
    /* fwait no-op */

loc_0x4937:
    ctx->rip = 0x4937ULL;
    /* 0x4937: ADD R14, 0x8 */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
    ctx->r14 = (uint64_t)(res);
    }
    /* 0x493b: CMP R14, 0x1e */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(0x1eULL); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x493f: JNE .-38 */
    if (!ctx->zf) goto loc_0x491b;

loc_0x4941:
    ctx->rip = 0x4941ULL;
    /* 0x4941: MOV [RIP+0xf825], 0x1 */
MEM_U32(0x14170ULL) = (uint32_t)(0x1ULL);

loc_0x494b:
    ctx->rip = 0x494bULL;
    /* 0x494b: ADD RSP, 0x158 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x158ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x4952: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x4953: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x4955: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function libc_start_init at 0x4956
void fn_0x4956(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x4956ULL) {
        switch (ctx->rip) {
            case 0x496fULL: goto loc_0x496f;
            case 0x4976ULL: goto loc_0x4976;
            case 0x4981ULL: goto loc_0x4981;
            default: goto loc_0x4956;
        }
    }

loc_0x4956:
    ctx->rip = 0x4956ULL;
    /* 0x4956: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x4958: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x4959: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x495a: CALL .-403 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x495fULL;
    ctx->rip = 0x47ccULL;
    fn_0x47cc(ctx);
    /* 0x495f: LEA RBX, [RIP+0x388a] */
    ctx->rbx = (uint64_t)(0x81f0ULL);
    /* 0x4966: CMP RBX, [RIP+0x7693] */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(MEM_U64(0xc000ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x496d: JAE .+18 */
    if (!ctx->cf) goto loc_0x4981;

loc_0x496f:
    ctx->rip = 0x496fULL;
    /* 0x496f: LEA R14, [RIP+0x387a] */
    ctx->r14 = (uint64_t)(0x81f0ULL);

loc_0x4976:
    ctx->rip = 0x4976ULL;
    /* 0x4976: CALL [RBX] */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4978ULL;
    ctx->rip = MEM_U64(ctx->rbx);
    recomp_dispatch(ctx, MEM_U64(ctx->rbx));
    /* 0x4978: ADD RBX, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
    ctx->rbx = (uint64_t)(res);
    }
    /* 0x497c: CMP RBX, R14 */
    { uint64_t a = (uint64_t)(ctx->rbx); uint64_t b = (uint64_t)(ctx->r14); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x497f: JB .-11 */
    if (ctx->cf) goto loc_0x4976;

loc_0x4981:
    ctx->rip = 0x4981ULL;
    /* 0x4981: ADD RSP, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x4985: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x4986: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x4988: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function __libc_start_main at 0x4989
void fn_0x4989(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x4989:
    ctx->rip = 0x4989ULL;
    /* 0x4989: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x498b: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x498d: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x498e: MOV RBX, RDX */
ctx->rbx = (uint64_t)(ctx->rdx);
    /* 0x4991: MOV R14, RDI */
ctx->r14 = (uint64_t)(ctx->rdi);
    /* 0x4994: MOVSXD R15, ESI */
    ctx->r15 = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->rsi)));
    /* 0x4997: LEA RDI, [RDX+8*R15] */
    ctx->rdi = (uint64_t)(ctx->rdx + (ctx->r15 * 8ULL));
    /* 0x499b: ADD RDI, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rdi); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rdi = (uint64_t)(res);
    }
    /* 0x499f: MOV RSI, [RDX] */
ctx->rsi = (uint64_t)(MEM_U64(ctx->rdx));
    /* 0x49a2: CALL .-471 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x49a7ULL;
    ctx->rip = 0x47d0ULL;
    fn_0x47d0(ctx);
    /* 0x49a7: LEA RAX, [RIP+0x11] */
    ctx->rax = (uint64_t)(0x49bfULL);
    /* 0x49ae: FWAIT */
    /* fwait no-op */
    /* 0x49af: MOV RDI, R14 */
ctx->rdi = (uint64_t)(ctx->r14);
    /* 0x49b2: MOV ESI, R15L */
ctx->rsi = (uint64_t)(uint32_t)(((uint32_t)ctx->r15));
    /* 0x49b5: MOV RDX, RBX */
ctx->rdx = (uint64_t)(ctx->rbx);
    /* 0x49b8: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x49b9: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x49bb: POP R15 */
    ctx->r15 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x49bd: JMP RAX */
    RECOMP_POP_UNWIND();
    ctx->rip = ctx->rax;
    recomp_dispatch(ctx, ctx->rax);
    return;
    RECOMP_POP_UNWIND();
}

// Function libc_start_main_stage2 at 0x49bf
void fn_0x49bf(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x49bf:
    ctx->rip = 0x49bfULL;
    /* 0x49bf: PUSH R15 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r15);
    /* 0x49c1: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x49c3: PUSH R12 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r12);
    /* 0x49c5: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x49c6: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x49c7: MOV RBX, RDX */
ctx->rbx = (uint64_t)(ctx->rdx);
    /* 0x49ca: MOV R14, RDI */
ctx->r14 = (uint64_t)(ctx->rdi);
    /* 0x49cd: MOVSXD R15, ESI */
    ctx->r15 = (uint64_t)((uint64_t)(int64_t)(int32_t)(((uint32_t)ctx->rsi)));
    /* 0x49d0: LEA R12, [RDX+8*R15] */
    ctx->r12 = (uint64_t)(ctx->rdx + (ctx->r15 * 8ULL));
    /* 0x49d4: ADD R12, 0x8 */
    { uint64_t a = (uint64_t)(ctx->r12); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->r12 = (uint64_t)(res);
    }
    /* 0x49d8: CALL .-135 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x49ddULL;
    ctx->rip = 0x4956ULL;
    fn_0x4956(ctx);
    /* 0x49dd: MOV EDI, R15L */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->r15));
    /* 0x49e0: MOV RSI, RBX */
ctx->rsi = (uint64_t)(ctx->rbx);
    /* 0x49e3: MOV RDX, R12 */
ctx->rdx = (uint64_t)(ctx->r12);
    /* 0x49e6: CALL R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x49e9ULL;
    ctx->rip = ctx->r14;
    recomp_dispatch(ctx, ctx->r14);
    /* 0x49e9: MOV EDI, EAX */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->rax));
    /* 0x49eb: CALL .+59 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x49f0ULL;
    ctx->rip = 0x4a2bULL;
    fn_0x4a2b(ctx);
    RECOMP_POP_UNWIND();
}

// Function dummy at 0x49f0
void fn_0x49f0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;

loc_0x49f0:
    ctx->rip = 0x49f0ULL;
    /* 0x49f0: RET */
    RECOMP_POP_UNWIND();
    ctx->rsp += 8;
    return;
    RECOMP_POP_UNWIND();
}

// Function libc_exit_fini at 0x49f1
void fn_0x49f1(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x49f1ULL) {
        switch (ctx->rip) {
            case 0x4a05ULL: goto loc_0x4a05;
            case 0x4a0cULL: goto loc_0x4a0c;
            case 0x4a1dULL: goto loc_0x4a1d;
            default: goto loc_0x49f1;
        }
    }

loc_0x49f1:
    ctx->rip = 0x49f1ULL;
    /* 0x49f1: PUSH R14 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->r14);
    /* 0x49f3: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x49f4: PUSH RAX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rax);
    /* 0x49f5: LEA RCX, [RIP+0xffffb604] */
    ctx->rcx = (uint64_t)(0x0ULL);
    /* 0x49fc: CMP RCX, [RIP+0x7605] */
    { uint64_t a = (uint64_t)(ctx->rcx); uint64_t b = (uint64_t)(MEM_U64(0xc008ULL)); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x4a03: JBE .+24 */
    if ((ctx->cf || ctx->zf)) goto loc_0x4a1d;

loc_0x4a05:
    ctx->rip = 0x4a05ULL;
    /* 0x4a05: LEA RBX, [RIP+0xffffb5f4] */
    ctx->rbx = (uint64_t)(0x0ULL);

loc_0x4a0c:
    ctx->rip = 0x4a0cULL;
    /* 0x4a0c: LEA R14, [RCX-0x8] */
    ctx->r14 = (uint64_t)(ctx->rcx + (uint64_t)(-0x8LL));
    /* 0x4a10: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x4a12: CALL [RCX-0x8] */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4a15ULL;
    ctx->rip = MEM_U64(ctx->rcx + (uint64_t)(-0x8LL));
    recomp_dispatch(ctx, MEM_U64(ctx->rcx + (uint64_t)(-0x8LL)));
    /* 0x4a15: MOV RCX, R14 */
ctx->rcx = (uint64_t)(ctx->r14);
    /* 0x4a18: CMP R14, RBX */
    { uint64_t a = (uint64_t)(ctx->r14); uint64_t b = (uint64_t)(ctx->rbx); uint64_t res;
      res = a - b;
      set_flags_sub_u64(ctx, a, b, res);
    }
    /* 0x4a1b: JA .-17 */
    if ((!ctx->cf && !ctx->zf)) goto loc_0x4a0c;

loc_0x4a1d:
    ctx->rip = 0x4a1dULL;
    /* 0x4a1d: XOR EAX, EAX */
    { uint32_t a = (uint32_t)(((uint32_t)ctx->rax)); uint32_t b = (uint32_t)(((uint32_t)ctx->rax)); uint32_t res;
      res = a ^ b;
    ctx->rax = (uint64_t)(uint32_t)(res);
    }
    /* 0x4a1f: ADD RSP, 0x8 */
    { uint64_t a = (uint64_t)(ctx->rsp); uint64_t b = (uint64_t)(0x8ULL); uint64_t res;
      res = a + b;
      set_flags_add_u64(ctx, a, b, res);
    ctx->rsp = (uint64_t)(res);
    }
    /* 0x4a23: POP RBX */
    ctx->rbx = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x4a24: POP R14 */
    ctx->r14 = (uint64_t)(MEM_U64(ctx->rsp));
    ctx->rsp += 8;
    /* 0x4a26: JMP .-607 */
    RECOMP_POP_UNWIND();
    ctx->rip = 0x47ccULL;
    fn_0x47cc(ctx);
    return;
    RECOMP_POP_UNWIND();
}

// Function exit at 0x4a2b (Forwarded directly to host shim shim_exit)
void fn_0x4a2b(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_exit(ctx);
    return;
}

// Function _Exit at 0x4a48
void fn_0x4a48(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    if (ctx->rip != 0x4a48ULL) {
        switch (ctx->rip) {
            case 0x4a4bULL: goto loc_0x4a4b;
            default: goto loc_0x4a48;
        }
    }

loc_0x4a48:
    ctx->rip = 0x4a48ULL;
    /* 0x4a48: PUSH RBX */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = (uint64_t)(ctx->rbx);
    /* 0x4a49: MOV EBX, EDI */
ctx->rbx = (uint64_t)(uint32_t)(((uint32_t)ctx->rdi));

loc_0x4a4b:
    ctx->rip = 0x4a4bULL;
    /* 0x4a4b: MOV EDI, EBX */
ctx->rdi = (uint64_t)(uint32_t)(((uint32_t)ctx->rbx));
    /* 0x4a4d: CALL .+494 */
    ctx->rsp -= 8;
    MEM_U64(ctx->rsp) = 0x4a52ULL;
    ctx->rip = 0x4c40ULL;
    fn_0x4c40(ctx);
    /* 0x4a52: JMP .-9 */
    goto loc_0x4a4b;
    RECOMP_POP_UNWIND();
}

// Function fn_4a70 at 0x4a70 (Forwarded directly to host shim shim_open)
void fn_0x4a70(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_open(ctx);
    return;
}

// Function fn_4a80 at 0x4a80 (Forwarded directly to host shim shim_write)
void fn_0x4a80(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_write(ctx);
    return;
}

// Function fn_4a90 at 0x4a90 (Forwarded directly to host shim shim_close)
void fn_0x4a90(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_close(ctx);
    return;
}

// Function fn_4aa0 at 0x4aa0 (Forwarded directly to host shim shim_read)
void fn_0x4aa0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_read(ctx);
    return;
}

// Function fn_4ab0 at 0x4ab0 (Forwarded directly to host shim shim_unlink)
void fn_0x4ab0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_unlink(ctx);
    return;
}

// Function fn_4ac0 at 0x4ac0 (Forwarded directly to host shim shim_pthread_rwlock_init)
void fn_0x4ac0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_pthread_rwlock_init(ctx);
    return;
}

// Function fn_4ad0 at 0x4ad0 (Forwarded directly to host shim shim_pthread_rwlock_wrlock)
void fn_0x4ad0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_pthread_rwlock_wrlock(ctx);
    return;
}

// Function fn_4ae0 at 0x4ae0 (Forwarded directly to host shim shim_pthread_rwlock_unlock)
void fn_0x4ae0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_pthread_rwlock_unlock(ctx);
    return;
}

// Function fn_4af0 at 0x4af0 (Forwarded directly to host shim shim_pthread_rwlock_rdlock)
void fn_0x4af0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_pthread_rwlock_rdlock(ctx);
    return;
}

// Function fn_4b00 at 0x4b00 (Forwarded directly to host shim shim_pthread_rwlock_destroy)
void fn_0x4b00(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_pthread_rwlock_destroy(ctx);
    return;
}

// Function fn_4b10 at 0x4b10 (Forwarded directly to host shim shim_scePthreadCreate)
void fn_0x4b10(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_scePthreadCreate(ctx);
    return;
}

// Function fn_4b20 at 0x4b20 (Forwarded directly to host shim shim_sceKernelUsleep)
void fn_0x4b20(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_sceKernelUsleep(ctx);
    return;
}

// Function fn_4b30 at 0x4b30 (Forwarded directly to host shim shim_connect)
void fn_0x4b30(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_connect(ctx);
    return;
}

// Function fn_4b40 at 0x4b40 (Forwarded directly to host shim shim_close)
void fn_0x4b40(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_close(ctx);
    return;
}

// Function fn_4b50 at 0x4b50 (Forwarded directly to host shim shim_scePthreadJoin)
void fn_0x4b50(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_scePthreadJoin(ctx);
    return;
}

// Function fn_4b60 at 0x4b60 (Forwarded directly to host shim shim_setsockopt)
void fn_0x4b60(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_setsockopt(ctx);
    return;
}

// Function fn_4b70 at 0x4b70 (Forwarded directly to host shim shim_bind)
void fn_0x4b70(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_bind(ctx);
    return;
}

// Function fn_4b80 at 0x4b80 (Forwarded directly to host shim shim_listen)
void fn_0x4b80(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_listen(ctx);
    return;
}

// Function fn_4b90 at 0x4b90 (Forwarded directly to host shim shim_accept)
void fn_0x4b90(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_accept(ctx);
    return;
}

// Function fn_4ba0 at 0x4ba0 (Forwarded directly to host shim shim_error)
void fn_0x4ba0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_error(ctx);
    return;
}

// Function fn_4bb0 at 0x4bb0 (Forwarded directly to host shim shim_open)
void fn_0x4bb0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_open(ctx);
    return;
}

// Function fn_4bc0 at 0x4bc0 (Forwarded directly to host shim shim_recvfrom)
void fn_0x4bc0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_recvfrom(ctx);
    return;
}

// Function fn_4bd0 at 0x4bd0 (Forwarded directly to host shim shim_sendto)
void fn_0x4bd0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_sendto(ctx);
    return;
}

// Function fn_4be0 at 0x4be0 (Forwarded directly to host shim shim_socket)
void fn_0x4be0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_socket(ctx);
    return;
}

// Function fn_4bf0 at 0x4bf0 (Forwarded directly to host shim shim_fcntl)
void fn_0x4bf0(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_fcntl(ctx);
    return;
}

// Function fn_4c00 at 0x4c00 (Forwarded directly to host shim shim_writev)
void fn_0x4c00(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_writev(ctx);
    return;
}

// Function fn_4c10 at 0x4c10 (Forwarded directly to host shim shim_write)
void fn_0x4c10(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_write(ctx);
    return;
}

// Function fn_4c20 at 0x4c20 (Forwarded directly to host shim shim_ioctl)
void fn_0x4c20(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_ioctl(ctx);
    return;
}

// Function fn_4c30 at 0x4c30 (Forwarded directly to host shim shim_lseek)
void fn_0x4c30(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_lseek(ctx);
    return;
}

// Function fn_4c40 at 0x4c40 (Forwarded directly to host shim shim_exit)
void fn_0x4c40(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_exit(ctx);
    return;
}

// Function fn_4c50 at 0x4c50 (Forwarded directly to host shim shim_poll)
void fn_0x4c50(GuestContext *__restrict__ ctx) {
    UnwindFrame *__cur_unwind_frame = NULL;
    (void)__cur_unwind_frame;
    shim_poll(ctx);
    return;
}

// Registration for chunk 0
void recomp_init_dispatch_chunk_0(void) {
    recomp_register_fn(0x20ULL, fn_0x20);
    recomp_register_fn(0x67ULL, fn_0x20);
    recomp_register_fn(0x84ULL, fn_0x20);
    recomp_register_fn(0xe8ULL, fn_0x20);
    recomp_register_fn(0x120ULL, fn_0x20);
    recomp_register_fn(0x13eULL, fn_0x20);
    recomp_register_fn(0x15bULL, fn_0x20);
    recomp_register_fn(0x1afULL, fn_0x20);
    recomp_register_fn(0x1c4ULL, fn_0x20);
    recomp_register_fn(0x1e5ULL, fn_0x20);
    recomp_register_fn(0x22aULL, fn_0x20);
    recomp_register_fn(0x24aULL, fn_0x20);
    recomp_register_fn(0x265ULL, fn_0x20);
    recomp_register_fn(0x285ULL, fn_0x20);
    recomp_register_fn(0x2a0ULL, fn_0x20);
    recomp_register_fn(0x2c0ULL, fn_0x20);
    recomp_register_fn(0x2dbULL, fn_0x20);
    recomp_register_fn(0x2fbULL, fn_0x20);
    recomp_register_fn(0x316ULL, fn_0x20);
    recomp_register_fn(0x336ULL, fn_0x20);
    recomp_register_fn(0x380ULL, fn_0x20);
    recomp_register_fn(0x3a0ULL, fn_0x20);
    recomp_register_fn(0x3caULL, fn_0x20);
    recomp_register_fn(0x3ebULL, fn_0x20);
    recomp_register_fn(0x455ULL, fn_0x20);
    recomp_register_fn(0x476ULL, fn_0x20);
    recomp_register_fn(0x4feULL, fn_0x20);
    recomp_register_fn(0x516ULL, fn_0x20);
    recomp_register_fn(0x534ULL, fn_0x20);
    recomp_register_fn(0x587ULL, fn_0x20);
    recomp_register_fn(0x5a0ULL, fn_0x5a0);
    recomp_register_fn(0x5c9ULL, fn_0x5a0);
    recomp_register_fn(0x5eeULL, fn_0x5a0);
    recomp_register_fn(0x658ULL, fn_0x5a0);
    recomp_register_fn(0x685ULL, fn_0x5a0);
    recomp_register_fn(0x697ULL, fn_0x5a0);
    recomp_register_fn(0x6c4ULL, fn_0x5a0);
    recomp_register_fn(0x6f7ULL, fn_0x5a0);
    recomp_register_fn(0x724ULL, fn_0x5a0);
    recomp_register_fn(0x766ULL, fn_0x5a0);
    recomp_register_fn(0x7c0ULL, fn_0x5a0);
    recomp_register_fn(0x7d8ULL, fn_0x5a0);
    recomp_register_fn(0x7e8ULL, fn_0x7e8);
    recomp_register_fn(0x7f0ULL, fn_0x7f0);
    recomp_register_fn(0x7f8ULL, fn_0x7f8);
    recomp_register_fn(0x800ULL, shim_inet_pton);
    recomp_register_fn(0xd54ULL, shim_recv);
    recomp_register_fn(0xd60ULL, shim_send);
    recomp_register_fn(0xd6cULL, shim_socket);
    recomp_register_fn(0xe10ULL, shim_fcntl);
    recomp_register_fn(0xfb4ULL, fn_0xfb4);
    recomp_register_fn(0xfdbULL, fn_0xfb4);
    recomp_register_fn(0x1012ULL, fn_0xfb4);
    recomp_register_fn(0x104cULL, fn_0x104c);
    recomp_register_fn(0x104fULL, fn_0x104f);
    recomp_register_fn(0x1060ULL, fn_0x1060);
    recomp_register_fn(0x1082ULL, fn_0x1060);
    recomp_register_fn(0x109bULL, fn_0x1060);
    recomp_register_fn(0x10a6ULL, fn_0x1060);
    recomp_register_fn(0x10c0ULL, fn_0x10c0);
    recomp_register_fn(0x10ffULL, fn_0x10c0);
    recomp_register_fn(0x1111ULL, fn_0x10c0);
    recomp_register_fn(0x1128ULL, fn_0x10c0);
    recomp_register_fn(0x112cULL, fn_0x10c0);
    recomp_register_fn(0x1132ULL, fn_0x10c0);
    recomp_register_fn(0x1145ULL, fn_0x10c0);
    recomp_register_fn(0x114eULL, fn_0x10c0);
    recomp_register_fn(0x1169ULL, fn_0x10c0);
    recomp_register_fn(0x116bULL, fn_0x10c0);
    recomp_register_fn(0x1172ULL, fn_0x10c0);
    recomp_register_fn(0x1179ULL, fn_0x10c0);
    recomp_register_fn(0x1183ULL, fn_0x10c0);
    recomp_register_fn(0x118aULL, fn_0x10c0);
    recomp_register_fn(0x11bbULL, fn_0x10c0);
    recomp_register_fn(0x11c0ULL, fn_0x10c0);
    recomp_register_fn(0x11c5ULL, fn_0x10c0);
    recomp_register_fn(0x11e8ULL, fn_0x10c0);
    recomp_register_fn(0x1204ULL, fn_0x10c0);
    recomp_register_fn(0x1208ULL, fn_0x10c0);
    recomp_register_fn(0x1211ULL, fn_0x10c0);
    recomp_register_fn(0x1220ULL, shim_ioctl);
    recomp_register_fn(0x12d4ULL, fn_0x12d4);
    recomp_register_fn(0x12dcULL, fn_0x12dc);
    recomp_register_fn(0x1333ULL, fn_0x12dc);
    recomp_register_fn(0x133cULL, fn_0x12dc);
    recomp_register_fn(0x134cULL, fn_0x12dc);
    recomp_register_fn(0x1357ULL, fn_0x12dc);
    recomp_register_fn(0x135aULL, fn_0x12dc);
    recomp_register_fn(0x1366ULL, fn_0x12dc);
    recomp_register_fn(0x136eULL, fn_0x12dc);
    recomp_register_fn(0x1375ULL, fn_0x12dc);
    recomp_register_fn(0x137fULL, fn_0x12dc);
    recomp_register_fn(0x1381ULL, fn_0x12dc);
    recomp_register_fn(0x13a5ULL, fn_0x12dc);
    recomp_register_fn(0x13b6ULL, fn_0x12dc);
    recomp_register_fn(0x13d3ULL, fn_0x12dc);
    recomp_register_fn(0x13dcULL, fn_0x12dc);
    recomp_register_fn(0x140aULL, fn_0x12dc);
    recomp_register_fn(0x1427ULL, fn_0x12dc);
    recomp_register_fn(0x142fULL, fn_0x12dc);
    recomp_register_fn(0x1444ULL, fn_0x1444);
    recomp_register_fn(0x14aaULL, fn_0x1444);
    recomp_register_fn(0x14b3ULL, fn_0x1444);
    recomp_register_fn(0x14c4ULL, fn_0x1444);
    recomp_register_fn(0x14d9ULL, fn_0x1444);
    recomp_register_fn(0x14dcULL, fn_0x1444);
    recomp_register_fn(0x14e0ULL, fn_0x1444);
    recomp_register_fn(0x14e8ULL, fn_0x1444);
    recomp_register_fn(0x14f1ULL, fn_0x1444);
    recomp_register_fn(0x14f7ULL, fn_0x1444);
    recomp_register_fn(0x14fdULL, fn_0x1444);
    recomp_register_fn(0x150dULL, fn_0x1444);
    recomp_register_fn(0x150fULL, fn_0x1444);
    recomp_register_fn(0x1514ULL, fn_0x1444);
    recomp_register_fn(0x1517ULL, fn_0x1444);
    recomp_register_fn(0x152dULL, fn_0x1444);
    recomp_register_fn(0x1532ULL, fn_0x1444);
    recomp_register_fn(0x1538ULL, fn_0x1444);
    recomp_register_fn(0x1546ULL, fn_0x1444);
    recomp_register_fn(0x154fULL, fn_0x1444);
    recomp_register_fn(0x156cULL, fn_0x1444);
    recomp_register_fn(0x1586ULL, fn_0x1444);
    recomp_register_fn(0x15a1ULL, fn_0x1444);
    recomp_register_fn(0x15b4ULL, fn_0x1444);
    recomp_register_fn(0x15bfULL, fn_0x1444);
    recomp_register_fn(0x15d5ULL, fn_0x1444);
    recomp_register_fn(0x15deULL, fn_0x1444);
    recomp_register_fn(0x15f9ULL, fn_0x1444);
    recomp_register_fn(0x160bULL, fn_0x1444);
    recomp_register_fn(0x160dULL, fn_0x1444);
    recomp_register_fn(0x1615ULL, fn_0x1444);
    recomp_register_fn(0x1620ULL, fn_0x1444);
    recomp_register_fn(0x1630ULL, fn_0x1444);
    recomp_register_fn(0x163aULL, fn_0x1444);
    recomp_register_fn(0x166aULL, fn_0x1444);
    recomp_register_fn(0x1679ULL, fn_0x1444);
    recomp_register_fn(0x167cULL, fn_0x1444);
    recomp_register_fn(0x1685ULL, fn_0x1444);
    recomp_register_fn(0x169aULL, fn_0x1444);
    recomp_register_fn(0x169fULL, fn_0x1444);
    recomp_register_fn(0x16c9ULL, fn_0x1444);
    recomp_register_fn(0x16cdULL, fn_0x1444);
    recomp_register_fn(0x16d2ULL, fn_0x1444);
    recomp_register_fn(0x16ddULL, fn_0x1444);
    recomp_register_fn(0x16e5ULL, fn_0x1444);
    recomp_register_fn(0x16eeULL, fn_0x1444);
    recomp_register_fn(0x16fdULL, fn_0x1444);
    recomp_register_fn(0x170aULL, fn_0x1444);
    recomp_register_fn(0x1716ULL, fn_0x1444);
    recomp_register_fn(0x1721ULL, fn_0x1444);
    recomp_register_fn(0x1726ULL, fn_0x1444);
    recomp_register_fn(0x1731ULL, fn_0x1444);
    recomp_register_fn(0x173dULL, fn_0x1444);
    recomp_register_fn(0x1746ULL, fn_0x1444);
    recomp_register_fn(0x1756ULL, fn_0x1444);
    recomp_register_fn(0x1760ULL, fn_0x1444);
    recomp_register_fn(0x178fULL, fn_0x1444);
    recomp_register_fn(0x1798ULL, fn_0x1444);
    recomp_register_fn(0x17aeULL, fn_0x1444);
    recomp_register_fn(0x17bfULL, fn_0x1444);
    recomp_register_fn(0x17cbULL, fn_0x1444);
    recomp_register_fn(0x17d9ULL, fn_0x1444);
    recomp_register_fn(0x17f1ULL, fn_0x1444);
    recomp_register_fn(0x1803ULL, fn_0x1444);
    recomp_register_fn(0x180cULL, fn_0x1444);
    recomp_register_fn(0x1817ULL, fn_0x1444);
    recomp_register_fn(0x1827ULL, fn_0x1444);
    recomp_register_fn(0x183aULL, fn_0x1444);
    recomp_register_fn(0x1849ULL, fn_0x1444);
    recomp_register_fn(0x184cULL, fn_0x1444);
    recomp_register_fn(0x1858ULL, fn_0x1444);
    recomp_register_fn(0x1864ULL, fn_0x1444);
    recomp_register_fn(0x1867ULL, fn_0x1444);
    recomp_register_fn(0x186eULL, fn_0x1444);
    recomp_register_fn(0x1871ULL, fn_0x1444);
    recomp_register_fn(0x1886ULL, fn_0x1444);
    recomp_register_fn(0x18a5ULL, fn_0x1444);
    recomp_register_fn(0x18aaULL, fn_0x1444);
    recomp_register_fn(0x18b2ULL, fn_0x1444);
    recomp_register_fn(0x18b6ULL, fn_0x1444);
    recomp_register_fn(0x18dcULL, fn_0x1444);
    recomp_register_fn(0x18e4ULL, fn_0x1444);
    recomp_register_fn(0x18efULL, fn_0x1444);
    recomp_register_fn(0x18f4ULL, fn_0x1444);
    recomp_register_fn(0x1900ULL, fn_0x1444);
    recomp_register_fn(0x1928ULL, fn_0x1444);
    recomp_register_fn(0x1964ULL, fn_0x1444);
    recomp_register_fn(0x1971ULL, fn_0x1444);
    recomp_register_fn(0x1981ULL, fn_0x1444);
    recomp_register_fn(0x198aULL, fn_0x1444);
    recomp_register_fn(0x1993ULL, fn_0x1444);
    recomp_register_fn(0x1999ULL, fn_0x1444);
    recomp_register_fn(0x19a3ULL, fn_0x1444);
    recomp_register_fn(0x19d3ULL, fn_0x1444);
    recomp_register_fn(0x19e2ULL, fn_0x1444);
    recomp_register_fn(0x1a07ULL, fn_0x1444);
    recomp_register_fn(0x1a3cULL, fn_0x1444);
    recomp_register_fn(0x1a59ULL, fn_0x1444);
    recomp_register_fn(0x1a7fULL, fn_0x1444);
    recomp_register_fn(0x1a8dULL, fn_0x1444);
    recomp_register_fn(0x1aa1ULL, fn_0x1444);
    recomp_register_fn(0x1abfULL, fn_0x1444);
    recomp_register_fn(0x1adbULL, fn_0x1444);
    recomp_register_fn(0x1af3ULL, fn_0x1444);
    recomp_register_fn(0x1b2eULL, fn_0x1444);
    recomp_register_fn(0x1b61ULL, fn_0x1444);
    recomp_register_fn(0x1b9dULL, fn_0x1444);
    recomp_register_fn(0x1c03ULL, fn_0x1444);
    recomp_register_fn(0x1c14ULL, fn_0x1444);
    recomp_register_fn(0x1c18ULL, fn_0x1444);
    recomp_register_fn(0x1c28ULL, fn_0x1444);
    recomp_register_fn(0x1c57ULL, fn_0x1444);
    recomp_register_fn(0x1c7dULL, fn_0x1444);
    recomp_register_fn(0x1cbeULL, fn_0x1444);
    recomp_register_fn(0x1cccULL, fn_0x1444);
    recomp_register_fn(0x1cdfULL, fn_0x1444);
    recomp_register_fn(0x1cf6ULL, fn_0x1444);
    recomp_register_fn(0x1d07ULL, fn_0x1444);
    recomp_register_fn(0x1d16ULL, fn_0x1444);
    recomp_register_fn(0x1d23ULL, fn_0x1444);
    recomp_register_fn(0x1d37ULL, fn_0x1444);
    recomp_register_fn(0x1d49ULL, fn_0x1444);
    recomp_register_fn(0x1d76ULL, fn_0x1444);
    recomp_register_fn(0x1d88ULL, fn_0x1444);
    recomp_register_fn(0x1d8eULL, fn_0x1444);
    recomp_register_fn(0x1dacULL, fn_0x1444);
    recomp_register_fn(0x1dbcULL, fn_0x1444);
    recomp_register_fn(0x1dd3ULL, fn_0x1444);
    recomp_register_fn(0x1ddcULL, fn_0x1444);
    recomp_register_fn(0x1de5ULL, fn_0x1444);
    recomp_register_fn(0x1dedULL, fn_0x1444);
    recomp_register_fn(0x1df3ULL, fn_0x1444);
    recomp_register_fn(0x1dfdULL, fn_0x1444);
    recomp_register_fn(0x1e15ULL, fn_0x1444);
    recomp_register_fn(0x1e22ULL, fn_0x1444);
    recomp_register_fn(0x1e2eULL, fn_0x1444);
    recomp_register_fn(0x1e40ULL, fn_0x1444);
    recomp_register_fn(0x1e68ULL, fn_0x1444);
    recomp_register_fn(0x1e6bULL, fn_0x1444);
    recomp_register_fn(0x1e77ULL, fn_0x1444);
    recomp_register_fn(0x1e96ULL, fn_0x1444);
    recomp_register_fn(0x1e9dULL, fn_0x1444);
    recomp_register_fn(0x1eadULL, fn_0x1444);
    recomp_register_fn(0x1eb6ULL, fn_0x1444);
    recomp_register_fn(0x1ebbULL, fn_0x1444);
    recomp_register_fn(0x1ed5ULL, fn_0x1444);
    recomp_register_fn(0x1ee2ULL, fn_0x1444);
    recomp_register_fn(0x1efaULL, fn_0x1444);
    recomp_register_fn(0x1f1aULL, fn_0x1444);
    recomp_register_fn(0x1f2dULL, fn_0x1444);
    recomp_register_fn(0x1f39ULL, fn_0x1444);
    recomp_register_fn(0x1f43ULL, fn_0x1444);
    recomp_register_fn(0x1f4fULL, fn_0x1444);
    recomp_register_fn(0x1f63ULL, fn_0x1444);
    recomp_register_fn(0x1f7aULL, fn_0x1444);
    recomp_register_fn(0x1f8dULL, fn_0x1444);
    recomp_register_fn(0x1fb7ULL, fn_0x1444);
    recomp_register_fn(0x1fc0ULL, fn_0x1444);
    recomp_register_fn(0x1fd6ULL, fn_0x1444);
    recomp_register_fn(0x1fefULL, fn_0x1444);
    recomp_register_fn(0x2015ULL, fn_0x1444);
    recomp_register_fn(0x2083ULL, fn_0x1444);
    recomp_register_fn(0x2097ULL, fn_0x1444);
    recomp_register_fn(0x209fULL, fn_0x1444);
    recomp_register_fn(0x20b0ULL, fn_0x1444);
    recomp_register_fn(0x20bfULL, fn_0x1444);
    recomp_register_fn(0x20eeULL, fn_0x1444);
    recomp_register_fn(0x20f2ULL, fn_0x1444);
    recomp_register_fn(0x20f9ULL, fn_0x1444);
    recomp_register_fn(0x2101ULL, fn_0x1444);
    recomp_register_fn(0x2109ULL, fn_0x1444);
    recomp_register_fn(0x2114ULL, fn_0x1444);
    recomp_register_fn(0x212bULL, fn_0x1444);
    recomp_register_fn(0x212dULL, fn_0x1444);
    recomp_register_fn(0x2138ULL, fn_0x1444);
    recomp_register_fn(0x2149ULL, fn_0x1444);
    recomp_register_fn(0x2165ULL, fn_0x1444);
    recomp_register_fn(0x217fULL, fn_0x1444);
    recomp_register_fn(0x218aULL, fn_0x1444);
    recomp_register_fn(0x2192ULL, fn_0x1444);
    recomp_register_fn(0x21a7ULL, fn_0x1444);
    recomp_register_fn(0x21b9ULL, fn_0x1444);
    recomp_register_fn(0x21d1ULL, fn_0x1444);
    recomp_register_fn(0x21f3ULL, fn_0x1444);
    recomp_register_fn(0x21faULL, fn_0x1444);
    recomp_register_fn(0x2201ULL, fn_0x1444);
    recomp_register_fn(0x2210ULL, fn_0x1444);
    recomp_register_fn(0x2212ULL, fn_0x1444);
    recomp_register_fn(0x224eULL, fn_0x1444);
    recomp_register_fn(0x2260ULL, fn_0x1444);
    recomp_register_fn(0x2277ULL, fn_0x1444);
    recomp_register_fn(0x227cULL, fn_0x1444);
    recomp_register_fn(0x2288ULL, fn_0x1444);
    recomp_register_fn(0x22d1ULL, fn_0x1444);
    recomp_register_fn(0x2316ULL, fn_0x1444);
    recomp_register_fn(0x2340ULL, fn_0x1444);
    recomp_register_fn(0x2372ULL, fn_0x1444);
    recomp_register_fn(0x23c5ULL, fn_0x1444);
    recomp_register_fn(0x23feULL, fn_0x1444);
    recomp_register_fn(0x2407ULL, fn_0x1444);
    recomp_register_fn(0x2419ULL, fn_0x1444);
    recomp_register_fn(0x2423ULL, fn_0x1444);
    recomp_register_fn(0x242aULL, fn_0x1444);
    recomp_register_fn(0x242fULL, fn_0x1444);
    recomp_register_fn(0x2437ULL, fn_0x1444);
    recomp_register_fn(0x245fULL, fn_0x1444);
    recomp_register_fn(0x2466ULL, fn_0x1444);
    recomp_register_fn(0x2470ULL, fn_0x1444);
    recomp_register_fn(0x2474ULL, fn_0x1444);
    recomp_register_fn(0x2479ULL, fn_0x1444);
    recomp_register_fn(0x2488ULL, fn_0x1444);
    recomp_register_fn(0x249fULL, fn_0x1444);
    recomp_register_fn(0x24a4ULL, fn_0x1444);
    recomp_register_fn(0x24b4ULL, fn_0x1444);
    recomp_register_fn(0x24d4ULL, fn_0x1444);
    recomp_register_fn(0x24f2ULL, fn_0x1444);
    recomp_register_fn(0x250eULL, fn_0x1444);
    recomp_register_fn(0x2540ULL, fn_0x1444);
    recomp_register_fn(0x254fULL, fn_0x1444);
    recomp_register_fn(0x2590ULL, fn_0x1444);
    recomp_register_fn(0x259eULL, fn_0x1444);
    recomp_register_fn(0x25c6ULL, fn_0x1444);
    recomp_register_fn(0x25d6ULL, fn_0x1444);
    recomp_register_fn(0x25f3ULL, fn_0x1444);
    recomp_register_fn(0x25fcULL, fn_0x1444);
    recomp_register_fn(0x2601ULL, fn_0x1444);
    recomp_register_fn(0x260aULL, fn_0x1444);
    recomp_register_fn(0x261dULL, fn_0x1444);
    recomp_register_fn(0x262fULL, fn_0x1444);
    recomp_register_fn(0x264dULL, fn_0x1444);
    recomp_register_fn(0x265fULL, fn_0x1444);
    recomp_register_fn(0x2663ULL, fn_0x1444);
    recomp_register_fn(0x266bULL, fn_0x1444);
    recomp_register_fn(0x2672ULL, fn_0x1444);
    recomp_register_fn(0x269cULL, fn_0x1444);
    recomp_register_fn(0x269eULL, fn_0x1444);
    recomp_register_fn(0x26aeULL, fn_0x1444);
    recomp_register_fn(0x26dbULL, fn_0x1444);
    recomp_register_fn(0x274fULL, fn_0x1444);
    recomp_register_fn(0x2766ULL, fn_0x1444);
    recomp_register_fn(0x276bULL, fn_0x1444);
    recomp_register_fn(0x2770ULL, fn_0x1444);
    recomp_register_fn(0x277bULL, fn_0x1444);
    recomp_register_fn(0x2792ULL, fn_0x1444);
    recomp_register_fn(0x27baULL, fn_0x1444);
    recomp_register_fn(0x2815ULL, fn_0x1444);
    recomp_register_fn(0x282aULL, fn_0x1444);
    recomp_register_fn(0x2851ULL, fn_0x1444);
    recomp_register_fn(0x2861ULL, fn_0x1444);
    recomp_register_fn(0x2887ULL, fn_0x1444);
    recomp_register_fn(0x288aULL, fn_0x1444);
    recomp_register_fn(0x2895ULL, fn_0x1444);
    recomp_register_fn(0x289dULL, fn_0x1444);
    recomp_register_fn(0x28a2ULL, fn_0x1444);
    recomp_register_fn(0x28c3ULL, fn_0x1444);
    recomp_register_fn(0x28f2ULL, fn_0x1444);
    recomp_register_fn(0x2900ULL, fn_0x1444);
    recomp_register_fn(0x2902ULL, fn_0x1444);
    recomp_register_fn(0x2907ULL, fn_0x1444);
    recomp_register_fn(0x2922ULL, fn_0x1444);
    recomp_register_fn(0x2932ULL, fn_0x1444);
    recomp_register_fn(0x2943ULL, fn_0x1444);
    recomp_register_fn(0x2953ULL, fn_0x1444);
    recomp_register_fn(0x296cULL, fn_0x1444);
    recomp_register_fn(0x297eULL, fn_0x1444);
    recomp_register_fn(0x298bULL, fn_0x1444);
    recomp_register_fn(0x29a1ULL, fn_0x1444);
    recomp_register_fn(0x29b7ULL, fn_0x1444);
    recomp_register_fn(0x29c9ULL, fn_0x1444);
    recomp_register_fn(0x29e1ULL, fn_0x1444);
    recomp_register_fn(0x29eeULL, fn_0x1444);
    recomp_register_fn(0x29f9ULL, fn_0x1444);
    recomp_register_fn(0x2a0cULL, fn_0x1444);
    recomp_register_fn(0x2a15ULL, fn_0x1444);
    recomp_register_fn(0x2a1dULL, fn_0x1444);
    recomp_register_fn(0x2a32ULL, fn_0x1444);
    recomp_register_fn(0x2a43ULL, fn_0x1444);
    recomp_register_fn(0x2a4eULL, fn_0x1444);
    recomp_register_fn(0x2a5fULL, fn_0x1444);
    recomp_register_fn(0x2a68ULL, fn_0x1444);
    recomp_register_fn(0x2ab8ULL, fn_0x1444);
    recomp_register_fn(0x2ac2ULL, fn_0x1444);
    recomp_register_fn(0x2acbULL, fn_0x1444);
    recomp_register_fn(0x2adcULL, fn_0x1444);
    recomp_register_fn(0x2ae4ULL, fn_0x1444);
    recomp_register_fn(0x2af8ULL, fn_0x1444);
    recomp_register_fn(0x2b16ULL, fn_0x1444);
    recomp_register_fn(0x2b35ULL, fn_0x1444);
    recomp_register_fn(0x2b40ULL, fn_0x1444);
    recomp_register_fn(0x2b5cULL, fn_0x1444);
    recomp_register_fn(0x2b8cULL, fn_0x1444);
    recomp_register_fn(0x2b9cULL, fn_0x1444);
    recomp_register_fn(0x2badULL, fn_0x1444);
    recomp_register_fn(0x2bb2ULL, fn_0x1444);
    recomp_register_fn(0x2bc6ULL, fn_0x1444);
    recomp_register_fn(0x2bf3ULL, fn_0x1444);
    recomp_register_fn(0x2bf5ULL, fn_0x1444);
    recomp_register_fn(0x2c00ULL, fn_0x1444);
    recomp_register_fn(0x2c14ULL, fn_0x1444);
    recomp_register_fn(0x2c1aULL, fn_0x1444);
    recomp_register_fn(0x2c2bULL, fn_0x1444);
    recomp_register_fn(0x2c5dULL, fn_0x1444);
    recomp_register_fn(0x2c73ULL, fn_0x1444);
    recomp_register_fn(0x2ca8ULL, fn_0x1444);
    recomp_register_fn(0x2cbbULL, fn_0x1444);
    recomp_register_fn(0x2cecULL, fn_0x1444);
    recomp_register_fn(0x2d0aULL, fn_0x1444);
    recomp_register_fn(0x2d13ULL, fn_0x1444);
    recomp_register_fn(0x2d22ULL, fn_0x1444);
    recomp_register_fn(0x2d4fULL, fn_0x1444);
    recomp_register_fn(0x2d54ULL, fn_0x1444);
    recomp_register_fn(0x2d5dULL, fn_0x1444);
    recomp_register_fn(0x2d5fULL, fn_0x1444);
    recomp_register_fn(0x2d6cULL, fn_0x1444);
    recomp_register_fn(0x2d78ULL, fn_0x1444);
    recomp_register_fn(0x2d7aULL, fn_0x1444);
    recomp_register_fn(0x2d80ULL, fn_0x1444);
    recomp_register_fn(0x2d90ULL, fn_0x1444);
    recomp_register_fn(0x2d96ULL, fn_0x1444);
    recomp_register_fn(0x2db8ULL, fn_0x1444);
    recomp_register_fn(0x2dc7ULL, fn_0x1444);
    recomp_register_fn(0x2dd1ULL, fn_0x1444);
    recomp_register_fn(0x2dd7ULL, fn_0x1444);
    recomp_register_fn(0x2dfaULL, fn_0x1444);
    recomp_register_fn(0x2e03ULL, fn_0x1444);
    recomp_register_fn(0x2e0cULL, fn_0x1444);
    recomp_register_fn(0x2e1dULL, fn_0x1444);
    recomp_register_fn(0x2e45ULL, fn_0x1444);
    recomp_register_fn(0x2e47ULL, fn_0x1444);
    recomp_register_fn(0x2e4eULL, fn_0x1444);
    recomp_register_fn(0x2e53ULL, fn_0x1444);
    recomp_register_fn(0x2e59ULL, fn_0x1444);
    recomp_register_fn(0x2e7eULL, fn_0x1444);
    recomp_register_fn(0x2e8fULL, fn_0x1444);
    recomp_register_fn(0x2e9cULL, fn_0x1444);
    recomp_register_fn(0x2ea1ULL, fn_0x1444);
    recomp_register_fn(0x2ec9ULL, fn_0x1444);
    recomp_register_fn(0x2eccULL, fn_0x1444);
    recomp_register_fn(0x2ed5ULL, fn_0x1444);
    recomp_register_fn(0x2eddULL, fn_0x1444);
    recomp_register_fn(0x2f05ULL, fn_0x1444);
    recomp_register_fn(0x2f07ULL, fn_0x1444);
    recomp_register_fn(0x2f17ULL, fn_0x1444);
    recomp_register_fn(0x2f1cULL, fn_0x1444);
    recomp_register_fn(0x2f21ULL, fn_0x1444);
    recomp_register_fn(0x2f2bULL, fn_0x1444);
    recomp_register_fn(0x2f31ULL, fn_0x1444);
    recomp_register_fn(0x2f51ULL, fn_0x1444);
    recomp_register_fn(0x2f5eULL, fn_0x1444);
    recomp_register_fn(0x2f63ULL, fn_0x1444);
    recomp_register_fn(0x2f69ULL, fn_0x1444);
    recomp_register_fn(0x2f8dULL, fn_0x1444);
    recomp_register_fn(0x2f9eULL, fn_0x1444);
    recomp_register_fn(0x2fc3ULL, fn_0x1444);
    recomp_register_fn(0x2fd1ULL, fn_0x1444);
    recomp_register_fn(0x2fe2ULL, fn_0x1444);
    recomp_register_fn(0x3004ULL, fn_0x1444);
    recomp_register_fn(0x3021ULL, fn_0x1444);
    recomp_register_fn(0x3024ULL, fn_0x1444);
    recomp_register_fn(0x303cULL, fn_0x1444);
    recomp_register_fn(0x3041ULL, fn_0x1444);
    recomp_register_fn(0x307bULL, fn_0x1444);
    recomp_register_fn(0x307dULL, fn_0x1444);
    recomp_register_fn(0x30a5ULL, fn_0x1444);
    recomp_register_fn(0x30aaULL, fn_0x1444);
    recomp_register_fn(0x30c5ULL, fn_0x1444);
    recomp_register_fn(0x30caULL, fn_0x1444);
    recomp_register_fn(0x30d6ULL, fn_0x1444);
    recomp_register_fn(0x30dbULL, fn_0x1444);
    recomp_register_fn(0x30e0ULL, fn_0x1444);
    recomp_register_fn(0x30e7ULL, fn_0x1444);
    recomp_register_fn(0x30ecULL, fn_0x1444);
    recomp_register_fn(0x3100ULL, fn_0x1444);
    recomp_register_fn(0x3115ULL, fn_0x1444);
    recomp_register_fn(0x311cULL, fn_0x1444);
    recomp_register_fn(0x312dULL, fn_0x1444);
    recomp_register_fn(0x3135ULL, fn_0x1444);
    recomp_register_fn(0x314dULL, fn_0x1444);
    recomp_register_fn(0x3154ULL, fn_0x1444);
    recomp_register_fn(0x3158ULL, fn_0x1444);
    recomp_register_fn(0x3163ULL, fn_0x1444);
    recomp_register_fn(0x316aULL, fn_0x1444);
    recomp_register_fn(0x3173ULL, fn_0x1444);
    recomp_register_fn(0x3178ULL, fn_0x1444);
    recomp_register_fn(0x3182ULL, fn_0x3182);
    recomp_register_fn(0x318eULL, fn_0x3182);
    recomp_register_fn(0x319eULL, fn_0x3182);
    recomp_register_fn(0x31a6ULL, fn_0x3182);
    recomp_register_fn(0x31b4ULL, fn_0x3182);
    recomp_register_fn(0x31c0ULL, fn_0x3182);
    recomp_register_fn(0x31c3ULL, fn_0x3182);
    recomp_register_fn(0x31c7ULL, fn_0x3182);
    recomp_register_fn(0x31d7ULL, fn_0x3182);
    recomp_register_fn(0x31e9ULL, fn_0x3182);
    recomp_register_fn(0x31f5ULL, fn_0x3182);
    recomp_register_fn(0x3206ULL, fn_0x3182);
    recomp_register_fn(0x3212ULL, fn_0x3182);
    recomp_register_fn(0x3223ULL, fn_0x3182);
    recomp_register_fn(0x322fULL, fn_0x3182);
    recomp_register_fn(0x3240ULL, fn_0x3182);
    recomp_register_fn(0x324cULL, fn_0x3182);
    recomp_register_fn(0x325dULL, fn_0x3182);
    recomp_register_fn(0x3276ULL, fn_0x3182);
    recomp_register_fn(0x3282ULL, fn_0x3182);
    recomp_register_fn(0x3293ULL, fn_0x3182);
    recomp_register_fn(0x329fULL, fn_0x3182);
    recomp_register_fn(0x32b0ULL, fn_0x3182);
    recomp_register_fn(0x32bcULL, fn_0x3182);
    recomp_register_fn(0x32cbULL, fn_0x3182);
    recomp_register_fn(0x32ceULL, fn_0x3182);
    recomp_register_fn(0x32cfULL, fn_0x3182);
    recomp_register_fn(0x32dbULL, fn_0x3182);
    recomp_register_fn(0x32e4ULL, fn_0x3182);
    recomp_register_fn(0x32f0ULL, fn_0x3182);
    recomp_register_fn(0x32f7ULL, fn_0x3182);
    recomp_register_fn(0x3303ULL, fn_0x3182);
    recomp_register_fn(0x330cULL, fn_0x3182);
    recomp_register_fn(0x3318ULL, fn_0x3182);
    recomp_register_fn(0x3320ULL, fn_0x3182);
    recomp_register_fn(0x332cULL, fn_0x3182);
    recomp_register_fn(0x3334ULL, fn_0x3182);
    recomp_register_fn(0x3340ULL, fn_0x3182);
    recomp_register_fn(0x3348ULL, fn_0x3348);
    recomp_register_fn(0x3359ULL, fn_0x3348);
    recomp_register_fn(0x3362ULL, fn_0x3348);
    recomp_register_fn(0x3387ULL, fn_0x3348);
    recomp_register_fn(0x338aULL, fn_0x3348);
    recomp_register_fn(0x338fULL, fn_0x3348);
    recomp_register_fn(0x339fULL, fn_0x3348);
    recomp_register_fn(0x33adULL, fn_0x3348);
    recomp_register_fn(0x33b2ULL, fn_0x3348);
    recomp_register_fn(0x33bfULL, fn_0x3348);
    recomp_register_fn(0x33ccULL, fn_0x33cc);
    recomp_register_fn(0x33e9ULL, fn_0x33cc);
    recomp_register_fn(0x33fdULL, fn_0x33cc);
    recomp_register_fn(0x340eULL, fn_0x33cc);
    recomp_register_fn(0x3412ULL, fn_0x33cc);
    recomp_register_fn(0x3418ULL, fn_0x33cc);
    recomp_register_fn(0x3429ULL, fn_0x33cc);
    recomp_register_fn(0x3436ULL, fn_0x33cc);
    recomp_register_fn(0x3439ULL, fn_0x3439);
    recomp_register_fn(0x3444ULL, fn_0x3444);
    recomp_register_fn(0x345bULL, fn_0x3444);
    recomp_register_fn(0x3479ULL, fn_0x3444);
    recomp_register_fn(0x3484ULL, fn_0x3484);
    recomp_register_fn(0x348cULL, fn_0x348c);
    recomp_register_fn(0x3495ULL, fn_0x348c);
    recomp_register_fn(0x349aULL, fn_0x348c);
    recomp_register_fn(0x34a8ULL, fn_0x348c);
    recomp_register_fn(0x34cdULL, fn_0x34cd);
    recomp_register_fn(0x34d3ULL, fn_0x34cd);
    recomp_register_fn(0x34dfULL, fn_0x34cd);
    recomp_register_fn(0x34e7ULL, fn_0x34cd);
    recomp_register_fn(0x34f1ULL, fn_0x34cd);
    recomp_register_fn(0x34fbULL, fn_0x34cd);
    recomp_register_fn(0x3505ULL, fn_0x34cd);
    recomp_register_fn(0x3514ULL, fn_0x34cd);
    recomp_register_fn(0x3518ULL, fn_0x3518);
    recomp_register_fn(0x352eULL, fn_0x352e);
    recomp_register_fn(0x353cULL, fn_0x353c);
    recomp_register_fn(0x3557ULL, fn_0x353c);
    recomp_register_fn(0x3563ULL, fn_0x353c);
    recomp_register_fn(0x357bULL, fn_0x353c);
    recomp_register_fn(0x3584ULL, fn_0x353c);
    recomp_register_fn(0x3589ULL, fn_0x353c);
    recomp_register_fn(0x3597ULL, fn_0x353c);
    recomp_register_fn(0x359fULL, fn_0x353c);
    recomp_register_fn(0x35aaULL, fn_0x353c);
    recomp_register_fn(0x35b0ULL, fn_0x353c);
    recomp_register_fn(0x35c7ULL, fn_0x353c);
    recomp_register_fn(0x35d3ULL, fn_0x353c);
    recomp_register_fn(0x35d5ULL, fn_0x353c);
    recomp_register_fn(0x35dfULL, fn_0x353c);
    recomp_register_fn(0x35e8ULL, fn_0x353c);
    recomp_register_fn(0x35fcULL, fn_0x353c);
    recomp_register_fn(0x3608ULL, fn_0x3608);
    recomp_register_fn(0x3636ULL, fn_0x3608);
    recomp_register_fn(0x3655ULL, fn_0x3608);
    recomp_register_fn(0x365fULL, fn_0x3608);
    recomp_register_fn(0x3670ULL, fn_0x3608);
    recomp_register_fn(0x3675ULL, fn_0x3608);
    recomp_register_fn(0x3680ULL, fn_0x3608);
    recomp_register_fn(0x3694ULL, fn_0x3694);
    recomp_register_fn(0x36a0ULL, fn_0x36a0);
    recomp_register_fn(0x36b3ULL, fn_0x36a0);
    recomp_register_fn(0x36b7ULL, fn_0x36a0);
    recomp_register_fn(0x36c4ULL, fn_0x36a0);
    recomp_register_fn(0x36cbULL, fn_0x36a0);
    recomp_register_fn(0x36d2ULL, fn_0x36a0);
    recomp_register_fn(0x36d3ULL, fn_0x36a0);
    recomp_register_fn(0x36ddULL, fn_0x36a0);
    recomp_register_fn(0x36ecULL, fn_0x36ec);
    recomp_register_fn(0x370eULL, fn_0x36ec);
    recomp_register_fn(0x3715ULL, fn_0x36ec);
    recomp_register_fn(0x372aULL, fn_0x36ec);
    recomp_register_fn(0x374fULL, fn_0x36ec);
    recomp_register_fn(0x3753ULL, fn_0x36ec);
    recomp_register_fn(0x377cULL, fn_0x36ec);
    recomp_register_fn(0x3784ULL, fn_0x3784);
    recomp_register_fn(0x3789ULL, fn_0x3784);
    recomp_register_fn(0x3799ULL, fn_0x3784);
    recomp_register_fn(0x379cULL, fn_0x379c);
    recomp_register_fn(0x37b0ULL, fn_0x379c);
    recomp_register_fn(0x37bbULL, fn_0x379c);
    recomp_register_fn(0x37c3ULL, fn_0x379c);
    recomp_register_fn(0x37dbULL, fn_0x379c);
    recomp_register_fn(0x37e5ULL, fn_0x379c);
    recomp_register_fn(0x37eeULL, fn_0x379c);
    recomp_register_fn(0x37f5ULL, fn_0x379c);
    recomp_register_fn(0x3811ULL, fn_0x379c);
    recomp_register_fn(0x381bULL, fn_0x379c);
    recomp_register_fn(0x381dULL, fn_0x379c);
    recomp_register_fn(0x3824ULL, fn_0x379c);
    recomp_register_fn(0x3834ULL, fn_0x379c);
    recomp_register_fn(0x3848ULL, fn_0x379c);
    recomp_register_fn(0x386eULL, fn_0x379c);
    recomp_register_fn(0x3878ULL, fn_0x3878);
    recomp_register_fn(0x3896ULL, fn_0x3896);
    recomp_register_fn(0x38afULL, fn_0x3896);
    recomp_register_fn(0x38b9ULL, fn_0x3896);
    recomp_register_fn(0x38e0ULL, shim_memcpy);
    recomp_register_fn(0x3c10ULL, shim_memmove);
    recomp_register_fn(0x42e0ULL, shim_memset);
    recomp_register_fn(0x43c0ULL, shim_strcmp);
    recomp_register_fn(0x4400ULL, shim_strlen);
    recomp_register_fn(0x4500ULL, fn_0x4500);
    recomp_register_fn(0x4530ULL, fn_0x4530);
    recomp_register_fn(0x4541ULL, fn_0x4530);
    recomp_register_fn(0x454aULL, fn_0x4530);
    recomp_register_fn(0x454fULL, fn_0x4530);
    recomp_register_fn(0x455aULL, fn_0x4530);
    recomp_register_fn(0x4570ULL, fn_0x4530);
    recomp_register_fn(0x4579ULL, fn_0x4530);
    recomp_register_fn(0x4582ULL, fn_0x4530);
    recomp_register_fn(0x4599ULL, fn_0x4530);
    recomp_register_fn(0x45a2ULL, fn_0x4530);
    recomp_register_fn(0x45abULL, fn_0x4530);
    recomp_register_fn(0x45c2ULL, fn_0x4530);
    recomp_register_fn(0x45cbULL, fn_0x4530);
    recomp_register_fn(0x45d4ULL, fn_0x4530);
    recomp_register_fn(0x45e7ULL, fn_0x4530);
    recomp_register_fn(0x45ecULL, fn_0x4530);
    recomp_register_fn(0x45f5ULL, fn_0x4530);
    recomp_register_fn(0x4608ULL, fn_0x4530);
    recomp_register_fn(0x460dULL, fn_0x4530);
    recomp_register_fn(0x4616ULL, fn_0x4530);
    recomp_register_fn(0x4629ULL, fn_0x4530);
    recomp_register_fn(0x462eULL, fn_0x4530);
    recomp_register_fn(0x4637ULL, fn_0x4530);
    recomp_register_fn(0x464aULL, fn_0x4530);
    recomp_register_fn(0x464fULL, fn_0x4530);
    recomp_register_fn(0x4658ULL, fn_0x4530);
    recomp_register_fn(0x4666ULL, fn_0x4530);
    recomp_register_fn(0x466cULL, fn_0x4530);
    recomp_register_fn(0x4675ULL, fn_0x4530);
    recomp_register_fn(0x467aULL, fn_0x4530);
    recomp_register_fn(0x4680ULL, fn_0x4530);
    recomp_register_fn(0x46b0ULL, fn_0x4530);
    recomp_register_fn(0x46cbULL, fn_0x4530);
    recomp_register_fn(0x46d9ULL, fn_0x4530);
    recomp_register_fn(0x46e4ULL, fn_0x4530);
    recomp_register_fn(0x46f0ULL, fn_0x4530);
    recomp_register_fn(0x46f5ULL, fn_0x4530);
    recomp_register_fn(0x46fdULL, fn_0x4530);
    recomp_register_fn(0x46ffULL, fn_0x4530);
    recomp_register_fn(0x4700ULL, fn_0x4700);
    recomp_register_fn(0x4709ULL, fn_0x4700);
    recomp_register_fn(0x4719ULL, fn_0x4700);
    recomp_register_fn(0x471eULL, fn_0x4700);
    recomp_register_fn(0x473aULL, fn_0x4700);
    recomp_register_fn(0x473eULL, fn_0x4700);
    recomp_register_fn(0x474aULL, fn_0x4700);
    recomp_register_fn(0x4768ULL, fn_0x4700);
    recomp_register_fn(0x4769ULL, fn_0x4769);
    recomp_register_fn(0x476eULL, fn_0x4769);
    recomp_register_fn(0x476fULL, fn_0x4769);
    recomp_register_fn(0x477cULL, shim___tls_get_addr);
    recomp_register_fn(0x4798ULL, fn_0x4798);
    recomp_register_fn(0x47a0ULL, fn_0x47a0);
    recomp_register_fn(0x47a8ULL, fn_0x47a8);
    recomp_register_fn(0x47ccULL, fn_0x47cc);
    recomp_register_fn(0x47cfULL, fn_0x47cf);
    recomp_register_fn(0x47d0ULL, fn_0x47d0);
    recomp_register_fn(0x47fbULL, fn_0x47d0);
    recomp_register_fn(0x4805ULL, fn_0x47d0);
    recomp_register_fn(0x4814ULL, fn_0x47d0);
    recomp_register_fn(0x4818ULL, fn_0x47d0);
    recomp_register_fn(0x481eULL, fn_0x47d0);
    recomp_register_fn(0x4827ULL, fn_0x47d0);
    recomp_register_fn(0x4833ULL, fn_0x47d0);
    recomp_register_fn(0x484fULL, fn_0x47d0);
    recomp_register_fn(0x4858ULL, fn_0x47d0);
    recomp_register_fn(0x4863ULL, fn_0x47d0);
    recomp_register_fn(0x4874ULL, fn_0x47d0);
    recomp_register_fn(0x487cULL, fn_0x47d0);
    recomp_register_fn(0x489bULL, fn_0x47d0);
    recomp_register_fn(0x489eULL, fn_0x47d0);
    recomp_register_fn(0x48aaULL, fn_0x47d0);
    recomp_register_fn(0x48aeULL, fn_0x47d0);
    recomp_register_fn(0x48caULL, fn_0x47d0);
    recomp_register_fn(0x48dcULL, fn_0x47d0);
    recomp_register_fn(0x48e7ULL, fn_0x47d0);
    recomp_register_fn(0x490cULL, fn_0x47d0);
    recomp_register_fn(0x490eULL, fn_0x47d0);
    recomp_register_fn(0x491bULL, fn_0x47d0);
    recomp_register_fn(0x4922ULL, fn_0x47d0);
    recomp_register_fn(0x4935ULL, fn_0x47d0);
    recomp_register_fn(0x4937ULL, fn_0x47d0);
    recomp_register_fn(0x4941ULL, fn_0x47d0);
    recomp_register_fn(0x494bULL, fn_0x47d0);
    recomp_register_fn(0x4956ULL, fn_0x4956);
    recomp_register_fn(0x496fULL, fn_0x4956);
    recomp_register_fn(0x4976ULL, fn_0x4956);
    recomp_register_fn(0x4981ULL, fn_0x4956);
    recomp_register_fn(0x4989ULL, fn_0x4989);
    recomp_register_fn(0x49bfULL, fn_0x49bf);
    recomp_register_fn(0x49f0ULL, fn_0x49f0);
    recomp_register_fn(0x49f1ULL, fn_0x49f1);
    recomp_register_fn(0x4a05ULL, fn_0x49f1);
    recomp_register_fn(0x4a0cULL, fn_0x49f1);
    recomp_register_fn(0x4a1dULL, fn_0x49f1);
    recomp_register_fn(0x4a2bULL, shim_exit);
    recomp_register_fn(0x4a48ULL, fn_0x4a48);
    recomp_register_fn(0x4a4bULL, fn_0x4a48);
    recomp_register_fn(0x4a70ULL, shim_open);
    recomp_register_fn(0x4a80ULL, shim_write);
    recomp_register_fn(0x4a90ULL, shim_close);
    recomp_register_fn(0x4aa0ULL, shim_read);
    recomp_register_fn(0x4ab0ULL, shim_unlink);
    recomp_register_fn(0x4ac0ULL, shim_pthread_rwlock_init);
    recomp_register_fn(0x4ad0ULL, shim_pthread_rwlock_wrlock);
    recomp_register_fn(0x4ae0ULL, shim_pthread_rwlock_unlock);
    recomp_register_fn(0x4af0ULL, shim_pthread_rwlock_rdlock);
    recomp_register_fn(0x4b00ULL, shim_pthread_rwlock_destroy);
    recomp_register_fn(0x4b10ULL, shim_scePthreadCreate);
    recomp_register_fn(0x4b20ULL, shim_sceKernelUsleep);
    recomp_register_fn(0x4b30ULL, shim_connect);
    recomp_register_fn(0x4b40ULL, shim_close);
    recomp_register_fn(0x4b50ULL, shim_scePthreadJoin);
    recomp_register_fn(0x4b60ULL, shim_setsockopt);
    recomp_register_fn(0x4b70ULL, shim_bind);
    recomp_register_fn(0x4b80ULL, shim_listen);
    recomp_register_fn(0x4b90ULL, shim_accept);
    recomp_register_fn(0x4ba0ULL, shim_error);
    recomp_register_fn(0x4bb0ULL, shim_open);
    recomp_register_fn(0x4bc0ULL, shim_recvfrom);
    recomp_register_fn(0x4bd0ULL, shim_sendto);
    recomp_register_fn(0x4be0ULL, shim_socket);
    recomp_register_fn(0x4bf0ULL, shim_fcntl);
    recomp_register_fn(0x4c00ULL, shim_writev);
    recomp_register_fn(0x4c10ULL, shim_write);
    recomp_register_fn(0x4c20ULL, shim_ioctl);
    recomp_register_fn(0x4c30ULL, shim_lseek);
    recomp_register_fn(0x4c40ULL, shim_exit);
    recomp_register_fn(0x4c50ULL, shim_poll);
}
