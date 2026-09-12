/*
 * 86Box memory dump server.
 *
 * A small TCP server for driving and inspecting the emulated machine
 * from outside the guest without ever suspending the CPU.  Memory reads
 * go straight to the emulated arrays (device pages through their
 * mapping handlers) and I/O writes use the same outb()/outw()/outl()
 * dispatch the CPU uses; reads may tear against the emulation thread
 * by design.  Enable per-VM with "memdump_port = <port>" under
 * [General] in 86box.cfg (0, the default, disables it).
 *
 * Line-based commands, one per line; the connection stays open until
 * "q".  The server emits a "- " prompt when the connection opens and
 * after every reply; every reply ends with an empty line, and bad
 * commands answer " ^ Error".  Numbers are hex, C/assembler style:
 * optional 0x prefix, optional trailing h/H, and addresses may be
 * absolute or 8086 segment:offset.
 *
 *   d  address [length]      dump memory, DEBUG-style lines
 *   dr address [length]      dump raw hex
 *   o[W|D] port value        I/O output (width sensed or forced)
 *   q                        quit
 *   ?                        help
 *
 * The full protocol description, the reference clients
 * (screenmon.py/guest_console.py) and the conformance suite live in
 * the 86Box-tool repository (MEMDUMP.md, memdump/).
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/io.h>
#include <86box/86box.h>
#include <cpu.h>
#include <86box/mem.h>
#include <86box/memdump.h>
#include <86box/thread.h>

#ifdef _WIN32
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

static int       memdump_socket = -1;
static volatile int memdump_running = 0;

/* Read one guest-physical byte without involving the CPU. */
static uint8_t
memdump_read_byte(uint32_t addr)
{
    uintptr_t      ptr = readlookup2[addr >> 12];
    mem_mapping_t *map;

    if (ptr != (uintptr_t) LOOKUP_INV)
        return *(uint8_t *) (ptr + addr);

    map = read_mapping[addr >> MEM_GRANULARITY_BITS];
    if (map && map->read_b)
        return map->read_b(addr, map->priv);

    return 0xff;
}

/* Parse one hexadecimal number: [0x|0X] <hex digits> [h|H].
   Returns 1 on success, with the value and the digit count. */
static int
memdump_parse_hex(const char *s, uint32_t *val, int *digits)
{
    uint32_t v = 0;
    int      n = 0;

    if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X')))
        s += 2;

    for (; s[n] != '\0'; n++) {
        const char c = s[n];
        int        d;

        if ((c >= '0') && (c <= '9'))
            d = c - '0';
        else if ((c >= 'a') && (c <= 'f'))
            d = (c - 'a') + 10;
        else if ((c >= 'A') && (c <= 'F'))
            d = (c - 'A') + 10;
        else
            break;

        if (n >= 8)
            return 0; /* does not fit in 32 bits */
        v = (v << 4) | (uint32_t) d;
    }

    if (n == 0)
        return 0;
    if ((s[n] == 'h') || (s[n] == 'H'))
        n++; /* skip the suffix for the junk check */
    if (s[n] != '\0')
        return 0; /* trailing junk */

    /* The h suffix is pure decoration: the digit count decides widths. */
    while ((n > 0) && ((s[n - 1] == 'h') || (s[n - 1] == 'H')))
        n--;

    *val    = v;
    *digits = n;
    return 1;
}

/* Parse an address: absolute, or 8086 segment:offset.  Segmented
   requests report the segment and offset so dumps can label lines
   DEBUG-style; reads always proceed linearly from (seg << 4) + off. */
static int
memdump_parse_address(char *s, uint32_t *addr, uint16_t *seg, uint16_t *off,
                      int *segmented)
{
    char     *colon = strchr(s, ':');
    uint32_t  part;
    uint32_t  part2;
    int       digits;

    if (colon != NULL) {
        *colon = '\0';
        if (!memdump_parse_hex(s, &part, &digits)
            || !memdump_parse_hex(colon + 1, &part2, &digits))
            return 0;
        *addr      = (part << 4) + part2;
        *seg       = (uint16_t) part;
        *off       = (uint16_t) part2;
        *segmented = 1;
        return 1;
    }

    if (!memdump_parse_hex(s, addr, &digits))
        return 0;
    *segmented = 0;
    return 1;
}

