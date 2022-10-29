/*
 * VARCem   Virtual ARchaeological Computer EMulator.
 *          An emulator of (mostly) x86-based PC systems and devices,
 *          using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *          spanning the era between 1981 and 1995.
 *
 *          Handle WinPcap library processing.
 *
 *
 *
 * Authors: Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *          Copyright 2017-2019 Fred N. van Kempen.
 *
 *          Redistribution and  use  in source  and binary forms, with
 *          or  without modification, are permitted  provided that the
 *          following conditions are met:
 *
 *          1. Redistributions of  source  code must retain the entire
 *             above notice, this list of conditions and the following
 *             disclaimer.
 *
 *          2. Redistributions in binary form must reproduce the above
 *             copyright  notice,  this list  of  conditions  and  the
 *             following disclaimer in  the documentation and/or other
 *             materials provided with the distribution.
 *
 *          3. Neither the  name of the copyright holder nor the names
 *             of  its  contributors may be used to endorse or promote
 *             products  derived from  this  software without specific
 *             prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>
#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <winsock2.h>
#else
#    include <poll.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <sys/select.h>
#endif

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_event.h>

#define PCAP_PKT_BATCH NET_QUEUE_LEN

enum {
    NET_EVENT_STOP = 0,
    NET_EVENT_TX,
    NET_EVENT_RX,
    NET_EVENT_MAX
};

#ifdef __APPLE__
#    include <pcap/pcap.h>
#else
typedef int          bpf_int32;
typedef unsigned int bpf_u_int32;

/*
 * The instruction data structure.
 */
struct bpf_insn {
    unsigned short code;
    unsigned char  jt;
    unsigned char  jf;
    bpf_u_int32    k;
};

/*
 * Structure for "pcap_compile()", "pcap_setfilter()", etc..
 */
struct bpf_program {
    unsigned int     bf_len;
    struct bpf_insn *bf_insns;
};

typedef struct pcap_if pcap_if_t;

#    define PCAP_ERRBUF_SIZE 256

struct pcap_pkthdr {
    struct timeval ts;
    bpf_u_int32    caplen;
    bpf_u_int32    len;
};

struct pcap_if {
    struct pcap_if *next;
    char           *name;
    char           *description;
    void           *addresses;
    bpf_u_int32     flags;
};

struct pcap_send_queue {
    u_int maxlen; /* Maximum size of the queue, in bytes. This
             variable contains the size of the buffer field. */
    u_int len;    /* Current size of the queue, in bytes. */
    char *buffer; /* Buffer containing the packets to be sent. */
};

typedef struct pcap_send_queue pcap_send_queue;

typedef void (*pcap_handler)(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes);
#endif

typedef struct {
    void      *pcap; /* handle to pcap lib instance */
    netcard_t *card; /* netcard linked to us */
    thread_t  *poll_tid;
    net_evt_t  tx_event;
    net_evt_t  stop_event;
    netpkt_t   pkt;
    netpkt_t   pktv[PCAP_PKT_BATCH];
    uint8_t    mac_addr[6];
#ifdef _WIN32
    struct pcap_send_queue *pcap_queue;
#endif
} net_pcap_t;

typedef struct {
    char    *intf_name;
    uint8_t *mac_addr;
} net_pcap_params_t;

static volatile void *libpcap_handle; /* handle to WinPcap DLL */

/* Pointers to the real functions. */
static const char *(*f_pcap_lib_version)(void);
static int (*f_pcap_findalldevs)(pcap_if_t **, char *);
static void (*f_pcap_freealldevs)(void *);
static void *(*f_pcap_open_live)(const char *, int, int, int, char *);
static int (*f_pcap_compile)(void *, void *, const char *, int, bpf_u_int32);
static int (*f_pcap_setfilter)(void *, void *);
static const unsigned char
    *(*f_pcap_next)(void *, void *);
static int (*f_pcap_sendpacket)(void *, const unsigned char *, int);
static void (*f_pcap_close)(void *);
static int (*f_pcap_setnonblock)(void *, int, char *);
static int (*f_pcap_set_immediate_mode)(void *, int);
static int (*f_pcap_set_promisc)(void *, int);
static int (*f_pcap_set_snaplen)(void *, int);
static int (*f_pcap_dispatch)(void *, int, pcap_handler callback, u_char *user);
static void *(*f_pcap_create)(const char *, char *);
static int (*f_pcap_activate)(void *);
static void *(*f_pcap_geterr)(void *);
#ifdef _WIN32
static HANDLE (*f_pcap_getevent)(void *);
static int (*f_pcap_sendqueue_queue)(void *, void *, void *);
static u_int (*f_pcap_sendqueue_transmit)(void *, void *, int sync);
static void *(*f_pcap_sendqueue_alloc)(u_int memsize);
static void (*f_pcap_sendqueue_destroy)(void *);
#else
static int (*f_pcap_get_selectable_fd)(void *);
#endif

