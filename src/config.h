/* Copyright holders: Sarah Walker
   see COPYING for more details
*/


extern wchar_t	config_file_default[256];


#ifdef __cplusplus
extern "C" {
#endif

extern int	config_get_int(char *head, char *name, int def);
extern int	config_get_hex16(char *head, char *name, int def);
extern int	config_get_hex20(char *head, char *name, int def);
extern int	config_get_mac(char *head, char *name, int def);
extern char	*config_get_string(char *head, char *name, char *def);
extern wchar_t	*config_get_wstring(char *head, char *name, wchar_t *def);
extern void	config_set_int(char *head, char *name, int val);
extern void	config_set_hex16(char *head, char *name, int val);
extern void	config_set_hex20(char *head, char *name, int val);
extern void	config_set_mac(char *head, char *name, int val);
extern void	config_set_string(char *head, char *name, char *val);
extern void	config_set_wstring(char *head, char *name, wchar_t *val);

extern char	*get_filename(char *s);
extern wchar_t	*get_filename_w(wchar_t *s);
extern void	append_filename(char *dest, char *s1, char *s2, int size);
extern void	append_filename_w(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size);
extern void	put_backslash(char *s);
extern void	put_backslash_w(wchar_t *s);
extern char	*get_extension(char *s);

extern wchar_t	*get_extension_w(wchar_t *s);

extern int	config_load(wchar_t *fn);
extern void	config_save(wchar_t *fn);
extern void	config_dump(void);

extern void	loadconfig(wchar_t *fn);
extern void	saveconfig(void);

#ifdef __cplusplus
}
#endif
