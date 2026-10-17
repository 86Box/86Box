/*
 * 86Box       A hypervisor and IBM PC system emulator.
 *
 *             PC Card (PCMCIA) support: the socket controller (pcic_pd6722.c),
 *             the cards that go in its sockets, their Card Information
 *             Structures (pccard_cis.c), and the PCMCIA settings (pcmcia.c).
 *             The controller and the TRENDnet TE100-PC16 card come from
 *             MegaPPBox, where they are the PC Card slots of the Merit MAXX
 *             I/O board.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef EMU_PCMCIA_H
#define EMU_PCMCIA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCMCIA_SOCKETS 2

/* A 16-bit PC Card as the socket sees it.  Addresses are card addresses:
   attribute and common memory from 0, I/O as the host port (a card decodes
   only its own low address lines).  reset is the RESET pin, and power-off
   resets a card too.  A card without common memory leaves common_read and
   common_write NULL; one without I/O, the io_ ones. */
typedef struct pccard_t {
    const char *name;   /* for the PC Card menu */
    uint8_t (*attr_read)(uint32_t addr, void *priv);
    void (*attr_write)(uint32_t addr, uint8_t val, void *priv);
    uint8_t (*common_read)(uint32_t addr, void *priv);
    void (*common_write)(uint32_t addr, uint8_t val, void *priv);
    uint8_t (*io_read)(uint16_t port, void *priv);
    uint16_t (*io_readw)(uint16_t port, void *priv);
    void (*io_write)(uint16_t port, uint8_t val, void *priv);
    void (*io_writew)(uint16_t port, uint16_t val, void *priv);
    void (*reset)(void *priv);
    void *priv;
} pccard_t;

/* The controller: added once, by the PCMCIA settings or by an I/O board that
   carries PC Card slots (the Merit MAXX), whichever comes first. */
extern void pcmcia_controller_add(void);
extern int  pcmcia_controller_present(void);

/* A card device plugs itself into its socket (0 = A, 1 = B) when it is
   created, and takes itself out (NULL) when it is closed.  A card may
   arrive before the controller does; it is picked up when the controller
   is created. */
extern void pcmcia_insert(int socket, const pccard_t *card);

/* Hot plugging (the PC Card status bar icon): put a card of a type from
   pcmcia.c's list into a socket, 0 to take it out.  Safe from the UI thread;
   carried out on the emulation thread by the controller's poll
   (pcmcia_slots_poll()), with the card-detect change the guest's socket
   services wait for. */
extern void        pcmcia_request_card(int socket, int type);
/* The card in the socket again, with the settings it has now: out, then in. */
extern void        pcmcia_request_reinsert(int socket);
extern int         pcmcia_slots_active(void);
extern void        pcmcia_slots_poll(void);
extern const char *pcmcia_socket_card_name(int socket);   /* NULL: an empty socket */

/* The card's IREQ line: 1 = interrupt requested.  The controller steers it
   to the ISA IRQ the driver chose, once the card is in I/O mode. */
extern void pcmcia_card_irq(int socket, int level);

/* The PCMCIA settings (86box.cfg, [PCMCIA]). */
extern int  pcmcia_enabled;                      /* the controller is fitted   */
extern int  pcmcia_card_type[PCMCIA_SOCKETS];    /* the card in each socket    */
extern int  pcmcia_net_type[PCMCIA_SOCKETS];     /* a network card's link      */
extern char pcmcia_net_host[PCMCIA_SOCKETS][128]; /* and its host interface    */

extern void        pcmcia_reset(void);
extern int         pcmcia_card_count(void);
extern const char *pcmcia_card_get_internal_name(int card);
extern const char *pcmcia_card_get_name(int card);
extern int         pcmcia_card_get_from_internal_name(const char *s);
extern int         pcmcia_card_has_config(int card);
extern int         pcmcia_card_is_network(int card);
extern int         pcmcia_card_is_scsi(int card);

