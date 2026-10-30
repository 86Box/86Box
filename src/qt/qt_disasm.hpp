#ifndef QT_DISASM_HPP
#define QT_DISASM_HPP

#include <QString>

#include <cstdint>

extern "C" {
#include <86box/debugger.h>
}

/* One instruction, decoded as the emulated processor would run it. */
struct DisasmInsn {
    uint32_t lin = 0;      /* linear address of its first byte */
    uint32_t ip  = 0;      /* its IP/EIP */
    int      len = 0;      /* bytes it takes; 0 if its first byte cannot be read */
    uint8_t  bytes[16] {}; /* those bytes, as the processor sees them */
    QString  text;         /* the instruction */
    QString  note;         /* how the processor differs from the instruction set, if it does */
    bool     invalid = false;
};

/* Disassembles in CS as the processor is now. Construct it, and decode, with
   the debugger's lock held: it reads the processor's state and memory. */
class Disassembler {
public:
    Disassembler();

    /* The instruction at ip in CS. current: it is the one at CS:IP, whose
       first bytes the processor may already have in its prefetch queue. */
    DisasmInsn decode(uint32_t ip, bool current);
    uint32_t   csBase() const { return csBase_; }
    /* Decode in a segment at base instead of CS, for an address CS does not
       reach on a processor whose IP wraps at 64 KB. */
    void       setBase(uint32_t base) { csBase_ = base; }
    bool       wraps() const { return info_.wrap16 || (info_.isa == DEBUGGER_ISA_I8080); }
    /* The address of the instruction after one at ip that took len bytes. */
    uint32_t   next(uint32_t ip, int len) const;

private:
    debugger_decode_info_t info_ {};
    uint32_t               csBase_ = 0;
};

#endif
