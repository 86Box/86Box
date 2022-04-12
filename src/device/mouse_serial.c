/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Serial Mouse devices.
 *
 * TODO:	Add the Genius Serial Mouse.
 *
 *
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>
#include <86box/mouse.h>


#define SERMOUSE_PORT			0			/* attach to Serial0 */

enum {
    PHASE_IDLE,
    PHASE_ID,
    PHASE_DATA,
    PHASE_STATUS,
    PHASE_DIAGNOSTIC,
    PHASE_FORMAT_AND_REVISION,
    PHASE_COPYRIGHT_STRING,
    PHASE_BUTTONS
};

enum {
    REPORT_PHASE_PREPARE,
    REPORT_PHASE_TRANSMIT
};


typedef struct {
    const char	*name;				/* name of this device */
    int8_t	type,				/* type of this device */
		port;
    uint8_t	flags, but,			/* device flags */
		want_data,
		status, format,
		prompt, on_change,
		id_len, id[255],
		data_len, data[5];
    int		abs_x, abs_y,
		rel_x, rel_y,
		rel_z,
		oldb, lastb;

    int		command_pos, command_phase,
		report_pos, report_phase,
		command_enabled, report_enabled;
    double	transmit_period, report_period;
    pc_timer_t	command_timer, report_timer;

    serial_t	*serial;
} mouse_t;
#define FLAG_INPORT	0x80			/* device is MS InPort */
#define FLAG_3BTN	0x20			/* enable 3-button mode */
#define FLAG_SCALED	0x10			/* enable delta scaling */
#define FLAG_INTR	0x04			/* dev can send interrupts */
#define FLAG_FROZEN	0x02			/* do not update counters */
#define FLAG_ENABLED	0x01			/* dev is enabled for use */


#ifdef ENABLE_MOUSE_SERIAL_LOG
int mouse_serial_do_log = ENABLE_MOUSE_SERIAL_LOG;