/* A SCSI card's bus: its socket's, kept from the first time a SCSI card is
   in it to the next hard reset (pcmcia.c); 0xFF when none is left. */
extern uint8_t     pcmcia_scsi_bus(int socket);
#ifdef EMU_DEVICE_H
extern const device_t *pcmcia_card_get_device(int card);
#endif

/* For a network card: attach to the network the way network_attach() does,
   with the link chosen for its socket in the PCMCIA settings.  Its link
   state (net_cards_conf[NET_CARD_MAX + socket].link_state: the status bar's
   Connected, saved as socket_x_net_link) is kept from one card to the next. */
#ifdef EMU_NETWORK_H
extern netcard_t *pcmcia_network_attach(int socket, void *card_drv, uint8_t *mac, NETRXCB rx,
                                        NETSETLINKSTATE set_link_state);
#endif

/* ---------------------------------------------- multi-function cards --- */

/* A PC Card 95 multi-function card (pccard_mfc.c): up to PCCARD_MFC_MAX
   functions -- a LAN and a modem, say -- behind one socket.  The card has one
   CIS for the card and one for each function (CISTPL_LONGLINK_MFC points to
   them), and each function its own configuration registers in attribute
   memory:

     +0x00  COR   bit 7 SRESET (the function's reset), bit 6 level IREQ,
                  bits 5:3 configuration index, bit 2 IREQ enable, bit 1 the
                  function decodes the I/O range its I/O base registers name,
                  bit 0 function enable
     +0x02  CCSR  bit 1 interrupt pending (the function's interrupt request),
                  bit 0 interrupt acknowledge mode; the rest kept
     +0x04  PRR, +0x06 SCR, +0x08 ESR: kept
     +0x0A..+0x10  I/O base 0-3 (bytes, low first)
     +0x12  I/O limit: the number of ports minus one

   A function answers I/O only while enabled, in the range at its I/O base
   (with address decode on; A15-A0 when its CONFIG tuple's register mask has
   I/O base 1, else A7-A0 against I/O base 0) or -- decode off -- at any port
   whose low address lines select one of its registers; its interrupt requests the card's one IREQ
   while its COR enables it, and shows in its CCSR either way.

   A function's I/O handlers get the offset into its range. */
#define PCCARD_MFC_MAX 4

typedef struct pccard_func_t {
    uint16_t io_len;   /* ports it decodes: a power of two (8 for a UART) */
    uint8_t (*io_read)(uint16_t off, void *priv);
    void (*io_write)(uint16_t off, uint8_t val, void *priv);
    uint16_t (*io_readw)(uint16_t off, void *priv);         /* NULL: two byte reads */
    void (*io_writew)(uint16_t off, uint16_t val, void *priv);
    void (*reset)(void *priv);          /* card RESET, power-off or COR SRESET */
    void (*enable)(int on, void *priv); /* COR function enable changed; may be NULL */
    void *priv;
} pccard_func_t;

typedef struct pccard_mfc_t {
    pccard_t             card;     /* what the socket holds */
    int                  socket;
    int                  nfunc;
    const pccard_func_t *func[PCCARD_MFC_MAX];
    uint32_t             cfg_base[PCCARD_MFC_MAX];   /* attribute address of its registers */
    uint16_t             cfg_rmask[PCCARD_MFC_MAX];  /* which are there (CONFIG's mask) */
    uint8_t              reg[PCCARD_MFC_MAX][10];    /* COR, CCSR, PRR, SCR, ESR, I/O base 0-3, limit */
    int                  intr[PCCARD_MFC_MAX];       /* the function's interrupt request */
    int                  ireq;                       /* the card's IREQ, as last told */
    uint8_t             *cis;      /* attribute memory's even bytes from 0 */
    int                  cis_len;
} pccard_mfc_t;