static dllimp_t pcap_imports[] = {
    {"pcap_lib_version",         &f_pcap_lib_version       },
    { "pcap_findalldevs",        &f_pcap_findalldevs       },
    { "pcap_freealldevs",        &f_pcap_freealldevs       },
    { "pcap_open_live",          &f_pcap_open_live         },
    { "pcap_compile",            &f_pcap_compile           },
    { "pcap_setfilter",          &f_pcap_setfilter         },
    { "pcap_next",               &f_pcap_next              },
    { "pcap_sendpacket",         &f_pcap_sendpacket        },
    { "pcap_close",              &f_pcap_close             },
    { "pcap_setnonblock",        &f_pcap_setnonblock       },
    { "pcap_set_immediate_mode", &f_pcap_set_immediate_mode},
    { "pcap_set_promisc",        &f_pcap_set_promisc       },
    { "pcap_set_snaplen",        &f_pcap_set_snaplen       },
    { "pcap_dispatch",           &f_pcap_dispatch          },
    { "pcap_create",             &f_pcap_create            },
    { "pcap_activate",           &f_pcap_activate          },
    { "pcap_geterr",             &f_pcap_geterr            },
#ifdef _WIN32
    { "pcap_getevent",           &f_pcap_getevent          },
    { "pcap_sendqueue_queue",    &f_pcap_sendqueue_queue   },
    { "pcap_sendqueue_transmit", &f_pcap_sendqueue_transmit},
    { "pcap_sendqueue_alloc",    &f_pcap_sendqueue_alloc   },
    { "pcap_sendqueue_destroy",  &f_pcap_sendqueue_destroy },
#else
    { "pcap_get_selectable_fd", &f_pcap_get_selectable_fd },
#endif
    { NULL,                      NULL                      },
};

#ifdef ENABLE_PCAP_LOG
int pcap_do_log = ENABLE_PCAP_LOG;

static void
pcap_log(const char *fmt, ...)
{
    va_list ap;

    if (pcap_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pcap_log(fmt, ...)
#endif

static void
net_pcap_rx_handler(uint8_t *user, const struct pcap_pkthdr *h, const uint8_t *bytes)
{
    net_pcap_t *pcap = (net_pcap_t *) user;
    memcpy(pcap->pkt.data, bytes, h->caplen);
    pcap->pkt.len = h->caplen;
    network_rx_put_pkt(pcap->card, &pcap->pkt);
}

/* Send a packet to the Pcap interface. */
void
net_pcap_in(void *pcap, uint8_t *bufp, int len)
{
    if (pcap == NULL)
        return;

    f_pcap_sendpacket((void *) pcap, bufp, len);
}

void
net_pcap_in_available(void *priv)
{
    net_pcap_t *pcap = (net_pcap_t *) priv;
    net_event_set(&pcap->tx_event);
}

#ifdef _WIN32
static void
net_pcap_thread(void *priv)
{
    net_pcap_t *pcap = (net_pcap_t *) priv;

    pcap_log("PCAP: polling started.\n");

    HANDLE events[NET_EVENT_MAX];
    events[NET_EVENT_STOP] = net_event_get_handle(&pcap->stop_event);
    events[NET_EVENT_TX]   = net_event_get_handle(&pcap->tx_event);
    events[NET_EVENT_RX]   = f_pcap_getevent((void *) pcap->pcap);

    bool run = true;

    struct pcap_pkthdr h;
    while (run) {
        int ret = WaitForMultipleObjects(NET_EVENT_MAX, events, FALSE, INFINITE);

        switch (ret - WAIT_OBJECT_0) {
            case NET_EVENT_STOP:
                net_event_clear(&pcap->stop_event);
                run = false;
                break;

            case NET_EVENT_TX:
                net_event_clear(&pcap->tx_event);
                int packets = network_tx_popv(pcap->card, pcap->pktv, PCAP_PKT_BATCH);
                for (int i = 0; i < packets; i++) {
                    h.caplen = pcap->pktv[i].len;
                    f_pcap_sendqueue_queue(pcap->pcap_queue, &h, pcap->pktv[i].data);
                }
                f_pcap_sendqueue_transmit(pcap->pcap, pcap->pcap_queue, 0);
                pcap->pcap_queue->len = 0;
                break;

            case NET_EVENT_RX:
                f_pcap_dispatch(pcap->pcap, PCAP_PKT_BATCH, net_pcap_rx_handler, (u_char *) pcap);
                break;
        }
    }

    pcap_log("PCAP: polling stopped.\n");
}
#else
static void
net_pcap_thread(void *priv)
{
    net_pcap_t *pcap = (net_pcap_t *) priv;

    pcap_log("PCAP: polling started.\n");

    struct pollfd pfd[NET_EVENT_MAX];
    pfd[NET_EVENT_STOP].fd     = net_event_get_fd(&pcap->stop_event);
    pfd[NET_EVENT_STOP].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_TX].fd     = net_event_get_fd(&pcap->tx_event);
    pfd[NET_EVENT_TX].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_RX].fd     = f_pcap_get_selectable_fd((void *) pcap->pcap);
    pfd[NET_EVENT_RX].events = POLLIN | POLLPRI;

    /* As long as the channel is open.. */
    while (1) {
        poll(pfd, NET_EVENT_MAX, -1);

        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
            net_event_clear(&pcap->stop_event);
            break;
        }

        if (pfd[NET_EVENT_TX].revents & POLLIN) {
            net_event_clear(&pcap->tx_event);

            int packets = network_tx_popv(pcap->card, pcap->pktv, PCAP_PKT_BATCH);
            for (int i = 0; i < packets; i++) {
                net_pcap_in(pcap->pcap, pcap->pktv[i].data, pcap->pktv[i].len);
            }
        }

        if (pfd[NET_EVENT_RX].revents & POLLIN) {
            f_pcap_dispatch(pcap->pcap, PCAP_PKT_BATCH, net_pcap_rx_handler, (u_char *) pcap);
        }
    }

    pcap_log("PCAP: polling stopped.\n");
}
#endif