static void
mouse_serial_log(const char *fmt, ...)
{
    va_list ap;

    if (mouse_serial_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define mouse_serial_log(fmt, ...)
#endif


static void
sermouse_timer_on(mouse_t *dev, double period, int report)
{
    pc_timer_t *timer;
    int *enabled;

    if (report) {
	timer = &dev->report_timer;
	enabled = &dev->report_enabled;
    } else {
	timer = &dev->command_timer;
	enabled = &dev->command_enabled;
    }

    timer_on_auto(timer, period);

    *enabled = 1;
}


static double
sermouse_transmit_period(mouse_t *dev, int bps, int rps)
{
    double dbps = (double) bps;
    double temp = 0.0;
    int word_len;

    switch (dev->format) {
	case 0:
	case 1:		/* Mouse Systems and Three Byte Packed formats: 8 data, no parity, 2 stop, 1 start */
		word_len = 11;
		break;
	case 2:		/* Hexadecimal format - 8 data, no parity, 1 stop, 1 start - number of stop bits is a guess because
			   it is not documented anywhere. */
		word_len = 10;
		break;
	case 3:
	case 6:		/* Bit Pad One formats: 7 data, even parity, 2 stop, 1 start */
		word_len = 11;
		break;
	case 5:		/* MM Series format: 8 data, odd parity, 1 stop, 1 start */
		word_len = 11;
		break;
	default:
	case 7:		/* Microsoft-compatible format: 7 data, no parity, 1 stop, 1 start */
		word_len = 9;
		break;
    }

    if (rps == -1)
	temp = (double) word_len;
    else {
	temp = (double) rps;
	temp = (9600.0 - (temp * 33.0));
	temp /= rps;
    }
    temp = (1000000.0 / dbps) * temp;

    return temp;
}


/* Callback from serial driver: RTS was toggled. */
static void
sermouse_callback(struct serial_s *serial, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Start a timer to wake us up in a little while. */
    dev->command_pos = 0;
    dev->command_phase = PHASE_ID;
    if (dev->id[0] != 'H')
	dev->format = 7;
    dev->transmit_period = sermouse_transmit_period(dev, 1200, -1);
    timer_stop(&dev->command_timer);
#ifdef USE_NEW_DYNAREC
    sermouse_timer_on(dev, cpu_use_dynarec ? 5000.0 : dev->transmit_period, 0);
#else
    sermouse_timer_on(dev, dev->transmit_period, 0);
#endif
}


static uint8_t
sermouse_data_msystems(mouse_t *dev, int x, int y, int b)
{
    dev->data[0] = 0x80;
    dev->data[0] |= (b & 0x01) ? 0x00 : 0x04;	/* left button */
    dev->data[0] |= (b & 0x02) ? 0x00 : 0x01;	/* middle button */
    dev->data[0] |= (b & 0x04) ? 0x00 : 0x02;	/* right button */
    dev->data[1] = x;
    dev->data[2] = -y;
    dev->data[3] = x;				/* same as byte 1 */
    dev->data[4] = -y;				/* same as byte 2 */

    return 5;
}


static uint8_t
sermouse_data_3bp(mouse_t *dev, int x, int y, int b)
{
    dev->data[0] |= (b & 0x01) ? 0x00 : 0x04;	/* left button */
    dev->data[0] |= (b & 0x04) ? 0x00 : 0x02;	/* middle button */
    dev->data[0] |= (b & 0x02) ? 0x00 : 0x01;	/* right button */
    dev->data[1] = x;
    dev->data[2] = -y;

    return 3;
}


static uint8_t
sermouse_data_mmseries(mouse_t *dev, int x, int y, int b)
{
    if (x < -127)
	x = -127;
    if (y < -127)
	y = -127;

    dev->data[0] = 0x80;
    if (x >= 0)
	dev->data[0] |= 0x10;
    if (y < 0)
	dev->data[0] |= 0x08;
    dev->data[0] |= (b & 0x01) ? 0x04 : 0x00;	/* left button */
    dev->data[0] |= (b & 0x04) ? 0x02 : 0x00;	/* middle button */
    dev->data[0] |= (b & 0x02) ? 0x01 : 0x00;	/* right button */
    dev->data[1] = abs(x);
    dev->data[2] = abs(y);

    return 3;
}


static uint8_t
sermouse_data_bp1(mouse_t *dev, int x, int y, int b)
{
    dev->data[0] = 0x80;
    dev->data[0] |= (b & 0x01) ? 0x10 : 0x00;	/* left button */
    dev->data[0] |= (b & 0x04) ? 0x08 : 0x00;	/* middle button */
    dev->data[0] |= (b & 0x02) ? 0x04 : 0x00;	/* right button */
    dev->data[1] = (x & 0x3f);
    dev->data[2] = (x >> 6);
    dev->data[3] = (y & 0x3f);
    dev->data[4] = (y >> 6);

    return 5;
}


static uint8_t
sermouse_data_ms(mouse_t *dev, int x, int y, int z, int b)
{
    uint8_t len;

    dev->data[0] = 0x40;
    dev->data[0] |= (((y >> 6) & 0x03) << 2);
    dev->data[0] |= ((x >> 6) & 0x03);
    if (b & 0x01)
	dev->data[0] |= 0x20;
    if (b & 0x02)
	dev->data[0] |= 0x10;
    dev->data[1] = x & 0x3F;
    dev->data[2] = y & 0x3F;
    if (dev->but == 3) {
	len = 3;
	if (dev->type == MOUSE_TYPE_LT3BUTTON) {
		if (b & 0x04) {
			dev->data[3] = 0x20;
			len++;
		}
	} else {
		if ((b ^ dev->oldb) & 0x04) {
			/* Microsoft 3-button mice send a fourth byte of 0x00 when the middle button
			   has changed. */
			dev->data[3] = 0x00;
			len++;
		}
	}
    } else if (dev->but == 4) {
	len = 4;
	dev->data[3] = z & 0x0F;
	if (b & 0x04)
		dev->data[3] |= 0x10;
    } else
	len = 3;

    return len;
}


static uint8_t
sermouse_data_hex(mouse_t *dev, int x, int y, int b)
{
    char ret[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t i, but = 0x00;

    but |= (b & 0x01) ? 0x04 : 0x00;	/* left button */
    but |= (b & 0x04) ? 0x02 : 0x00;	/* middle button */
    but |= (b & 0x02) ? 0x01 : 0x00;	/* right button */

    sprintf(ret, "%02X%02X%01X", (int8_t) y, (int8_t) x, but & 0x0f);

    for (i = 0; i < 5; i++)
	dev->data[i] = ret[4 - i];

    return 5;
}


static void
sermouse_report(int x, int y, int z, int b, mouse_t *dev)
{
    int len = 0;

    memset(dev->data, 0, 5);

    /* If the mouse is 2-button, ignore the middle button. */
    if (dev->but == 2)
	b &= ~0x04;

    switch (dev->format) {
	case 0:
		len = sermouse_data_msystems(dev, x, y, b);
		break;
	case 1:
		len = sermouse_data_3bp(dev, x, y, b);
		break;
	case 2:
		len = sermouse_data_hex(dev, x, y, b);
		break;
	case 3:	/* Relative */
		len = sermouse_data_bp1(dev, x, y, b);
		break;
	case 5:
		len = sermouse_data_mmseries(dev, x, y, b);
		break;
	case 6:	/* Absolute */
		len = sermouse_data_bp1(dev, dev->abs_x, dev->abs_y, b);
		break;
	case 7:
		len = sermouse_data_ms(dev, x, y, z, b);
		break;
    }

    dev->data_len = len;
}


static void
sermouse_command_phase_idle(mouse_t *dev)
{
    dev->command_pos = 0;
    dev->command_phase = PHASE_IDLE;
    dev->command_enabled = 0;
}


static void
sermouse_command_pos_check(mouse_t *dev, int len)
{
    if (++dev->command_pos == len)
	sermouse_command_phase_idle(dev);
    else
	timer_on_auto(&dev->command_timer, dev->transmit_period);
}


static uint8_t
sermouse_last_button_status(mouse_t *dev)
{
    uint8_t ret = 0x00;

    if (dev->oldb & 0x01)
	ret |= 0x04;
    if (dev->oldb & 0x02)
	ret |= 0x02;
    if (dev->oldb & 0x04)
	ret |= 0x01;

    return ret;
}


static void
sermouse_update_delta(mouse_t *dev, int *local, int *global)
{
    int min, max;

    if (dev->format == 3) {
	min = -2048;
	max = 2047;
    } else {
	min = -128;
	max = 127;
    }

    if (*global > max) {
	*local = max;
	*global -= max;
    } else if (*global < min) {
	*local = min;
	*global += -min;
    } else {
	*local = *global;
	*global = 0;
    }
}


static uint8_t
sermouse_update_data(mouse_t *dev)
{
    uint8_t ret = 0;
    int delta_x, delta_y, delta_z;

    /* Update the deltas and the delays. */
    sermouse_update_delta(dev, &delta_x, &dev->rel_x);
    sermouse_update_delta(dev, &delta_y, &dev->rel_y);
    sermouse_update_delta(dev, &delta_z, &dev->rel_z);

    sermouse_report(delta_x, delta_y, delta_z, dev->oldb, dev);

    mouse_serial_log("delta_x = %i, delta_y = %i, delta_z = %i, dev->oldb = %02X\n",
		     delta_x, delta_y, delta_z, dev->oldb);

    if (delta_x || delta_y || delta_z || (dev->oldb != dev->lastb) || !dev->on_change)
	ret = 1;

    dev->lastb = dev->oldb;

    mouse_serial_log("sermouse_update_data(): ret = %i\n", ret);

    return ret;
}


static double
sermouse_report_period(mouse_t *dev)
{
    if (dev->report_period == 0)
	return dev->transmit_period;
    else
	return dev->report_period;
}


static void
sermouse_report_prepare(mouse_t *dev)
{
    if (sermouse_update_data(dev)) {
	/* Start sending data. */
	dev->report_phase = REPORT_PHASE_TRANSMIT;
	dev->report_pos = 0;
	sermouse_timer_on(dev, dev->transmit_period, 1);
    } else {
	dev->report_phase = REPORT_PHASE_PREPARE;
	sermouse_timer_on(dev, sermouse_report_period(dev), 1);
    }
}


static void
sermouse_report_timer(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    if (dev->report_phase == REPORT_PHASE_PREPARE)
	sermouse_report_prepare(dev);
    else {
	/* If using the Mouse Systems format, update data because
	   the last two bytes are the X and Y delta since bytes 1
	   and 2 were transmitted. */
	if (!dev->format && (dev->report_pos == 3))
		sermouse_update_data(dev);
	serial_write_fifo(dev->serial, dev->data[dev->report_pos]);
	if (++dev->report_pos == dev->data_len) {
		if (!dev->report_enabled)
			sermouse_report_prepare(dev);
		else {
			sermouse_timer_on(dev, sermouse_report_period(dev), 1);
			dev->report_phase = REPORT_PHASE_PREPARE;
		}
	} else
		sermouse_timer_on(dev, dev->transmit_period, 1);
    }
}


/* Callback timer expired, now send our "mouse ID" to the serial port. */
static void
sermouse_command_timer(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    switch (dev->command_phase) {
	case PHASE_ID:
		serial_write_fifo(dev->serial, dev->id[dev->command_pos]);
		sermouse_command_pos_check(dev, dev->id_len);
		if ((dev->command_phase == PHASE_IDLE) && (dev->type != MOUSE_TYPE_MSYSTEMS)) {
			/* This resets back to Microsoft-compatible mode. */
			dev->report_phase = REPORT_PHASE_PREPARE;
			sermouse_report_timer((void *) dev);
		}
		break;
	case PHASE_DATA:
		serial_write_fifo(dev->serial, dev->data[dev->command_pos]);
		sermouse_command_pos_check(dev, dev->data_len);
		break;
	case PHASE_STATUS:
		serial_write_fifo(dev->serial, dev->status);
		sermouse_command_phase_idle(dev);
		break;
	case PHASE_DIAGNOSTIC:
		if (dev->command_pos)
			serial_write_fifo(dev->serial, 0x00);
		else
			serial_write_fifo(dev->serial, sermouse_last_button_status(dev));
		sermouse_command_pos_check(dev, 3);
		break;
	case PHASE_FORMAT_AND_REVISION:
		serial_write_fifo(dev->serial, 0x10 | (dev->format << 1));
		sermouse_command_phase_idle(dev);
		break;
	case PHASE_BUTTONS:
		serial_write_fifo(dev->serial, dev->but);
		sermouse_command_phase_idle(dev);
		break;
	default:
		sermouse_command_phase_idle(dev);
		break;
    }
}


static int
sermouse_poll(int x, int y, int z, int b, void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    if (!x && !y && !z && (b == dev->oldb)) {
	dev->oldb = b;
	return(1);
    }

    dev->oldb = b;
    dev->abs_x += x;
    dev->abs_y += y;
    if (dev->abs_x < 0)
	dev->abs_x = 0;
    if (dev->abs_x > 4095)
	dev->abs_x = 4095;
    if (dev->abs_y < 0)
	dev->abs_y = 0;
    if (dev->abs_y > 4095)
	dev->abs_y = 4095;

    if (dev->format == 3) {
	if (x > 2047) x = 2047;
	if (y > 2047) y = 2047;
	if (x <- 2048) x = -2048;
	if (y <- 2048) y = -2048;
    } else {
	if (x > 127) x = 127;
	if (y > 127) y = 127;
	if (x <- 128) x = -128;
	if (y <- 128) y = -128;
    }

    dev->rel_x += x;
    dev->rel_y += y;
    dev->rel_z += z;

    return(0);
}


static void
ltsermouse_prompt_mode(mouse_t *dev, int prompt)
{
    dev->prompt = prompt;
    dev->status &= 0xBF;
    if (prompt)
	dev->status |= 0x40;
}


static void
ltsermouse_command_phase(mouse_t *dev, int phase)
{
    dev->command_pos = 0;
    dev->command_phase = phase;
    timer_stop(&dev->command_timer);
    sermouse_timer_on(dev, dev->transmit_period, 0);
}


static void
ltsermouse_set_report_period(mouse_t *dev, int rps)
{
    dev->report_period = sermouse_transmit_period(dev, 9600, rps);
    timer_stop(&dev->report_timer);
    sermouse_timer_on(dev, dev->report_period, 1);
    ltsermouse_prompt_mode(dev, 0);
    dev->report_phase = REPORT_PHASE_PREPARE;
}


static void
ltsermouse_write(struct serial_s *serial, void *priv, uint8_t data)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Stop reporting when we're processing a command. */
    dev->report_phase = REPORT_PHASE_PREPARE;

    if (dev->want_data)  switch (dev->want_data) {
	case 0x2A:
		dev->data_len--;
		dev->want_data = 0;
		switch (data) {
			default:
				mouse_serial_log("Serial mouse: Invalid period %02X, using 1200 bps\n", data);
				/*FALLTHROUGH*/
			case 0x6E:
				dev->transmit_period = sermouse_transmit_period(dev, 1200, -1);
				break;
			case 0x6F:
				dev->transmit_period = sermouse_transmit_period(dev, 2400, -1);
				break;
			case 0x70:
				dev->transmit_period = sermouse_transmit_period(dev, 4800, -1);
				break;
			case 0x71:
				dev->transmit_period = sermouse_transmit_period(dev, 9600, -1);
				break;
		}
		break;
    } else  switch (data) {
	case 0x2A:
		dev->want_data = data;
		dev->data_len = 1;
		break;
	case 0x44:	/* Set prompt mode */
		ltsermouse_prompt_mode(dev, 1);
		break;
	case 0x50:
		if (!dev->prompt)
			ltsermouse_prompt_mode(dev, 1);
		sermouse_update_data(dev);
		ltsermouse_command_phase(dev, PHASE_DATA);
		break;
	case 0x73:	/* Status */
		ltsermouse_command_phase(dev, PHASE_STATUS);
		break;
	case 0x4A:	/* Report Rate Selection commands */
		ltsermouse_set_report_period(dev, 10);
		break;
	case 0x4B:
		ltsermouse_set_report_period(dev, 20);
		break;
	case 0x4C:
		ltsermouse_set_report_period(dev, 35);
		break;
	case 0x52:
		ltsermouse_set_report_period(dev, 50);
		break;
	case 0x4D:
		ltsermouse_set_report_period(dev, 70);
		break;
	case 0x51:
		ltsermouse_set_report_period(dev, 100);
		break;
	case 0x4E:
		ltsermouse_set_report_period(dev, 150);
		break;
	case 0x4F:
		ltsermouse_prompt_mode(dev, 0);
		dev->report_period = 0;
		timer_stop(&dev->report_timer);
		dev->report_phase = REPORT_PHASE_PREPARE;
		sermouse_report_timer((void *) dev);
		break;
	case 0x41:
		dev->format = 6;	/* Aboslute Bit Pad One Format */
		dev->abs_x = dev->abs_y = 0;
		break;
	case 0x42:
		dev->format = 3;	/* Relative Bit Pad One Format */
		break;
	case 0x53:
		dev->format = 5;	/* MM Series Format */
		break;
	case 0x54:
		dev->format = 1;	/* Three Byte Packed Binary Format */
		break;
	case 0x55:	/* This is the Mouse Systems-compatible format */
		dev->format = 0;	/* Five Byte Packed Binary Format */
		break;
	case 0x56:
		dev->format = 7;	/* Microsoft Compatible Format */
		break;
	case 0x57:
		dev->format = 2;	/* Hexadecimal Format */
		break;
	case 0x05:
		ltsermouse_command_phase(dev, PHASE_DIAGNOSTIC);
		break;
	case 0x66:
		ltsermouse_command_phase(dev, PHASE_FORMAT_AND_REVISION);
		break;
	case 0x6B:
		ltsermouse_command_phase(dev, PHASE_BUTTONS);
		break;
    }
}


static void
sermouse_speed_changed(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    if (dev->report_enabled) {
	timer_stop(&dev->report_timer);
	if (dev->report_phase == REPORT_PHASE_TRANSMIT)
		sermouse_timer_on(dev, dev->transmit_period, 1);
	else
		sermouse_timer_on(dev, sermouse_report_period(dev), 1);
    }

    if (dev->command_enabled) {
	timer_stop(&dev->command_timer);
	sermouse_timer_on(dev, dev->transmit_period, 0);
    }
}


static void
sermouse_close(void *priv)
{
    mouse_t *dev = (mouse_t *)priv;

    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd)
	memset(dev->serial->sd, 0, sizeof(serial_device_t));

    free(dev);
}


/* Initialize the device for use by the user. */
static void *
sermouse_init(const device_t *info)
{
    mouse_t *dev;

    dev = (mouse_t *)malloc(sizeof(mouse_t));
    memset(dev, 0x00, sizeof(mouse_t));
    dev->name = info->name;
    dev->but = device_get_config_int("buttons");
    if (dev->but > 2)
	dev->flags |= FLAG_3BTN;

    if (info->local == MOUSE_TYPE_MSYSTEMS) {
	dev->on_change = 1;
	dev->format = 0;
	dev->type = info->local;
	dev->id_len = 1;
	dev->id[0] = 'H';
    } else {
	dev->on_change = !info->local;
	dev->format = 7;
	dev->status = 0x0f;
	dev->id_len = 1;
	dev->id[0] = 'M';
	switch(dev->but) {
		case 2:
		default:
			dev->type = info->local ? MOUSE_TYPE_LOGITECH : MOUSE_TYPE_MICROSOFT;
			break;
		case 3:
			dev->type = info->local ? MOUSE_TYPE_LT3BUTTON : MOUSE_TYPE_MS3BUTTON;
			dev->id_len = 2;
			dev->id[1] = '3';
			break;
		case 4:
			dev->type = MOUSE_TYPE_MSWHEEL;
			dev->id_len = 6;
			dev->id[1] = 'Z';
			dev->id[2] = '@';
			break;
	}
    }

    dev->transmit_period = sermouse_transmit_period(dev, 1200, -1);

    /* Default: Continuous reporting = no delay between reports. */
    dev->report_phase = REPORT_PHASE_PREPARE;
    dev->report_period = 0;

    /* Default: Doing nothing - command transmit timer deactivated. */
    dev->command_phase = PHASE_IDLE;

    dev->port = device_get_config_int("port");

    /* Attach a serial port to the mouse. */
    if (info->local)
	dev->serial = serial_attach(dev->port, sermouse_callback, ltsermouse_write, dev);
    else
	dev->serial = serial_attach(dev->port, sermouse_callback, NULL, dev);

    mouse_serial_log("%s: port=COM%d\n", dev->name, dev->port + 1);

    timer_add(&dev->report_timer, sermouse_report_timer, dev, 0);
    timer_add(&dev->command_timer, sermouse_command_timer, dev, 0);

    if (info->local == MOUSE_TYPE_MSYSTEMS) {
	sermouse_timer_on(dev, dev->transmit_period, 1);
	dev->report_enabled = 1;
    }

    /* Tell them how many buttons we have. */
    mouse_set_buttons((dev->flags & FLAG_3BTN) ? 3 : 2);

    /* Return our private data to the I/O layer. */
    return(dev);
}

static const device_config_t mssermouse_config[] = {
// clang-format off
    {
        .name = "port",
        .description = "Serial Port",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "COM1", .value = 0 },
            { .description = "COM2", .value = 1 },
            { .description = "COM3", .value = 2 },
            { .description = "COM4", .value = 3 },
            { .description = ""                 }
        }
    },
    {
        .name = "buttons",
        .description = "Buttons",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 2,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Two",   .value = 2 },
            { .description = "Three", .value = 3 },
            { .description = "Wheel", .value = 4 },
            { .description = ""                  }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

static const device_config_t ltsermouse_config[] = {
// clang-format off
    {
        .name = "port",
        .description = "Serial Port",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 0,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "COM1", .value = 0 },
            { .description = "COM2", .value = 1 },
            { .description = "COM3", .value = 2 },
            { .description = "COM4", .value = 3 },
            { .description = ""                 }
        }
    },
    {
        .name = "buttons",
        .description = "Buttons",
        .type = CONFIG_SELECTION,
        .default_string = "",
        .default_int = 2,
        .file_filter = "",
        .spinner = { 0 },
        .selection = {
            { .description = "Two",   .value = 2 },
            { .description = "Three", .value = 3 },
            { .description = ""                  }
        }
    },
    { .name = "", .description = "", .type = CONFIG_END }
