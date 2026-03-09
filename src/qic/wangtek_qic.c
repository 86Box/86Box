/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Wangtek PC-36/PC-02 controllers.
 *
 *
 * Authors: 
 *
 *          Copyright 2025 seal331.
 */

/* This code is 100% AI-free */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <unistd.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/pic.h>
#include <86box/dma.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/plat.h>
#include <86box/wangtek_qic.h>

#ifdef _WIN32
#define DEVNULL "nul"
#else
#define DEVNULL "/dev/null"
#endif

/* for debugging; to be removed when upstreaming */
#define ENABLE_WANGTEK_QIC_LOG 1

/* temporary: TODO: make configurable */
#define WANGTEK_QIC_IO_BASE     0x300
#define WANGTEK_QIC_IRQ         5
#define WANGTEK_QIC_DMA_CHANNEL 2
#define WANGTEK_QIC_TAPE_PATH   "J:\\qic_test_data\\new_test_tape\\"

/* I'm going to forget this math a billion times if I don't declare it here */
#define WANGTEK_QIC_CHK_BIT_SET(var, pos) ((var) & (1 << (pos)))
#define WANGTEK_QIC_SET_BIT(var, pos)     ((var) |= (1 << (pos)))
#define WANGTEK_QIC_CLR_BIT(var, pos)     ((var) &= ~((1) << (pos)))

typedef struct wangtek_qic_t {
    bool qic11flag; /* FLAG: is our tape QIC-11? */
    bool qic24flag; /* FLAG: is our tape QIC-24? */
    bool statusflag; /* FLAG: are we currently transmitting status? */
    bool statusflag2; /* FLAG: is this the last byte of the status? */
    bool statusflag3; /* FLAG: did we just transmit the last byte of the status? */

    uint8_t status;  /* Status port (R/O) - address I/O base + 0x00 */
    uint8_t control; /* Control port (W/O) - address I/O base + 0x00 */
    uint8_t controlold; /* old control port - for status reading */
    uint8_t command; /* Command port (W/O) - address I/O base + 0x01 */
    uint8_t data;    /* Data port (R/W) - address I/O base + 0x01 */

    uint8_t statusbytes; /* how many status bytes do we still need to transmit */
    uint8_t statusbytetotransfer;

    uint16_t devstatus;

    uint64_t currentfile;
    uint64_t fileoffset;

    uint8_t currentdma;
    uint8_t currentirq;

    bool interruptflag; /* FLAG: are our interrupts enabled? */

    bool qicthreadrun;
    thread_t *qicthread;

    bool readflag; /* FLAG: are we currently reading? */
    bool setstatusflag; /* FLAG: set status in 0.05s or not */
    bool clrstatusflag; /* FLAG: clear status in 0.05s or not */
    bool readingstatbyteflag;
    bool actuallysendcommandflag;

    uint8_t reqtosend; /* send this command after request is reset */
} wangtek_qic_t;

typedef struct qic_tape_block_t {
    uint16_t block_in_dma;
    bool fullreadflag; /* FLAG: did we read the full 512 bytes? */
} qic_tape_block_t;

#ifdef ENABLE_WANGTEK_QIC_LOG
int wangtek_qic_do_log = ENABLE_WANGTEK_QIC_LOG;

static void
wangtek_qic_log(const char *fmt, ...)
{
    va_list ap;

    if (wangtek_qic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define wangtek_qic_log(fmt, ...)
#endif

static void
wangtek_qic_trigger_interrupt(void *priv)
{
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
    if (wangtek_qic->interruptflag && ((!(WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->status, 0))) ||
            (!(WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->status, 1))))) {
        wangtek_qic_log("PC-36: INTERRUPT\n");
        picint(1 << wangtek_qic->currentirq);
    }
}

