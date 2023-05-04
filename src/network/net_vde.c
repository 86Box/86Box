 /*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VDE networking for 86box
 *          See https://wiki.virtualsquare.org/#!tutorials/vdebasics.md 
 *          for basic information about VDE. You can browse the source
 *          code at https://github.com/virtualsquare/vde-2
 *
 *          VDE support is only available in Linux and MacOS. It _should_
 *          be available in BSD*, and if someday there is a BSD version of
 *          86box this _could_ work out of the box.
 *
 * Authors: jguillaumes <jguillaumes@gmail.com>
 *          Copyright 2023 jguillaumes.
 * 
 * See the COPYING file at the top of the 86box for license details.
 * TL;DR: GPL version 2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#if !defined(_WIN32)
#include <poll.h>
#include <unistd.h>
#else
#error VDE is not supported under windows
#endif

#include <libvdeplug.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h>
#include <86box/net_event.h>

#define VDE_PKT_BATCH NET_QUEUE_LEN
#define VDE_DESCRIPTION "86Box virtual card"

enum {
    NET_EVENT_STOP = 0,
    NET_EVENT_TX,
    NET_EVENT_RX,
    NET_EVENT_VDE,
    NET_EVENT_MAX
};

static volatile void *libvde_handle = NULL;

//+
// VDE connection structure
//-
typedef struct {
    void *vdeconn;          // VDEPLUG Connection          
    netcard_t *card;        // NIC linked to
    thread_t *poll_tid;     // Polling thread
    net_evt_t tx_event;     // Packets to transmit event
    net_evt_t stop_event;   // Stop thread event
    netpkt_t pkt;                       // Packet read/sent
    netpkt_t pktv[VDE_PKT_BATCH];       // Packet queue
    uint8_t  mac_addr[6];               // MAC Address
} net_vde_t;

//+
// VDE libvdeplug function pointers
//-
static VDECONN *(*f_vde_open)(char *, char *, int, struct vde_open_args *); // This is vde_open_real()
static void (*f_vde_close)(VDECONN *);
static int  (*f_vde_datafd)(VDECONN *);             // Get the data (read/write) handle
static int  (*f_vde_ctlfd)(VDECONN *);              // Get the control handle
static ssize_t (*f_vde_recv)(VDECONN *, void *, size_t, int);           // Receive a packet
static ssize_t (*f_vde_send)(VDECONN *, const void *, size_t, int);     // Send a packet

//+
// VDE libvdeplug function table (for import)
//-
static dllimp_t vde_imports[] = {
    {"vde_open_real",   &f_vde_open},
    {"vde_close",       &f_vde_close},
    {"vde_datafd",      &f_vde_datafd},
    {"vde_ctlfd",       &f_vde_ctlfd},
    {"vde_recv",        &f_vde_recv},
    {"vde_send",        &f_vde_send},
    { NULL,             NULL}
};

#ifdef ENABLE_VDE_LOG
#include <stdarg.h>
int vde_do_log = ENABLE_VDE_LOG;

static void
vde_log(const char *fmt, ...) {
    va_list ap;

    if (vde_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define vde_log(fmt, ...)
#endif

#ifdef _WIN32
#error VDE networking is not supported under windows
#else 

//+
// VDE thread
//-
static void net_vde_thread(void *priv) {
    net_vde_t *vde = (net_vde_t *) priv;
    vde_log("VDE: Polling started.\n");

    struct pollfd pfd[NET_EVENT_MAX];
    pfd[NET_EVENT_STOP].fd     = net_event_get_fd(&vde->stop_event);
    pfd[NET_EVENT_STOP].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_TX].fd     = net_event_get_fd(&vde->tx_event);
    pfd[NET_EVENT_TX].events = POLLIN | POLLPRI;

    pfd[NET_EVENT_RX].fd     = f_vde_datafd(vde->vdeconn);
    pfd[NET_EVENT_RX].events = POLLIN;

    pfd[NET_EVENT_VDE].fd     = f_vde_ctlfd(vde->vdeconn);
    pfd[NET_EVENT_VDE].events = POLLIN;

    while(1) {
        poll(pfd, NET_EVENT_MAX, -1);

        // Acvity in the control handle means the link is closed
        // We send ourselves a STOP event
        if (pfd[NET_EVENT_VDE].revents & POLLIN) {
            net_event_set(&vde->stop_event);  
        }

        // There are packets queued to transmit
        if (pfd[NET_EVENT_TX].revents & POLLIN) {
            net_event_clear(&vde->tx_event);
            int packets = network_tx_popv(vde->card, vde->pktv, VDE_PKT_BATCH);
            for (int i=0; i<packets; i++) {
                int nc = f_vde_send(vde->vdeconn, vde->pktv[i].data,vde->pktv[i].len, 0 );
                if (nc == 0) {
                    vde_log("VDE: Problem, no bytes sent.\n");
                }
            }
        }

        // Packets are available for reading. Read packet and queue it
        if (pfd[NET_EVENT_RX].revents & POLLIN) {
            int nc = f_vde_recv(vde->vdeconn, vde->pkt.data, NET_MAX_FRAME, 0);
            vde->pkt.len = nc;
            network_rx_put_pkt(vde->card, &vde->pkt);
        }

        // We have been told to close
        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
            net_event_clear(&vde->stop_event);
            break;
        }
    }
    vde_log("VDE: Polling stopped.\n");
}
#endif


//+
// Prepare the VDE libvdeplug interface.
// Load the dynamic library libvdeplug.
// Returns zero if the library has been loaded, -1 in case of error.
//-
int net_vde_prepare(void) {

#if defined(_WIN32)
    #error VDE is not supported in Windows
#elif defined(__APPLE__)
    libvde_handle = dynld_module("libvdeplug.dylib", vde_imports);
#else
    libvde_handle = dynld_module("libvdeplug.so", vde_imports);
#endif

    if (libvde_handle == NULL) {
        vde_log("VDE: error loading VDEPLUG module\n");
        return -1;
    } else {
        network_devmap.has_vde = 1;
    }
    return 0;
}

//+
// Close a VDE socket connection
//-
void net_vde_close(void *priv) {
    int i;

    if (!priv)  return;

    net_vde_t *vde = (net_vde_t *) priv;
    vde_log("VDE: closing.\n");
    net_event_set(&vde->stop_event);        // Tell the thread to finish
    vde_log("VDE: Waiting for the thread to finish...\n");
    thread_wait(vde->poll_tid);
    vde_log("VDE: Thread finished.\n");

    // Free all the mallocs!
    for(i=0;i<VDE_PKT_BATCH; i++) {
        free(vde->pktv[i].data);
    }
    free(vde->pkt.data);
    f_vde_close((void *) vde->vdeconn);
    net_event_close(&vde->tx_event);
    net_event_close(&vde->stop_event);
    free(vde);
}

//+
// Signal packets are available to be transmitted
//-
void net_vde_in_available(void *priv) {
    net_vde_t *vde = (net_vde_t *) priv;
    net_event_set(&vde->tx_event);
}

//+
// Initialize VDE for use
// At this point the vdeplug library is already loaded
// card: network card we are attaching
// mac_addr: MAC address we are using
// priv: Name of the VDE contol socket directory
//-
void *net_vde_init(const netcard_t *card, const uint8_t *mac_addr, void *priv) {
    struct vde_open_args vde_args;
    int i;

    char *socket_name = (char *) priv;

    if (libvde_handle == NULL) {
        vde_log("VDE: net_vde_init without library handle!\n");
        return NULL;
    }

    if ((socket_name[0] == '\0') || !strcmp(socket_name, "none")) {
        vde_log("VDE: No socket name configured!\n");
        return NULL;
    }

    vde_log("VDE: Attaching to virtual switch at %s\n", socket_name);
    
    net_vde_t *vde = calloc(1, sizeof(net_vde_t));
    vde->card = (netcard_t *) card;
    memcpy(vde->mac_addr, mac_addr, sizeof(vde->mac_addr));

    vde_args.group = 0;
    vde_args.port  = 0;
    vde_args.mode  = 0;

    // We are calling vde_open_real(), not the vde_open() macro...
    if ((vde->vdeconn = f_vde_open(socket_name, VDE_DESCRIPTION, 
                                        LIBVDEPLUG_INTERFACE_VERSION, &vde_args)) == NULL) {
        vde_log("VDE: Unable to open socket %s (%s)!\n", socket_name, strerror(errno));
        free(vde);
        //+
        // There is a bug upstream that causes an uncontrolled crash if the network is not
        // properly initialized. 
        // To avoid that crash, we tell the user we cannot continue and exit the program.
        // TODO: Once there is a solution for the mentioned crash, this should be removed
        // and/or replaced by proper error handling code.
        //-
        fatal("Could not open the specified VDE socket (%s). Please fix your networking configuration.", socket_name);
        // It makes no sense to issue this warning since the program will crash anyway...
        // ui_msgbox_header(MBX_WARNING, (wchar_t *) IDS_2167, (wchar_t *) IDS_2168);
        return NULL;
    }
    vde_log("VDE: Socket opened (%s).\n", socket_name);

    for(i=0; i < VDE_PKT_BATCH; i++) {
        vde->pktv[i].data = calloc(1, NET_MAX_FRAME);
    }
    vde->pkt.data = calloc(1,NET_MAX_FRAME);
    net_event_init(&vde->tx_event);
    net_event_init(&vde->stop_event);
    vde->poll_tid = thread_create(net_vde_thread, vde);     // Fire up the read-write thread!

    return vde;
}

//+
// VDE Driver structure
//-
const netdrv_t net_vde_drv =  {
    &net_vde_in_available,
    &net_vde_init,
    &net_vde_close,
    NULL
};

