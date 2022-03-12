/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		GDB stub server for remote debugging.
 *
 *
 *
 * Authors:	RichardG, <richardg867@gmail.com>
 *
 *		Copyright 2022 RichardG.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
#else
# include <arpa/inet.h>
# include <sys/socket.h>
#endif
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86seg.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/plat.h>


#define GDBSTUB_MAX_REG 38

enum {
    GDB_SIGINT = 2,
    GDB_SIGTRAP = 5
};

typedef struct _gdbstub_client_ {
    int                socket;
    struct sockaddr_in addr;

    char packet[16384], response[16384];
    int  packet_pos, response_pos;

    event_t *response_event;

    struct _gdbstub_client_ *next;
} gdbstub_client_t;

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
#define gdbstub_log(fmt, ...)
#endif

static x86seg *segment_regs[] = {&cpu_state.seg_cs, &cpu_state.seg_ss, &cpu_state.seg_ds, &cpu_state.seg_es, &cpu_state.seg_fs, &cpu_state.seg_gs};
static uint32_t *cr_regs[] = {&cpu_state.CR0.l, &cr2, &cr3, &cr4};
static void *fpu_regs[] = {&cpu_state.npxc, &cpu_state.npxs, NULL, x87_pc_seg, x87_pc_off, x87_op_seg, x87_op_off};
static const char target_xml[] = /* based on qemu's i386-32bit.xml */
    "<?xml version=\"1.0\"?>"
    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
    "<target>"
        "<architecture>i8086</architecture>"
        "<feature name=\"org.gnu.gdb.i386.core\">"
                "<flags id=\"i386_eflags\" size=\"4\">"
                        "<field name=\"ID\" start=\"21\" end=\"21\"/>"
                        "<field name=\"VIP\" start=\"20\" end=\"20\"/>"
                        "<field name=\"VIF\" start=\"19\" end=\"19\"/>"
                        "<field name=\"AC\" start=\"18\" end=\"18\"/>"
                        "<field name=\"VM\" start=\"17\" end=\"17\"/>"
                        "<field name=\"RF\" start=\"16\" end=\"16\"/>"
                        "<field name=\"NT\" start=\"14\" end=\"14\"/>"
                        "<field name=\"IOPL\" start=\"12\" end=\"13\"/>"
                        "<field name=\"OF\" start=\"11\" end=\"11\"/>"
                        "<field name=\"DF\" start=\"10\" end=\"10\"/>"
                        "<field name=\"IF\" start=\"9\" end=\"9\"/>"
                        "<field name=\"TF\" start=\"8\" end=\"8\"/>"
                        "<field name=\"SF\" start=\"7\" end=\"7\"/>"
                        "<field name=\"ZF\" start=\"6\" end=\"6\"/>"
                        "<field name=\"AF\" start=\"4\" end=\"4\"/>"
                        "<field name=\"PF\" start=\"2\" end=\"2\"/>"
                        "<field name=\"CF\" start=\"0\" end=\"0\"/>"
                "</flags>"
                ""
                "<reg name=\"eax\" bitsize=\"32\" type=\"int32\"/>"
                "<reg name=\"ecx\" bitsize=\"32\" type=\"int32\"/>"
                "<reg name=\"edx\" bitsize=\"32\" type=\"int32\"/>"
                "<reg name=\"ebx\" bitsize=\"32\" type=\"int32\"/>"
                "<reg name=\"esp\" bitsize=\"32\" type=\"data_ptr\"/>"
                "<reg name=\"ebp\" bitsize=\"32\" type=\"data_ptr\"/>"
                "<reg name=\"esi\" bitsize=\"32\" type=\"int32\"/>"
                "<reg name=\"edi\" bitsize=\"32\" type=\"int32\"/>"
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
                "<reg name=\"fs_base\" bitsize=\"32\" type=\"int32\"/>"
                "<reg name=\"gs_base\" bitsize=\"32\" type=\"int32\"/>"
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
        "</feature>"
    "</target>";

