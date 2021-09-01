#include <inttypes.h>
void	plat_midi_init(void)
{

}

void	plat_midi_close(void)
{

}

void	plat_midi_play_msg(uint8_t *msg)
{

}

void	plat_midi_play_sysex(uint8_t *sysex, unsigned int len)
{

}

int	plat_midi_write(uint8_t val)
{
    return 0;
}

int	plat_midi_get_num_devs(void)
{
    return 0;
}

void	plat_midi_get_dev_name(int num, char *s)
{
    s[0] = ' ';
    s[1] = 0;
}

void plat_midi_input_init(void)
{

}

void plat_midi_input_close(void)
{

}

int plat_midi_in_get_num_devs(void)
{
    return 0;
}

void plat_midi_in_get_dev_name(int num, char *s)
{
    s[0] = ' ';
    s[1] = 0;
}