static void
wangtek_qic_sanitize_devstatus(void *priv)
{
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
    uint8_t        lowbyte     = wangtek_qic->devstatus & 0x00ff;
    uint8_t        highbyte    = wangtek_qic->devstatus >> 8;
    if (lowbyte & 0x7f) {
        WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 7);
    } else {
        WANGTEK_QIC_CLR_BIT(wangtek_qic->devstatus, 7);
    }
    if (highbyte & 0x7f) {
        WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 15);
    } else {
        WANGTEK_QIC_CLR_BIT(wangtek_qic->devstatus, 15);
    }
}

#define TAPE_READ_DEBUG 1

static qic_tape_block_t*
wangtek_qic_get_block(uint64_t file, uint64_t offset) {
    qic_tape_block_t *retvalue = calloc(1, sizeof(qic_tape_block_t));
    uint16_t block = 0x00;
    char filename[32792]; /* this length to accomodate for the word "file", the maximum length of
                           * a number we could put in here (type uint64_t),
                           * the max path length on Windows systems (Linux has less),
                           * and the null terminator
                           */
    snprintf(filename, sizeof(filename), "%sfile%llu", WANGTEK_QIC_TAPE_PATH, file);
    FILE* filehandle = plat_fopen(filename, "rb");
    #ifdef TAPE_READ_DEBUG
    FILE* writehandle = plat_fopen("J:\test.dat", "wb");
    #endif
    //wangtek_qic_log("PC-36 file load debug: %s\n", filename);
    if (filehandle == NULL) {
        fatal("PC-36: wangtek_qic_get_block: error opening requested file");
    }
    fseek(filehandle, offset, SEEK_SET);
    char blocktmp[2];
    size_t bytesread = fread(blocktmp, 1, 2, filehandle);
    #ifdef TAPE_READ_DEBUG
    fseek(writehandle, offset, SEEK_SET);
    fwrite(blocktmp, 2, 1, writehandle);
    #endif
    block = (blocktmp[0] << 8) | blocktmp[1];
    //if (block != 0) {
    //    wangtek_qic_log("PC-36 extremely verbose log: resulting blockvalue is %i\n", block);
    //}
    retvalue->block_in_dma = block;
    //wangtek_qic_log("PC-36 tape read debug: read %i bytes\n", bytesread);
    if (bytesread == 2) {
        retvalue->fullreadflag = true;
    } else {
        wangtek_qic_log("PC-36 tape read debug: setting fullreadflag to false\n");
        retvalue->fullreadflag = false;
    }
    fclose(filehandle);
    #ifdef TAPE_READ_DEBUG
    fclose(writehandle);
    #endif
    return retvalue;
}    

static void wangtek_qic_read_data_thread(void *priv) {
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
    FILE* throwawayoutput = fopen(DEVNULL, "w");
    wangtek_qic_log("PC-36: starting read data thread\n");
    while (wangtek_qic->qicthreadrun) {
        /* for whatever reason this fprintf MUST be here or we hang forever at shutdown */
        fprintf(throwawayoutput, "%i", wangtek_qic->qicthreadrun);
        if(wangtek_qic->readflag) {
            data_read_start:
            wangtek_qic_log("QIC file read debug: fileoffset is %i\n", wangtek_qic->fileoffset);
            qic_tape_block_t *block;
            for (int i = 0; i < 256; i++) { /* the Wangtek controllers transfer data in 512 bytes */
                block = wangtek_qic_get_block(wangtek_qic->currentfile, wangtek_qic->fileoffset);
                dma_set_drq(wangtek_qic->currentdma, 1);
                dma_channel_write(wangtek_qic->currentdma, block->block_in_dma);
                if (!(block->fullreadflag)) {
                    WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 1);
                    WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 8); /* File Mark Detected */
                    wangtek_qic_sanitize_devstatus(priv);
                    dma_set_drq(wangtek_qic->currentdma, 0);
                    goto data_read_end;
                }
                dma_set_drq(wangtek_qic->currentdma, 0);
            }
            WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
            wangtek_qic_trigger_interrupt(priv);
            wangtek_qic->fileoffset = wangtek_qic->fileoffset + 512;
            goto data_read_start;
            data_read_end:
            wangtek_qic->readflag = false;
        }
        if(wangtek_qic->setstatusflag) {
            usleep(50000);
            WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
            wangtek_qic->setstatusflag = false;
        }
        if(wangtek_qic->clrstatusflag) {
            usleep(50000);
            WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
            wangtek_qic->clrstatusflag = false;
        }
    }
    fclose(throwawayoutput);
}

