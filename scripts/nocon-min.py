#!/usr/bin/env python3
"""Drop NoContraction from float ops that provably have no fusion partner.

NoContraction only constrains mul+add -> fma fusion, but SPIRV-Cross lowers
EVERY decorated op to a [[clang::optnone]] noinline MSL helper -- a real
non-inlinable call per op in the per-pixel body. About half the decorations
in this kernel sit on ops with no reachable fusion partner, where the
decoration costs a call and buys nothing.

Safety rests on one property: NoContraction on EITHER member of a mul+add
pair blocks the fma, so a decoration may be dropped only when the op cannot
pair with anything. An FMul is kept unless EVERY consumer is on SINKS, an
allowlist of ops that can neither be an add nor route the value to one; an
FAdd/FSub is dropped when no operand is an FMul or a value-forwarding op,
which is sound exactly because the FMul rule already keeps every multiply
whose result escapes into something opaque.

The argument must hold for a NATIVE SPIR-V consumer, not just for the
SPIRV-Cross/MSL lowering this exists to speed up: off Metal the decorations
are the WHOLE bit-exactness contract (MVK_CONFIG_FAST_MATH_ENABLED does
nothing there, and Vulkan only guarantees correctly-rounded FAdd/FSub/FMul
-- fusion is the one thing left to forbid). Every rule is therefore an
ALLOWLIST that fails safe:
  - a consumer not on SINKS keeps its FMul, because "SPIRV-Cross does not
    wrap it" is an MSL fact, not a fusion fact. Composite construct/extract
    and Load/Store are the routes an MSL-only argument would have missed.
  - a NoContraction target whose opcode is not FMul/FAdd/FSub is dropped
    only if it is on DROPPABLE, ops that can be neither half of an fma.
  - FUSABLE names opcodes that hide a multiply-add of their own (OpDot,
    OpVectorTimesScalar, GLSL.std.450 Fma/FMix/Length/...). The kernel
    contains none, and the reasoning above assumes that, so one appearing
    is a hard error rather than a silent drop.

One semantic delta, deliberate: spvFMul(l,r) is fma(l,r,0), which yields
+0.0 where a plain l*r yields -0.0. Dropping the wrapper moves the GPU
toward the CPU interpreter, which uses a plain multiply. The gputri fuzz
gate is the oracle for that, not this comment.
"""
import collections
import re
import sys

FORWARDS = {"Phi", "Store", "Select", "CopyObject", "CopyMemory", "CopyLogical"}
PARTNERS = {"FAdd", "FSub"}
WRAPPED = ("FMul", "FAdd", "FSub")
# Consumers that cannot make a dropped FMul the multiply half of an fma:
# they are not adds and they do not forward the value anywhere def-use
# cannot follow. Comparisons and conversions consume it outright; the
# GLSL.std.450 entries are unary and exact.
SINKS = {"FMul", "FDiv", "FRem", "FMod", "FNegate",
         "FOrdEqual", "FOrdNotEqual", "FOrdLessThan", "FOrdGreaterThan",
         "FOrdLessThanEqual", "FOrdGreaterThanEqual",
         "FUnordEqual", "FUnordNotEqual", "FUnordLessThan",
         "FUnordGreaterThan", "FUnordLessThanEqual", "FUnordGreaterThanEqual",
         "IsNan", "IsInf", "ConvertFToS", "ConvertFToU", "FConvert",
         "ExtInst:Floor", "ExtInst:Ceil", "ExtInst:Trunc", "ExtInst:Round",
         "ExtInst:RoundEven", "ExtInst:Fract", "ExtInst:FAbs",
         "ExtInst:Sqrt", "ExtInst:InverseSqrt",
         "ExtInst:FMin", "ExtInst:FMax", "ExtInst:FClamp", "ExtInst:FSign"}
