/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The memory viewer's disassembler: Zydis, with what 86Box's
 *          processors do differently from the modern instruction set it
 *          decodes worked around here, outside the library.
 *
 *          - The 8086 and 80186 (808x.c) and the NEC V20/V30 (vx0.c) alias
 *            opcodes later processors gave other meanings: 60-6F are the
 *            70-7F jumps on the 8086, 0F is POP CS (and on the NEC a prefix
 *            for its own instructions), C0/C1/C8/C9 are returns, F1 is
 *            LOCK, the unused ModR/M reg values of 8C/8E/8F/C6/C7/FE/FF
 *            repeat the used ones, shifts /6 are SETMO, and on the NEC
 *            64/65 are REPNC/REPC and D6 is XLAT.
 *          - The NEC's 8080 emulation mode runs 8080 code.
 *          - The 8086-class prefetch queue: the processor runs the bytes it
 *            has already fetched, not what memory holds now.
 *          - The 286 and later run what their opcode tables hold: an
 *            opcode a processor does not have raises #UD, and some mean
 *            what they did on that processor (LOADALL, UMOV, the Cyrix
 *            and MediaGX system management instructions, Virtual PC's
 *            0F 3F), not what they mean now (SYSCALL, SSE, VMX, three-byte
 *            opcode maps). VEX, EVEX and XOP are LES, LDS, BOUND and POP
 *            with a register operand, which raise #UD.
 *          - The library's decoder modes for extensions that reuse old
 *            encodings (MPX, CET, LZCNT/TZCNT, WBNOINVD, CLDEMOTE,
 *            instruction prefetch hints) are off: these processors have
 *            none of them.
 *
 *          Each byte is read once, and only bytes of the instruction being
 *          decoded are read.
 */
#include "qt_disasm.hpp"

#include <vector>

#include <QRegularExpression>
#include <QStringList>

#ifdef USE_DISASM
#    include <Zydis/Zydis.h>
#endif

namespace {

/* Reads an instruction's bytes on demand, each once. */
class Fetcher {
public:
    Fetcher(uint32_t csBase, uint32_t ip, bool wrap16, const uint8_t *queue, int queueLen)
        : csBase_(csBase)
        , ip_(ip)
        , wrap16_(wrap16)
        , queue_(queue)
        , queueLen_(queueLen)
    {
    }

    /* Byte i of the instruction; false if it cannot be read. */
    bool get(int i, uint8_t *val)
    {
        if (i >= 16)
            return false;
        while (count_ <= i) {
            uint8_t b;
            uint8_t cls = DEBUGGER_MEM_RAM;

            if (count_ < queueLen_)
                b = queue_[count_];
            else
                debugger_mem_read(DEBUGGER_SPACE_LINEAR, addr(count_), 1, &b, &cls);
            if ((cls != DEBUGGER_MEM_RAM) && (cls != DEBUGGER_MEM_ROM) && (cls != DEBUGGER_MEM_VIDEO)) {
                failCls_ = cls;
                return false;
            }
            bytes_[count_++] = b;
        }
        *val = bytes_[i];
        return true;
    }

    uint32_t addr(int i) const { return csBase_ + (wrap16_ ? ((ip_ + i) & 0xffff) : (ip_ + i)); }
    const uint8_t *bytes() const { return bytes_; }
    int            count() const { return count_; }
    int            fromQueue() const { return queueLen_; }
    uint8_t        failCls() const { return failCls_; }

private:
    uint32_t       csBase_;
    uint32_t       ip_;
    bool           wrap16_;
    const uint8_t *queue_;
    int            queueLen_;
    uint8_t        bytes_[16] {};
    int            count_   = 0;
    uint8_t        failCls_ = DEBUGGER_MEM_RAM;
};

/* What the library is given in place of the bytes: head (the prefixes and
   opcode as rewritten), then the original bytes from resume on, with the
   ModR/M byte's reg field optionally forced. */
struct View {
    std::vector<uint8_t> head;
    int                  resume   = 0;
    int                  modrmAt  = -1; /* original index of the ModR/M byte */
    int                  forceReg = -1;
};

struct Result {
    bool    ok  = false;
    int     len = 0; /* in original bytes */
    QString text;
};

const char *const reg8Names[8] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };

/* Words the library puts before the mnemonic. */
bool
isPrefixWord(const QString &w)
{
    static const char *const words[] = { "rep", "repe", "repz", "repne", "repnz", "lock", "xacquire", "xrelease", "bnd", "bht", "bhnt", "notrack" };
    for (const char *p : words) {
        if (w == QLatin1String(p))
            return true;
    }
    return false;
}

/* text with its mnemonic replaced. */
QString
renameMnemonic(const QString &text, const QString &name)
{
    QStringList words = text.split(QLatin1Char(' '));
    for (int i = 0; i < words.size(); i++) {
        if (!isPrefixWord(words[i])) {
            words[i] = name;
            break;
        }
    }
    return words.join(QLatin1Char(' '));
}

/* The operands of text: what follows the mnemonic. */
QString
operandsOf(const QString &text)
{
    QStringList words = text.split(QLatin1Char(' '));
    int         i     = 0;
    while ((i < words.size()) && isPrefixWord(words[i]))
        i++;
    return words.mid(i + 1).join(QLatin1Char(' '));
}

/* Bytes of displacement and SIB after a ModR/M byte, as the processor reads
   them; the SIB byte, if any, must be fetchable at modrmAt + 1. */
bool
modrmExtra(Fetcher &f, int modrmAt, bool addr32, int *extra)
{
    uint8_t m;
    if (!f.get(modrmAt, &m))
        return false;
    const int mod = m >> 6;
    const int rm  = m & 7;

    if (mod == 3) {
        *extra = 0;
        return true;
    }
    if (!addr32) {
        *extra = (mod == 1) ? 1 : ((mod == 2) || ((mod == 0) && (rm == 6))) ? 2 : 0;
        return true;
    }

    int n = 0;
    if (rm == 4) {
        uint8_t sib;
        if (!f.get(modrmAt + 1, &sib))
            return false;
        n = 1;
        if ((mod == 0) && ((sib & 7) == 5))
            n += 4;
    } else if ((mod == 0) && (rm == 5))
        n += 4;
    if (mod == 1)
        n += 1;
    else if (mod == 2)
        n += 4;
    *extra = n;
    return true;
}

