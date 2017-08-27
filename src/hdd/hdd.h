#ifndef EMU_HDD_H
# define EMU_HDD_H


extern char hdd_controller_name[16];


extern char	*hdd_controller_get_name(int hdd);
extern char	*hdd_controller_get_internal_name(int hdd);
extern int	hdd_controller_get_flags(int hdd);
extern int	hdd_controller_available(int hdd);
extern int	hdd_controller_current_is_mfm(void);
extern void	hdd_controller_init(char *internal_name);


#endif	/*EMU_HDD_H*/