void
wangtek_qic_sel_drive_0()
{
    /* For now we'll only support one drive, but still, error out while we're implementing */
    fatal("Unimplemented QIC command SELECT DRIVE 0");
}

void
wangtek_qic_sel_drive_1()
{
    /* For now we'll only support one drive (so this'll be a noop later),
     * but still, error out while we're implementing
     */
    fatal("Unimplemented QIC command SELECT DRIVE 1");
}

void
wangtek_qic_sel_drive_2()
{
    /* For now we'll only support one drive (so this'll be a noop later),
     * but still, error out while we're implementing
     */
    fatal("Unimplemented QIC command SELECT DRIVE 2");
}

void
wangtek_qic_rew_to_bot(void *priv)
{
    wangtek_qic_log("QIC command REWIND TO BOT issued\n");
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
    wangtek_qic->currentfile = 1;
    wangtek_qic->fileoffset = 0x00;
    WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
    wangtek_qic->setstatusflag = true;
}

void
wangtek_qic_erase_tape()
{
    fatal("Unimplemented QIC command ERASE TAPE");
}

void
wangtek_qic_init_tape()
{
    fatal("Unimplemented QIC command INITIALIZE (RETENSION) TAPE");
}

void
wangtek_qic_write_data()
{
    fatal("Unimplemented QIC command WRITE DATA");
}

void
wangtek_qic_write_file_mark()
{
    fatal("Unimplemented QIC command WRITE FILE MARK");
}

void
wangtek_qic_read_data(void *priv)
{
    wangtek_qic_log("QIC command READ DATA\n");
    bool useless = false;
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
    /* TODO: figure out what to do when this gets called with DMA disabled */
    /* the following code raises EXC and sets the necessary stuff to tell the OS
     * that the tape is not inserted, keep it commented out for now until we make the tape
     * ejectable
     */
    #if 0
    WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 1);
    WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 14); /* Cartridge Not In Place */
    wangtek_qic_sanitize_devstatus(priv);
    #endif
    WANGTEK_QIC_SET_BIT(wangtek_qic->status, 1);
    wangtek_qic->readflag = 1;
}

void
wangtek_qic_read_file_mark(void *priv)
{
    wangtek_qic_log("PC-36: QIC READ FILE MARK received\n");
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
    wangtek_qic->currentfile++;
    WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
    WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 1);
    WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 8); /* File Mark Detected */
}

void
wangtek_qic_read_status(void *priv)
{
        wangtek_qic_log("PC-36: QIC READ STATUS received\n");
        wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
        wangtek_qic->statusflag = true;
        wangtek_qic->statusflag2 = true;
        wangtek_qic->statusflag3 = true;
        wangtek_qic->statusbytes = 6;
        wangtek_qic->statusbytetotransfer = wangtek_qic->devstatus >> 8;
        WANGTEK_QIC_SET_BIT(wangtek_qic->status, 1);
        //WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
}

static void
wangtek_qic_sel_qic_11_format()
{
    fatal("Unimplemented QIC command SELECT QIC-11 FORMAT");
}

static void
wangtek_qic_sel_qic_24_format()
{
    fatal("Unimplemented QIC command SELECT QIC-24 FORMAT");
}

