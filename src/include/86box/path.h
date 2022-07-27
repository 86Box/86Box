extern void	path_get_dirname(char *dest, const char *path);
extern char	*path_get_filename(char *s);
extern char	*path_get_extension(char *s);
extern void	path_append_filename(char *dest, const char *s1, const char *s2);
extern void	path_slash(char *path);
extern void path_normalize(char *path);
extern int	path_abs(char *path);