/*
 * Prepare the (Win)Pcap module for use.
 *
 * This is called only once, during application init,
 * to check for availability of PCAP, and to retrieve
 * a list of (usable) intefaces for it.
 */
int
net_pcap_prepare(netdev_t *list)
{
    char       errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *devlist, *dev;
    int        i = 0;

    /* Try loading the DLL. */
#ifdef _WIN32
    libpcap_handle = dynld_module("wpcap.dll", pcap_imports);
#elif defined __APPLE__
    libpcap_handle = dynld_module("libpcap.dylib", pcap_imports);
#else
    libpcap_handle = dynld_module("libpcap.so", pcap_imports);
#endif
    if (libpcap_handle == NULL) {
        pcap_log("PCAP: error loading pcap module\n");
        return (-1);
    }

    /* Retrieve the device list from the local machine */
    if (f_pcap_findalldevs(&devlist, errbuf) == -1) {
        pcap_log("PCAP: error in pcap_findalldevs: %s\n", errbuf);
        return (-1);
    }

    for (dev = devlist; dev != NULL; dev = dev->next) {
        if (i >= (NET_HOST_INTF_MAX - 1))
            break;

        /**
         * we initialize the strings to NULL first for strncpy
         */

        memset(list->device, '\0', sizeof(list->device));
        memset(list->description, '\0', sizeof(list->description));

        strncpy(list->device, dev->name, sizeof(list->device) - 1);
        if (dev->description) {
            strncpy(list->description, dev->description, sizeof(list->description) - 1);
        } else {
            /* if description is NULL, set the name. This allows pcap to display *something* useful under WINE */
            strncpy(list->description, dev->name, sizeof(list->description) - 1);
        }

        list++;
        i++;
    }

    /* Release the memory. */
    f_pcap_freealldevs(devlist);

    return (i);
}

/*
 * Initialize (Win)Pcap for use.
 *
 * We already know we have PCAP available, as this
 * is called when the network activates itself and
 * tries to attach to the network module.
 */
