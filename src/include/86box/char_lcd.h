/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          HD44780 character LCD module.
 *
 * Authors: Nathan Lewis, <git@nrlewis.dev>
 *
 *          Copyright 2026 Nathan Lewis.
 */
#ifndef EMU_CHAR_LCD_H
#define EMU_CHAR_LCD_H

#define CHAR_LCD_MAX_COLS 40
#define CHAR_LCD_MAX_ROWS 4

/* The glass's colours, as the panel configuration value. */
enum {
    CHAR_LCD_PANEL_YELLOW_GREEN = 0, /* Positive STN, yellow-green LED backlight. */
    CHAR_LCD_PANEL_BLUE,             /* Negative STN, white LED backlight. */
    CHAR_LCD_PANEL_GREY,             /* Positive FSTN, reflective, no backlight. */
    CHAR_LCD_PANEL_AMBER             /* Negative, amber backlight. */
};

/* How a row of the glass is driven. */
enum {
    CHAR_LCD_ROW_UNDRIVEN = 0, /* Its common lines are not scanned: blank. */
    CHAR_LCD_ROW_DRIVEN,
    CHAR_LCD_ROW_OVERDRIVEN    /* 1-line mode's 1/8 duty on glass biased for 1/16: unlit dots darken. */
};

/* What the glass shows, for the user interface. */
typedef struct char_lcd_frame_t {
    int      cols;
    int      rows;
    int      panel;
    uint32_t generation; /* Changes whenever the module is powered up. */
    uint8_t  row_drive[CHAR_LCD_MAX_ROWS];
    /* Each character cell's eight dot rows, bit 4 the leftmost dot. */
    uint8_t  dots[CHAR_LCD_MAX_ROWS][CHAR_LCD_MAX_COLS][8];
} char_lcd_frame_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the module is fitted. */
extern int      char_lcd_present(void);
/* Whether the module is fitted and should be shown: once something has written to it. */
extern int      char_lcd_shown(void);
/* Fills in the frame, with the blinking cursor in the phase given; 0 if no module is fitted. */
extern int      char_lcd_get_frame(char_lcd_frame_t *frame, int blink_on);

/* A whole bus cycle, as a board with an LCD bus makes. Unfitted, the bus reads high. */
extern uint8_t  char_lcd_bus_read(int rs);
extern void     char_lcd_bus_write(int rs, uint8_t val);

#ifdef EMU_DEVICE_H
extern const device_t char_lcd_device;
#endif

#ifdef __cplusplus
}
#endif

#endif /*EMU_CHAR_LCD_H*/