#ifdef USE_DISASM
/* Lower case, with single spaces, and RETF as it is usually written
   (the library has "ret far"). */
QString
tidy(const char *text)
{
    const QStringList words = QString::fromLatin1(text).toLower().split(QLatin1Char(' '));
    QStringList       out;

    for (const QString &w : words) {
        if (!w.isEmpty())
            out.append(w);
    }
    for (int i = 0; (i + 1) < out.size(); i++) {
        if (!isPrefixWord(out[i])) {
            if ((out[i] == QLatin1String("ret")) && (out[i + 1] == QLatin1String("far"))) {
                out[i] = QStringLiteral("retf");
                out.removeAt(i + 1);
            }
            break;
        }
    }
    return out.join(QLatin1Char(' '));
}

/* Decodes the view with the library, feeding it one byte at a time so that
   no byte past the instruction's end is read. */
Result
libDecode(Fetcher &f, const View &v, const debugger_decode_info_t &info, bool code32, uint32_t ip)
{
    /* Extensions that give old encodings new meanings, which none of these
       processors have: BND and NOTRACK prefixes, LZCNT/TZCNT for REP
       BSR/BSF, WBNOINVD for REP WBINVD, and the NOP space hints. */
    static const ZydisDecoderMode modesOff[] = {
        ZYDIS_DECODER_MODE_MPX, ZYDIS_DECODER_MODE_CET, ZYDIS_DECODER_MODE_LZCNT, ZYDIS_DECODER_MODE_TZCNT,
        ZYDIS_DECODER_MODE_WBNOINVD, ZYDIS_DECODER_MODE_CLDEMOTE, ZYDIS_DECODER_MODE_IPREFETCH
    };
    Result                  r;
    ZydisDecoder            dec;
    ZydisFormatter          fmt;
    ZydisDecodedInstruction ix;
    ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];
    uint8_t                 buf[ZYDIS_MAX_INSTRUCTION_LENGTH];
    char                    text[256];
    const int               headLen = (int) v.head.size();

    if (!ZYAN_SUCCESS(ZydisDecoderInit(&dec, code32 ? ZYDIS_MACHINE_MODE_LEGACY_32 : ZYDIS_MACHINE_MODE_LEGACY_16,
                                       info.stack32 ? ZYDIS_STACK_WIDTH_32 : ZYDIS_STACK_WIDTH_16)))
        return r;
    for (const ZydisDecoderMode m : modesOff)
        ZydisDecoderEnableMode(&dec, m, ZYAN_FALSE);
    if (!ZYAN_SUCCESS(ZydisFormatterInit(&fmt, ZYDIS_FORMATTER_STYLE_INTEL)))
        return r;
    /* Memory operand sizes always, which the Cyrix and UMOV rewrites use,
       and 16-bit addresses as written ([bx+si], not [bx+si*1]). */
    ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_TRUE);
    ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_FORCE_SCALE_ONE, ZYAN_FALSE);
    ZydisFormatterSetProperty(&fmt, ZYDIS_FORMATTER_PROP_ADDR_PADDING_ABSOLUTE, ZYDIS_PADDING_DISABLED);

    for (int i = 0; (i < headLen) && (i < ZYDIS_MAX_INSTRUCTION_LENGTH); i++)
        buf[i] = v.head[i];

    for (int n = (headLen ? headLen : 1); n <= ZYDIS_MAX_INSTRUCTION_LENGTH; n++) {
        if (n > headLen) {
            const int orig = v.resume + (n - 1 - headLen);
            uint8_t   b;
            if (!f.get(orig, &b))
                return r;
            if ((orig == v.modrmAt) && (v.forceReg >= 0))
                b = (uint8_t) ((b & ~0x38) | (v.forceReg << 3));
            buf[n - 1] = b;
        }

        const ZyanStatus st = ZydisDecoderDecodeFull(&dec, buf, (ZyanUSize) n, &ix, ops);
        if (st == ZYDIS_STATUS_NO_MORE_DATA)
            continue;
        if (!ZYAN_SUCCESS(st))
            return r;

        const uint64_t rip = code32 ? ip : (ip & 0xffff);
        if (!ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&fmt, &ix, ops, ix.operand_count_visible, text, sizeof(text), rip, ZYAN_NULL)))
            qstrncpy(text, "?", sizeof(text));
        r.ok   = true;
        r.len  = v.resume + (ix.length - headLen);
        r.text = tidy(text);
        return r;
    }

    return r;
}
#else
Result
libDecode(Fetcher &, const View &, const debugger_decode_info_t &, bool, uint32_t)
{
    return Result();
}
#endif

/* The prefix bytes before index at, as the library is to see them. */
std::vector<uint8_t>
prefixHead(Fetcher &f, int at, bool nec)
{
    std::vector<uint8_t> head;
    for (int i = 0; i < at; i++) {
        uint8_t b = 0;
        f.get(i, &b); /* already fetched, so never fails */
        if (b == 0xf1)
            b = 0xf0; /* the LOCK alias */
        else if (nec && ((b == 0x64) || (b == 0x65) || (b == 0x0f)))
            continue; /* REPNC/REPC and the NEC's 0F are not the library's */
        head.push_back(b);
    }
    return head;
}

/* NEC REPC/REPNC, which the library does not know, written out. */
QString
necRepPrefix(Fetcher &f, int at)
{
    QString s;
    for (int i = 0; i < at; i++) {
        uint8_t b = 0;
        f.get(i, &b); /* already fetched, so never fails */
        if (b == 0x64)
            s += QStringLiteral("repnc ");
        else if (b == 0x65)
            s += QStringLiteral("repc ");
    }
    return s;
}

