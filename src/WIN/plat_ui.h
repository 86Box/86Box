#ifndef __unix
extern void	plat_msgbox_error(int i);
extern wchar_t	*plat_get_string_from_id(int i);

#ifndef IDS_2219
#define IDS_2219 2219
#endif

#ifndef IDS_2077
#define IDS_2077 2077
#endif

#ifndef IDS_2078
#define IDS_2078 2078
#endif

#ifndef IDS_2079
#define IDS_2079 2079
#endif

#ifndef IDS_2139
#define IDS_2139 2139
#endif
#endif

extern void	plat_msgbox_fatal(char *string);
extern void	get_executable_name(wchar_t *s, int size);
extern void	set_window_title(wchar_t *s);
extern void	startblit(void);
extern void	endblit(void);