#ifdef _WIN32
static WSADATA           wsa;
#endif
static int               gdbstub_socket = -1, gdbstub_paused = 0;
static char              stop_reason[2048];
static gdbstub_client_t *first_client, *last_client;
static mutex_t          *client_list_mutex;
static void              (*cpu_exec_shadow)(int cycs);

int gdbstub_singlestep = 0;


static void
gdbstub_break()
{
    /* Initiate pause. */
    plat_pause(1);

    /* Force CPU execution to return as soon as possible. */
    gdbstub_singlestep = 1;
}


static void
gdbstub_singlestep_exec(int cycs)
{
    /* Call the original cpu_exec function. */
    cpu_exec_shadow(cycs);

    /* Swap the original function back in. */
    cpu_exec = cpu_exec_shadow;

    /* Break immediately. */
    gdbstub_break();
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
        return c - 10 + 'A';
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
        *buf    = gdbstub_hex_decode(client->packet[pp++]) << 4;
        *buf++ |= gdbstub_hex_decode(client->packet[pp++]);
    }
    return pp - client->packet_pos;
}


static int
gdbstub_client_read_string(gdbstub_client_t *client, char *buf, int size, char terminator)
{
    int pp = client->packet_pos;
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
        case 0 ... 7: /* [EAX:EDI] */
                cpu_state.regs[index].l = *((uint32_t *) buf);
                break;

        case 8: /* EIP */
                gdbstub_jump(*((uint32_t *) buf));
                break;

        case 9: /* EFLAGS */
                cpu_state.flags = *((uint16_t *) &buf[0]);
                cpu_state.eflags = *((uint16_t *) &buf[2]);
                break;

        case 10 ... 15: /* [CS:GS] */
                width = 2;
                loadseg(*((uint16_t *) buf), segment_regs[index - 10]);
                break;

        case 16 ... 17: /* FSbase, GSbase */
                /* Do what qemu does and just load the base. */
                segment_regs[index - 12]->base = *((uint32_t *) buf);
                break;

        case 18 ... 21: /* [CR0:CR4] */
                *cr_regs[index - 18] = *((uint32_t *) buf);
                break;

        case 22: /* EFER */
                msr.amd_efer = *((uint64_t *) buf);
                break;

        case 23 ... 30: /* ST(0:7) */
                width = 10;
                break;

        /* FPU CONTROL REGS */

        default:
                width = 0;
    }

    flushmmucache(); /* incredibly cursed to be calling that from here */

    return width;
}


static void
gdbstub_client_respond(gdbstub_client_t *client)
{
    /* Calculate checksum. */
    uint8_t checksum = 0;
    int i;
    for (i = 0; i < client->response_pos; i++)
        checksum += client->response[i];

    /* Send response packet. */
    client->response[client->response_pos] = '\0';
#ifdef ENABLE_GDBSTUB_LOG
    i = client->response[995]; /* pclog_ex buffer too small */
    client->response[995] = '\0';
    gdbstub_log("GDB Stub: Sending response: %s\n", client->response);
    client->response[995] = i;
#endif
    send(client->socket, "$", 1, 0);
    send(client->socket, client->response, client->response_pos, 0);
    char response_cksum[3] = {'#', gdbstub_hex_encode(checksum >> 4), gdbstub_hex_encode(checksum & 0x0f)};
    send(client->socket, response_cksum, sizeof(response_cksum), 0);
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
        case 0 ... 7: /* [EAX:EDI] */
                *((uint32_t *) buf) = cpu_state.regs[index].l;
                break;

        case 8: /* EIP */
                *((uint32_t *) buf) = cs + cpu_state.pc;
                break;

        case 9: /* EFLAGS */
                *((uint16_t *) &buf[0]) = cpu_state.flags;
                *((uint16_t *) &buf[2]) = cpu_state.eflags;
                break;

        case 10 ... 15: /* [CS:GS] */
                *((uint16_t *) buf) = segment_regs[index - 10]->seg;
                break;

        case 16 ... 17: /* FSbase, GSbase */
                *((uint32_t *) buf) = segment_regs[index - 12]->base;
                break;

        case 18 ... 21: /* [CR0:CR4] */
                *((uint32_t *) buf) = *cr_regs[index - 18];
                break;

        case 22: /* EFER */
                *((uint64_t *) buf) = msr.amd_efer;
                break;

        case 23 ... 30: /* ST(0:7) */
                width = 10;
                *((uint64_t *) &buf[0]) = 0;
                *((uint16_t *) &buf[8]) = 0;
                break;

        case 31 ... 32: /* [FCTRL:FSTAT] */
        case 34: /* [FISEG] */
        case 36: /* [FOSEG] */
                width = 2;
                *((uint16_t *) buf) = *((uint16_t *) fpu_regs[index - 31]);
                break;

        case 33: /* FTAG */
                width = 2;
                *((uint16_t *) buf) = x87_gettag();
                break;

        case 35: /* [FIOFF] */
        case 37: /* [FOOFF] */
                *((uint32_t *) buf) = *((uint32_t *) fpu_regs[index - 31]);
                break;

        case 38: /* [FOP] */
                width = 2;
                *((uint16_t *) buf) = 0; /* we don't store the FPU opcode */
                break;

        default:
                width = 0;
    }

    return width;
}


