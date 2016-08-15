#include "allegro-main.h"
#include "ibm.h"
#include "cpu.h"
#include "model.h"
#include "nvr.h"
#include "video.h"

#undef printf

int mousecapture = 0;
int quited = 0;
int winsizex = -1, winsizey = -1;

int romspresent[ROM_MAX];
int gfx_present[GFX_MAX];

void updatewindowsize(int x, int y)
{
        if (x < 128)
                x = 128;
        if (y < 128)
                y = 128;
        if (winsizex != x || winsizey != y)
        {
                winsizex = x;
                winsizey = y;
                allegro_video_update_size(x, y);
        }
}

void startblit()
{
}

void endblit()
{
}

static int ticks = 0;
static void timer_rout()
{
        ticks++;
}

uint64_t timer_freq;
uint64_t timer_read()
{
        return 0;
}

int main(int argc, char *argv[])
{
        int frames = 0;
        int c, d;
        allegro_init();
        allegro_video_init();
        install_timer();
        install_int_ex(timer_rout, BPS_TO_TIMER(100));
	install_int_ex(onesec, BPS_TO_TIMER(1));
	midi_init();
        
        initpc(argc, argv);

        d = romset;
        for (c = 0; c < ROM_MAX; c++)
        {
                romset = c;
                romspresent[c] = loadbios();
                pclog("romset %i - %i\n", c, romspresent[c]);
        }
        
        for (c = 0; c < ROM_MAX; c++)
        {
                if (romspresent[c])
                   break;
        }
        if (c == ROM_MAX)
        {
                printf("No ROMs present!\nYou must have at least one romset to use 86Box.");
                return 0;
        }

        romset=d;
        c=loadbios();

        if (!c)
        {
                if (romset != -1)
			printf("Configured romset not available.\nDefaulting to available romset.");
                for (c = 0; c < ROM_MAX; c++)
                {
                        if (romspresent[c])
                        {
                                romset = c;
                                model = model_getmodel(romset);
                                saveconfig();
                                resetpchard();
                                break;
                        }
                }
        }

        for (c = 0; c < GFX_MAX; c++)
                gfx_present[c] = video_card_available(video_old_to_new(c));

        if (!video_card_available(video_old_to_new(gfxcard)))
        {
                if (gfxcard) printf("Configured video BIOS not available.\nDefaulting to available romset.");
                for (c = GFX_MAX-1; c >= 0; c--)
                {
                        if (gfx_present[c])
                        {
                                gfxcard = c;
                                saveconfig();
                                resetpchard();
                                break;
                        }
                }
        }

	resetpchard();

        ticks = 0;
        while (!quited)
        {
                if (ticks)
                {
                        ticks--;
                        runpc();
                        frames++;
                        if (frames >= 200 && nvr_dosave)
                        {
                                frames = 0;
                                nvr_dosave = 0;
                                savenvr();
                        }
                }
                else
                        rest(1);

		if (ticks > 10)
			ticks = 0;
                
		if ((mouse_b & 1) && !mousecapture)
			mousecapture = 1;

		if (((key[KEY_LCONTROL] || key[KEY_RCONTROL]) && key[KEY_END]) || (mouse_b & 4))
			mousecapture = 0;

                if ((key[KEY_LCONTROL] || key[KEY_RCONTROL]) && key[KEY_ALT] && key[KEY_PGDN])
                {
			int old_winsizex = winsizex, old_winsizey = winsizey;
			if (winsizex < 512 || winsizey < 350)
				updatewindowsize(512, 350);
                        gui_enter();
			if (old_winsizex < 512 || old_winsizey < 350)
				updatewindowsize(old_winsizex, old_winsizey);
                        ticks = 0;
                }
        }
        
        closepc();

	midi_close();

        return 0;
}

END_OF_MAIN();