// clang-format on
};

const device_t mouse_mssystems_device = {
    .name = "Mouse Systems Serial Mouse",
    .internal_name = "mssystems",
    .flags = DEVICE_COM,
    .local = MOUSE_TYPE_MSYSTEMS,
    .init = sermouse_init,
    .close = sermouse_close,
    .reset = NULL,
    { .poll = sermouse_poll },
    .speed_changed = sermouse_speed_changed,
    .force_redraw = NULL,
    .config = mssermouse_config
};

const device_t mouse_msserial_device = {
    .name = "Microsoft Serial Mouse",
    .internal_name = "msserial",
    .flags = DEVICE_COM,
    .local = 0,
    .init = sermouse_init,
    .close = sermouse_close,
    .reset = NULL,
    { .poll = sermouse_poll },
    .speed_changed = sermouse_speed_changed,
    .force_redraw = NULL,
    .config = mssermouse_config
};

const device_t mouse_ltserial_device = {
    .name = "Logitech Serial Mouse",
    .internal_name = "ltserial",
    .flags = DEVICE_COM,
    .local = 1,
    .init = sermouse_init,
    .close = sermouse_close,
    .reset = NULL,
    { .poll = sermouse_poll },
    .speed_changed = sermouse_speed_changed,
    .force_redraw = NULL,
    .config = ltsermouse_config
};
