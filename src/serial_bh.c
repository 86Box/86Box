/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of host serial port services for Win32.
 *
 *		This code is based on a universal serial port driver for
 *		Windows and UNIX systems, with support for FTDI and Prolific
 *		USB ports. Support for these has been removed.
 *
 * Version:	@(#)serial_bh.c	1.0.1	2017/04/14
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#define _WIN32_WINNT	0x0501
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#define BHTTY_C
#include "serial_bh.h"


extern void	pclog(char *__fmt, ...);


/* Set the state of a port. */
int
bhtty_sstate(BHTTY *pp, void *arg)
{
    int i = 0;

    /* Make sure we can do this. */
    if (pp == NULL || arg == NULL) {
	pclog("invalid argument\n");
	return(-1);
    }

    if (SetCommState(pp->handle, (DCB *)arg) == FALSE) {
	/* Mark an error. */
	pclog("%s: set state: %d\n", pp->name, GetLastError());
	return(-1);
    }

    return(0);
}


/* Fetch the state of a port. */
int
bhtty_gstate(BHTTY *pp, void *arg)
{
    int i = 0;

    /* Make sure we can do this. */
    if (pp == NULL || arg == NULL) {
	pclog("BHTTY: invalid argument\n");
	return(-1);
    }

    if (GetCommState(pp->handle, (DCB *)arg) == FALSE) {
	/* Mark an error. */
	pclog("%s: get state: %d\n", pp->name, GetLastError());
	return(-1);
    }

    return(0);
}


/* Enable or disable RTS/CTS mode (hardware handshaking.) */
int
bhtty_crtscts(BHTTY *pp, char yesno)
{
    /* Make sure we can do this. */
    if (pp == NULL) {
	pclog("invalid handle\n");
	return(-1);
    }

    /* Get the current mode. */
    if (bhtty_gstate(pp, &pp->dcb) < 0) return(-1);

    switch(yesno) {
	case 0:		/* disable CRTSCTS */
		pp->dcb.fOutxDsrFlow = 0;	/* disable DSR/DCD mode */
		pp->dcb.fDsrSensitivity = 0;

		pp->dcb.fOutxCtsFlow = 0;	/* disable RTS/CTS mode */

		pp->dcb.fTXContinueOnXoff = 0;	/* disable XON/XOFF mode */
		pp->dcb.fOutX = 0;
		pp->dcb.fInX = 0;
		break;

	case 1:		/* enable CRTSCTS */
		pp->dcb.fOutxDsrFlow = 0;	/* disable DSR/DCD mode */
		pp->dcb.fDsrSensitivity = 0;

		pp->dcb.fOutxCtsFlow = 1;	/* enable RTS/CTS mode */

		pp->dcb.fTXContinueOnXoff = 0;	/* disable XON/XOFF mode */
		pp->dcb.fOutX = 0;
		pp->dcb.fInX = 0;
		break;

	default:
		pclog("%s: invalid parameter '%d'!\n", pp->name, yesno);
		return(-1);
    }

    /* Set new mode. */
    if (bhtty_sstate(pp, &pp->dcb) < 0) return(-1);

    return(0);
}


/* Set the port parameters. */
int
bhtty_params(BHTTY *pp, char dbit, char par, char sbit)
{
    /* Make sure we can do this. */
    if (pp == NULL) {
	pclog("invalid handle\n");
	return(-1);
    }

    /* Get the current mode. */
    if (bhtty_gstate(pp, &pp->dcb) < 0) return(-1);

    /* Set the desired word length. */
    switch((int)dbit) {
	case -1:			/* no change */
		break;

	case 5:				/* FTDI doesnt like these */
	case 6:
	case 9:
		break;

	case 7:
	case 8:
		pp->dcb.ByteSize = dbit;
		break;

	default:
		pclog("%s: invalid parameter '%d'!\n", pp->name, dbit);
		return(-1);
    }

    /* Set the type of parity encoding. */
    switch((int)par) {
	case -1:			/* no change */
	case ' ':
		break;

	case 0:
	case 'N':
		pp->dcb.fParity = FALSE;
		pp->dcb.Parity = NOPARITY;
		break;

	case 1:
	case 'O':
		pp->dcb.fParity = TRUE;
		pp->dcb.Parity = ODDPARITY;
		break;

	case 2:
	case 'E':
		pp->dcb.fParity = TRUE;
		pp->dcb.Parity = EVENPARITY;
		break;

	case 3:
	case 'M':
	case 4:
	case 'S':
		break;

	default:
		pclog("%s: invalid parameter '%c'!\n", pp->name, par);
		return(-1);
    }

    /* Set the number of stop bits. */
    switch((int)sbit) {
	case -1:			/* no change */
		break;

	case 1:
		pp->dcb.StopBits = ONESTOPBIT;
		break;

	case 2:
		pp->dcb.StopBits = TWOSTOPBITS;
		break;

	default:
		pclog("%s: invalid parameter '%d'!\n", pp->name, sbit);
		return(-1);
    }

    /* Set new mode. */
    if (bhtty_sstate(pp, &pp->dcb) < 0) return(-1);

    return(0);
}


/* Put a port in transparent ("raw") state. */
void
bhtty_raw(BHTTY *pp, void *arg)
{
    DCB *dcb = (DCB *)arg;

    /* Make sure we can do this. */
    if (pp == NULL || arg == NULL) {
	pclog("invalid parameter\n");
	return;
    }

    /* Enable BINARY transparent mode. */
    dcb->fBinary = 1;
    dcb->fErrorChar = 0;			/* disable Error Replacement */
    dcb->fNull = 0;				/* disable NUL stripping */

    /* Disable the DTR and RTS lines. */
    dcb->fDtrControl = DTR_CONTROL_DISABLE;	/* DTR line */
    dcb->fRtsControl = RTS_CONTROL_DISABLE;	/* RTS line */

    /* Disable DSR/DCD handshaking. */
    dcb->fOutxDsrFlow = 0;			/* DSR handshaking */
    dcb->fDsrSensitivity = 0;			/* DSR Sensitivity */

    /* Disable RTS/CTS handshaking. */
    dcb->fOutxCtsFlow = 0;			/* CTS handshaking */

    /* Disable XON/XOFF handshaking. */
    dcb->fTXContinueOnXoff = 0;			/* continue TX after Xoff */
    dcb->fOutX = 0;				/* enable output X-ON/X-OFF */
    dcb->fInX = 0;				/* enable input X-ON/X-OFF */
    dcb->XonChar = 0x11;			/* ASCII XON */
    dcb->XoffChar = 0x13;			/* ASCII XOFF */
    dcb->XonLim = 100;
    dcb->XoffLim = 100;

    dcb->fParity = FALSE;
    dcb->Parity = NOPARITY;
    dcb->StopBits = ONESTOPBIT;
    dcb->BaudRate = CBR_1200;
}


/* Set the port speed. */
int
bhtty_speed(BHTTY *pp, long speed)
{
    int i;

    /* Make sure we can do this. */
    if (pp == NULL) {
	pclog("invalid handle\n");
	return(-1);
    }

    /* Get the current mode and speed. */
    if (bhtty_gstate(pp, &pp->dcb) < 0) return(-1);

    /*
     * Set speed.
     *
     * This is not entirely correct, we should use a table
     * with DCB_xxx speed values here, but we removed that
     * and just hardcode the speed value into DCB.  --FvK
     */
    pp->dcb.BaudRate = speed;

    /* Set new speed. */
    if (bhtty_sstate(pp, &pp->dcb) < 0) return(-1);

    return(0);
}


/* Clean up and flush. */
int
bhtty_flush(BHTTY *pp)
{
    DWORD dwErrs;
    COMSTAT cs;
    int i = 0;

    /* Make sure we can do this. */
    if (pp == NULL) {
	pclog("invalid handle\n");
	return(-1);
    }

    /* First, clear any errors. */
    (void)ClearCommError(pp->handle, &dwErrs, &cs);

    /* Now flush all buffers. */
    if (PurgeComm(pp->handle,
		  (PURGE_RXABORT | PURGE_TXABORT | \
		   PURGE_RXCLEAR | PURGE_TXCLEAR)) == FALSE) {
	pclog("%s: flush: %d\n", pp->name, GetLastError());
	return(-1);
    }

    /* Re-clear any errors. */
    if (ClearCommError(pp->handle, &dwErrs, &cs) == FALSE) {
	pclog("%s: clear errors: %d\n", pp->name, GetLastError());
	return(-1);
    }

    return(0);
}


/* Close an open serial port. */
void
bhtty_close(BHTTY *pp)
{
    /* Make sure we can do this. */
    if (pp == NULL) {
	pclog("BHTTY: invalid handle\n");
	return;
    }

    if (pp->handle != INVALID_HANDLE_VALUE) {
	/* Restore the previous port state, if any. */
	(void)bhtty_sstate(pp, &pp->odcb);

	/* Close the port. */
	CloseHandle(pp->handle);
	pp->handle = INVALID_HANDLE_VALUE;
    }

    /* Release the control block. */
    free(pp);
}


/* Open a host serial port for I/O. */
BHTTY *
bhtty_open(char *port, int tmo)
{
    char buff[64];
    COMMTIMEOUTS to;
#if 0
    COMMCONFIG conf;
    DWORD d;
#endif
    BHTTY *pp;
    int i = 0;

    /* Make sure we can do this. */
    if (port == NULL) {
	pclog("invalid argument!\n");
	return(NULL);
    }

    /* First things first... create a control block. */
    if ((pp = (BHTTY *)malloc(sizeof(BHTTY))) == NULL) {
	pclog("%s: out of memory!\n", port);
	return(NULL);
    }
    memset(pp, 0x00, sizeof(BHTTY));
    strncpy(pp->name, port, sizeof(pp->name)-1);

    /* Try a regular Win32 serial port. */
    sprintf(buff, "\\\\.\\%s", pp->name);
    pp->handle = CreateFile(buff,
			    (GENERIC_READ|GENERIC_WRITE),
			    0, NULL, OPEN_EXISTING,
			    FILE_FLAG_OVERLAPPED,
			    0);
    if (pp->handle == INVALID_HANDLE_VALUE) {
	pclog("%s: open port: %d\n", pp->name, GetLastError());
	free(pp);
	return(NULL);
    }

#if 0
    /* Set up buffer size of the port. */
    if (SetupComm(pp->handle, 32768L, 32768L) == FALSE) {
	/* This fails on FTDI-based devices. */
	pclog("%s: set buffers: %d\n", pp->name, GetLastError());
//	CloseHandle(pp->handle);
//	free(pp);
//	return(NULL);
    }

    /* Grab default config for the driver and set it. */
    d = sizeof(COMMCONFIG);
    memset(&conf, 0x00, d);
    conf.dwSize = d;
    if (GetDefaultCommConfig(pp->name, &conf, &d) == TRUE) {
	/* Change config here... */

	/* Set new configuration. */
	if (SetCommConfig(pp->handle, &conf, d) == FALSE) {
		/* This fails on FTDI-based devices. */
		pclog("%s: set configuration: %d\n", pp->name, GetLastError());
//		CloseHandle(pp->handle);
//		free(pp);
//		return(NULL);
	}
    }
#endif

    /*
     * We now have an open port. To allow for clean exit
     * of the application, we first retrieve the port's
     * current settings, and save these for later.
     */
    if (bhtty_gstate(pp, &pp->odcb) < 0) {
	(void)bhtty_close(pp);
	return(NULL);
    }
    memcpy(&pp->dcb, &pp->odcb, sizeof(DCB));

    /* Force the port to BINARY mode. */
    bhtty_raw(pp, &pp->dcb);

    /* Set new state of this port. */
    if (bhtty_sstate(pp, &pp->dcb) < 0) {
	(void)bhtty_close(pp);
	return(NULL);
    }

    /* Just to make sure.. disable RTS/CTS mode. */
    (void)bhtty_crtscts(pp, 0);

    /* Set new timeout values. */
    if (GetCommTimeouts(pp->handle, &to) == FALSE) {
	pclog("%s: error %d while getting current TO\n",
				pp->name, GetLastError());
	(void)bhtty_close(pp);
	return(NULL);
    }
    if (tmo < 0) {
	/* No timeout, immediate return. */
	to.ReadIntervalTimeout = MAXDWORD;
	to.ReadTotalTimeoutMultiplier = 0;
	to.ReadTotalTimeoutConstant = 0;
    } else if (tmo == 0) {
	/* No timeout, wait for data. */
	memset(&to, 0x00, sizeof(to));
    } else {
	/* Timeout specified. */
	to.ReadIntervalTimeout = MAXDWORD;
	to.ReadTotalTimeoutMultiplier = MAXDWORD;
	to.ReadTotalTimeoutConstant = tmo;
    }
    if (SetCommTimeouts(pp->handle, &to) == FALSE) {
	pclog("%s: error %d while setting TO\n",
			pp->name, GetLastError());
	(void)bhtty_close(pp);
	return(NULL);
    }

    /* Clear all errors and flush all buffers. */
    if (bhtty_flush(pp) < 0) {
	(void)bhtty_close(pp);
	return(NULL);
    }

    return(pp);
}


/* A pending WRITE has finished, handle it. */
static VOID CALLBACK
bhtty_write_comp(DWORD err, DWORD num, OVERLAPPED *priv)
{
    BHTTY *pp = (BHTTY *)priv->hEvent;

//pclog("%s: write complete, status %d, num %d\n", pp->name, err, num);
#if 0
    if (
	if (GetOverlappedResult(p->handle,
		    &p->rov, &mst, TRUE) == FALSE) {
			r = GetLastError();
			if (r != ERROR_OPERATION_ABORTED)
		    	/* OK, we're being shut down. */
		    	sprintf(serial_errmsg,
				"%s: I/O read error!", p->name);
			return(-1);
		}
#endif
}


/* Try to write data to an open port. */
int
bhtty_write(BHTTY *pp, unsigned char val)
{
    DWORD n;

    /* Make sure we can do this. */
    if (pp == NULL) {
	pclog("invalid parameter\n");
	return(-1);
    }
//pclog("BHwrite(%08lx, %02x, '%c')\n", pp->handle, val, val);

    /* Save the control pointer for later use. */
    pp->wov.hEvent = (HANDLE)pp;

    if (WriteFileEx(pp->handle,
		    &val, 1,
		    &pp->wov,
		    bhtty_write_comp) == FALSE) {
	n = GetLastError();
	pclog("%s: I/O error %d in write!\n", pp->name, n);
	return(-1);
    }

    /* Its pending, so handled in the completion routine. */
    SleepEx(1, TRUE);

    return(0);
}


/*
 * A pending READ has finished, handle it.
 */
static VOID CALLBACK
bhtty_read_comp(DWORD err, DWORD num, OVERLAPPED *priv)
{
    BHTTY *pp = (BHTTY *)priv->hEvent;
    DWORD r;
//pclog("%s: read complete, status %d, num %d\n", pp->name, err, num);

    if (GetOverlappedResult(pp->handle, &pp->rov, &r, TRUE) == FALSE) {
	r = GetLastError();
	if (r != ERROR_OPERATION_ABORTED) 
    		/* OK, we're being shut down. */
    		pclog("%s: I/O read error!", pp->name);
	return;
    }
//pclog("%s: read done, num=%d (%d)\n", pp->name, num, r);

    /* Do a callback to let them know. */
    if (pp->rd_done != NULL)
	pp->rd_done(pp->rd_arg, num);
}


/*
 * Try to read data from an open port.
 *
 * For now, we will use one byte per call.  Eventually,
 * we should go back to loading a buffer full of data,
 * just to speed things up a bit.  --FvK
 *
 * Also, not that we do not wait here. We just POST a
 * read operation, and the completion routine will do
 * the clean-up and notify the caller.
 */
int
bhtty_read(BHTTY *pp, unsigned char *bufp, int max)
{
    DWORD r;

    /* Just one byte. */
    max = 1;

    /* Make sure we can do this. */
    if (pp == NULL) {
	pclog("invalid parameter\n");
	return(-1);
    }

    /* Save the control pointer for later use. */
    pp->rov.hEvent = (HANDLE)pp;
//pclog("%s: read(%08lx, %d)\n", pp->name, pp->handle, max);

    /* Post a READ on the device. */
    if (ReadFileEx(pp->handle,
		   bufp, (DWORD)max,
		   &pp->rov,
		   bhtty_read_comp) == FALSE) {
	r = GetLastError();
	if (r != ERROR_IO_PENDING) {
		/* OK, we're being shut down. */
		if (r != ERROR_INVALID_HANDLE)
			pclog("%s: I/O read error!\n", pp->name);
		return(-1);
	}
    }

    /* Make ourself alertable. */
    SleepEx(1, TRUE);

    /* OK, it's pending, so we are good for now. */
    return(0);
}