static void
wangtek_qic_reset(void *priv)
{
    wangtek_qic_log("PC-36: resetting controller\n");
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) priv;
    wangtek_qic->status = 0xff;
    wangtek_qic->control = 0x00;
    wangtek_qic->controlold = 0x00;
    wangtek_qic->command = 0xff;
    wangtek_qic->data = 0xff;
    wangtek_qic->devstatus = 0x0000;
    wangtek_qic->qic11flag = false;
    wangtek_qic->qic24flag = true;
    wangtek_qic->statusflag = false;
    wangtek_qic->statusbytes = 0;
    wangtek_qic->statusbytetotransfer = 0x00;
    wangtek_qic->currentfile = 1;
    wangtek_qic->fileoffset = 0x00;
    wangtek_qic->currentdma = WANGTEK_QIC_DMA_CHANNEL;
    wangtek_qic->currentirq = WANGTEK_QIC_IRQ;
    wangtek_qic->interruptflag = false;
    wangtek_qic->readflag = false;
    wangtek_qic->setstatusflag = false;
    wangtek_qic->clrstatusflag = false;
    wangtek_qic->readingstatbyteflag = false;
    wangtek_qic->statusflag2 = false;
    wangtek_qic->statusflag3 = false;
    wangtek_qic->reqtosend = 0xff;
    wangtek_qic->actuallysendcommandflag = false;
    WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
    WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 1);
    WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 0);
}

#define WANGTEK_QIC_LOG_CONTROL_PORT_WRITES 1

static void
wangtek_qic_process_commands(uint8_t val, void *priv) {
    wangtek_qic_t* wangtek_qic = (wangtek_qic_t*)priv;
    wangtek_qic->actuallysendcommandflag = false;
    WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
    switch (val) {
            case QIC_SEL_DRIVE_0:
                wangtek_qic_sel_drive_0();
                break;

            case QIC_SEL_DRIVE_1:
                wangtek_qic_sel_drive_1();
                break;

            case QIC_SEL_DRIVE_2:
                /* controller manual labels this "not used",
                 * so probably should never be called but still
                 */
                wangtek_qic_sel_drive_2();
                break;

            case QIC_REW_TO_BOT:
                wangtek_qic_rew_to_bot(priv);
                break;

            case QIC_ERASE_TAPE:
                wangtek_qic_erase_tape();
                break;

            case QIC_INIT_TAPE:
                wangtek_qic_init_tape();
                break;

            case QIC_WRITE_DATA:
                wangtek_qic_write_data();
                break;

            case QIC_WRITE_FILE_MARK:
                wangtek_qic_write_file_mark();
                break;

            case QIC_READ_DATA:
                wangtek_qic_read_data(priv);
                break;

            case QIC_READ_FILE_MARK:
                wangtek_qic_log("PC-36: test READ FILE MARK log\n");
                wangtek_qic_read_file_mark(priv);
                break;

            case QIC_READ_STATUS:
                wangtek_qic_read_status(priv);
                break;

            case WANGTEK_QIC_SEL_QIC_11_FORMAT:
                wangtek_qic_sel_qic_11_format();
                break;

            case WANGTEK_QIC_SEL_QIC_24_FORMAT:
                wangtek_qic_sel_qic_24_format();
                break;

            default:
                wangtek_qic_log("PC-36: unknown command 0x%x\n", val);
                WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 1);
                WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 6); /* Illegal Command */
                wangtek_qic_sanitize_devstatus(priv);
                break;
        }
}

