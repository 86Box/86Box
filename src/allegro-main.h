/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#define getr8 allegro_getr8
#define setr8 allegro_setr8
#define get_filename allegro_get_filename
#define append_filename allegro_append_filename
#define put_backslash allegro_put_backslash
#define get_extension allegro_get_extension
#define GFX_VGA allegro_GFX_VGA
#define MAX_JOYSTICKS allegro_MAX_JOYSTICKS

#include <allegro.h>

#undef MAX_JOYSTICKS
#undef GFX_VGA
#undef getr8
#undef setr8
#undef get_filename
#undef append_filename
#undef put_backslash
#undef get_extension
