#!/usr/bin/env python3
"""Compare native.bin (x86-64 under Rosetta 2) and asm.bin (lifted arm64) from lifter-difftest-asm.

Usage: compare.py native.bin asm.bin tests.txt [--low] [--any-nan] [--summary FILE]

tests.txt columns: index, guest address, signature, instruction text, flag rule, vector regs,
destination vector register.
Flag rule: letters of the flags compared (C Z S O P), "-" for none, or shift:OP:SIZE:COUNT,
resolved per trial from the masked shift count (the input CL is stored in DtState.pad); a
"deadco:" prefix drops CF and OF (the instruction was lifted with CF/OF dead).
Vector registers are compared over all 256 bits; differences in a register the instruction does
not write (column 7: destination vector register) are reported as "clobber" (scratch-register
aliasing, mostly YMM upper halves in v16-v31) rather than as a semantic difference of the
instruction itself.
NaNs are compared bit for bit. The one exception is the default NaN of an invalid operation:
x86 produces the negative quiet NaN (0xFFC00000 / 0xFFF8000000000000, "real indefinite") and
ARM the positive one (0x7FC00000 / 0x7FF8000000000000). Lanes that differ only that way are
counted as "nan-default" and reported separately, not as mismatches (see the Task 2b report for
the reasoning); --any-nan treats every pair of NaNs as equal.
x87: ST(0)..ST(7) are compared as doubles where the native tag word says the register holds a
value, together with TOP and the control word. The native registers are 80-bit: a result is
accepted within one double ulp of the native value converted to double (both roundings).
"""
import struct, sys
from collections import OrderedDict

TRIALS = 8
STATE = 16 * 8 + 8 + 4 + 4 + 16 * 32 + 8 * 8 + 4 + 2 + 2 + 4 + 4
X87 = 16 * 8 + 8 + 4 + 4 + 16 * 32  # offset of DtState.st
REC = 8 + STATE + 128
GPR = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
FLAGBIT = {"C": 0, "P": 2, "Z": 6, "S": 7, "O": 11}
CCNAMES = ["O", "NO", "B", "AE", "E", "NE", "BE", "A", "S", "NS", "P", "NP", "L", "GE", "LE", "G"]
# Per pair of condition codes: (name, flags it depends on, value of the even code).
_PAIRS = [("O", "O", lambda f: f["O"]), ("B", "C", lambda f: f["C"]), ("E", "Z", lambda f: f["Z"]),
          ("BE", "CZ", lambda f: f["C"] | f["Z"]), ("S", "S", lambda f: f["S"]), ("P", "P", lambda f: f["P"]),
          ("L", "SO", lambda f: f["S"] ^ f["O"]), ("LE", "ZSO", lambda f: f["Z"] | (f["S"] ^ f["O"]))]
CONDS = [p for p in _PAIRS for _ in (0, 1)]


def parse(buf, off):
    status, = struct.unpack_from("<I", buf, off)
    gpr = struct.unpack_from("<16Q", buf, off + 8)
    flags, mxcsr, cl = struct.unpack_from("<QII", buf, off + 8 + 128)
    ymm = [buf[off + 8 + 144 + 32 * i: off + 8 + 144 + 32 * (i + 1)] for i in range(16)]
    mem = buf[off + 8 + STATE: off + 8 + STATE + 128]
    st = struct.unpack_from("<8Q", buf, off + 8 + X87)
    ftop, fcw, fsw, ftag = struct.unpack_from("<IHHI", buf, off + 8 + X87 + 64)
    return status, gpr, flags, mxcsr, cl, ymm, mem, (st, ftop, fcw, fsw, ftag)


def flags_for(rule, cl):
    """Return (flags to compare, whether register/memory results are defined) for one trial."""
    if rule.startswith("deadco:"):  # CF and OF dead after the instruction: not compared
        fl, defined = flags_for(rule[len("deadco:"):], cl)
        return fl.replace("C", "").replace("O", ""), defined
    if not rule.startswith("shift:"):
        return ("" if rule == "-" else rule), True
    _, op, size, count = rule.split(":")
    size = int(size)
    c = cl if count == "cl" else int(count)
    c &= 0x3F if size == 64 else 0x1F
    if c == 0:
        return "CZSO", True  # flags unchanged
    one = "O" if c == 1 else ""  # OF is defined only for 1-bit shifts
    if op in ("SHL", "SHR", "SAR"):
        return ("C" if c < size else "") + "ZS" + one, True
    if op in ("ROL", "ROR", "RCL", "RCR"):
        return "CZS" + one, True  # ZF/SF unchanged
    if op in ("SHLD", "SHRD"):
        if c > size:
            return "", False  # result and flags undefined
        return "CZS" + one, True
    return "CZSO", True