void *
net_pcap_init(const netcard_t *card, const uint8_t *mac_addr, void *priv)
{
    char               errbuf[PCAP_ERRBUF_SIZE];
    char              *str;
    char               filter_exp[255];
    struct bpf_program fp;

    char *intf_name = (char *) priv;

    /* Did we already load the library? */
    if (libpcap_handle == NULL) {
        pcap_log("PCAP: net_pcap_init without handle.\n");
        return NULL;
    }

    /* Get the PCAP library name and version. */
    strcpy(errbuf, f_pcap_lib_version());
    str = strchr(errbuf, '(');
    if (str != NULL)
        *(str - 1) = '\0';
    pcap_log("PCAP: initializing, %s\n", errbuf);

    /* Get the value of our capture interface. */
    if ((intf_name[0] == '\0') || !strcmp(intf_name, "none")) {
        pcap_log("PCAP: no interface configured!\n");
        return NULL;
    }

    pcap_log("PCAP: interface: %s\n", intf_name);

    net_pcap_t *pcap = calloc(1, sizeof(net_pcap_t));
    pcap->card       = (netcard_t *) card;
    memcpy(pcap->mac_addr, mac_addr, sizeof(pcap->mac_addr));

    if ((pcap->pcap = f_pcap_create(intf_name, errbuf)) == NULL) {
        pcap_log(" Unable to open device: %s!\n", intf_name);
        free(pcap);
        return NULL;
    }

    if (f_pcap_setnonblock((void *) pcap->pcap, 1, errbuf) != 0)
        pcap_log("PCAP: failed nonblock %s\n", errbuf);

    if (f_pcap_set_immediate_mode((void *) pcap->pcap, 1) != 0)
        pcap_log("PCAP: error setting immediate mode\n");

    if (f_pcap_set_promisc((void *) pcap->pcap, 1) != 0)
        pcap_log("PCAP: error enabling promiscuous mode\n");

    if (f_pcap_set_snaplen((void *) pcap->pcap, NET_MAX_FRAME) != 0)
        pcap_log("PCAP: error setting snaplen\n");

    if (f_pcap_activate((void *) pcap->pcap) != 0) {
        pcap_log("PCAP: failed pcap_activate");
        f_pcap_close((void *) pcap->pcap);
        free(pcap);
        return NULL;
    }

    /* Create a MAC address based packet filter. */
    pcap_log("PCAP: installing filter for MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    sprintf(filter_exp,
            "( ((ether dst ff:ff:ff:ff:ff:ff) or (ether dst %02x:%02x:%02x:%02x:%02x:%02x)) and not (ether src %02x:%02x:%02x:%02x:%02x:%02x) )",
            mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
            mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    if (f_pcap_compile((void *) pcap->pcap, &fp, filter_exp, 0, 0xffffffff) != -1) {
        if (f_pcap_setfilter((void *) pcap->pcap, &fp) != 0) {
            pcap_log("PCAP: error installing filter (%s) !\n", filter_exp);
            f_pcap_close((void *) pcap->pcap);
            free(pcap);
            return NULL;
        }
    } else {
        pcap_log("PCAP: could not compile filter (%s) : %s!\n", filter_exp, f_pcap_geterr((void *) pcap->pcap));
        f_pcap_close((void *) pcap->pcap);
        free(pcap);
        return NULL;
    }

#ifdef _WIN32
    pcap->pcap_queue = f_pcap_sendqueue_alloc(PCAP_PKT_BATCH * NET_MAX_FRAME);
#endif

    for (int i = 0; i < PCAP_PKT_BATCH; i++) {
        pcap->pktv[i].data = calloc(1, NET_MAX_FRAME);
    }
    pcap->pkt.data = calloc(1, NET_MAX_FRAME);

    net_event_init(&pcap->tx_event);
    net_event_init(&pcap->stop_event);
    pcap->poll_tid = thread_create(net_pcap_thread, pcap);

    return pcap;
}

/* Close up shop. */
void
net_pcap_close(void *priv)
{
    if (!priv)
        return;

    net_pcap_t *pcap = (net_pcap_t *) priv;

    pcap_log("PCAP: closing.\n");

    /* Tell the thread to terminate. */
    net_event_set(&pcap->stop_event);

    /* Wait for the thread to finish. */
    pcap_log("PCAP: waiting for thread to end...\n");
    thread_wait(pcap->poll_tid);
    pcap_log("PCAP: thread ended\n");

    for (int i = 0; i < PCAP_PKT_BATCH; i++) {
        free(pcap->pktv[i].data);
    }
    free(pcap->pkt.data);

#ifdef _WIN32
    f_pcap_sendqueue_destroy((void *) pcap->pcap_queue);
#endif
    /* OK, now shut down Pcap itself. */
    f_pcap_close((void *) pcap->pcap);

    net_event_close(&pcap->tx_event);
    net_event_close(&pcap->stop_event);

    free(pcap);
}

const netdrv_t net_pcap_drv = {
    &net_pcap_in_available,
    &net_pcap_init,
    &net_pcap_close,
    NULL
};
