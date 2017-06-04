#ifndef EMU_MOUSE_H
# define EMU_MOUSE_H


#define MOUSE_TYPE_SERIAL	0	/* Serial Mouse */
#define MOUSE_TYPE_PS2		1	/* IBM PS/2 series Bus Mouse */
#define MOUSE_TYPE_PS2_MS	2	/* Microsoft Intellimouse PS/2 */
#define MOUSE_TYPE_BUS		3	/* Logitech/ATI Bus Mouse */
#define MOUSE_TYPE_AMSTRAD	4	/* Amstrad PC system mouse */
#define MOUSE_TYPE_OLIM24	5	/* Olivetti M24 system mouse */
#define MOUSE_TYPE_MSYSTEMS	6	/* Mouse Systems mouse */
#define MOUSE_TYPE_GENIUS	7	/* Genius Bus Mouse */

#define MOUSE_TYPE_MASK		0x0f
#define MOUSE_TYPE_3BUTTON	(1<<7)	/* device has 3+ buttons */


typedef struct {
    char	name[80];
    char	internal_name[24];
    int		type;
    void	*(*init)(void);
    void	(*close)(void *p);
    uint8_t	(*poll)(int x, int y, int z, int b, void *p);
} mouse_t;


extern int	mouse_type;


extern void	mouse_emu_init(void);
extern void	mouse_emu_close(void);
extern void	mouse_poll(int x, int y, int z, int b);
extern char	*mouse_get_name(int mouse);
extern char	*mouse_get_internal_name(int mouse);
extern int	mouse_get_from_internal_name(char *s);
extern int	mouse_get_type(int mouse);
extern int	mouse_get_ndev(void);


#endif	/*EMU_MOUSE_H*/