# LAHF: AF and PF (AH bits 4 and 2) are not modeled by the lifter's NZCV flags (PF only after
# FP compares, where it is in V), so LAHF stores 0 there.
LAHF_UNMODELED = (1 << 12) | (1 << 10)

# Approximation instructions: x86 guarantees |relative error| <= 1.5 * 2^-12, the exact bits are
# implementation specific (Intel and AMD differ), so the lifted result only has to meet that bound.
APPROX = {"RCPPS", "RCPSS", "RSQRTPS", "RSQRTSS", "VRCPPS", "VRCPSS", "VRSQRTPS", "VRSQRTSS"}


def close_approx(ua, ub):
    fa = struct.unpack("<f", struct.pack("<I", ua))[0]
    fb = struct.unpack("<f", struct.pack("<I", ub))[0]
    if fa != fa or fb != fb or fa in (float("inf"), float("-inf")) or fa == 0:
        return False
    return abs(fb - fa) <= abs(fa) * 1.5 * 2.0 ** -12 * 2  # native is itself within 1.5*2^-12 of exact


def nan32(u):
    return (u & 0x7f800000) == 0x7f800000 and (u & 0x7fffff) != 0


def nan64u(u):
    return (u & 0x7ff0000000000000) == 0x7ff0000000000000 and (u & 0xfffffffffffff) != 0


def nan64(lo, hi):
    return nan64u(lo | (hi << 32))


X86_DEFAULT_NAN32, ARM_DEFAULT_NAN32 = 0xFFC00000, 0x7FC00000
X86_DEFAULT_NAN64, ARM_DEFAULT_NAN64 = 0xFFF8000000000000, 0x7FF8000000000000


# Rosetta 2 computes (V)HSUBPS/PD and (V)ADDSUBPS/PD subtractions as additions of the negated
# operand, so a NaN subtrahend comes back with its sign flipped; x86 returns a NaN operand
# unchanged (SDM; SUBPS under Rosetta 2 does). Lanes that differ only that way are accepted.
ROSETTA_NEG_NAN = {"HSUBPS", "VHSUBPS", "HSUBPD", "VHSUBPD", "ADDSUBPS", "VADDSUBPS", "ADDSUBPD", "VADDSUBPD"}


def ulp_close(ua, ub):
    """Two doubles (as u64) within one ulp of each other, both finite and of the same sign."""
    fa = struct.unpack("<d", struct.pack("<Q", ua))[0]
    fb = struct.unpack("<d", struct.pack("<Q", ub))[0]
    if fa != fa or fb != fb or abs(fa) == float("inf") or abs(fb) == float("inf") or (ua ^ ub) >> 63:
        return False
    return abs(ua - ub) <= 1