QString
dbText(Fetcher &f, int len)
{
    QStringList parts;
    for (int i = 0; i < len; i++) {
        uint8_t b;
        if (f.get(i, &b))
            parts << QString::asprintf("%02Xh", b);
    }
    return QStringLiteral("db ") + parts.join(QStringLiteral(", "));
}

/* An operand of the V20/V30 ModR/M form at index at, as the library prints
   it, by decoding opcode subst (FE: byte, FF: word) with reg 0 (INC). */
bool
modrmOperand(Fetcher &f, int at, uint8_t subst, const debugger_decode_info_t &info, bool code32, uint32_t ip, QString *out, int *len)
{
    View v;
    v.head     = prefixHead(f, at - 1, true);
    v.head.push_back(subst);
    v.resume   = at;
    v.modrmAt  = at;
    v.forceReg = 0;
    Result r   = libDecode(f, v, info, code32, ip);
    if (!r.ok)
        return false;
    *out = operandsOf(r.text);
    *len = r.len;
    return true;
}

/* ---- the 8080 ---- */

const char *const i8080Regs[8]  = { "b", "c", "d", "e", "h", "l", "m", "a" };
const char *const i8080Pairs[4] = { "b", "d", "h", "sp" };
const char *const i8080Cond[8]  = { "nz", "z", "nc", "c", "po", "pe", "p", "m" };
const char *const i8080Alu[8]   = { "add", "adc", "sub", "sbb", "ana", "xra", "ora", "cmp" };
const char *const i8080AluI[8]  = { "adi", "aci", "sui", "sbi", "ani", "xri", "ori", "cpi" };

/* As i8080.c runs it, with the NEC's ED escapes. */
void
decode8080(Fetcher &f, DisasmInsn &d)
{
    uint8_t op;
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    int     len = 1;

    if (!f.get(0, &op))
        return;

    auto imm8 = [&](int at) { return f.get(at, &b1); };
    auto imm16 = [&](int at) { return f.get(at, &b1) && f.get(at + 1, &b2); };
    auto w = [&]() { return QString::asprintf("%04Xh", b1 | (b2 << 8)); };

    switch (op >> 6) {
        case 0:
            switch (op & 7) {
                case 0:
                    d.text = QStringLiteral("nop");
                    if (op)
                        d.note = QStringLiteral("Undocumented NOP");
                    break;
                case 1:
                    if (op & 8)
                        d.text = QStringLiteral("dad %1").arg(i8080Pairs[(op >> 4) & 3]);
                    else {
                        if (!imm16(1))
                            return;
                        len    = 3;
                        d.text = QStringLiteral("lxi %1, %2").arg(i8080Pairs[(op >> 4) & 3], w());
                    }
                    break;
                case 2: {
                    static const char *const names[8] = { "stax b", "ldax b", "stax d", "ldax d", "shld", "lhld", "sta", "lda" };
                    const int                n        = (op >> 3) & 7;
                    if (n >= 4) {
                        if (!imm16(1))
                            return;
                        len    = 3;
                        d.text = QStringLiteral("%1 %2").arg(names[n], w());
                    } else
                        d.text = QString::fromLatin1(names[n]);
                    break;
                }
                case 3:
                    d.text = QStringLiteral("%1 %2").arg((op & 8) ? "dcx" : "inx", i8080Pairs[(op >> 4) & 3]);
                    break;
                case 4:
                    d.text = QStringLiteral("inr %1").arg(i8080Regs[(op >> 3) & 7]);
                    break;
                case 5:
                    d.text = QStringLiteral("dcr %1").arg(i8080Regs[(op >> 3) & 7]);
                    break;
                case 6:
                    if (!imm8(1))
                        return;
                    len    = 2;
                    d.text = QStringLiteral("mvi %1, %2").arg(i8080Regs[(op >> 3) & 7], QString::asprintf("%02Xh", b1));
                    break;
                default: {
                    static const char *const names[8] = { "rlc", "rrc", "ral", "rar", "daa", "cma", "stc", "cmc" };
                    d.text                            = QString::fromLatin1(names[(op >> 3) & 7]);
                    break;
                }
            }
            break;
        case 1:
            d.text = (op == 0x76) ? QStringLiteral("hlt") : QStringLiteral("mov %1, %2").arg(i8080Regs[(op >> 3) & 7], i8080Regs[op & 7]);
            break;
        case 2:
            d.text = QStringLiteral("%1 %2").arg(i8080Alu[(op >> 3) & 7], i8080Regs[op & 7]);
            break;
        default:
            switch (op & 7) {
                case 0:
                    d.text = QStringLiteral("r%1").arg(i8080Cond[(op >> 3) & 7]);
                    break;
                case 1:
                    if (!(op & 8))
                        d.text = QStringLiteral("pop %1").arg(((op >> 4) & 3) == 3 ? "psw" : i8080Pairs[(op >> 4) & 3]);
                    else if (op == 0xe9)
                        d.text = QStringLiteral("pchl");
                    else if (op == 0xf9)
                        d.text = QStringLiteral("sphl");
                    else {
                        d.text = QStringLiteral("ret");
                        if (op == 0xd9)
                            d.note = QStringLiteral("Undocumented RET");
                    }
                    break;
                case 2:
                    if (!imm16(1))
                        return;
                    len    = 3;
                    d.text = QStringLiteral("j%1 %2").arg(i8080Cond[(op >> 3) & 7], w());
                    break;
                case 3:
                    switch (op) {
                        case 0xc3:
                        case 0xcb:
                            if (!imm16(1))
                                return;
                            len    = 3;
                            d.text = QStringLiteral("jmp %1").arg(w());
                            if (op == 0xcb)
                                d.note = QStringLiteral("Undocumented JMP");
                            break;
                        case 0xd3:
                        case 0xdb:
                            if (!imm8(1))
                                return;
                            len    = 2;
                            d.text = QStringLiteral("%1 %2").arg((op == 0xd3) ? "out" : "in", QString::asprintf("%02Xh", b1));
                            break;
                        case 0xe3:
                            d.text = QStringLiteral("xthl");
                            break;
                        case 0xeb:
                            d.text = QStringLiteral("xchg");
                            break;
                        case 0xf3:
                            d.text = QStringLiteral("di");
                            break;
                        default:
                            d.text = QStringLiteral("ei");
                            break;
                    }
                    break;
                case 4:
                    if (!imm16(1))
                        return;
                    len    = 3;
                    d.text = QStringLiteral("c%1 %2").arg(i8080Cond[(op >> 3) & 7], w());
                    break;
                case 5:
                    if (!(op & 8)) {
                        d.text = QStringLiteral("push %1").arg(((op >> 4) & 3) == 3 ? "psw" : i8080Pairs[(op >> 4) & 3]);
                        break;
                    }
                    if (op == 0xed) {
                        /* The NEC's way back: CALLN and RETEM. */
                        if (!imm8(1))
                            return;
                        if (b1 == 0xed) {
                            if (!f.get(2, &b2))
                                return;
                            len    = 3;
                            d.text = QStringLiteral("calln %1").arg(QString::asprintf("%02Xh", b2));
                            d.note = QStringLiteral("NEC: native-mode interrupt from 8080 mode");
                            break;
                        }
                        if (b1 == 0xfd) {
                            len    = 2;
                            d.text = QStringLiteral("retem");
                            d.note = QStringLiteral("NEC: return from 8080 mode");
                            break;
                        }
                        if (!f.get(2, &b2))
                            return;
                        len    = 3;
                        d.text = QStringLiteral("call %1").arg(w());
                        d.note = QStringLiteral("Undocumented CALL");
                        break;
                    }
                    if (!imm16(1))
                        return;
                    len    = 3;
                    d.text = QStringLiteral("call %1").arg(w());
                    if (op != 0xcd)
                        d.note = QStringLiteral("Undocumented CALL");
                    break;
                case 6:
                    if (!imm8(1))
                        return;
                    len    = 2;
                    d.text = QStringLiteral("%1 %2").arg(i8080AluI[(op >> 3) & 7], QString::asprintf("%02Xh", b1));
                    break;
                default:
                    d.text = QStringLiteral("rst %1").arg((op >> 3) & 7);
                    break;
            }
            break;
    }

    d.len = len;
}