static void
wangtek_qic_write(uint16_t port, uint8_t val, void *priv)
{
    wangtek_qic_t* wangtek_qic = (wangtek_qic_t*)priv;
    if (port == WANGTEK_QIC_IO_BASE + 0x00) {
        /* Control port */
        #ifdef WANGTEK_QIC_LOG_CONTROL_PORT_WRITES
        wangtek_qic_log("PC-36: control port write started\n");
        if (WANGTEK_QIC_CHK_BIT_SET(val, 0)) {
            wangtek_qic_log("PC-36: online bit set writing to control port\n");
        } else {
            wangtek_qic_log("PC-36: online bit unset writing to control port\n");
        }
        if (WANGTEK_QIC_CHK_BIT_SET(val, 1)) {
            wangtek_qic_log("PC-36: reset bit set writing to control port\n");
        } else {
            wangtek_qic_log("PC-36: reset bit unset writing to control port\n");
        }
        if (WANGTEK_QIC_CHK_BIT_SET(val, 2)) {
            wangtek_qic_log("PC-36: request bit set writing to control port\n");
        } else {
            wangtek_qic_log("PC-36: request bit unset writing to control port\n");
        }
        if (WANGTEK_QIC_CHK_BIT_SET(val, 3)) {
            wangtek_qic_log("PC-36: DMA 1/2 requested, enabling it + interrupts\n");
        }
        wangtek_qic_log("PC-36: control port write ended\n");
        #endif
        if (((WANGTEK_QIC_CHK_BIT_SET(val, 0)))) {
            WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
        }
        if (!(WANGTEK_QIC_CHK_BIT_SET(val, 0))) {
            wangtek_qic->currentfile = 1;
            wangtek_qic->fileoffset = 0x00;
        }
        if (WANGTEK_QIC_CHK_BIT_SET(val, 1)) {
            wangtek_qic_reset(priv);
        }
        if (WANGTEK_QIC_CHK_BIT_SET(val, 2)) {
            WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
        }
        if (WANGTEK_QIC_CHK_BIT_SET(val, 3)) {   
            wangtek_qic->interruptflag = true;
        }
        if (WANGTEK_QIC_CHK_BIT_SET(val, 4)) {
            fatal("PC-36: \"enable DMA 3 and interrupt\" issued, not implemented");
        }
        wangtek_qic->controlold = wangtek_qic->control;
        wangtek_qic->control = val;
        if (((WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->control, 2))) && (wangtek_qic->statusflag)) {
            //wangtek_qic->setstatusflag = true;
            //WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
            //usleep(50000);
            WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
            wangtek_qic_log("PC-36: in statusflag condition\n");
            if (wangtek_qic->statusbytes == 6) {
                wangtek_qic->statusbytetotransfer = wangtek_qic->devstatus >> 8;
            } else if (wangtek_qic->statusbytes == 5) {
                wangtek_qic->statusbytetotransfer = wangtek_qic->devstatus & 0x00ff;
            } else {
                wangtek_qic->statusbytetotransfer = 0xC3; /* temporarily changed to C3, should be changed back to 00 */
                /* either data error counter or underrun counter, neither of which we emulate */
            }
        }
        if ((!(wangtek_qic->statusflag3)) && ((WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->controlold, 2))) && (!(WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->control, 2)))) {
            wangtek_qic_log("PC-36: REQ set in controlold but not in control, assuming intended command execution\n");
            wangtek_qic->actuallysendcommandflag = true;
            WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
            wangtek_qic_process_commands(wangtek_qic->reqtosend, priv);
        }
        if ((wangtek_qic->statusflag3) && (!(wangtek_qic->statusflag2))) {
            WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
            wangtek_qic->statusflag3 = false;
        }
        if ((wangtek_qic->statusflag2) && (!(wangtek_qic->statusflag))) {
            WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
            wangtek_qic->statusflag2 = false;
        }
        if ((!(WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->control, 2))) && (wangtek_qic->statusflag)) {
            //WANGTEK_QIC_SET_BIT(wangtek_qic->status, 0);
            //wangtek_qic->setstatusflag = true;
        }
    } else if (port == WANGTEK_QIC_IO_BASE + 0x01) {
        /* Command/Data port */
        if ((!(WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->status, 1))) && (val != QIC_READ_STATUS)) {
            wangtek_qic_log("PC-36: EXC set, ignoring command 0x%x\n", val);
            return; /* per the manual we should reject anything that's not READ STATUS during EXC */
        }

        wangtek_qic_log("PC-36: 0x%x recorded to execute\n", val);

        wangtek_qic->reqtosend = val;
        //WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);

    } else {
        /* alright now what even happened */
        wangtek_qic_log("PC-36: fell down wangtek_qic_write switch case to unreachable code\n");
    }
}