def main():
    args = sys.argv[1:]
    nat = open(args[0], "rb").read()
    lif = open(args[1], "rb").read()
    tests = [l.rstrip("\n").split("\t") for l in open(args[2])]
    low_only = "--low" in args
    ignore_nan = "--any-nan" in args
    summary_path = args[args.index("--summary") + 1] if "--summary" in args else None
    n = len(nat) // REC // TRIALS
    if len(lif) // REC // TRIALS != n or len(tests) != n:
        print("record count mismatch: native %d asm %d tests %d" % (n, len(lif) // REC // TRIALS, len(tests)))
        sys.exit(2)
    bad = 0
    native_faults = 0
    nan_default = OrderedDict()  # opcode -> tests whose only difference is the default NaN sign
    x87_ulp = OrderedDict()  # opcode -> tests accepted within the 80-bit/double tolerance
    x87_partial = OrderedDict()  # opcode -> FPREM tests with a native partial remainder
    rosetta_nan = OrderedDict()  # opcode -> tests whose NaN sign Rosetta 2 flips (ROSETTA_NEG_NAN)
    rosetta_cx = OrderedDict()  # opcode -> CMPXCHG tests checked against Rosetta 2's CMPXCHG deviations
    by_op = OrderedDict()  # opcode -> {kind -> [count, example]}
    for t in range(n):
        idx, addr, sig, text, rule, vregs, vdest = tests[t]
        dest = None if vdest == "-" else int(vdest)
        op = sig.split(" ")[0]
        if op == "LOCK":
            op = sig.split(" ")[1]
        approx = op in APPROX
        diffs = []
        kinds = set()
        native_fault = False
        saw_nan_default = False
        saw_ulp = False
        saw_partial = False
        saw_rosetta_nan = False
        saw_rosetta_cx = False
        cx32 = op == "CMPXCHG" and ",r32" in sig
        for k in range(TRIALS):
            o = (t * TRIALS + k) * REC
            s1, g1, f1, m1, cl, y1, mem1, x1 = parse(nat, o)
            s2, g2, f2, m2, _, y2, mem2, x2 = parse(lif, o)
            if s1:
                native_fault = True
                break
            if s2:
                diffs.append("lifted code crashed")
                kinds.add("crash")
                break
            fl, defined = flags_for(rule, cl)
            # CMPXCHG under Rosetta 2 (checked with a native test): the flags are those of
            # DEST - accumulator, not of accumulator - DEST (SDM, Bochs, QEMU, the C backend), and
            # a 32-bit compare that succeeds clears RAX[63:32] (the SDM writes the accumulator
            # only on failure). On a failed compare (ZF = 0) exactly one of each pair of
            # opposite orders is "below" / "less", so the lifted B and L conditions must be the
            # negation of the native ones; on success both orders give the same flags.
            cx_fail = op == "CMPXCHG" and not (f1 >> 6 & 1) and fl
            if cx_fail:
                want_l = "S" in fl and "O" in fl
                want_b = "C" in fl
                fl = "Z"
                if want_b and (f2 >> (32 + 2) & 1) != 1 - (f1 & 1):
                    diffs.append("CMPXCHG cond B: lifted %d, want !native %d" % (f2 >> 34 & 1, f1 & 1))
                    kinds.add("cond-B")
                if want_l and (f2 >> (32 + 12) & 1) != 1 - ((f1 >> 7 ^ f1 >> 11) & 1):
                    diffs.append("CMPXCHG cond L: lifted %d, want !native %d" % (f2 >> 44 & 1, (f1 >> 7 ^ f1 >> 11) & 1))
                    kinds.add("cond-L")
                # SF and OF of the reversed subtraction: with r = acc - dest != 0, -r has the
                # opposite sign and the same OF, except r = MIN (-r = r): SF = 1 on both sides
                # and OF opposite. Lifted S/O conditions must fit one of the two.
                if want_l:
                    sl, ol = f2 >> (32 + 8) & 1, f2 >> 32 & 1
                    sn, on = f1 >> 7 & 1, f1 >> 11 & 1
                    if not ((sl != sn and ol == on) or (sl == sn == 1 and ol != on)):
                        diffs.append("CMPXCHG SF/OF: lifted S=%d O=%d, native (reversed) S=%d O=%d" % (sl, ol, sn, on))
                        kinds.add("cond-SO")
                saw_rosetta_cx = True
            if defined:
                for i in range(16):
                    if (cx32 and i == 0 and f1 >> 6 & 1 and g1[0] != g2[0] and g1[0] == g2[0] & 0xFFFFFFFF):
                        saw_rosetta_cx = True
                        continue
                    if g1[i] != g2[i] and not (op == "LAHF" and i == 0 and
                                               (g1[i] ^ g2[i]) & ~LAHF_UNMODELED == 0):
                        diffs.append("%s: native %016x lifted %016x" % (GPR[i], g1[i], g2[i]))
                        kinds.add("gpr")
            for i in range(16):
                if y1[i] == y2[i]:
                    continue
                a = struct.unpack("<8I", y1[i]); b = struct.unpack("<8I", y2[i])
                same = [a[j] == b[j] or (ignore_nan and nan32(a[j]) and nan32(b[j])) or
                        (approx and i == dest and close_approx(a[j], b[j])) for j in range(8)]
                for j in range(8):
                    if not same[j] and a[j] == X86_DEFAULT_NAN32 and b[j] == ARM_DEFAULT_NAN32:
                        same[j] = saw_nan_default = True
                    if (not same[j] and op in ROSETTA_NEG_NAN and nan32(a[j]) and nan32(b[j]) and
                            a[j] ^ b[j] == 0x80000000 and "PS" in op):
                        same[j] = saw_rosetta_nan = True
                for j in range(0, 8, 2):
                    if same[j] and same[j + 1]:
                        continue
                    ua, ub = a[j] | a[j + 1] << 32, b[j] | b[j + 1] << 32
                    if (ignore_nan and nan64u(ua) and nan64u(ub)) or (ua == X86_DEFAULT_NAN64 and ub == ARM_DEFAULT_NAN64):
                        same[j] = same[j + 1] = True
                        saw_nan_default = saw_nan_default or not ignore_nan
                    elif (op in ROSETTA_NEG_NAN and "PD" in op and nan64u(ua) and nan64u(ub) and
                          ua ^ ub == 1 << 63):
                        same[j] = same[j + 1] = saw_rosetta_nan = True
                lanes = [j for j in range(8) if not same[j] and (j < 4 or not low_only)]
                if not lanes:
                    continue
                if i != dest:
                    kinds.add("clobber")
                else:
                    kinds.add("vec" if lanes[0] < 4 else "vec-hi")
                fa = struct.unpack("<8f", y1[i]); fb = struct.unpack("<8f", y2[i])
                diffs.append("%symm%d lanes %s: native %s lifted %s (f: %s vs %s)" % (
                    "CLOBBER " if i != dest else "", i, lanes,
                    " ".join("%08x" % a[j] for j in lanes), " ".join("%08x" % b[j] for j in lanes),
                    " ".join("%g" % fa[j] for j in lanes), " ".join("%g" % fb[j] for j in lanes)))
            if op == "PUSHFQ":
                # The pushed RFLAGS (window bytes 88..95): PF and AF are not modeled.
                mem1 = mem1[:88] + bytes([mem1[88] & ~0x14 & 0xff]) + mem1[89:]
                mem2 = mem2[:88] + bytes([mem2[88] & ~0x14 & 0xff]) + mem2[89:]
            if defined and mem1 != mem2:
                ix = [j for j in range(128) if mem1[j] != mem2[j]]
                diffs.append("mem bytes %d..%d differ: native %s lifted %s" % (
                    ix[0] - 32, ix[-1] - 32, mem1[ix[0]:ix[-1] + 1].hex(), mem2[ix[0]:ix[-1] + 1].hex()))
                kinds.add("mem")
            for name in fl:
                bit = FLAGBIT[name]
                if (f1 >> bit & 1) != (f2 >> bit & 1):
                    diffs.append("flag %sF native %d lifted %d" % (name, f1 >> bit & 1, f2 >> bit & 1))
                    kinds.add("flag-" + name + "F")
            # Every condition code as the lifted Jcc/SETcc/CMOVcc would evaluate it (bits 32-47 of
            # the lifted flags word) against the native EFLAGS, when its input flags are defined.
            nf = {name: (f1 >> bit) & 1 for name, bit in FLAGBIT.items()}
            for cc, (cname, need, fn) in enumerate(CONDS):
                if not set(need) <= set(fl):
                    continue
                want = fn(nf) ^ (cc & 1)
                got = (f2 >> (32 + cc)) & 1
                if want != got:
                    diffs.append("cond %s native %d lifted %d" % (CCNAMES[cc], want, got))
                    kinds.add("cond-" + CCNAMES[cc])
            if (m1 & ~0x3f) != (m2 & ~0x3f):
                diffs.append("mxcsr native %x lifted %x" % (m1, m2))
                kinds.add("mxcsr")
            # x87: TOP, control word, and every ST(i) the native tag word marks as holding a value.
            (st1, top1, cw1, sw1, tag1), (st2, top2, cw2, sw2, _) = x1, x2
            if top1 != top2:
                diffs.append("x87 TOP native %d lifted %d" % (top1, top2))
                kinds.add("x87-top")
            if cw1 != cw2:
                diffs.append("x87 FCW native %04x lifted %04x" % (cw1, cw2))
                kinds.add("x87-cw")
            partial = op in ("FPREM", "FPREM1") and sw1 & 0x400
            if partial:
                # x87 reports a partial remainder (C2 = 1, exponent difference >= 64); the
                # lifter completes the reduction (the result of the whole FPREM loop).
                saw_partial = True
            elif op in ("FPREM", "FPREM1") and (sw1 & 0x4300) != (sw2 & 0x4300):
                diffs.append("x87 FSW C3/C1/C0 (quotient bits) native %04x lifted %04x" % (sw1, sw2))
                kinds.add("x87-sw")
            if top1 == top2 and not partial:
                for i in range(8):
                    if not tag1 >> i & 1 or st1[i] == st2[i]:
                        continue
                    if st1[i] == X86_DEFAULT_NAN64 and st2[i] == ARM_DEFAULT_NAN64:
                        saw_nan_default = True
                        continue
                    if ignore_nan and nan64u(st1[i]) and nan64u(st2[i]):
                        continue
                    if ulp_close(st1[i], st2[i]):
                        saw_ulp = True
                        continue
                    da = struct.unpack("<d", struct.pack("<Q", st1[i]))[0]
                    db = struct.unpack("<d", struct.pack("<Q", st2[i]))[0]
                    diffs.append("x87 ST(%d): native %016x (%r) lifted %016x (%r)" % (i, st1[i], da, st2[i], db))
                    kinds.add("x87-st")
            if diffs:
                diffs.insert(0, "trial %d (input CL %d):" % (k, cl))
                break
        if native_fault:
            native_faults += 1
            continue
        if not diffs and saw_nan_default:
            nan_default[op] = nan_default.get(op, 0) + 1
        if not diffs and saw_ulp:
            x87_ulp[op] = x87_ulp.get(op, 0) + 1
        if not diffs and saw_partial:
            x87_partial[op] = x87_partial.get(op, 0) + 1
        if not diffs and saw_rosetta_nan:
            rosetta_nan[op] = rosetta_nan.get(op, 0) + 1
        if not diffs and saw_rosetta_cx:
            rosetta_cx[op] = rosetta_cx.get(op, 0) + 1
        if diffs:
            bad += 1
            print("FAIL #%s %s  | %s  [%s] flags=%s" % (idx, sig, text, addr, rule))
            for d in diffs[:7]:
                print("     ", d)
            ent = by_op.setdefault(op, {})
            for kd in sorted(kinds):
                if kd not in ent:
                    ent[kd] = [0, "%s  [%s]" % (text, addr)]
                ent[kd][0] += 1
    extra = ""
    if nan_default:
        extra += "; accepted: default-NaN sign in %d tests (%s)" % (
            sum(nan_default.values()), ", ".join("%s %d" % kv for kv in nan_default.items()))
    if x87_ulp:
        extra += "; accepted: x87 within 1 ulp of 80-bit in %d tests (%s)" % (
            sum(x87_ulp.values()), ", ".join("%s %d" % kv for kv in x87_ulp.items()))
    if rosetta_nan:
        extra += "; accepted: Rosetta 2 NaN sign flip in %d tests (%s)" % (
            sum(rosetta_nan.values()), ", ".join("%s %d" % kv for kv in rosetta_nan.items()))
    if rosetta_cx:
        extra += "; accepted: Rosetta 2 CMPXCHG flag order / RAX[63:32] in %d tests (%s)" % (
            sum(rosetta_cx.values()), ", ".join("%s %d" % kv for kv in rosetta_cx.items()))
    if x87_partial:
        extra += "; accepted: x87 partial remainder (native C2 = 1) in %d tests (%s)" % (
            sum(x87_partial.values()), ", ".join("%s %d" % kv for kv in x87_partial.items()))
    print("%d/%d tests differ (%d skipped: native fault)%s" % (bad, n, native_faults, extra))
    if summary_path:
        lines = ["| opcode | kind | tests | example |", "|---|---|---|---|"]
        for op in sorted(by_op):
            for kd, (cnt, ex) in sorted(by_op[op].items()):
                lines.append("| %s | %s | %d | `%s` |" % (op, kd, cnt, ex))
        with open(summary_path, "w") as fh:
            fh.write("%d/%d tests differ (%d skipped: native fault)%s\n\n" % (bad, n, native_faults, extra))
            fh.write("\n".join(lines) + "\n")


main()
