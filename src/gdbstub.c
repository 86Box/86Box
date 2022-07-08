/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          GDB stub server for remote debugging.
 *
 *
 *
 * Authors: RichardG, <richardg867@gmail.com>
 *
 *          Copyright 2022 RichardG.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <sys/socket.h>
#    include <errno.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86seg.h"
#include "x87.h"
#include "x87_ops_conv.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/gdbstub.h>

#define FAST_RESPONSE(s)         \
    strcpy(client->response, s); \
    client->response_pos = sizeof(s) - 1;
#define FAST_RESPONSE_HEX(s) gdbstub_client_respond_hex(client, (uint8_t *) s, sizeof(s));

enum {
    GDB_SIGINT  = 2,
    GDB_SIGTRAP = 5
};

enum {
    GDB_REG_EAX = 0,
    GDB_REG_ECX,
    GDB_REG_EDX,
    GDB_REG_EBX,
    GDB_REG_ESP,
    GDB_REG_EBP,
    GDB_REG_ESI,
    GDB_REG_EDI,
    GDB_REG_EIP,
    GDB_REG_EFLAGS,
    GDB_REG_CS,
    GDB_REG_SS,
    GDB_REG_DS,
    GDB_REG_ES,
    GDB_REG_FS,
    GDB_REG_GS,
    GDB_REG_FS_BASE,
    GDB_REG_GS_BASE,
    GDB_REG_CR0,
    GDB_REG_CR2,
    GDB_REG_CR3,
    GDB_REG_CR4,
    GDB_REG_EFER,
    GDB_REG_ST0,
    GDB_REG_ST1,
    GDB_REG_ST2,
    GDB_REG_ST3,
    GDB_REG_ST4,
    GDB_REG_ST5,
    GDB_REG_ST6,
    GDB_REG_ST7,
    GDB_REG_FCTRL,
    GDB_REG_FSTAT,
    GDB_REG_FTAG,
    GDB_REG_FISEG,
    GDB_REG_FIOFF,
    GDB_REG_FOSEG,
    GDB_REG_FOOFF,
    GDB_REG_FOP,
    GDB_REG_MM0,
    GDB_REG_MM1,
    GDB_REG_MM2,
    GDB_REG_MM3,
    GDB_REG_MM4,
    GDB_REG_MM5,
    GDB_REG_MM6,
    GDB_REG_MM7,
    GDB_REG_MAX
};

enum {
    GDB_MODE_BASE10 = 0,
    GDB_MODE_HEX,
    GDB_MODE_OCT,
    GDB_MODE_BIN
};

typedef struct _gdbstub_client_ {
    int                socket;
    struct sockaddr_in addr;

    char packet[16384], response[16384];
    int  has_packet, waiting_stop, packet_pos, response_pos;

    event_t *processed_event, *response_event;

    uint16_t last_io_base, last_io_len, last_io_value;

    struct _gdbstub_client_ *next;
} gdbstub_client_t;

typedef struct _gdbstub_breakpoint_ {
    uint32_t addr;
    union {
        uint8_t  orig_val;
        uint32_t end;
    };

    struct _gdbstub_breakpoint_ *next;
} gdbstub_breakpoint_t;

#define ENABLE_GDBSTUB_LOG 1
#ifdef ENABLE_GDBSTUB_LOG
int gdbstub_do_log = ENABLE_GDBSTUB_LOG;