/* Set up m for socket with the CIS in cis (cis_len bytes, kept by the
   caller), then add the functions in CIS order, each with the attribute
   address of its configuration registers and their mask as its CONFIG tuple
   gives it; then pcmcia_insert(socket,
   &m->card).  A function raises and drops its interrupt request with
   pccard_mfc_irq(). */
extern void pccard_mfc_init(pccard_mfc_t *m, int socket, const char *name, uint8_t *cis, int cis_len);
extern int  pccard_mfc_add(pccard_mfc_t *m, const pccard_func_t *f, uint32_t cfg_base, uint16_t rmask);
extern void pccard_mfc_irq(pccard_mfc_t *m, int func, int level);

/* ------------------------------------------- Card Information Structure --- */

#define CISTPL_NULL          0x00
#define CISTPL_DEVICE        0x01
#define CISTPL_LONGLINK_MFC  0x06
#define CISTPL_LINKTARGET    0x13
#define CISTPL_NO_LINK       0x14
#define CISTPL_VERS_1        0x15
#define CISTPL_DEVICE_A      0x17
#define CISTPL_CONFIG        0x1a
#define CISTPL_CFTABLE_ENTRY 0x1b
#define CISTPL_MANFID        0x20
#define CISTPL_FUNCID        0x21
#define CISTPL_FUNCE         0x22
#define CISTPL_END           0xff

#define CISTPL_FUNCID_MULTI   0x00
#define CISTPL_FUNCID_MEMORY  0x01
#define CISTPL_FUNCID_SERIAL  0x02
#define CISTPL_FUNCID_NETWORK 0x06

typedef struct pccard_cis_t {
    uint8_t *buf;
    int      len, max;
    int      filler;   /* where the CONFIG tuple's two filler bytes are */
} pccard_cis_t;

extern void     pccard_cis_init(pccard_cis_t *c, uint8_t *buf, int max);
extern void     pccard_cis_tuple(pccard_cis_t *c, uint8_t code, const uint8_t *data, int len);
extern void     pccard_cis_vers1(pccard_cis_t *c, const char *const *s, int n);
extern void     pccard_cis_manfid(pccard_cis_t *c, uint16_t manf, uint16_t card);
extern void     pccard_cis_config(pccard_cis_t *c, uint16_t base, uint8_t rmask, uint8_t last);
extern void     pccard_cis_end(pccard_cis_t *c);
extern int      pccard_cis_longlink_mfc(pccard_cis_t *c, int n);
extern void     pccard_cis_mfc_link(pccard_cis_t *c, int link, int f, int at);
extern void     pccard_cis_linktarget(pccard_cis_t *c);
extern void     pccard_cis_cftable_io(pccard_cis_t *c, uint8_t index, uint16_t base, uint8_t len, uint8_t lines,
                                      int bits16, uint16_t irqs);
extern uint16_t pccard_cis_win9x_crc(const uint8_t *cis, int len);
extern void     pccard_cis_win9x_id(const uint8_t *cis, int len, char *out, int outlen);
extern int      pccard_cis_match_id(pccard_cis_t *c, uint16_t crc);

/* Each card's CIS, as its device builds it (and the CIS tests check it):
   into buf (at least 256 bytes), returning its length. */
extern int pccard_cis_te100pc16(uint8_t *buf, const uint8_t mac[6]);
extern int pccard_cis_3c589d(uint8_t *buf);
extern int pccard_cis_apa1460(uint8_t *buf);
/* The Hayes Accura 56K PC Card's, with where its configuration registers are. */
extern int pccard_cis_accura56k(uint8_t *buf, uint32_t *cfg_base);
/* A multi-function card's: into buf (512 bytes), with where each function's
   configuration registers are. */
extern int pccard_cis_3c562d(uint8_t *buf, const uint8_t mac[6], uint32_t *lan_cfg, uint32_t *modem_cfg);

#ifdef __cplusplus
}
#endif

#endif /*EMU_PCMCIA_H*/