static const char *const memdump_help[] = {
    "dump                   D address [length]\n",
    "dump raw               DR address [length]\n",
    "output                 O[W|D] port value\n",
    "quit                   Q\n",
    "help                   ?\n",
    NULL
};

static void
memdump_server_thread(void *priv)
{
    struct sockaddr_in client_addr;
    socklen_t          client_len = sizeof(client_addr);
    char               request[128];
    char               reply[4096];
    char              *buf = NULL;
    uint32_t           addr;
    uint32_t           len;
    ssize_t            n;

    (void) priv;

    while (memdump_running) {
        int conn = accept(memdump_socket, (struct sockaddr *) &client_addr, &client_len);
        if (conn < 0) {
            if (!memdump_running)
                break;
            continue;
        }

        for (;;) {
            char *save = NULL;
            char *cmd;
            char *arg1;
            char *arg2;
            char *arg3;

            send(conn, "- ", 2, 0); /* DEBUG-style prompt */
            /* Read one command line. */
            n = 0;
            while (n < (ssize_t) (sizeof(request) - 1)) {
                ssize_t r = recv(conn, request + n, 1, 0);
                if (r <= 0)
                    goto close_conn;
                if (request[n] == '\n')
                    break;
                if (request[n] != '\r')
                    n += r;
            }
            request[n] = '\0';

            cmd = strtok_r(request, " \t", &save);
            if (cmd == NULL) {
                send(conn, " ^ Error\n\n", 10, 0);
                continue;
            }

            if (!strcasecmp(cmd, "q"))
                break; /* close the connection */

            if (!strcmp(cmd, "?")) {
                for (int i = 0; memdump_help[i] != NULL; i++)
                    send(conn, memdump_help[i], (int) strlen(memdump_help[i]), 0);
                send(conn, "\n", 1, 0);
            } else if (!strcasecmp(cmd, "d") || !strcasecmp(cmd, "dr")) {
                const int   raw = !strcasecmp(cmd, "dr");
                uint16_t    seg = 0;
                uint16_t    off = 0;
                int         segmented = 0;
                uint32_t    v;
                int         digits;

                arg1 = strtok_r(NULL, " \t", &save);
                arg2 = strtok_r(NULL, " \t", &save);
                arg3 = strtok_r(NULL, " \t", &save);

                if ((arg1 == NULL) || (arg3 != NULL)
                    || !memdump_parse_address(arg1, &addr, &seg, &off, &segmented)) {
                    send(conn, " ^ Error\n\n", 10, 0);
                    continue;
                }

                len = 128; /* DEBUG's default */
                if ((arg2 != NULL) && !memdump_parse_hex(arg2, &v, &digits)) {
                    send(conn, " ^ Error\n\n", 10, 0);
                    continue;
                }
                if (arg2 != NULL)
                    len = v;
                if (len > 0x1000)
                    len = 0x1000;

                if (raw) {
                    if (len * 2 + 1 <= sizeof(reply)) {
                        for (uint32_t i = 0; i < len; i++)
                            sprintf(reply + i * 2, "%02x", memdump_read_byte(addr + i));
                        send(conn, reply, (int) (len * 2), 0);
                    } else {
                        buf = (char *) malloc((size_t) len * 2 + 1);
                        if (buf != NULL) {
                            for (uint32_t i = 0; i < len; i++)
                                sprintf(buf + i * 2, "%02x", memdump_read_byte(addr + i));
                            send(conn, buf, (int) (len * 2), 0);
                            free(buf);
                        }
                    }
                } else {
                    /* DEBUG-style: address column, hex with a dash
                       between bytes 8 and 9, then ASCII with '.' for
                       non-printables.  Segmented requests label lines
                       SEG:OFF (segment fixed, offset wrapping mod 64K,
                       like DEBUG); absolute requests label lines with
                       the 32-bit linear address.  Reads always advance
                       linearly either way. */
                    buf = (char *) malloc((((size_t) len + 15) >> 4) * 80 + 1);
                    if (buf != NULL) {
                        char *p = buf;

                        for (uint32_t i = 0; i < len; i += 16) {
                            const int n = ((len - i) < 16) ? (int) (len - i) : 16;

                            if (segmented)
                                p += sprintf(p, "%04X:%04X  ", (unsigned) seg,
                                             (unsigned) ((off + i) & 0xffff));
                            else
                                p += sprintf(p, "%08X  ", (unsigned) (addr + i));

                            for (int j = 0; j < 16; j++) {
                                if (j > 0)
                                    *p++ = (j == 8) ? '-' : ' ';
                                if (j < n)
                                    p += sprintf(p, "%02X", memdump_read_byte(addr + i + j));
                                else
                                    p += sprintf(p, "  ");
                            }

                            p += sprintf(p, "   ");
                            for (int j = 0; j < n; j++) {
                                const uint8_t b = memdump_read_byte(addr + i + j);
                                *p++ = ((b >= 0x20) && (b < 0x7f)) ? (char) b : '.';
                            }
                            *p++ = '\n';
                        }
                        send(conn, buf, (int) (p - buf), 0);
                        free(buf);
                    }
                }
                send(conn, "\n", 1, 0);
            } else if (!strcasecmp(cmd, "o") || !strcasecmp(cmd, "ow")
                       || !strcasecmp(cmd, "od")) {
                const int force_word = !strcasecmp(cmd, "ow");
                const int force_dword = !strcasecmp(cmd, "od");
                uint32_t port;
                uint32_t val;
                int      digits;

                arg1 = strtok_r(NULL, " \t", &save);
                arg2 = strtok_r(NULL, " \t", &save);
                arg3 = strtok_r(NULL, " \t", &save);

                if ((arg1 == NULL) || (arg2 == NULL) || (arg3 != NULL)
                    || !memdump_parse_hex(arg1, &port, &digits)
                    || !memdump_parse_hex(arg2, &val, &digits)) {
                    send(conn, " ^ Error\n\n", 10, 0);
                    continue;
                }

                /* Access width: OW and OD force a width; plain O senses
                   it from the digit count and the value (8 digits or
                   >16 bits = dword, >=3 digits or >8 bits = word,
                   otherwise byte). */
                if (force_dword || (!force_word
                                    && ((digits >= 8) || (val > 0xffff))))
                    outl((uint16_t) port, val);
                else if (force_word || (digits >= 3) || (val > 0xff))
                    outw((uint16_t) port, (uint16_t) val);
                else
                    outb((uint16_t) port, (uint8_t) val);

                send(conn, "ok\n\n", 4, 0);
            } else {
                send(conn, " ^ Error\n\n", 10, 0);
            }
        }

close_conn:
#ifdef _WIN32
        closesocket(conn);
#else
        close(conn);
#endif
    }
}

void
memdump_init(void)
{
    struct sockaddr_in bind_addr;

    if (memdump_port <= 0)
        return;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    memdump_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (memdump_socket == -1) {
        pclog("MemDump: failed to create socket\n");
        return;
    }

    int yes = 1;
    setsockopt(memdump_socket, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
               (const char *) &yes,
#else
               &yes,
#endif
               sizeof(yes));

    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port        = htons((uint16_t) memdump_port);

    if (bind(memdump_socket, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) == -1) {
        pclog("MemDump: failed to bind on port %d\n", memdump_port);
        return;
    }
    if (listen(memdump_socket, 1) == -1) {
        pclog("MemDump: failed to listen on port %d\n", memdump_port);
        return;
    }

    memdump_running = 1;
    pclog("MemDump: Listening on port %d\n", memdump_port);
    thread_create(memdump_server_thread, NULL);
}

void
memdump_close(void)
{
    if (memdump_socket < 0)
        return;

    memdump_running = 0;
#ifdef _WIN32
    shutdown(memdump_socket, SD_BOTH);
    closesocket(memdump_socket);
    WSACleanup();
#else
    shutdown(memdump_socket, SHUT_RDWR);
    close(memdump_socket);
#endif
    memdump_socket = -1;
}