static void
gdbstub_client_packet(gdbstub_client_t *client)
{
    uint8_t rcv_checksum = 0, checksum = 0;
    int i, j = 0, k = 0, l;
    uint8_t buf[10] = {0};
    char *p;

    /* Validate checksum. */
    client->packet_pos -= 2;
    gdbstub_client_read_hex(client, &rcv_checksum, 1);
    client->packet[client->packet_pos] = '\0';
    client->packet[client->packet_pos + 1] = '\0';
    client->packet[--client->packet_pos] = '\0';
    for (i = 0; i < client->packet_pos; i++)
        checksum += client->packet[i];

#if 0 /* msys2 gdb 11.1 transmits qSupported and H with invalid checksum... */
    if (checksum != rcv_checksum) {
        /* Send negative acknowledgement. */
#ifdef ENABLE_GDBSTUB_LOG
        i = client->packet[953]; /* pclog_ex buffer too small */
        client->packet[953] = '\0';
        gdbstub_log("GDB Stub: Received packet with invalid checksum (expected %02X got %02X): %s\n", checksum, rcv_checksum, client->packet);
        client->packet[953] = i;
#endif
        send(client->socket, "-", 1, 0);
        return;
    }
#endif

    /* Send positive acknowledgement. */
#ifdef ENABLE_GDBSTUB_LOG
    i = client->packet[996]; /* pclog_ex buffer too small */
    client->packet[996] = '\0';
    gdbstub_log("GDB Stub: Received packet: %s\n", client->packet);
    client->packet[996] = i;
#endif
    send(client->socket, "+", 1, 0);

    /* Block other responses from being written while this one isn't acknowledged. */
    thread_wait_event(client->response_event, 0);
    thread_reset_event(client->response_event);
    client->response_pos = 0;
    client->packet_pos = 1;

    /* Parse command. */
    switch (client->packet[0]) {
        case '?': /* stop reason */
                /* Respond with a stop reply packet. */
                strcpy(client->response, stop_reason);
                client->response_pos = strlen(client->response);
                break;

        case 'c': /* continue */
        case 's': /* step */
                /* No immediate response. */
                thread_set_event(client->response_event);

                /* Jump to address if specified. */
                if (gdbstub_client_read_word(client, &j))
                        gdbstub_jump(j);

                /* Resume emulation. */
                if (client->packet[0] == 'c') {
                        gdbstub_singlestep = 0;
                } else {
                        /* Replace cpu_exec with our own function, which breaks after a single step. */
                        gdbstub_singlestep = 1;
                        if (cpu_exec != gdbstub_singlestep_exec)
                                cpu_exec_shadow = cpu_exec;
                        cpu_exec = gdbstub_singlestep_exec;
                }
                gdbstub_paused = 0;
                plat_pause(0);
                return;

        case 'D': /* detach */
                /* Resume emulation. */
                gdbstub_paused = 0;
                plat_pause(0);

                /* Respond positively. */
                client->response_pos = sprintf(client->response, "OK");
                break;

        case 'g': /* read all registers */
                /* Output the values of all registers. */
                for (i = 0; i <= GDBSTUB_MAX_REG; i++)
                        gdbstub_client_respond_hex(client, buf, gdbstub_client_read_reg(i, buf));
                break;

        case 'G': /* write all registers */
                /* Write the values of all registers. */
                for (i = 0; i <= GDBSTUB_MAX_REG; i++) {
                        if (!gdbstub_client_read_hex(client, buf, sizeof(buf)))
                                break;
                        client->packet_pos += gdbstub_client_write_reg(i, buf);
                }
                break;

        case 'H': /* set thread */
                /* Read operation type and thread ID. */
                if ((client->packet[1] == '\0') || (client->packet[2] == '\0')) {
e22:                    client->response_pos = sprintf(client->response, "E22");
                        break;
                }

                /* Respond positively only on thread 1. */
                if ((client->packet[2] == '1') && !client->packet[3])
                        client->response_pos = sprintf(client->response, "OK");
                else
                        goto e22;
                break;

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
                client->response_pos = sprintf(client->response, "OK");
                break;

        case 'p': /* read register */
                /* Read register index. */
                if (!gdbstub_client_read_word(client, &j)) {
e14:                    client->response_pos = sprintf(client->response, "E14");
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
                client->response_pos = sprintf(client->response, "OK");
                break;

        case 'q': /* query */
                /* Erase response, as we'll use it as a scratch buffer. */
                memset(client->response, 0, sizeof(client->response));

                /* Read the query type. */
                client->packet_pos += gdbstub_client_read_string(client, client->response, sizeof(client->response) - 1, ':') + 1;

                /* Perform the query. */
                if (!strcmp(client->response, "Supported")) {
                        /* Go through the feature list and negate ones we don't support. */
                        while ((client->response_pos < (sizeof(client->response) - 1)) &&
                               (i = gdbstub_client_read_string(client, &client->response[client->response_pos], sizeof(client->response) - client->response_pos - 1, ';'))) {
                                client->packet_pos += i + 1;
                                if (strncmp(&client->response[client->response_pos], "PacketSize", 10) &&
                                    strcmp(&client->response[client->response_pos], "swbreak") &&
                                    strcmp(&client->response[client->response_pos], "hwbreak") &&
                                    strncmp(&client->response[client->response_pos], "xmlRegisters", 12) &&
                                    strcmp(&client->response[client->response_pos], "qXfer:features:read")) {
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
                                                                 "PacketSize=%X;swbreak+;hwbreak+;qXfer:features:read+", sizeof(client->packet) - 1);
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
e00:                                            client->response_pos = sprintf(client->response, "E00");
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
                                                k = l - j;
                                        }

                                        /* Encode the data. */
                                        while (k--) {
                                                i = *p++;
                                                if ((i == '#') || (i == '$') || (i == '*') || (i == '}')) {
                                                        client->response[client->response_pos++] = '}';
                                                        client->response[client->response_pos++] = i ^ 0x20;
                                                } else {
                                                        client->response[client->response_pos++] = i;
                                                }
                                        }
                                        break;
                                }
                        }
                }

                /* No response by default. */
                client->response_pos = 0;
                break;
    }

    /* Send response. */
    gdbstub_client_respond(client);
}