/* ---- the NEC V20/V30's own instructions, after its 0F prefix ---- */

void
decodeNec0F(Fetcher &f, int at, const debugger_decode_info_t &info, uint32_t ip, DisasmInsn &d)
{
    uint8_t op;
    if (!f.get(at, &op))
        return;

    /* Which take a ModR/M byte, as the V20/V30 has them. */
    const bool documented = ((op >= 0x10) && (op <= 0x1f)) || (op == 0x28) || (op == 0x2a) || (op == 0x31) || (op == 0x33) ||
                            (op == 0x39) || (op == 0x3b);
    const bool modrm      = debugger_nec_0f_modrm(op);
    const bool imm        = ((op >= 0x18) && (op <= 0x1f)) || (op == 0x39) || (op == 0x3b) || (op == 0xff);
    QString    name;
    QString    operands;
    int        len = at + 1;

    switch (op) {
        case 0x10 ... 0x1f: {
            static const char *const names[4] = { "test1", "clr1", "set1", "not1" };
            name                              = QString::fromLatin1(names[(op >> 1) & 3]);
            break;
        }
        case 0x20:
            name = QStringLiteral("add4s");
            break;
        case 0x22:
            name = QStringLiteral("sub4s");
            break;
        case 0x26:
            name = QStringLiteral("cmp4s");
            break;
        case 0x28:
            name = QStringLiteral("rol4");
            break;
        case 0x2a:
            name = QStringLiteral("ror4");
            break;
        case 0x31:
        case 0x39:
            name = QStringLiteral("ins");
            break;
        case 0x33:
        case 0x3b:
            name = QStringLiteral("ext");
            break;
        case 0xff:
            name = QStringLiteral("brkem");
            break;
        default:
            break;
    }

    if (modrm) {
        int     mlen;
        uint8_t m;
        /* Bit operations take a byte or a word; the rest byte registers. */
        const bool word = ((op >= 0x10) && (op <= 0x1f)) && (op & 1);
        if (!f.get(at + 1, &m))
            return;
        if (((op >= 0x10) && (op <= 0x1f)) || (op == 0x28) || (op == 0x2a)) {
            if (!modrmOperand(f, at + 1, word ? 0xff : 0xfe, info, false, ip, &operands, &mlen))
                return;
            len = mlen;
        } else {
            int extra;
            if (!modrmExtra(f, at + 1, false, &extra))
                return;
            len      = at + 2 + extra;
            operands = ((m >> 6) == 3) ? QStringLiteral("%1, %2").arg(reg8Names[m & 7], reg8Names[(m >> 3) & 7]) : QString();
        }
        if ((op >= 0x10) && (op <= 0x17))
            operands += QStringLiteral(", cl");
    }
    if (imm) {
        uint8_t i8;
        if (!f.get(len, &i8))
            return;
        len++;
        if (op == 0xff)
            operands = QString::asprintf("%02Xh", i8);
        else
            operands += (operands.isEmpty() ? QString() : QStringLiteral(", ")) + QString::asprintf("%d", i8 & ((op & 1) ? 15 : 7));
    }

    d.len  = len;
    d.text = necRepPrefix(f, at) + (name.isEmpty() ? QStringLiteral("(undefined)") : name) + (operands.isEmpty() ? QString() : (QLatin1Char(' ') + operands));
    if (name.isEmpty())
        d.note = QStringLiteral("NEC: undefined 0F opcode, runs as a no-op");
    else if (op == 0xff)
        d.note = QStringLiteral("NEC: enter 8080 emulation mode through the vector");
    if (documented != modrm)
        d.note += (d.note.isEmpty() ? QString() : QStringLiteral("; ")) +
                  (modrm ? QStringLiteral("86Box's V20/V30 core reads a ModR/M byte here") : QStringLiteral("86Box's V20/V30 core reads no ModR/M byte here"));
}