static void
gdbstub_log(const char *fmt, ...)
{
    va_list ap;

    if (gdbstub_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define gdbstub_log(fmt, ...)
#endif

static x86seg    *segment_regs[] = { &cpu_state.seg_cs, &cpu_state.seg_ss, &cpu_state.seg_ds, &cpu_state.seg_es, &cpu_state.seg_fs, &cpu_state.seg_gs };
static uint32_t  *cr_regs[]      = { &cpu_state.CR0.l, &cr2, &cr3, &cr4 };
static void      *fpu_regs[]     = { &cpu_state.npxc, &cpu_state.npxs, NULL, &x87_pc_seg, &x87_pc_off, &x87_op_seg, &x87_op_off };
static const char target_xml[]   = /* QEMU gdb-xml/i386-32bit.xml with modifications (described in comments) */
    // clang-format off
    "<?xml version=\"1.0\"?>"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
    "<target>"
        "<architecture>i8086</architecture>" /* start in 16-bit mode to work around known GDB bug preventing 32->16 switching */
        ""
        "<feature name=\"org.gnu.gdb.i386.core\">"
            "<flags id=\"i386_eflags\" size=\"4\">"
                "<field name=\"\" start=\"22\" end=\"31\"/>"
                "<field name=\"ID\" start=\"21\" end=\"21\"/>"
                "<field name=\"VIP\" start=\"20\" end=\"20\"/>"
                "<field name=\"VIF\" start=\"19\" end=\"19\"/>"
                "<field name=\"AC\" start=\"18\" end=\"18\"/>"
                "<field name=\"VM\" start=\"17\" end=\"17\"/>"
                "<field name=\"RF\" start=\"16\" end=\"16\"/>"
                "<field name=\"\" start=\"15\" end=\"15\"/>"
                "<field name=\"NT\" start=\"14\" end=\"14\"/>"
                "<field name=\"IOPL\" start=\"12\" end=\"13\"/>"
                "<field name=\"OF\" start=\"11\" end=\"11\"/>"
                "<field name=\"DF\" start=\"10\" end=\"10\"/>"
                "<field name=\"IF\" start=\"9\" end=\"9\"/>"
                "<field name=\"TF\" start=\"8\" end=\"8\"/>"
                "<field name=\"SF\" start=\"7\" end=\"7\"/>"
                "<field name=\"ZF\" start=\"6\" end=\"6\"/>"
                "<field name=\"\" start=\"5\" end=\"5\"/>"
                "<field name=\"AF\" start=\"4\" end=\"4\"/>"
                "<field name=\"PF\" start=\"2\" end=\"2\"/>"
                "<field name=\"\" start=\"1\" end=\"1\"/>"
                "<field name=\"CF\" start=\"0\" end=\"0\"/>"
            "</flags>"
            ""
            "<reg name=\"eax\" bitsize=\"32\" type=\"int32\" regnum=\"0\"/>"
            "<reg name=\"ecx\" bitsize=\"32\" type=\"int32\"/>"
            "<reg name=\"edx\" bitsize=\"32\" type=\"int32\"/>"
            "<reg name=\"ebx\" bitsize=\"32\" type=\"int32\"/>"
            "<reg name=\"esp\" bitsize=\"32\" type=\"data_ptr\"/>"
            "<reg name=\"ebp\" bitsize=\"32\" type=\"data_ptr\"/>"
            "<reg name=\"esi\" bitsize=\"32\" type=\"int32\"/>"
            "<reg name=\"edi\" bitsize=\"32\" type=\"int32\"/>"
            ""
            "<reg name=\"eip\" bitsize=\"32\" type=\"code_ptr\"/>"
            "<reg name=\"eflags\" bitsize=\"32\" type=\"i386_eflags\"/>"
            ""
            "<reg name=\"cs\" bitsize=\"16\" type=\"int32\"/>"
            "<reg name=\"ss\" bitsize=\"16\" type=\"int32\"/>"
            "<reg name=\"ds\" bitsize=\"16\" type=\"int32\"/>"
            "<reg name=\"es\" bitsize=\"16\" type=\"int32\"/>"
            "<reg name=\"fs\" bitsize=\"16\" type=\"int32\"/>"
            "<reg name=\"gs\" bitsize=\"16\" type=\"int32\"/>"
            ""
            "<flags id=\"i386_cr0\" size=\"4\">"
                "<field name=\"PG\" start=\"31\" end=\"31\"/>"
                "<field name=\"CD\" start=\"30\" end=\"30\"/>"
                "<field name=\"NW\" start=\"29\" end=\"29\"/>"
                "<field name=\"AM\" start=\"18\" end=\"18\"/>"
                "<field name=\"WP\" start=\"16\" end=\"16\"/>"
                "<field name=\"NE\" start=\"5\" end=\"5\"/>"
                "<field name=\"ET\" start=\"4\" end=\"4\"/>"
                "<field name=\"TS\" start=\"3\" end=\"3\"/>"
                "<field name=\"EM\" start=\"2\" end=\"2\"/>"
                "<field name=\"MP\" start=\"1\" end=\"1\"/>"
                "<field name=\"PE\" start=\"0\" end=\"0\"/>"
            "</flags>"
            ""
            "<flags id=\"i386_cr3\" size=\"4\">"
                "<field name=\"PDBR\" start=\"12\" end=\"31\"/>"
                "<field name=\"PCID\" start=\"0\" end=\"11\"/>"
            "</flags>"
            ""
            "<flags id=\"i386_cr4\" size=\"4\">"
                "<field name=\"VME\" start=\"0\" end=\"0\"/>"
                "<field name=\"PVI\" start=\"1\" end=\"1\"/>"
                "<field name=\"TSD\" start=\"2\" end=\"2\"/>"
                "<field name=\"DE\" start=\"3\" end=\"3\"/>"
                "<field name=\"PSE\" start=\"4\" end=\"4\"/>"
                "<field name=\"PAE\" start=\"5\" end=\"5\"/>"
                "<field name=\"MCE\" start=\"6\" end=\"6\"/>"
                "<field name=\"PGE\" start=\"7\" end=\"7\"/>"
                "<field name=\"PCE\" start=\"8\" end=\"8\"/>"
                "<field name=\"OSFXSR\" start=\"9\" end=\"9\"/>"
                "<field name=\"OSXMMEXCPT\" start=\"10\" end=\"10\"/>"
                "<field name=\"UMIP\" start=\"11\" end=\"11\"/>"
                "<field name=\"LA57\" start=\"12\" end=\"12\"/>"
                "<field name=\"VMXE\" start=\"13\" end=\"13\"/>"
                "<field name=\"SMXE\" start=\"14\" end=\"14\"/>"
                "<field name=\"FSGSBASE\" start=\"16\" end=\"16\"/>"
                "<field name=\"PCIDE\" start=\"17\" end=\"17\"/>"
                "<field name=\"OSXSAVE\" start=\"18\" end=\"18\"/>"
                "<field name=\"SMEP\" start=\"20\" end=\"20\"/>"
                "<field name=\"SMAP\" start=\"21\" end=\"21\"/>"
                "<field name=\"PKE\" start=\"22\" end=\"22\"/>"
            "</flags>"
            ""
            "<flags id=\"i386_efer\" size=\"4\">"
                "<field name=\"TCE\" start=\"15\" end=\"15\"/>"
                "<field name=\"FFXSR\" start=\"14\" end=\"14\"/>"
                "<field name=\"LMSLE\" start=\"13\" end=\"13\"/>"
                "<field name=\"SVME\" start=\"12\" end=\"12\"/>"
                "<field name=\"NXE\" start=\"11\" end=\"11\"/>"
                "<field name=\"LMA\" start=\"10\" end=\"10\"/>"
                "<field name=\"LME\" start=\"8\" end=\"8\"/>"
                "<field name=\"SCE\" start=\"0\" end=\"0\"/>"
            "</flags>"
            ""
            "<reg name=\"cr0\" bitsize=\"32\" type=\"i386_cr0\"/>"
            "<reg name=\"cr2\" bitsize=\"32\" type=\"int32\"/>"
            "<reg name=\"cr3\" bitsize=\"32\" type=\"i386_cr3\"/>"
            "<reg name=\"cr4\" bitsize=\"32\" type=\"i386_cr4\"/>"
            "<reg name=\"efer\" bitsize=\"64\" type=\"i386_efer\"/>"
            ""
            "<reg name=\"st0\" bitsize=\"80\" type=\"i387_ext\"/>"
            "<reg name=\"st1\" bitsize=\"80\" type=\"i387_ext\"/>"
            "<reg name=\"st2\" bitsize=\"80\" type=\"i387_ext\"/>"
            "<reg name=\"st3\" bitsize=\"80\" type=\"i387_ext\"/>"
            "<reg name=\"st4\" bitsize=\"80\" type=\"i387_ext\"/>"
            "<reg name=\"st5\" bitsize=\"80\" type=\"i387_ext\"/>"
            "<reg name=\"st6\" bitsize=\"80\" type=\"i387_ext\"/>"
            "<reg name=\"st7\" bitsize=\"80\" type=\"i387_ext\"/>"
            ""
            "<reg name=\"fctrl\" bitsize=\"16\" type=\"int\" group=\"float\"/>"
            "<reg name=\"fstat\" bitsize=\"16\" type=\"int\" group=\"float\"/>"
            "<reg name=\"ftag\" bitsize=\"16\" type=\"int\" group=\"float\"/>"
            "<reg name=\"fiseg\" bitsize=\"16\" type=\"int\" group=\"float\"/>"
            "<reg name=\"fioff\" bitsize=\"32\" type=\"int\" group=\"float\"/>"
            "<reg name=\"foseg\" bitsize=\"16\" type=\"int\" group=\"float\"/>"
            "<reg name=\"fooff\" bitsize=\"32\" type=\"int\" group=\"float\"/>"
            "<reg name=\"fop\" bitsize=\"16\" type=\"int\" group=\"float\"/>"
            ""
            "<vector id=\"v8i8\" type=\"int8\" count=\"8\"/>"
            "<vector id=\"v8u8\" type=\"uint8\" count=\"8\"/>"
            "<vector id=\"v4i16\" type=\"int16\" count=\"4\"/>"
            "<vector id=\"v4u16\" type=\"uint16\" count=\"4\"/>"
            "<vector id=\"v2i32\" type=\"int32\" count=\"2\"/>"
            "<vector id=\"v2u32\" type=\"uint32\" count=\"2\"/>"
            "<union id=\"mmx\">"
                "<field name=\"uint64\" type=\"uint64\"/>"
                "<field name=\"v2_int32\" type=\"v2i32\"/>"
                "<field name=\"v4_int16\" type=\"v4i16\"/>"
                "<field name=\"v8_int8\" type=\"v8i8\"/>"
            "</union>"
            ""
            "<reg name=\"mm0\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
            "<reg name=\"mm1\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
            "<reg name=\"mm2\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
            "<reg name=\"mm3\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
            "<reg name=\"mm4\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
            "<reg name=\"mm5\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
            "<reg name=\"mm6\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
            "<reg name=\"mm7\" bitsize=\"64\" type=\"mmx\" group=\"mmx\"/>"
        "</feature>"
    "</target>";
// clang-format on

#ifdef _WIN32
static WSADATA wsa;
#endif
static int      gdbstub_socket = -1, stop_reason_len = 0, in_gdbstub = 0;
static uint32_t watch_addr;
static char     stop_reason[2048];

static gdbstub_client_t *first_client = NULL, *last_client = NULL;
static mutex_t          *client_list_mutex;

static void (*cpu_exec_shadow)(int cycs);
static gdbstub_breakpoint_t *first_swbreak = NULL, *first_hwbreak = NULL,
                            *first_rwatch = NULL, *first_wwatch = NULL, *first_awatch = NULL;

int      gdbstub_step = 0, gdbstub_next_asap = 0;
uint64_t gdbstub_watch_pages[(((uint32_t) -1) >> (MEM_GRANULARITY_BITS + 6)) + 1];

static void
gdbstub_break()
{
    /* Pause CPU execution as soon as possible. */
    if (gdbstub_step <= GDBSTUB_EXEC)
        gdbstub_step = GDBSTUB_BREAK;
}

static void
gdbstub_jump(uint32_t new_pc)
{
    /* Nasty hack; qemu always uses the full 32-bit EIP internally... */
    if (cpu_state.op32 || ((new_pc >= cs) && (new_pc < (cs + 65536)))) {
        cpu_state.pc = new_pc - cs;
    } else {
        loadseg((new_pc >> 4) & 0xf000, &cpu_state.seg_cs);
        cpu_state.pc = new_pc & 0xffff;
    }
    flushmmucache();
}

static inline int
gdbstub_hex_decode(int c)
{
    if ((c >= '0') && (c <= '9'))
        return c - '0';
    else if ((c >= 'A') && (c <= 'F'))
        return c - 'A' + 10;
    else if ((c >= 'a') && (c <= 'f'))
        return c - 'a' + 10;
    else
        return 0;
}

static inline int
gdbstub_hex_encode(int c)
{
    if (c < 10)
        return c + '0';
    else
        return c - 10 + 'a';
}

static int
gdbstub_num_decode(char *p, int *dest, int mode)
{
    /* Stop if the pointer is invalid. */
    if (!p)
        return 0;

    /* Read sign. */
    int sign = 1;
    if ((p[0] == '+') || (p[0] == '-')) {
        if (p[0] == '-')
            sign = -1;
        p++;
    }

    /* Read type identifer if present (0x/0o/0b/0n) */
    if (p[0] == '0') {
        switch (p[1]) {
            case 'x':
                mode = GDB_MODE_HEX;
                break;

            case '0' ... '7':
                p -= 1;
                /* fall-through */

            case 'o':
                mode = GDB_MODE_OCT;
                break;

            case 'b':
                mode = GDB_MODE_BIN;
                break;

            case 'n':
                mode = GDB_MODE_BASE10;
                break;

            default:
                p -= 2;
                break;
        }
        p += 2;
    }

    /* Parse each character. */
    *dest = 0;
    while (*p) {
        switch (mode) {
            case GDB_MODE_BASE10:
                if ((*p >= '0') && (*p <= '9'))
                    *dest = ((*dest) * 10) + ((*p) - '0');
                else
                    return 0;
                break;

            case GDB_MODE_HEX:
                if (((*p >= '0') && (*p <= '9')) || ((*p >= 'A') && (*p <= 'F')) || ((*p >= 'a') && (*p <= 'f')))
                    *dest = ((*dest) << 4) | gdbstub_hex_decode(*p);
                else
                    return 0;
                break;

            case GDB_MODE_OCT:
                if ((*p >= '0') && (*p <= '7'))
                    *dest = ((*dest) << 3) | ((*p) - '0');
                else
                    return 0;
                break;

            case GDB_MODE_BIN:
                if ((*p == '0') || (*p == '1'))
                    *dest = ((*dest) << 1) | ((*p) - '0');
                else
                    return 0;
                break;
        }
        p++;
    }

    /* Apply sign. */
    if (sign < 0)
        *dest = -(*dest);

    /* Return success. */
    return 1;
}

static int
gdbstub_client_read_word(gdbstub_client_t *client, int *dest)
{
    char *p = &client->packet[client->packet_pos], *q = p;
    while (((*p >= '0') && (*p <= '9')) || ((*p >= 'A') && (*p <= 'F')) || ((*p >= 'a') && (*p <= 'f')))
        *dest = ((*dest) << 4) | gdbstub_hex_decode(*p++);
    return p - q;
}

static int
gdbstub_client_read_hex(gdbstub_client_t *client, uint8_t *buf, int size)
{
    int pp = client->packet_pos;
    while (size-- && (pp < (sizeof(client->packet) - 2))) {
        *buf = gdbstub_hex_decode(client->packet[pp++]) << 4;
        *buf++ |= gdbstub_hex_decode(client->packet[pp++]);
    }
    return pp - client->packet_pos;
}

static int
gdbstub_client_read_string(gdbstub_client_t *client, char *buf, int size, char terminator)
{
    int  pp = client->packet_pos;
    char c;
    while (size-- && (pp < (sizeof(client->packet) - 1))) {
        c = client->packet[pp];
        if ((c == terminator) || (c == '\0')) {
            *buf = '\0';
            break;
        }
        pp++;
        *buf++ = c;
    }
    return pp - client->packet_pos;
}

static int
gdbstub_client_write_reg(int index, uint8_t *buf)
{
    int width = 4;
    switch (index) {
        case GDB_REG_EAX ... GDB_REG_EDI:
            cpu_state.regs[index - GDB_REG_EAX].l = *((uint32_t *) buf);
            break;

        case GDB_REG_EIP:
            gdbstub_jump(*((uint32_t *) buf));
            break;

        case GDB_REG_EFLAGS:
            cpu_state.flags  = *((uint16_t *) &buf[0]);
            cpu_state.eflags = *((uint16_t *) &buf[2]);
            break;

        case GDB_REG_CS ... GDB_REG_GS:
            width = 2;
            loadseg(*((uint16_t *) buf), segment_regs[index - GDB_REG_CS]);
            flushmmucache();
            break;

        case GDB_REG_FS_BASE ... GDB_REG_GS_BASE:
            /* Do what qemu does and just load the base. */
            segment_regs[(index - 16) + (GDB_REG_FS - GDB_REG_CS)]->base = *((uint32_t *) buf);
            break;

        case GDB_REG_CR0 ... GDB_REG_CR4:
            *cr_regs[index - GDB_REG_CR0] = *((uint32_t *) buf);
            flushmmucache();
            break;

        case GDB_REG_EFER:
            msr.amd_efer = *((uint64_t *) buf);
            break;

        case GDB_REG_ST0 ... GDB_REG_ST7:
            width           = 10;
            x87_conv_t conv = {
                .eind  = { .ll = *((uint64_t *) &buf[0]) },
                .begin = *((uint16_t *) &buf[8])
            };
            cpu_state.ST[(cpu_state.TOP + (index - GDB_REG_ST0)) & 7] = x87_from80(&conv);
            break;

        case GDB_REG_FCTRL:
        case GDB_REG_FISEG:
        case GDB_REG_FOSEG:
            width                                           = 2;
            *((uint16_t *) fpu_regs[index - GDB_REG_FCTRL]) = *((uint16_t *) buf);
            if (index >= GDB_REG_FISEG)
                flushmmucache();
            break;

        case GDB_REG_FSTAT:
        case GDB_REG_FOP:
            width = 2;
            break;

        case GDB_REG_FTAG:
            width = 2;
            x87_settag(*((uint16_t *) buf));
            break;

        case GDB_REG_FIOFF:
        case GDB_REG_FOOFF:
            *((uint32_t *) fpu_regs[index - GDB_REG_FCTRL]) = *((uint32_t *) buf);
            break;

        case GDB_REG_MM0 ... GDB_REG_MM7:
            width                               = 8;
            cpu_state.MM[index - GDB_REG_MM0].q = *((uint64_t *) buf);
            break;

        default:
            width = 0;
    }

#ifdef ENABLE_GDBSTUB_LOG
    char logbuf[256], *p = logbuf + sprintf(logbuf, "GDB Stub: Setting register %d to ", index);
    for (int i = width - 1; i >= 0; i--)
        p += sprintf(p, "%02X", buf[i]);
    sprintf(p, "\n");
    gdbstub_log(logbuf);
#endif

    return width;
}

static void
gdbstub_client_respond(gdbstub_client_t *client)
{
    /* Calculate checksum. */
    int checksum = 0, i;
    for (i = 0; i < client->response_pos; i++)
        checksum += client->response[i];

    /* Send response packet. */
    client->response[client->response_pos] = '\0';
#ifdef ENABLE_GDBSTUB_LOG
    i                     = client->response[995]; /* pclog_ex buffer too small */
    client->response[995] = '\0';
    gdbstub_log("GDB Stub: Sending response: %s\n", client->response);
    client->response[995] = i;
#endif
    send(client->socket, "$", 1, 0);
    send(client->socket, client->response, client->response_pos, 0);
    char response_cksum[3] = { '#', gdbstub_hex_encode((checksum >> 4) & 0x0f), gdbstub_hex_encode(checksum & 0x0f) };
    send(client->socket, response_cksum, sizeof(response_cksum), 0);
}

static void
gdbstub_client_respond_partial(gdbstub_client_t *client)
{
    /* Send response. */
    gdbstub_client_respond(client);

    /* Wait for the response to be acknowledged. */
    thread_wait_event(client->response_event, -1);
    thread_reset_event(client->response_event);
}

static void
gdbstub_client_respond_hex(gdbstub_client_t *client, uint8_t *buf, int size)
{
    while (size-- && (client->response_pos < (sizeof(client->response) - 2))) {
        client->response[client->response_pos++] = gdbstub_hex_encode((*buf) >> 4);
        client->response[client->response_pos++] = gdbstub_hex_encode((*buf++) & 0x0f);
    }
}

static int
gdbstub_client_read_reg(int index, uint8_t *buf)
{
    int width = 4;
    switch (index) {
        case GDB_REG_EAX ... GDB_REG_EDI:
            *((uint32_t *) buf) = cpu_state.regs[index].l;
            break;

        case GDB_REG_EIP:
            *((uint32_t *) buf) = cs + cpu_state.pc;
            break;

        case GDB_REG_EFLAGS:
            *((uint16_t *) &buf[0]) = cpu_state.flags;
            *((uint16_t *) &buf[2]) = cpu_state.eflags;
            break;

        case GDB_REG_CS ... GDB_REG_GS:
            *((uint16_t *) buf) = segment_regs[index - GDB_REG_CS]->seg;
            break;

        case GDB_REG_FS_BASE ... GDB_REG_GS_BASE:
            *((uint32_t *) buf) = segment_regs[(index - 16) + (GDB_REG_FS - GDB_REG_CS)]->base;
            break;

        case GDB_REG_CR0 ... GDB_REG_CR4:
            *((uint32_t *) buf) = *cr_regs[index - GDB_REG_CR0];
            break;

        case GDB_REG_EFER:
            *((uint64_t *) buf) = msr.amd_efer;
            break;

        case GDB_REG_ST0 ... GDB_REG_ST7:
            width = 10;
            x87_conv_t conv;
            x87_to80(cpu_state.ST[(cpu_state.TOP + (index - GDB_REG_ST0)) & 7], &conv);
            *((uint64_t *) &buf[0]) = conv.eind.ll;
            *((uint16_t *) &buf[8]) = conv.begin;
            break;

        case GDB_REG_FCTRL ... GDB_REG_FSTAT:
        case GDB_REG_FISEG:
        case GDB_REG_FOSEG:
            width               = 2;
            *((uint16_t *) buf) = *((uint16_t *) fpu_regs[index - GDB_REG_FCTRL]);
            break;

        case GDB_REG_FTAG:
            width               = 2;
            *((uint16_t *) buf) = x87_gettag();
            break;

        case GDB_REG_FIOFF:
        case GDB_REG_FOOFF:
            *((uint32_t *) buf) = *((uint32_t *) fpu_regs[index - GDB_REG_FCTRL]);
            break;

        case GDB_REG_FOP:
            width               = 2;
            *((uint16_t *) buf) = 0; /* we don't store the FPU opcode */
            break;

        case GDB_REG_MM0 ... GDB_REG_MM7:
            width               = 8;
            *((uint64_t *) buf) = cpu_state.MM[index - GDB_REG_MM0].q;
            break;

        default:
            width = 0;
    }

    return width;
}

static void
gdbstub_client_packet(gdbstub_client_t *client)
{
#ifdef GDBSTUB_CHECK_CHECKSUM /* msys2 gdb 11.1 transmits qSupported and H with invalid checksum... */
    uint8_t rcv_checksum = 0, checksum = 0;
#endif
    int     i, j = 0, k = 0, l;
    uint8_t buf[10] = { 0 };
    char   *p;

    /* Validate checksum. */
    client->packet_pos -= 2;
#ifdef GDBSTUB_CHECK_CHECKSUM
    gdbstub_client_read_hex(client, &rcv_checksum, 1);
#endif
    *((uint16_t *) &client->packet[--client->packet_pos]) = 0;
#ifdef GDBSTUB_CHECK_CHECKSUM
    for (i = 0; i < client->packet_pos; i++)
        checksum += client->packet[i];

    if (checksum != rcv_checksum) {
        /* Send negative acknowledgement. */
#    ifdef ENABLE_GDBSTUB_LOG
        i                   = client->packet[953]; /* pclog_ex buffer too small */
        client->packet[953] = '\0';
        gdbstub_log("GDB Stub: Received packet with invalid checksum (expected %02X got %02X): %s\n", checksum, rcv_checksum, client->packet);
        client->packet[953] = i;
#    endif
        send(client->socket, "-", 1, 0);
        return;
    }
#endif

    /* Send positive acknowledgement. */
#ifdef ENABLE_GDBSTUB_LOG
    i                   = client->packet[996]; /* pclog_ex buffer too small */
    client->packet[996] = '\0';
    gdbstub_log("GDB Stub: Received packet: %s\n", client->packet);
    client->packet[996] = i;
#endif
    send(client->socket, "+", 1, 0);

    /* Block other responses from being written while this one (if any is produced) isn't acknowledged. */
    if ((client->packet[0] != 'c') && (client->packet[0] != 's') && (client->packet[0] != 'v')) {
        thread_wait_event(client->response_event, -1);
        thread_reset_event(client->response_event);
    }
    client->response_pos = 0;
    client->packet_pos   = 1;

    /* Parse command. */
    switch (client->packet[0]) {
        case '?': /* stop reason */
            /* Respond with a stop reply packet if one is present. */
            if (stop_reason_len) {
                strcpy(client->response, stop_reason);
                client->response_pos = strlen(client->response);
            }
            break;

        case 'c': /* continue */
        case 's': /* step */
            /* Flag that the client is waiting for a stop reason. */
            client->waiting_stop = 1;

            /* Jump to address if specified. */
            if (client->packet[1] && gdbstub_client_read_word(client, &j))
                gdbstub_jump(j);

            /* Resume CPU. */
            gdbstub_step = gdbstub_next_asap = (client->packet[0] == 's') ? GDBSTUB_SSTEP : GDBSTUB_EXEC;
            return;

        case 'D': /* detach */
            /* Resume emulation. */
            gdbstub_step = GDBSTUB_EXEC;

            /* Respond positively. */
ok:
            FAST_RESPONSE("OK");
            break;

        case 'g': /* read all registers */
            /* Output the values of all registers. */
            for (i = 0; i < GDB_REG_MAX; i++)
                gdbstub_client_respond_hex(client, buf, gdbstub_client_read_reg(i, buf));
            break;

        case 'G': /* write all registers */
            /* Write the values of all registers. */
            for (i = 0; i < GDB_REG_MAX; i++) {
                if (i == GDB_REG_MAX)
                    goto e22;
                if (!gdbstub_client_read_hex(client, buf, sizeof(buf)))
                    break;
                client->packet_pos += gdbstub_client_write_reg(i, buf) << 1;
            }

            /* Respond positively. */
            goto ok;

        case 'H': /* set thread */
            /* Read operation type and thread ID. */
            if ((client->packet[1] == '\0') || (client->packet[2] == '\0')) {
e22:
                FAST_RESPONSE("E22");
                break;
            }

            /* Respond positively only on thread 1. */
            if ((client->packet[2] == '1') && !client->packet[3])
                goto ok;
            else
                goto e22;

        case 'm': /* read memory */
            /* Read address and length. */
            if (!(i = gdbstub_client_read_word(client, &j)))
                goto e22;
            client->packet_pos += i + 1;
            gdbstub_client_read_word(client, &k);
            if (!k)
                goto e22;

            /* Clamp length. */
            if (k >= (sizeof(client->response) >> 1))
                k = (sizeof(client->response) >> 1) - 1;

            /* Read by qwords, then by dwords, then by words, then by bytes. */
            i = 0;
            if (is386) {
                for (; i < (k & ~7); i += 8) {
                    *((uint64_t *) buf) = readmemql(j);
                    j += 8;
                    gdbstub_client_respond_hex(client, buf, 8);
                }
                for (; i < (k & ~3); i += 4) {
                    *((uint32_t *) buf) = readmemll(j);
                    j += 4;
                    gdbstub_client_respond_hex(client, buf, 4);
                }
            }
            for (; i < (k & ~1); i += 2) {
                *((uint16_t *) buf) = readmemwl(j);
                j += 2;
                gdbstub_client_respond_hex(client, buf, 2);
            }
            for (; i < k; i++) {
                buf[0] = readmembl(j++);
                gdbstub_client_respond_hex(client, buf, 1);
            }
            break;

        case 'M': /* write memory */
        case 'X': /* write memory binary */
            /* Read address and length. */
            if (!(i = gdbstub_client_read_word(client, &j)))
                goto e22;
            client->packet_pos += i + 1;
            client->packet_pos += gdbstub_client_read_word(client, &k) + 1;
            if (!k)
                goto e22;

            /* Clamp length. */
            if (k >= ((sizeof(client->response) >> 1) - client->packet_pos))
                k = (sizeof(client->response) >> 1) - client->packet_pos - 1;

            /* Decode the data. */
            if (client->packet[0] == 'M') { /* hex encoded */
                gdbstub_client_read_hex(client, (uint8_t *) client->packet, k);
            } else { /* binary encoded */
                i = 0;
                while (i < k) {
                    if (client->packet[client->packet_pos] == '}') {
                        client->packet_pos++;
                        client->packet[i++] = client->packet[client->packet_pos++] ^ 0x20;
                    } else {
                        client->packet[i++] = client->packet[client->packet_pos++];
                    }
                }
            }

            /* Write by qwords, then by dwords, then by words, then by bytes. */
            p = client->packet;
            i = 0;
            if (is386) {
                for (; i < (k & ~7); i += 8) {
                    writememql(j, *((uint64_t *) p));
                    j += 8;
                    p += 8;
                }
                for (; i < (k & ~3); i += 4) {
                    writememll(j, *((uint32_t *) p));
                    j += 4;
                    p += 4;
                }
            }
            for (; i < (k & ~1); i += 2) {
                writememwl(j, *((uint16_t *) p));
                j += 2;
                p += 2;
            }
            for (; i < k; i++) {
                writemembl(j++, p[0]);
                p++;
            }

            /* Respond positively. */
            goto ok;

        case 'p': /* read register */
            /* Read register index. */
            if (!gdbstub_client_read_word(client, &j)) {
e14:
                FAST_RESPONSE("E14");
                break;
            }

            /* Read the register's value. */
            if (!(i = gdbstub_client_read_reg(j, buf)))
                goto e14;

            /* Return value. */
            gdbstub_client_respond_hex(client, buf, i);
            break;

        case 'P': /* write register */
            /* Read register index and value. */
            if (!(i = gdbstub_client_read_word(client, &j)))
                goto e14;
            client->packet_pos += i + 1;
            if (!gdbstub_client_read_hex(client, buf, sizeof(buf)))
                goto e14;

            /* Write the value to the register. */
            if (!gdbstub_client_write_reg(j, buf))
                goto e14;

            /* Respond positively. */
            goto ok;

        case 'q': /* query */
            /* Erase response, as we'll use it as a scratch buffer. */
            memset(client->response, 0, sizeof(client->response));

            /* Read the query type. */
            client->packet_pos += gdbstub_client_read_string(client, client->response, sizeof(client->response) - 1,
                                                             (client->packet[1] == 'R') ? ',' : ':')
                + 1;

            /* Perform the query. */
            if (!strcmp(client->response, "Supported")) {
                /* Go through the feature list and negate ones we don't support. */
                while ((client->response_pos < (sizeof(client->response) - 1)) && (i = gdbstub_client_read_string(client, &client->response[client->response_pos], sizeof(client->response) - client->response_pos - 1, ';'))) {
                    client->packet_pos += i + 1;
                    if (strncmp(&client->response[client->response_pos], "PacketSize", 10) && strcmp(&client->response[client->response_pos], "swbreak") && strcmp(&client->response[client->response_pos], "hwbreak") && strncmp(&client->response[client->response_pos], "xmlRegisters", 12) && strcmp(&client->response[client->response_pos], "qXfer:features:read")) {
                        gdbstub_log("GDB Stub: Feature \"%s\" is not supported\n", &client->response[client->response_pos]);
                        client->response_pos += i;
                        client->response[client->response_pos++] = '-';
                        client->response[client->response_pos++] = ';';
                    } else {
                        gdbstub_log("GDB Stub: Feature \"%s\" is supported\n", &client->response[client->response_pos]);
                    }
                }

                /* Add our supported features to the end. */
                if (client->response_pos < (sizeof(client->response) - 1))
                    client->response_pos += snprintf(&client->response[client->response_pos], sizeof(client->response) - client->response_pos,
                                                     "PacketSize=%lX;swbreak+;hwbreak+;qXfer:features:read+", sizeof(client->packet) - 1);
                break;
            } else if (!strcmp(client->response, "Xfer")) {
                /* Read the transfer object. */
                client->packet_pos += gdbstub_client_read_string(client, client->response, sizeof(client->response) - 1, ':') + 1;
                if (!strcmp(client->response, "features")) {
                    /* Read the transfer operation. */
                    client->packet_pos += gdbstub_client_read_string(client, client->response, sizeof(client->response) - 1, ':') + 1;
                    if (!strcmp(client->response, "read")) {
                        /* Read the transfer annex. */
                        client->packet_pos += gdbstub_client_read_string(client, client->response, sizeof(client->response) - 1, ':') + 1;
                        if (!strcmp(client->response, "target.xml"))
                            p = (char *) target_xml;
                        else
                            p = NULL;

                        /* Stop if the file wasn't found. */
                        if (!p) {
e00:
                            FAST_RESPONSE("E00");
                            break;
                        }

                        /* Read offset and length. */
                        if (!(i = gdbstub_client_read_word(client, &j)))
                            goto e22;
                        client->packet_pos += i + 1;
                        client->packet_pos += gdbstub_client_read_word(client, &k) + 1;
                        if (!k)
                            goto e22;

                        /* Check if the offset is valid. */
                        l = strlen(p);
                        if (j > l)
                            goto e00;
                        p += j;

                        /* Return the more/less flag while also clamping the length. */
                        if (k >= ((sizeof(client->response) >> 1) - 2))
                            k = (sizeof(client->response) >> 1) - 3;
                        if (k < (l - j)) {
                            client->response[client->response_pos++] = 'm';
                        } else {
                            client->response[client->response_pos++] = 'l';
                            k                                        = l - j;
                        }

                        /* Encode the data. */
                        while (k--) {
                            i = *p++;
                            if ((i == '\0') || (i == '#') || (i == '$') || (i == '*') || (i == '}')) {
                                client->response[client->response_pos++] = '}';
                                client->response[client->response_pos++] = i ^ 0x20;
                            } else {
                                client->response[client->response_pos++] = i;
                            }
                        }
                        break;
                    }
                }
            } else if (!strncmp(client->response, "Attached", 8)) {
                FAST_RESPONSE("1");
            } else if (!strcmp(client->response, "C")) {
                FAST_RESPONSE("QC1");
            } else if (!strcmp(client->response, "Rcmd")) {
                /* Read and decode command in-place. */
                i                 = gdbstub_client_read_hex(client, (uint8_t *) client->packet, strlen(client->packet) - client->packet_pos);
                client->packet[i] = 0;
                gdbstub_log("GDB Stub: Monitor command: %s\n", client->packet);

                /* Parse the command name. */
                char *strtok_save;
                p = strtok_r(client->packet, " ", &strtok_save);
                if (!p)
                    goto ok;
                i = strlen(p) - 1; /* get last character offset */

                /* Interpret the command. */
                if (p[0] == 'i') {
                    /* Read I/O operation width. */
                    l = (i < 1) ? '\0' : p[i];

                    /* Read optional I/O port. */
                    if (!(p = strtok_r(NULL, " ", &strtok_save)) || !gdbstub_num_decode(p, &j, GDB_MODE_HEX) || (j < 0) || (j >= 65536))
                        j = client->last_io_base;
                    else
                        client->last_io_base = j;

                    /* Read optional length. */
                    if (!(p = strtok_r(NULL, " ", &strtok_save)) || !gdbstub_num_decode(p, &k, GDB_MODE_BASE10))
                        k = client->last_io_len;
                    else
                        client->last_io_len = k;

                    /* Clamp length. */
                    if (k < 1)
                        k = 1;
                    if (k > (65536 - j))
                        k = 65536 - j;

                    /* Read ports. */
                    i = 0;
                    while (i < k) {
                        if ((i % 16) == 0) {
                            if (i) {
                                client->packet[client->packet_pos++] = '\n';

                                /* Provide partial response with the last line. */
                                client->response_pos                     = 0;
                                client->response[client->response_pos++] = 'O';
                                gdbstub_client_respond_hex(client, (uint8_t *) client->packet, client->packet_pos);
                                gdbstub_client_respond_partial(client);
                            }
                            client->packet_pos = sprintf(client->packet, "%04X:", j + i);
                        }
                        /* Act according to I/O operation width. */
                        switch (l) {
                            case 'd':
                            case 'l':
                                client->packet_pos += sprintf(&client->packet[client->packet_pos], " %08X", inl(j + i));
                                i += 4;
                                break;

                            case 'w':
                                client->packet_pos += sprintf(&client->packet[client->packet_pos], " %04X", inw(j + i));
                                i += 2;
                                break;

                            case 'b':
                            case '\0':
                                client->packet_pos += sprintf(&client->packet[client->packet_pos], " %02X", inb(j + i));
                                i++;
                                break;

                            default:
                                goto unknown;
                        }
                    }
                    client->packet[client->packet_pos++] = '\n';

                    /* Respond with the final line. */
                    client->response_pos = 0;
                    gdbstub_client_respond_hex(client, (uint8_t *) &client->packet, client->packet_pos);
                    break;
                } else if (p[0] == 'o') {
                    /* Read I/O operation width. */
                    l = (i < 1) ? '\0' : p[i];

                    /* Read optional I/O port. */
                    if (!(p = strtok_r(NULL, " ", &strtok_save)) || !gdbstub_num_decode(p, &j, GDB_MODE_HEX) || (j < 0) || (j >= 65536))
                        j = -1;

                    /* Read optional value. */
                    if (!(p = strtok_r(NULL, " ", &strtok_save)) || !gdbstub_num_decode(p, &k, GDB_MODE_HEX)) {
                        if (j == -1)
                            k = client->last_io_value;
                        else
                            k = j; /* only one specified = treat as value on last port */
                        j = -1;
                    }
                    if (j == -1)
                        j = client->last_io_base;
                    else
                        client->last_io_base = j;
                    client->last_io_value = k;

                    /* Write port. */
                    switch (l) {
                        case 'd':
                        case 'l':
                            outl(j, k);
                            break;

                        case 'w':
                            outw(j, k);
                            break;

                        case 'b':
                        case 't':
                        case '\0':
                            outb(j, k);
                            break;

                        default:
                            goto unknown;
                    }
                } else if (p[0] == 'r') {
                    pc_reset_hard();
                } else if ((p[0] == '?') || !strcmp(p, "help")) {
                    FAST_RESPONSE_HEX(
                        "Commands:\n"
                        "- ib/iw/il [port [length]] - Read {length} (default 1) I/O ports starting from {port} (default last)\n"
                        "- ob/ow/ol [[port] value] - Write {value} to I/O {port} (both default last)\n"
                        "- r - Hard reset the emulated machine\n");
                    break;
                } else {
unknown:
                    FAST_RESPONSE_HEX("Unknown command\n");
                    break;
                }

                goto ok;
            }
            break;

        case 'z': /* remove break/watchpoint */
        case 'Z': /* insert break/watchpoint */
            gdbstub_breakpoint_t *breakpoint, *prev_breakpoint = NULL, **first_breakpoint;

            /* Parse breakpoint type. */
            switch (client->packet[1]) {
                case '0': /* software breakpoint */
                    first_breakpoint = &first_swbreak;
                    break;

                case '1': /* hardware breakpoint */
                    first_breakpoint = &first_hwbreak;
                    break;

                case '2': /* write watchpoint */
                    first_breakpoint = &first_wwatch;
                    break;

                case '3': /* read watchpoint */
                    first_breakpoint = &first_rwatch;
                    break;

                case '4': /* access watchpoint */
                    first_breakpoint = &first_awatch;
                    break;

                default:                      /* unknown type */
                    client->packet[2] = '\0'; /* force address check to fail */
                    break;
            }

            /* Read address. */
            if (client->packet[2] != ',')
                break;
            client->packet_pos = 3;
            if (!(i = gdbstub_client_read_word(client, &j)))
                break;
            client->packet_pos += i;
            if (client->packet[client->packet_pos++] == ',')
                gdbstub_client_read_word(client, &k);
            else
                k = 1;

            /* Test writability of software breakpoint. */
            if (client->packet[1] == '0') {
                buf[0] = readmembl(j);
                writemembl(j, 0xcc);
                buf[1] = readmembl(j);
                writemembl(j, buf[0]);
                if (buf[1] != 0xcc)
                    goto end;
            }

            /* Find an existing breakpoint with this address. */
            breakpoint = *first_breakpoint;
            while (breakpoint) {
                if (breakpoint->addr == j)
                    break;
                prev_breakpoint = breakpoint;
                breakpoint      = breakpoint->next;
            }

            /* Check if the breakpoint is already present (when inserting) or not found (when removing). */
            if ((!!breakpoint) ^ (client->packet[0] == 'z'))
                goto e22;

            /* Insert or remove the breakpoint. */
            if (client->packet[0] != 'z') {
                /* Allocate a new breakpoint. */
                breakpoint       = malloc(sizeof(gdbstub_breakpoint_t));
                breakpoint->addr = j;
                breakpoint->end  = j + k;
                breakpoint->next = NULL;

                /* Add the new breakpoint to the list. */
                if (!(*first_breakpoint))
                    *first_breakpoint = breakpoint;
                else if (prev_breakpoint)
                    prev_breakpoint->next = breakpoint;
            } else {
                /* Remove breakpoint from the list. */
                if (breakpoint == *first_breakpoint)
                    *first_breakpoint = breakpoint->next;
                else if (prev_breakpoint)
                    prev_breakpoint->next = breakpoint->next;

                /* De-allocate breakpoint. */
                free(breakpoint);
            }

            /* Update the page watchpoint map if we're dealing with a watchpoint. */
            if (client->packet[1] >= '2') {
                /* Clear this watchpoint's corresponding page map groups,
                   as everything is going to be recomputed soon anyway. */
                memset(&gdbstub_watch_pages[j >> (MEM_GRANULARITY_BITS + 6)], 0,
                       (((k - 1) >> (MEM_GRANULARITY_BITS + 6)) + 1) * sizeof(gdbstub_watch_pages[0]));

                /* Go through all watchpoint lists. */
                l          = 0;
                breakpoint = first_rwatch;
                while (1) {
                    if (breakpoint) {
                        /* Flag this watchpoint's corresponding pages as having a watchpoint. */
                        k = (breakpoint->end - 1) >> MEM_GRANULARITY_BITS;
                        for (i = breakpoint->addr >> MEM_GRANULARITY_BITS; i <= k; i++)
                            gdbstub_watch_pages[i >> 6] |= (1 << (i & 63));

                        breakpoint = breakpoint->next;
                    } else {
                        /* Jump from list to list as a shortcut. */
                        if (l == 0)
                            breakpoint = first_wwatch;
                        else if (l == 1)
                            breakpoint = first_awatch;
                        else
                            break;
                        l++;
                    }
                }
            }

            /* Respond positively. */
            goto ok;
    }
end:
    /* Send response. */
    gdbstub_client_respond(client);
}

static void
gdbstub_cpu_exec(int cycs)
{
    /* Flag that we're now in the debugger context to avoid triggering watchpoints. */
    in_gdbstub = 1;

    /* Handle CPU execution if it isn't paused. */
    if (gdbstub_step <= GDBSTUB_SSTEP) {
        /* Swap in any software breakpoints. */
        gdbstub_breakpoint_t *swbreak = first_swbreak;
        while (swbreak) {
            /* Swap the INT 3 opcode into the address. */
            swbreak->orig_val = readmembl(swbreak->addr);
            writemembl(swbreak->addr, 0xcc);
            swbreak = swbreak->next;
        }

        /* Call the original cpu_exec function outside the debugger context. */
        if ((gdbstub_step == GDBSTUB_SSTEP) && ((cycles + cycs) <= 0))
            cycs += -(cycles + cycs) + 1;
        in_gdbstub = 0;
        cpu_exec_shadow(cycs);
        in_gdbstub = 1;

        /* Swap out any software breakpoints. */
        swbreak = first_swbreak;
        while (swbreak) {
            if (readmembl(swbreak->addr) == 0xcc)
                writemembl(swbreak->addr, swbreak->orig_val);
            swbreak = swbreak->next;
        }
    }

    /* Populate stop reason if we have stopped. */
    stop_reason_len = 0;
    if (gdbstub_step > GDBSTUB_EXEC) {
        /* Assemble stop reason manually, avoiding sprintf and friends for performance. */
        stop_reason[stop_reason_len++] = 'T';
        stop_reason[stop_reason_len++] = '0';
        stop_reason[stop_reason_len++] = '0' + ((gdbstub_step == GDBSTUB_BREAK) ? GDB_SIGINT : GDB_SIGTRAP);

        /* Add extended break reason. */
        if (gdbstub_step >= GDBSTUB_BREAK_RWATCH) {
            if (gdbstub_step != GDBSTUB_BREAK_WWATCH)
                stop_reason[stop_reason_len++] = (gdbstub_step == GDBSTUB_BREAK_RWATCH) ? 'r' : 'a';
            stop_reason[stop_reason_len++] = 'w';
            stop_reason[stop_reason_len++] = 'a';
            stop_reason[stop_reason_len++] = 't';
            stop_reason[stop_reason_len++] = 'c';
            stop_reason[stop_reason_len++] = 'h';
            stop_reason[stop_reason_len++] = ':';
            stop_reason_len += sprintf(&stop_reason[stop_reason_len], "%X;", watch_addr);
        } else if (gdbstub_step >= GDBSTUB_BREAK_SW) {
            stop_reason[stop_reason_len++] = (gdbstub_step == GDBSTUB_BREAK_SW) ? 's' : 'h';
            stop_reason[stop_reason_len++] = 'w';
            stop_reason[stop_reason_len++] = 'b';
            stop_reason[stop_reason_len++] = 'r';
            stop_reason[stop_reason_len++] = 'e';
            stop_reason[stop_reason_len++] = 'a';
            stop_reason[stop_reason_len++] = 'k';
            stop_reason[stop_reason_len++] = ':';
            stop_reason[stop_reason_len++] = ';';
        }

        /* Add register dump. */
        uint8_t buf[10] = { 0 };
        int     i, j, k;
        for (i = 0; i < GDB_REG_MAX; i++) {
            if (i >= 0x10)
                stop_reason[stop_reason_len++] = gdbstub_hex_encode(i >> 4);
            stop_reason[stop_reason_len++] = gdbstub_hex_encode(i & 0x0f);
            stop_reason[stop_reason_len++] = ':';
            j                              = gdbstub_client_read_reg(i, buf);
            for (k = 0; k < j; k++) {
                stop_reason[stop_reason_len++] = gdbstub_hex_encode(buf[k] >> 4);
                stop_reason[stop_reason_len++] = gdbstub_hex_encode(buf[k] & 0x0f);
            }
            stop_reason[stop_reason_len++] = ';';
        }

        /* Don't execute the CPU any further if single-stepping. */
        gdbstub_step = GDBSTUB_BREAK;
    }

    /* Return the framerate to normal. */
    gdbstub_next_asap = 0;

    /* Process client packets. */
    thread_wait_mutex(client_list_mutex);
    gdbstub_client_t *client = first_client;
    while (client) {
        /* Report stop reason if the client is waiting for one. */
        if (client->waiting_stop && stop_reason_len) {
            client->waiting_stop = 0;

            /* Wait for any pending responses to be acknowledged. */
            if (!thread_wait_event(client->response_event, -1)) {
                /* Block other responses from being written while this one isn't acknowledged. */
                thread_reset_event(client->response_event);

                /* Write stop reason response. */
                strcpy(client->response, stop_reason);
                client->response_pos = stop_reason_len;
                gdbstub_client_respond(client);
            } else {
                gdbstub_log("GDB Stub: Timed out waiting for client %s:%d\n", inet_ntoa(client->addr.sin_addr), client->addr.sin_port);
            }
        }

        if (client->has_packet) {
            gdbstub_client_packet(client);
            client->has_packet = client->packet_pos = 0;
            thread_set_event(client->processed_event);
        }

#ifdef GDBSTUB_ALLOW_MULTI_CLIENTS
        client = client->next;
#else
        break;
#endif
    }
    thread_release_mutex(client_list_mutex);

    /* Flag that we're now out of the debugger context. */
    in_gdbstub = 0;
}

static void
gdbstub_client_thread(void *priv)
{
    gdbstub_client_t *client = (gdbstub_client_t *) priv;
    uint8_t           buf[256];
    ssize_t           bytes_read;
    int               i;

    gdbstub_log("GDB Stub: New connection from %s:%d\n", inet_ntoa(client->addr.sin_addr), client->addr.sin_port);

    /* Allow packets to be processed. */
    thread_set_event(client->processed_event);

    /* Read data from client. */
    while ((bytes_read = recv(client->socket, (char *) buf, sizeof(buf), 0)) > 0) {
        for (i = 0; i < bytes_read; i++) {
            switch (buf[i]) {
                case '$': /* packet start */
                    /* Wait for any existing packets to be processed. */
                    thread_wait_event(client->processed_event, -1);

                    client->packet_pos = 0;
                    break;

                case '-': /* negative acknowledgement */
                    /* Retransmit the current response. */
                    gdbstub_client_respond(client);
                    break;

                case '+': /* positive acknowledgement */
                    /* Allow another response to be written. */
                    thread_set_event(client->response_event);
                    break;

                case 0x03: /* break */
                    /* Wait for any existing packets to be processed. */
                    thread_wait_event(client->processed_event, -1);

                    /* Break immediately. */
                    gdbstub_log("GDB Stub: Break requested\n");
                    gdbstub_break();
                    break;

                default:
                    /* Wait for any existing packets to be processed, just in case. */
                    thread_wait_event(client->processed_event, -1);

                    if (client->packet_pos < (sizeof(client->packet) - 1)) {
                        /* Append byte to the packet. */
                        client->packet[client->packet_pos++] = buf[i];

                        /* Check if we're at the end of a packet. */
                        if ((client->packet_pos >= 3) && (client->packet[client->packet_pos - 3] == '#')) { /* packet checksum start */
                            /* Small hack to speed up IDA instruction trace mode. */
                            if (*((uint32_t *) client->packet) == ('H' | ('c' << 8) | ('1' << 16) | ('#' << 24))) {
                                /* Send pre-computed response. */
                                send(client->socket, "+$OK#9A", 7, 0);

                                /* Skip processing. */
                                continue;
                            }

                            /* Flag that a packet should be processed. */
                            client->packet[client->packet_pos] = '\0';
                            thread_reset_event(client->processed_event);
                            gdbstub_next_asap = client->has_packet = 1;
                        }
                    }
                    break;
            }
        }
    }

    gdbstub_log("GDB Stub: Connection with %s:%d broken\n", inet_ntoa(client->addr.sin_addr), client->addr.sin_port);

    /* Close socket. */
    if (client->socket != -1) {
        close(client->socket);
        client->socket = -1;
    }

    /* Unblock anyone waiting on the response event. */
    thread_set_event(client->response_event);

    /* Remove this client from the list. */
    thread_wait_mutex(client_list_mutex);
#ifdef GDBSTUB_ALLOW_MULTI_CLIENTS
    if (client == first_client) {
#endif
        first_client = client->next;
        if (first_client == NULL) {
            last_client  = NULL;
            gdbstub_step = GDBSTUB_EXEC; /* unpause CPU when all clients are disconnected */
        }
#ifdef GDBSTUB_ALLOW_MULTI_CLIENTS
    } else {
        other_client = first_client;
        while (other_client) {
            if (other_client->next == client) {
                if (last_client == client)
                    last_client = other_client;
                other_client->next = client->next;
                break;
            }
            other_client = other_client->next;
        }
    }
#endif

    free(client);
    thread_release_mutex(client_list_mutex);
}

static void
gdbstub_server_thread(void *priv)
{
    /* Listen on GDB socket. */
    listen(gdbstub_socket, 1);

    /* Accept connections. */
    gdbstub_client_t *client;
    socklen_t         sl = sizeof(struct sockaddr_in);
    while (1) {
        /* Allocate client structure. */
        client = malloc(sizeof(gdbstub_client_t));
        memset(client, 0, sizeof(gdbstub_client_t));
        client->processed_event = thread_create_event();
        client->response_event  = thread_create_event();

        /* Accept connection. */
        client->socket = accept(gdbstub_socket, (struct sockaddr *) &client->addr, &sl);
        if (client->socket < 0)
            break;

        /* Add to client list. */
        thread_wait_mutex(client_list_mutex);
        if (first_client) {
#ifdef GDBSTUB_ALLOW_MULTI_CLIENTS
            last_client->next = client;
            last_client       = client;
#else
            first_client->next = last_client = client;
            close(first_client->socket);
#endif
        } else {
            first_client = last_client = client;
        }
        thread_release_mutex(client_list_mutex);

        /* Pause CPU execution. */
        gdbstub_break();

        /* Start client thread. */
        thread_create(gdbstub_client_thread, client);
    }

    /* Deallocate the redundant client structure. */
    thread_destroy_event(client->processed_event);
    thread_destroy_event(client->response_event);
    free(client);
}

void
gdbstub_cpu_init()
{
    /* Replace cpu_exec with our own function if the GDB stub is active. */
    if ((gdbstub_socket != -1) && (cpu_exec != gdbstub_cpu_exec)) {
        cpu_exec_shadow = cpu_exec;
        cpu_exec        = gdbstub_cpu_exec;
    }
}

int
gdbstub_instruction()
{
    /* Check hardware breakpoints if any are present. */
    gdbstub_breakpoint_t *breakpoint = first_hwbreak;
    if (breakpoint) {
        /* Calculate the current instruction's address. */
        uint32_t wanted_addr = cs + cpu_state.pc;

        /* Go through the list of software breakpoints. */
        do {
            /* Check if the breakpoint coincides with this address. */
            if (breakpoint->addr == wanted_addr) {
                gdbstub_log("GDB Stub: Hardware breakpoint at %08X\n", wanted_addr);

                /* Flag that we're in a hardware breakpoint. */
                gdbstub_step = GDBSTUB_BREAK_HW;

                /* Pause execution. */
                return 1;
            }

            breakpoint = breakpoint->next;
        } while (breakpoint);
    }

    /* No breakpoint found, continue execution or stop if execution is paused. */
    return gdbstub_step - GDBSTUB_EXEC;
}

int
gdbstub_int3()
{
    /* Check software breakpoints if any are present. */
    gdbstub_breakpoint_t *breakpoint = first_swbreak;
    if (breakpoint) {
        /* Calculate the breakpoint instruction's address. */
        uint32_t new_pc = cpu_state.pc - 1;
        if (cpu_state.op32)
            new_pc &= 0xffff;
        uint32_t wanted_addr = cs + new_pc;

        /* Go through the list of software breakpoints. */
        do {
            /* Check if the breakpoint coincides with this address. */
            if (breakpoint->addr == wanted_addr) {
                gdbstub_log("GDB Stub: Software breakpoint at %08X\n", wanted_addr);

                /* Move EIP back to where the break instruction was. */
                cpu_state.pc = new_pc;

                /* Flag that we're in a software breakpoint. */
                gdbstub_step = GDBSTUB_BREAK_SW;

                /* Abort INT 3 execution. */
                return 1;
            }

            breakpoint = breakpoint->next;
        } while (breakpoint);
    }

    /* No breakpoint found, continue INT 3 execution as normal. */
    return 0;
}

void
gdbstub_mem_access(uint32_t *addrs, int access)
{
    /* Stop if we're in the debugger context. */
    if (in_gdbstub)
        return;

    int width = access & (GDBSTUB_MEM_WRITE - 1), i;

    /* Go through the lists of watchpoints for this type of access. */
    gdbstub_breakpoint_t *watchpoint = (access & GDBSTUB_MEM_WRITE) ? first_wwatch : first_rwatch;
    while (1) {
        if (watchpoint) {
            /* Check if any component of this address is within the breakpoint's range. */
            for (i = 0; i < width; i++) {
                if ((addrs[i] >= watchpoint->addr) && (addrs[i] < watchpoint->end))
                    break;
            }
            if (i < width) {
                gdbstub_log("GDB Stub: %s watchpoint at %08X\n", (access & GDBSTUB_MEM_AWATCH) ? "Access" : ((access & GDBSTUB_MEM_WRITE) ? "Write" : "Read"), watch_addr);

                /* Flag that we're in a read/write watchpoint. */
                gdbstub_step = (access & GDBSTUB_MEM_AWATCH) ? GDBSTUB_BREAK_AWATCH : ((access & GDBSTUB_MEM_WRITE) ? GDBSTUB_BREAK_WWATCH : GDBSTUB_BREAK_RWATCH);

                /* Stop looking. */
                return;
            }

            watchpoint = watchpoint->next;
        } else {
            /* Jump from list to list as a shortcut. */
            if (access & GDBSTUB_MEM_AWATCH) {
                break;
            } else {
                watchpoint = first_awatch;
                access |= GDBSTUB_MEM_AWATCH;
            }
        }
    }
}

void
gdbstub_init()
{
#ifdef _WIN32
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    /* Create GDB server socket. */
    if ((gdbstub_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        pclog("GDB Stub: Failed to create socket\n");
        return;
    }

    /* Bind GDB server socket. */
    int                port      = 12345;
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr   = { .s_addr = INADDR_ANY },
        .sin_port   = htons(port)
    };
    if (bind(gdbstub_socket, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) == -1) {
        pclog("GDB Stub: Failed to bind on port %d (%d)\n", port,
#ifdef _WIN32
              WSAGetLastError()
#else
              errno
#endif
        );
        gdbstub_socket = -1;
        return;
    }

    /* Create client list mutex. */
    client_list_mutex = thread_create_mutex();

    /* Clear watchpoint page map. */
    memset(gdbstub_watch_pages, 0, sizeof(gdbstub_watch_pages));

    /* Start server thread. */
    pclog("GDB Stub: Listening on port %d\n", port);
    thread_create(gdbstub_server_thread, NULL);

    /* Start the CPU paused. */
    gdbstub_step = GDBSTUB_BREAK;
}

void
gdbstub_close()
{
    /* Stop if the GDB server hasn't initialized. */
    if (gdbstub_socket < 0)
        return;

    /* Close GDB server socket. */
    close(gdbstub_socket);

    /* Clear client list. */
    thread_wait_mutex(client_list_mutex);
    gdbstub_client_t *client = first_client;
    int               socket;
    while (client) {
        socket         = client->socket;
        client->socket = -1;
        close(socket);
        client = client->next;
    }
    thread_release_mutex(client_list_mutex);
    thread_close_mutex(client_list_mutex);
}