static void
gdbstub_client_thread(void *priv)
{
    gdbstub_client_t *client = (gdbstub_client_t *) priv, *other_client;
    uint8_t           buf[256];
    ssize_t           bytes_read;
    int               i;

    gdbstub_log("GDB Stub: New connection from %s:%d\n", inet_ntoa(client->addr.sin_addr), client->addr.sin_port);

    /* Read data from client. */
    while ((bytes_read = recv(client->socket, (char *) buf, sizeof(buf), 0)) > 0) {
        for (i = 0; i < bytes_read; i++) {
                switch (buf[i]) {
                        case '$': /* packet start */
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
                                /* Break immediately. */
                                gdbstub_log("GDB Stub: Break requested\n");
                                gdbstub_paused = 0;
                                gdbstub_break();
                                break;

                        default:
                                if (client->packet_pos < (sizeof(client->packet) - 1)) {
                                        /* Append byte to the packet. */
                                        client->packet[client->packet_pos++] = buf[i];

                                        /* Check if this is the end of a packet. */
                                        if ((client->packet_pos >= 3) && (client->packet[client->packet_pos - 3] == '#')) { /* packet checksum start */
                                                gdbstub_client_packet(client);
                                                client->packet_pos = 0;
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
    if (client == first_client) {
        first_client = client->next;
        if (first_client == NULL)
                last_client = NULL;
        gdbstub_paused = 0; /* allow user to unpause when all clients are disconnected */
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
    socklen_t sl = sizeof(struct sockaddr_in);
    while (1) {
        /* Allocate client structure. */
        client = malloc(sizeof(gdbstub_client_t));
        memset(client, 0, sizeof(gdbstub_client_t));
        client->response_event = thread_create_event();

        /* Accept connection. */
        client->socket = accept(gdbstub_socket, (struct sockaddr *) &client->addr, &sl);
        if (client->socket < 0)
                break;

        /* Add to client list. */
        thread_wait_mutex(client_list_mutex);
        if (first_client) {
                last_client->next = client;
                last_client = client;
        } else {
                first_client = last_client = client;
        }
        thread_release_mutex(client_list_mutex);

        /* Pause emulation. */
        gdbstub_paused = 1;
        gdbstub_break();

        /* Start client thread. */
        thread_create(gdbstub_client_thread, client);
    }

    /* Deallocate the redundant client structure. */
    thread_destroy_event(client->response_event);
    free(client);
}


void
gdbstub_pause(int *p)
{
    if (!(*p) && gdbstub_paused) {
        /* Don't allow the user to unpause if we're pausing. */
        gdbstub_log("GDB Stub: Blocked user unpause\n");
        *p = 1;
    } else if (*p) {
        sprintf(stop_reason, "S%02X", gdbstub_singlestep ? GDB_SIGTRAP : GDB_SIGINT);
        if (!gdbstub_paused) {
                /* Send interrupt packet to all clients. */
                gdbstub_log("GDB Stub: Pausing\n");
                gdbstub_paused = 1;
                thread_wait_mutex(client_list_mutex);
                gdbstub_client_t *client = first_client;
                while (client) {
                        if (!thread_wait_event(client->response_event, -1)) {
                                /* Block other responses from being written while this one isn't acknowledged. */
                                thread_reset_event(client->response_event);

                                /* Write stop reply packet. */
                                client->response_pos = sprintf(client->response, "%s", stop_reason);
                                gdbstub_client_respond(client);
                        } else {
                                gdbstub_log("GDB Stub: Timed out waiting for client %08X\n", client);
                        }
                        client = client->next;
                }
                thread_release_mutex(client_list_mutex);
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
    int port = 12345;
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = INADDR_ANY },
        .sin_port = htons(port)
    };
    if (bind(gdbstub_socket, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) == -1) {
        pclog("GDB Stub: Failed to bind on port %d (%d)\n", port, WSAGetLastError());
        gdbstub_socket = -1;
        return;
    }

    /* Create client list mutex. */
    client_list_mutex = thread_create_mutex();

    /* Start server thread. */
    pclog("GDB Stub: Listening on port %d\n", port);
    thread_create(gdbstub_server_thread, NULL);
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
    int socket;
    while (client) {
        socket = client->socket;
        client->socket = -1;
        close(socket);
        client = client->next;
    }
    thread_release_mutex(client_list_mutex);
    thread_close_mutex(client_list_mutex);
}