QString
fpuNote(const debugger_decode_info_t &info)
{
    if (info.fpu != DEBUGGER_FPU_NONE)
        return QString();
    if (info.fpu_trap)
        return QStringLiteral("No FPU: raises #NM (CR0.EM/TS set)");
    return QStringLiteral("No FPU: reads the operand and does nothing else");
}

/* The x87 opcodes whose meaning depends on which coprocessor it is. */
void
fpuOverride(Fetcher &f, int at, const debugger_decode_info_t &info, DisasmInsn &d)
{
    uint8_t op;
    uint8_t m;

    if ((info.fpu == DEBUGGER_FPU_NONE) || !f.get(at, &op) || (op != 0xdb) || !f.get(at + 1, &m))
        return;

    if ((m == 0xe0) || (m == 0xe1) || (m == 0xe4)) {
        if ((info.fpu == DEBUGGER_FPU_8087) && (m != 0xe4))
            d.text = (m == 0xe0) ? QStringLiteral("feni") : QStringLiteral("fdisi");
        else if ((info.fpu == DEBUGGER_FPU_287) && (m == 0xe4))
            d.text = QStringLiteral("fsetpm");
        else {
            d.text = QStringLiteral("fnop");
            d.note = QStringLiteral("Ignored by this coprocessor");
        }
    }
}

} // namespace

Disassembler::Disassembler()
{
    debugger_cpu_info_t cpu;

    debugger_decode_info(&info_);
    debugger_cpu_info(&cpu);
    csBase_ = cpu.cs_base;
}

uint32_t
Disassembler::next(uint32_t ip, int len) const
{
    ip += (len > 0) ? len : 1;
    return info_.wrap16 ? (ip & 0xffff) : ip;
}