static uint8_t
wangtek_qic_read(uint16_t port, void *priv)
{
    wangtek_qic_t* wangtek_qic = (wangtek_qic_t*)priv;
    if (port == WANGTEK_QIC_IO_BASE + 0x00) {
        /* Status port */
        wangtek_qic_log("PC-36: status port read, returned 0x%x\n", wangtek_qic->status);
        // uint8_t tmp = wangtek_qic->status; /* second part of NetBSD hack */
        uint8_t retval = wangtek_qic->status;
        if ((WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->status, 0)) && wangtek_qic->statusflag) {
            WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
        }
        if (wangtek_qic->statusflag) {
            //WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
            //wangtek_qic->clrstatusflag = true;
        }
        return retval;
    } else if (port == WANGTEK_QIC_IO_BASE + 0x01) {
        /* Data port */
        if (wangtek_qic->statusflag) {
            wangtek_qic_log("PC-36: reading from data port, status flag set\n");
            wangtek_qic_log("PC-36: sending status port byte %i\n", 7 - wangtek_qic->statusbytes);
            wangtek_qic_log("PC-36: full devstatus is 0x%x\n", wangtek_qic->devstatus);
            wangtek_qic_log("PC-36: currently sending 0x%x\n", wangtek_qic->statusbytetotransfer);
            wangtek_qic->statusbytes--;
            if (wangtek_qic->statusbytes == 0) {
                wangtek_qic_log("PC-36: 0 bytes of status left to transfer\n");
                wangtek_qic->statusflag = false;
                bool keepbit3flag = false;
                //WANGTEK_QIC_CLR_BIT(wangtek_qic->devstatus, 0);
                if (WANGTEK_QIC_CHK_BIT_SET(wangtek_qic->devstatus, 3)) {
                    keepbit3flag = true;
                }
                wangtek_qic->devstatus = 0x0000;
                if (keepbit3flag) {
                    WANGTEK_QIC_SET_BIT(wangtek_qic->devstatus, 3);
                }
                //WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
                //return wangtek_qic->statusbytetotransfer;
            }
            WANGTEK_QIC_CLR_BIT(wangtek_qic->status, 0);
            return wangtek_qic->statusbytetotransfer;
        } else {
            fatal("PC-36: data port read while not in status mode");
            return 0x00; /* here to shut up the compiler */
        }
    } else {
        /* alright now what even happened */
        wangtek_qic_log("PC-36: fell down wangtek_qic_read switch case to unreachable code, returning 0xC3\n");
        return 0xC3;
    }
}

static void *
wangtek_qic_init(UNUSED(const device_t *info))
{
    wangtek_qic_t *wangtek_qic = (wangtek_qic_t *) calloc(1, sizeof(wangtek_qic_t));

    wangtek_qic->qicthreadrun = true;
    wangtek_qic->qicthread = thread_create(wangtek_qic_read_data_thread, wangtek_qic);

    io_sethandler(WANGTEK_QIC_IO_BASE, 2, wangtek_qic_read, NULL, NULL, wangtek_qic_write, NULL, NULL, wangtek_qic);
    wangtek_qic_reset(wangtek_qic);
    wangtek_qic_log("PC-36 controller initialized\n");

    return wangtek_qic;
}


static void
wangtek_qic_close(void *priv)
{
    wangtek_qic_t* wangtek_qic = (wangtek_qic_t*)priv;

    wangtek_qic_log("PC-36: shutting down thread\n");
    wangtek_qic->qicthreadrun = false;
    thread_wait(wangtek_qic->qicthread);
    wangtek_qic_log("PC-36: shut down thread\n");

    io_removehandler(WANGTEK_QIC_IO_BASE, 2, wangtek_qic_read, NULL, NULL, wangtek_qic_write, NULL, NULL, wangtek_qic);

    if (priv) {
        free(priv);
    }

    wangtek_qic_log("PC-36 controller de-initialized\n");
}

const device_t wangtek_qic_device = {
    .name          = "Wangtek PC-36",
    .internal_name = "wangtek_qic",
    .flags         = DEVICE_ISA,
    .local         = 0,
    .init          = wangtek_qic_init,
    .close         = wangtek_qic_close,
    .reset         = wangtek_qic_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