# neither half of an fma: integer ops (NoContraction is float-only), and
# float ops that are not a multiply and not an add
DROPPABLE = {"IAdd", "ISub", "IMul", "SDiv", "UDiv", "SRem", "SMod", "UMod",
             "ShiftLeftLogical", "ShiftRightLogical", "ShiftRightArithmetic",
             "FDiv", "FRem", "FMod", "FNegate", "FConvert", "ConvertFToS",
             "ConvertFToU", "ConvertSToF", "ConvertUToF"}
# opcodes that contain a multiply-add the decoration would have to guard
FUSABLE = {"Dot", "VectorTimesScalar", "VectorTimesMatrix", "MatrixTimesVector",
           "MatrixTimesScalar", "MatrixTimesMatrix",
           "ExtInst:Fma", "ExtInst:FMix", "ExtInst:Length", "ExtInst:Distance",
           "ExtInst:Normalize", "ExtInst:Cross", "ExtInst:Reflect",
           "ExtInst:Refract", "ExtInst:SmoothStep"}


def main(path_in, path_out):
    lines = open(path_in).read().split("\n")
    nocon, defs, nores = set(), {}, []
    for l in lines:
        m = re.match(r"\s*OpDecorate %(\S+) NoContraction", l)
        if m:
            nocon.add(m.group(1))
            continue
        m = re.match(r"\s*%(\S+) = OpExtInst %\S+ %\S+ (\w+)(.*)$", l)
        if m:
            defs[m.group(1)] = ("ExtInst:" + m.group(2),
                                re.findall(r"%(\S+)", m.group(3)))
            continue
        m = re.match(r"\s*%(\S+) = Op(\w+) %\S+(.*)$", l)
        if m:
            defs[m.group(1)] = (m.group(2), re.findall(r"%(\S+)", m.group(3)))
            continue
        m = re.match(r"\s*Op(\w+)(.*)$", l)
        if m and not l.strip().startswith(("OpDecorate", "OpMemberDecorate")):
            nores.append((m.group(1), re.findall(r"%(\S+)", m.group(2))))

    if not nocon:
        sys.exit("nocon-min: no NoContraction decorations found -- parse failed")

    cons = collections.defaultdict(set)
    for rid, (op, ops) in defs.items():
        for o in ops:
            cons[o].add(op)
    for op, ops in nores:
        for o in ops:
            cons[o].add(op)

    # the allowlist reasoning below assumes every multiply-add in the
    # module is a visible FMul/FAdd pair
    hidden = sorted({op for op, _ in defs.values()} & FUSABLE)
    if hidden:
        sys.exit("nocon-min: module contains fma-capable opcodes the drop "
                 "rules do not model: " + ", ".join(hidden))

    keep, unknown = set(), collections.Counter()
    for i in sorted(nocon):
        if i not in defs:
            # never drop a decoration whose defining instruction did not
            # parse: the unsafe default is silently losing bit-exactness
            sys.exit("nocon-min: undecodable NoContraction target %%%s" % i)
        op, ops = defs[i]
        if op not in WRAPPED:
            if op not in DROPPABLE:
                # unrecognised: keep, and say so. Fail-safe by default.
                unknown["Op" + op] += 1
                keep.add(i)
            continue
        if op == "FMul":
            escapes = cons.get(i, set()) - SINKS
            if escapes:
                keep.add(i)
                for c in escapes - PARTNERS - FORWARDS:
                    unknown["consumer Op" + c] += 1
        elif any(defs.get(o, ("", []))[0] in {"FMul"} | FORWARDS for o in ops):
            keep.add(i)
    for op, n in sorted(unknown.items()):
        sys.stderr.write("nocon-min: keeping %d NoContraction on unmodelled "
                         "%s\n" % (n, op))

    out, dropped = [], 0
    for l in lines:
        m = re.match(r"\s*OpDecorate %(\S+) NoContraction", l)
        if m and m.group(1) not in keep:
            dropped += 1
            continue
        out.append(l)
    open(path_out, "w").write("\n".join(out))
    sys.stderr.write("nocon-min: %d kept, %d dropped\n" % (len(keep), dropped))


main(sys.argv[1], sys.argv[2])