DisasmInsn
Disassembler::decode(uint32_t ip, bool current)
{
    DisasmInsn d;
    uint8_t    queue[16];
    const int  queueLen = current ? debugger_prefetch_queue(queue, sizeof(queue)) : 0;
    Fetcher    f(csBase_, ip, info_.wrap16 || (info_.isa == DEBUGGER_ISA_I8080), queue, queueLen);
    const bool x86_86  = (info_.isa == DEBUGGER_ISA_8086) || (info_.isa == DEBUGGER_ISA_80186) || (info_.isa == DEBUGGER_ISA_V30);
    const bool nec     = info_.isa == DEBUGGER_ISA_V30;

    d.lin = f.addr(0);
    d.ip  = ip;

    auto finish = [&]() {
        if (d.len <= 0) {
            if (f.count() == 0) {
                d.text = (f.failCls() == DEBUGGER_MEM_UNMAPPED) ? QStringLiteral("(unmapped)") : QStringLiteral("(inaccessible)");
                d.len  = 0;
            } else {
                d.len  = f.count();
                d.text = QStringLiteral("(runs into memory that cannot be read)");
            }
        }
        for (int i = 0; (i < d.len) && (i < f.count()) && (i < 16); i++)
            d.bytes[i] = f.bytes()[i];
        if ((queueLen > 0) && (d.len > 0)) {
            /* Show where the queue and memory part ways. */
            for (int i = 0; (i < queueLen) && (i < d.len); i++) {
                uint8_t mem;
                uint8_t cls;
                debugger_mem_read(DEBUGGER_SPACE_LINEAR, f.addr(i), 1, &mem, &cls);
                if (((cls == DEBUGGER_MEM_RAM) || (cls == DEBUGGER_MEM_ROM)) && (mem != f.bytes()[i])) {
                    d.note += (d.note.isEmpty() ? QString() : QStringLiteral("; ")) +
                              QStringLiteral("Runs the prefetch queue's bytes; memory has since changed");
                    break;
                }
            }
        }
        return d;
    };
    auto invalid = [&](int len, const QString &why) {
        /* A byte that could not be read is not a bad opcode. */
        if (f.failCls() != DEBUGGER_MEM_RAM) {
            d.len = 0;
            return;
        }
        d.len     = len;
        d.text    = dbText(f, len);
        d.note    = why;
        d.invalid = true;
    };

    if (info_.isa == DEBUGGER_ISA_I8080) {
        decode8080(f, d);
        return finish();
    }

    /* Prefixes, as this processor has them. */
    int     at  = 0;
    int     map = 0;
    int     opsize = info_.code32 ? 0x300 : 0;
    uint8_t op;
    for (;;) {
        if (!f.get(at, &op))
            return finish();

        bool prefix = (op == 0x26) || (op == 0x2e) || (op == 0x36) || (op == 0x3e) || (op == 0xf0) || (op == 0xf2) || (op == 0xf3);
        if (x86_86)
            prefix |= (op == 0xf1) || (nec && ((op == 0x64) || (op == 0x65) || (op == 0x0f)));
        else if (info_.isa == DEBUGGER_ISA_286)
            prefix |= (op == 0xf1) && (debugger_opcode_kind(0, op, 0) == DEBUGGER_OP_LOCK);
        else
            prefix |= (op == 0x64) || (op == 0x65) || (op == 0x66) || (op == 0x67);

        /* The 286 and later raise #UD at an opcode they do not have. */
        if (!x86_86 && !prefix && (debugger_opcode_kind(0, op, opsize) == DEBUGGER_OP_ILLEGAL)) {
            invalid(at + 1, QStringLiteral("Not an instruction on this processor: #UD"));
            return finish();
        }
        if (!prefix)
            break;

        if (op == 0x66)
            opsize ^= 0x100;
        else if (op == 0x67)
            opsize ^= 0x200;
        else if (op == 0x0f)
            map = 1;
        at++;
        /* Only so many fit in an instruction the library will take; the
           processor takes each prefix on its own anyway. */
        if (at >= 14) {
            d.len  = 1;
            d.text = dbText(f, 1);
            d.note = QStringLiteral("A prefix, of a long run of them");
            return finish();
        }
    }

    const bool addr32 = !!(opsize & 0x200);
    const bool code32 = info_.code32;
    View       v;
    v.head   = prefixHead(f, at, nec);
    v.resume = at + 1;

    auto lib = [&](const View &view) {
        Result r = libDecode(f, view, info_, code32, ip);
        if (r.ok) {
            d.len  = r.len;
            d.text = (nec ? necRepPrefix(f, at) : QString()) + r.text;
            return true;
        }

        /* The library takes LOCK only where the 386 and later do; these
           processors may take it anywhere. Decode without it, then ask
           the processor. */
        View      unlocked = view;
        const int pre      = (int) unlocked.head.size(); /* the prefixes; F1 is F0 by now */
        bool      locked   = false;
        for (int i = 0; i < pre; i++) {
            if (unlocked.head[i] == 0xf0) {
                unlocked.head.erase(unlocked.head.begin() + i);
                locked = true;
                break;
            }
        }
        if (!locked || (f.failCls() != DEBUGGER_MEM_RAM))
            return false;
        r = libDecode(f, unlocked, info_, code32, ip);
        if (!r.ok)
            return false;

        uint8_t b[3] = { 0, 0, 0 };
        for (int i = 0; (i < 3) && ((at + i) < r.len); i++)
            f.get(at + i, &b[i]);
        d.len  = r.len;
        d.text = (nec ? necRepPrefix(f, at) : QString()) + QStringLiteral("lock ") + r.text;
        if (!debugger_lock_legal(b[0], b[1], b[2])) {
            d.invalid = true;
            d.note    = QStringLiteral("LOCK on an instruction that cannot be locked: #UD");
        } else if (!x86_86)
            d.note = QStringLiteral("The 286 locks any instruction");
        else
            d.note = QStringLiteral("The 8086 class locks any instruction");
        return true;
    };
    auto withOp = [&](uint8_t subst, int resume) {
        View w   = v;
        w.head.push_back(subst);
        w.resume = resume;
        return w;
    };
    auto modrmReg = [&](int idx, int *reg, int *mod) {
        uint8_t m;
        if (!f.get(idx, &m))
            return false;
        *reg = (m >> 3) & 7;
        if (mod)
            *mod = m >> 6;
        return true;
    };

    /* ---- the 8086, 80186 and NEC V20/V30 ---- */
    if (x86_86) {
        int reg;

        if (map) {
            decodeNec0F(f, at, info_, ip, d);
            return finish();
        }

        if ((op == 0x0f) && !nec) {
            d.len  = at + 1;
            d.text = QStringLiteral("pop cs");
            d.note = QStringLiteral("8086/80186: 0F is POP CS");
            return finish();
        }
        if ((op >= 0x60) && (op <= 0x6f)) {
            if (info_.isa == DEBUGGER_ISA_8086) {
                lib(withOp(op | 0x10, at + 1));
                d.note = QString::asprintf("8086: %02X is an alias of %02X", op, op | 0x10);
                return finish();
            }
            if ((info_.isa == DEBUGGER_ISA_80186) && (op >= 0x63) && (op <= 0x67)) {
                uint8_t dummy;
                if (!f.get(at + 1, &dummy))
                    return finish();
                invalid(at + 2, QStringLiteral("80186: undefined; 86Box skips it and the next byte"));
                return finish();
            }
            if (nec && ((op == 0x63) || (op == 0x66) || (op == 0x67))) {
                int extra;
                if (!modrmExtra(f, at + 1, false, &extra))
                    return finish();
                d.len  = at + 2 + extra;
                d.text = (op == 0x63) ? QStringLiteral("(undefined)") : QStringLiteral("fpo2");
                d.note = (op == 0x63) ? QStringLiteral("NEC: undefined, reads its ModR/M operand") : QStringLiteral("NEC: coprocessor escape");
                return finish();
            }
        }
        if (info_.isa == DEBUGGER_ISA_8086) {
            if ((op == 0xc0) || (op == 0xc1) || (op == 0xc8) || (op == 0xc9)) {
                lib(withOp(op | 0x02, at + 1));
                d.note = QString::asprintf("8086: %02X is an alias of %02X", op, op | 0x02);
                return finish();
            }
        }
        if (nec && (op == 0xd6)) {
            lib(withOp(0xd7, at + 1));
            d.note = QStringLiteral("NEC: D6 is an alias of XLAT");
            return finish();
        }

        switch (op) {
            case 0xd0 ... 0xd3:
            case 0xc0 ... 0xc1:
                /* 8086: C0/C1 were handled above. */
                if (!modrmReg(at + 1, &reg, nullptr))
                    return finish();
                if (reg == 6) {
                    View w     = withOp(op, at + 1);
                    w.modrmAt  = at + 1;
                    w.forceReg = 4;
                    if (lib(w)) {
                        d.text = renameMnemonic(d.text, QStringLiteral("setmo"));
                        d.note = QStringLiteral("Undocumented: sets the operand to all ones");
                    }
                    return finish();
                }
                break;
            case 0x62:
            case 0xc4:
            case 0xc5: {
                /* BOUND, LES and LDS (the 8086 took 62 above). The library
                   would take them as EVEX or VEX with a register operand. */
                int mod;
                if (!modrmReg(at + 1, &reg, &mod))
                    return finish();
                if (mod == 3) {
                    static const char *const regs[8] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
                    d.len  = at + 2;
                    d.text = QStringLiteral("%1 %2, ?").arg((op == 0x62) ? QLatin1String("bound") : ((op == 0xc4) ? QLatin1String("les") : QLatin1String("lds")), QLatin1String(regs[reg]));
                    d.note = QStringLiteral("Register operand: undefined");
                    return finish();
                }
                break;
            }
            case 0x8c:
            case 0x8e:
                if (!modrmReg(at + 1, &reg, nullptr))
                    return finish();
                if ((reg >= 4) || ((op == 0x8e) && (reg == 1))) {
                    static const char *const segs[4] = { "es", "cs", "ss", "ds" };
                    View                     w       = withOp(op, at + 1);
                    w.modrmAt                        = at + 1;
                    w.forceReg                       = ((reg & 3) == 1) ? 3 : (reg & 3); /* the library takes no MOV CS */
                    if (lib(w)) {
                        if ((reg & 3) == 1) {
                            const int ds = d.text.indexOf(QRegularExpression(QStringLiteral("\\bds\\b")));
                            if (ds >= 0)
                                d.text.replace(ds, 2, QLatin1String(segs[1]));
                        }
                        if (reg >= 4)
                            d.note = QStringLiteral("Segment register %1 is an alias of %2").arg(reg).arg(segs[reg & 3]);
                        else
                            d.note = QStringLiteral("8086-class: loads CS");
                    }
                    return finish();
                }
                break;
            case 0x8f:
            case 0xc6:
            case 0xc7:
                if (!modrmReg(at + 1, &reg, nullptr))
                    return finish();
                if (reg) {
                    View w     = withOp(op, at + 1);
                    w.modrmAt  = at + 1;
                    w.forceReg = 0;
                    if (lib(w))
                        d.note = QStringLiteral("ModR/M reg %1 is an alias of reg 0").arg(reg);
                    return finish();
                }
                break;
            case 0xfe:
            case 0xff:
                if (!modrmReg(at + 1, &reg, nullptr))
                    return finish();
                if ((op == 0xfe) && (reg >= 2)) {
                    static const char *const names[8] = { "inc", "dec", "call", "callf", "jmp", "jmpf", "push", "push" };
                    View                     w        = withOp(0xfe, at + 1);
                    w.modrmAt                         = at + 1;
                    w.forceReg                        = 0;
                    if (lib(w)) {
                        d.text = renameMnemonic(d.text, QString::fromLatin1(names[reg]));
                        d.note = QStringLiteral("FE /%1: FF /%1 on a byte").arg(reg);
                    }
                    return finish();
                }
                if ((op == 0xff) && (reg == 7)) {
                    View w     = withOp(0xff, at + 1);
                    w.modrmAt  = at + 1;
                    w.forceReg = 6;
                    if (lib(w))
                        d.note = QStringLiteral("FF /7 is an alias of PUSH (FF /6)");
                    return finish();
                }
                break;
            case 0xd8 ... 0xdf: {
                if (!lib(withOp(op, at + 1))) {
                    int extra;
                    if (!modrmExtra(f, at + 1, false, &extra))
                        return finish();
                    d.len  = at + 2 + extra;
                    d.text = QStringLiteral("esc %1").arg(op & 7);
                }
                fpuOverride(f, at, info_, d);
                if (d.note.isEmpty())
                    d.note = fpuNote(info_);
                return finish();
            }
            default:
                break;
        }

        if (!lib(withOp(op, at + 1)))
            invalid(at + 1, QStringLiteral("Not decodable"));
        return finish();
    }

    /* ---- the 286 and later, from their opcode tables ---- */
    if (op == 0x0f) {
        uint8_t op2;
        if (!f.get(at + 1, &op2))
            return finish();

        const int kind = debugger_opcode_kind(1, op2, opsize);
        /* MOV to and from the test registers, which the library lacks: the
           ModR/M byte always names a register, as for CRn and DRn. */
        if (((op2 == 0x24) || (op2 == 0x26)) && (kind != DEBUGGER_OP_ILLEGAL)) {
            static const char *const regs[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
            uint8_t                  m;
            if (!f.get(at + 2, &m))
                return finish();
            d.len  = at + 3;
            d.text = (op2 == 0x24) ? QStringLiteral("mov %1, tr%2").arg(QLatin1String(regs[m & 7])).arg((m >> 3) & 7)
                                   : QStringLiteral("mov tr%1, %2").arg((m >> 3) & 7).arg(QLatin1String(regs[m & 7]));
            return finish();
        }
        switch (kind) {
            case DEBUGGER_OP_ILLEGAL:
                invalid(at + 2, QStringLiteral("Not an instruction on this processor: #UD"));
                return finish();
            case DEBUGGER_OP_LOADALL:
            case DEBUGGER_OP_LOADALL386:
            case DEBUGGER_OP_SMINT:
            case DEBUGGER_OP_BB0_RESET:
            case DEBUGGER_OP_BB1_RESET:
            case DEBUGGER_OP_CPU_WRITE:
            case DEBUGGER_OP_CPU_READ: {
                static const char *const names[] = { "loadall", "loadall", "smint", "bb0_reset", "bb1_reset", "cpu_write", "cpu_read" };
                const int idx = (kind == DEBUGGER_OP_LOADALL) ? 0 : (kind == DEBUGGER_OP_LOADALL386) ? 1 : (kind == DEBUGGER_OP_SMINT) ? 2 :
                                (kind - DEBUGGER_OP_BB0_RESET + 3);
                d.len  = at + 2;
                d.text = QString::fromLatin1(names[idx]);
                if (kind == DEBUGGER_OP_LOADALL)
                    d.note = QStringLiteral("286 LOADALL (later SYSCALL)");
                else if (kind == DEBUGGER_OP_LOADALL386)
                    d.note = QStringLiteral("386 LOADALL (later SYSRET)");
                else
                    d.note = QStringLiteral("Cyrix system management");
                return finish();
            }
            case DEBUGGER_OP_VPCEXT: {
                uint8_t a;
                uint8_t b;
                if (!f.get(at + 2, &a) || !f.get(at + 3, &b))
                    return finish();
                d.len  = at + 4;
                d.text = QString::asprintf("vpcext %02Xh, %02Xh", a, b);
                d.note = info_.vpc ? QStringLiteral("Virtual PC extension") : QStringLiteral("Virtual PC extension: #UD on other machines");
                return finish();
            }
            case DEBUGGER_OP_UMOV:
                /* 0F 10-13 are MOV 88-8B on the bus as user memory. */
                if (lib(withOp(0x88 + (op2 - 0x10), at + 2))) {
                    d.text = renameMnemonic(d.text, QStringLiteral("umov"));
                    d.note = QStringLiteral("UMOV (later SSE moves)");
                }
                return finish();
            case DEBUGGER_OP_RDSHR:
            case DEBUGGER_OP_WRSHR:
            case DEBUGGER_OP_SVDC:
            case DEBUGGER_OP_RSDC:
            case DEBUGGER_OP_SVLDT:
            case DEBUGGER_OP_RSLDT:
            case DEBUGGER_OP_SVTS:
            case DEBUGGER_OP_RSTS: {
                static const char *const names[] = { "rdshr", "wrshr", "svdc", "rsdc", "svldt", "rsldt", "svts", "rsts" };
                const int                idx     = (kind <= DEBUGGER_OP_WRSHR) ? (kind - DEBUGGER_OP_RDSHR) : (kind - DEBUGGER_OP_SVDC + 2);
                View                     w;
                if ((kind == DEBUGGER_OP_SVDC) || (kind == DEBUGGER_OP_RSDC)) {
                    /* m80 and a segment register, as MOV Ew, Sw has them. */
                    w = withOp(0x8c, at + 2);
                } else {
                    /* One operand, as PUSH Ev has it. */
                    w          = withOp(0xff, at + 2);
                    w.modrmAt  = at + 2;
                    w.forceReg = 6;
                }
                if (lib(w)) {
                    QString ops = operandsOf(d.text);
                    if (kind >= DEBUGGER_OP_SVDC)
                        ops.replace(QRegularExpression(QStringLiteral("\\b(word|dword) ptr")), QStringLiteral("tbyte ptr"));
                    if (kind == DEBUGGER_OP_RSDC) {
                        const int comma = ops.lastIndexOf(QLatin1String(", "));
                        if (comma > 0)
                            ops = ops.mid(comma + 2) + QStringLiteral(", ") + ops.left(comma);
                    }
                    d.text = QString::fromLatin1(names[idx]) + QLatin1Char(' ') + ops;
                    d.note = QStringLiteral("Cyrix system management (later VMX/MMX/GETSEC)");
                }
                return finish();
            }
            case DEBUGGER_OP_HINT_NOP:
                if (lib(withOp(0x0f, at + 1)) && !d.text.startsWith(QLatin1String("nop")))
                    d.text = renameMnemonic(d.text, QStringLiteral("nop"));
                d.note = QStringLiteral("Hint NOP on this processor");
                return finish();
            default: {
                /* 86Box has no SSE: 66 only picks the operand size half of
                   the tables, and REP before 0F runs the 0F opcode as if it
                   were not there (opREPE/opREPNE), so these are the MMX
                   instructions, where the library would decode SSE ones. */
                View       w        = withOp(0x0f, at + 1);
                const bool sseSpace = ((op2 >= 0x50) && (op2 <= 0x7f)) || ((op2 >= 0xc2) && (op2 <= 0xc6)) || (op2 >= 0xd0);
                bool       dropped  = false;
                if (sseSpace) {
                    for (size_t i = 0; i + 1 < w.head.size();) {
                        if ((w.head[i] == 0x66) || (w.head[i] == 0xf2) || (w.head[i] == 0xf3)) {
                            w.head.erase(w.head.begin() + (ptrdiff_t) i);
                            dropped = true;
                        } else
                            i++;
                    }
                }
                if (!lib(w))
                    invalid(at + 2, QStringLiteral("In the opcode tables, but not decodable"));
                else if (dropped)
                    d.note = QStringLiteral("No SSE: the 66/F2/F3 prefix does not make this an SSE instruction");
                return finish();
            }
        }
    }

    int reg;
    int mod;
    switch (op) {
        case 0x62:
        case 0xc4:
        case 0xc5:
            /* The library would take these as EVEX or VEX with a register
               operand; here they are BOUND, LES and LDS, which raise #UD. */
            if (!modrmReg(at + 1, &reg, &mod))
                return finish();
            if (mod == 3) {
                invalid(at + 2, QStringLiteral("Register operand: #UD"));
                return finish();
            }
            break;
        case 0x8f:
        case 0xc6:
        case 0xc7:
            /* XOP, XABORT and XBEGIN to the library. */
            if (!modrmReg(at + 1, &reg, nullptr))
                return finish();
            if (reg) {
                invalid(at + 2, QStringLiteral("ModR/M reg %1: #UD").arg(reg));
                return finish();
            }
            break;
        case 0x8c:
        case 0x8e:
            if (!modrmReg(at + 1, &reg, nullptr))
                return finish();
            if ((reg >= ((info_.isa == DEBUGGER_ISA_386) ? 6 : 4)) || ((op == 0x8e) && (reg == 1))) {
                invalid(at + 2, QStringLiteral("No such segment register here: #UD"));
                return finish();
            }
            break;
        case 0xd8 ... 0xdf: {
            uint8_t m;
            if (!f.get(at + 1, &m))
                return finish();
            if (!lib(withOp(op, at + 1))) {
                int extra;
                if (!modrmExtra(f, at + 1, addr32, &extra))
                    return finish();
                d.len  = at + 2 + extra;
                d.text = QStringLiteral("esc %1").arg(op & 7);
            }
            fpuOverride(f, at, info_, d);
            /* The coprocessor's own tables say which it has: the library
               knows the later ones (FCMOVcc, FCOMI, FUCOMI and the like). */
            if ((info_.fpu != DEBUGGER_FPU_NONE) && (debugger_fpu_kind(op, m, addr32) == DEBUGGER_OP_ILLEGAL)) {
                if (info_.fpu_ud) {
                    invalid(d.len, QStringLiteral("Not on this coprocessor: #UD"));
                    return finish();
                }
                d.note = QStringLiteral("Not on this coprocessor: reads the operand and does nothing");
            }
            if (d.note.isEmpty())
                d.note = fpuNote(info_);
            return finish();
        }
        default:
            break;
    }

    if (!lib(withOp(op, at + 1)))
        invalid(at + 1, QStringLiteral("In the opcode tables, but not decodable"));
    return finish();
}
