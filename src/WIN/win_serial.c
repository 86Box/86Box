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
 * Version:	@(#)win_serial.c	1.0.3	2017/06/04
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "plat_thread.h"
#define BHTTY_C
#include "plat_serial.h"


extern void	pclog(char *__fmt, ...);


/* Handle the receiving of data from the host port. */
static void
bhtty_reader(void *arg)
{
    BHTTY *pp = (BHTTY *)arg;
    unsigned char b;
    DWORD n;

    pclog("%s: thread started\n", pp->name);

    /* As long as the channel is open.. */
    while (pp->tid != NULL) {
	/* Post a READ on the device. */
	n = 0;
	if (ReadFile(pp->handle, &b, (DWORD)1, &n, &pp->rov) == FALSE) {
		n = GetLastError();
		if (n != ERROR_IO_PENDING) {
			/* Not good, we got an error. */
			pclog("%s: I/O error %d in read!\n", pp->name, n);
			break;
		}

		/* The read is pending, wait for it.. */
		if (GetOverlappedResult(pp->handle, &pp->rov, &n, TRUE) == FALSE) {
			n = GetLastError();
			pclog("%s: I/O error %d in read!\n", pp->name, n);
			break;
		}
	}

pclog("%s: got %d bytes of data\n", pp->name, n);
	if (n == 1) {
		/* We got data, update stuff. */
		if (pp->icnt < sizeof(pp->buff)) {
pclog("%s: queued byte %02x (%d)\n", pp->name, b, pp->icnt+1);
			pp->buff[pp->ihead++] = b;
			pp->ihead &= (sizeof(pp->buff)-1);
			pp->icnt++;

			/* Do a callback to let them know. */
			if (pp->rd_done != NULL)
				pp->rd_done(pp->rd_arg, n);
		} else {
			pclog("%s: RX buffer overrun!\n", pp->name);
		}
	}
    }

    /* Error or done, clean up. */
    pp->tid = NULL;
    pclog("%s: thread stopped.\n", pp->name);
}


/* Set the state of a port. */
int
bhtty_sstate(BHTTY *pp, void *arg)
{
    /* Make sure we can do this. */
    if (arg == NULL) {
	pclog("%s: invalid argument\n", pp->name);
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
    /* Make sure we can do this. */
    if (arg == NULL) {
	pclog("%s: invalid argument\n", pp->name);
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
    if (arg == NULL) {
	pclog("%s: invalid parameter\n", pp->name);
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
    /* If the polling thread is running, stop it. */
    (void)bhtty_active(pp, 0);

    /* Close the event handles. */
    if (pp->rov.hEvent != INVALID_HANDLE_VALUE)
	CloseHandle(pp->rov.hEvent);
    if (pp->wov.hEvent != INVALID_HANDLE_VALUE)
	CloseHandle(pp->wov.hEvent);

    if (pp->handle != INVALID_HANDLE_VALUE) {
	pclog("%s: closing host port\n", pp->name);

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
    char temp[64];
    COMMTIMEOUTS to;
    COMMCONFIG conf;
    BHTTY *pp;
    DWORD d;

    /* First things first... create a control block. */
    if ((pp = (BHTTY *)malloc(sizeof(BHTTY))) == NULL) {
	pclog("%s: out of memory!\n", port);
	return(NULL);
    }
    memset(pp, 0x00, sizeof(BHTTY));
    strncpy(pp->name, port, sizeof(pp->name)-1);

    /* Try a regular Win32 serial port. */
    sprintf(temp, "\\\\.\\%s", pp->name);
    if ((pp->handle = CreateFile(temp,
				 (GENERIC_READ|GENERIC_WRITE),
				 0,
				 NULL,
				 OPEN_EXISTING,
				 FILE_FLAG_OVERLAPPED,
				 0)) == INVALID_HANDLE_VALUE) {
	pclog("%s: open port: %d\n", pp->name, GetLastError());
	free(pp);
	return(NULL);
    }

    /* Create event handles. */
    pp->rov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    pp->wov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    /* Set up buffer size of the port. */
    if (SetupComm(pp->handle, 32768L, 32768L) == FALSE) {
	/* This fails on FTDI-based devices. */
	pclog("%s: set buffers: %d\n", pp->name, GetLastError());
#if 0
	CloseHandle(pp->handle);
	free(pp);
	return(NULL);
#endif
    }

    /* Grab default config for the driver and set it. */
    d = sizeof(COMMCONFIG);
    memset(&conf, 0x00, d);
    conf.dwSize = d;
    if (GetDefaultCommConfig(temp, &conf, &d) == TRUE) {
	/* Change config here... */

	/* Set new configuration. */
	if (SetCommConfig(pp->handle, &conf, d) == FALSE) {
		/* This fails on FTDI-based devices. */
		pclog("%s: set configuration: %d\n", pp->name, GetLastError());
#if 0
		CloseHandle(pp->handle);
		free(pp);
		return(NULL);
#endif
	}
    }
    pclog("%s: host port '%s' open\n", pp->name, temp);

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
	pclog("%s: error %d while setting TO\n", pp->name, GetLastError());
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


/* Activate the I/O for this port. */
int
bhtty_active(BHTTY *pp, int flg)
{
    if (flg) {
	pclog("%s: starting thread..\n", pp->name);
	pp->tid = thread_create(bhtty_reader, pp);
    } else {
	if (pp->tid != NULL) {
		pclog("%s: stopping thread..\n", pp->name);
		thread_kill(pp->tid);
		pp->tid = NULL;
	}
    }

    return(0);
}


/* Try to write data to an open port. */
int
bhtty_write(BHTTY *pp, unsigned char val)
{
    DWORD n = 0;

pclog("%s: writing byte %02x\n", pp->name, val);
    if (WriteFile(pp->handle, &val, 1, &n, &pp->wov) == FALSE) {
	n = GetLastError();
	if (n != ERROR_IO_PENDING) {
		/* Not good, we got an error. */
		pclog("%s: I/O error %d in write!\n", pp->name, n);
		return(-1);
	}

	/* The write is pending, wait for it.. */
	if (GetOverlappedResult(pp->handle, &pp->wov, &n, TRUE) == FALSE) {
		n = GetLastError();
		pclog("%s: I/O error %d in write!\n", pp->name, n);
		return(-1);
	}
    }

    return((int)n);
}


/*
 * Try to read data from an open port.
 *
 * For now, we will use one byte per call.  Eventually,
 * we should go back to loading a buffer full of data,
 * just to speed things up a bit.  --FvK
 */
int
bhtty_read(BHTTY *pp, unsigned char *bufp, int max)
{
    if (pp->icnt == 0) return(0);

    while (max-- > 0) {
	*bufp++ = pp->buff[pp->itail++];
pclog("%s: dequeued byte %02x (%d)\n", pp->name, *(bufp-1), pp->icnt);
	pp->itail &= (sizeof(pp->buff)-1);
	if (--pp->icnt == 0) break;
    }

    return(max);
}
