#include <SDL.h>
#include <SDL_messagebox.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>
/* This #undef is needed because a SDL include header redefines HAVE_STDARG_H. */
#undef HAVE_STDARG_H
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/video.h>
#include <86box/ui.h>
#include <86box/version.h>
#include <86box/unix_sdl.h>
#include <86box/unix_osd.h>

#include <stdio.h>
#include <dirent.h>
#include <string.h>

static int SCREEN_W = 640;
static int SCREEN_H = 480;
static int BOX_W = 240;
static int BOX_H = 160;
#define LINE_HEIGHT 16
#define TITLE_HEIGHT 16
#define SCROLLBAR_WIDTH 6
#define CHAR_W 8
#define CHAR_H 8

extern SDL_Window         *sdl_win;
extern SDL_Renderer       *sdl_render;

typedef enum {
    STATE_MENU,
    STATE_FILESELECT
} AppState;

static const char *menu_items[] = {
    "fddload - Load floppy disk image",
    "cdload - Load CD-ROM image",
    "rdiskload - Load removable disk image",
    "cartload - Load cartridge image",
    "moload - Load MO image",
    "fddeject - eject disk from floppy drive",
    "cdeject - eject disc from CD-ROM drive",
    "rdiskeject - eject removable disk",
    "carteject - eject cartridge",
    "moeject - eject image from MO drive",
    "hardreset - hard reset the emulated system",
    "pause - pause the the emulated system",
    "fullscreen - toggle fullscreen",
    "version - print version and license information",
    "exit - exit 86Box",
    "close OSD"
};
#define MENU_ITEMS (sizeof(menu_items) / sizeof(menu_items[0]))

static char selected_file[256] = ""; // memoria della selezione

static SDL_Texture *font_texture = NULL;
static int font_cols = 16; // numero colonne nella bitmap font (16x16 caratteri)
static int font_rows = 16;

static int selected = 0;
static int file_selected = 0;
static int scroll_offset = 0;
static AppState state = STATE_MENU;

static char files[100][256];
static int file_count = 0;

static int max_visible = 0;

// ------------------- Funzione: trova i file .iso -------------------
int load_iso_files(char files[][256], int max_files) {
    DIR *d;
    struct dirent *dir;
    int count = 0;
    d = opendir(".");
    if (!d) return 0;

    while ((dir = readdir(d)) != NULL && count < max_files) {
        if (strstr(dir->d_name, ".iso")) {
            strncpy(files[count], dir->d_name, 255);
            files[count][255] = '\0';
            count++;
        }
    }
    closedir(d);
    return count;
}

// ------------------- Disegna testo da bitmap font -------------------
void draw_text(SDL_Renderer *renderer, const char *text, int x, int y, SDL_Color color) {
    if (!font_texture) return;
    SDL_SetTextureColorMod(font_texture, color.r, color.g, color.b);

    int i = 0;
    while (text[i]) {
        unsigned char c = text[i];
        int tx = (c % font_cols) * CHAR_W;
        int ty = (c / font_cols) * CHAR_H;
        SDL_Rect src = {tx, ty, CHAR_W, CHAR_H};
        SDL_Rect dst = {x + i * CHAR_W, y, CHAR_W, CHAR_H};
        SDL_RenderCopy(renderer, font_texture, &src, &dst);
        i++;
    }
}

// ------------------- Disegna cornice rossa + riquadro -------------------
void draw_box_with_border(SDL_Renderer *renderer, SDL_Rect box) {
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &box);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_Rect inner = {box.x + 2, box.y + 2, box.w - 4, box.h - 4};
    SDL_RenderDrawRect(renderer, &inner);

    SDL_SetRenderDrawColor(renderer, 0, 0, 128, 255);
    SDL_RenderFillRect(renderer, &inner);
}

// ------------------- Disegna menu principale -------------------
void draw_menu(SDL_Renderer *renderer, int selected) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 128, 255);
    SDL_RenderClear(renderer);

    int x0 = (SCREEN_W - BOX_W) / 2;
    int y0 = (SCREEN_H - BOX_H) / 2;
    SDL_Rect box = {x0, y0, BOX_W, BOX_H};
    draw_box_with_border(renderer, box);

    draw_text(renderer, "MAIN MENU", x0 + 20, y0 + 5, (SDL_Color){255,255,255,255});

    for (int i = 0; i < MENU_ITEMS; i++) {
        int tx = x0 + 20;
        int ty = y0 + TITLE_HEIGHT + i * LINE_HEIGHT;

        SDL_Color textColor;
        SDL_Rect bgRect = {tx - 5, ty - 2, BOX_W - 20, LINE_HEIGHT};

        if (i == selected) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderFillRect(renderer, &bgRect);
            textColor = (SDL_Color){0, 0, 0, 255};
        } else {
            textColor = (SDL_Color){255, 255, 0, 255};
        }

        draw_text(renderer, menu_items[i], tx, ty, textColor);
    }

    if (selected_file[0] != '\0') {
        char buffer[300];
        snprintf(buffer, sizeof(buffer), "File: %s", selected_file);
        draw_text(renderer, buffer, 20, SCREEN_H - 20, (SDL_Color){255,255,255,255});
    }

    SDL_RenderPresent(renderer);
}

// ------------------- Disegna selezione file -------------------
void draw_file_selector(SDL_Renderer *renderer,
                        char files[][256], int file_count,
                        int selected, int scroll_offset, int max_visible) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 128, 255);
    SDL_RenderClear(renderer);

    int x0 = (SCREEN_W - BOX_W) / 2;
    int y0 = (SCREEN_H - BOX_H) / 2;
    SDL_Rect box = {x0, y0, BOX_W, BOX_H};
    draw_box_with_border(renderer, box);

    draw_text(renderer, "SELEZIONE FILE", x0 + 20, y0 + 5, (SDL_Color){255,255,255,255});

    if (file_count == 0) {
        draw_text(renderer, "Nessun file trovato",
                  x0 + 20, y0 + TITLE_HEIGHT + 10,
                  (SDL_Color){255, 255, 0, 255});
    } else {
        for (int i = 0; i < max_visible && (i + scroll_offset) < file_count; i++) {
            int index = i + scroll_offset;
            int tx = x0 + 20;
            int ty = y0 + TITLE_HEIGHT + i * LINE_HEIGHT;

            SDL_Color textColor;
            SDL_Rect bgRect = {tx - 5, ty - 2, 200, LINE_HEIGHT};

            if (index == selected) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                SDL_RenderFillRect(renderer, &bgRect);
                textColor = (SDL_Color){0, 0, 0, 255};
            } else {
                textColor = (SDL_Color){255, 255, 0, 255};
            }

            draw_text(renderer, files[index], tx, ty, textColor);
        }
    }

    SDL_RenderPresent(renderer);
}

int osd_open(SDL_Event event)
{
    // ok opened
    fprintf(stderr, "OSD OPEN\n");

    SDL_GetWindowSize(sdl_win, &SCREEN_W, &SCREEN_H);
    BOX_W = (SCREEN_W / 4) * 3;
    BOX_H = (SCREEN_H / 4) * 3;

    max_visible = (BOX_H - TITLE_HEIGHT) / LINE_HEIGHT;

    // Carica font bitmap (font.bmp 128x128, 16x16 caratteri, 8x8 ciascuno)
    SDL_Surface *font_surface = SDL_LoadBMP("font.bmp");
    if (!font_surface) {
        printf("Errore caricamento font.bmp: %s\n", SDL_GetError());
        return 1;
    }
    // Imposta trasparenza sul nero puro
    SDL_SetColorKey(font_surface, SDL_TRUE, SDL_MapRGB(font_surface->format, 0, 0, 0));
    font_texture = SDL_CreateTextureFromSurface(sdl_render, font_surface);
    SDL_FreeSurface(font_surface);

    return 1;
}

int osd_close(SDL_Event event)
{
    // ok closed
    fprintf(stderr, "OSD CLOSE\n");

    SDL_DestroyTexture(font_texture);

    return 1;
}


int osd_handle(SDL_Event event)
{
    fprintf(stderr, "OSD HANDLE\n");

    if (state == STATE_MENU) {
        draw_menu(sdl_render, selected);
    } else if (state == STATE_FILESELECT) {
        draw_file_selector(sdl_render, files, file_count, file_selected, scroll_offset, max_visible);
    }

    if (event.type == SDL_KEYUP)
    {
        if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
        {
            // Close the OSD
            fprintf(stderr, "OSD HANDLE: escape\n");
            return 0;
        }
    }

    if (event.type == SDL_KEYDOWN) {
        if (state == STATE_MENU) {
            switch (event.key.keysym.sym) {
                case SDLK_UP:
                    selected = (selected - 1 + MENU_ITEMS) % MENU_ITEMS;
                    break;
                case SDLK_DOWN:
                    selected = (selected + 1) % MENU_ITEMS;
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (selected == MENU_ITEMS - 1) {
                        return 0; // Esci
                    }
                    else if (strcmp(menu_items[selected], "Carica partita") == 0)
                    {
                        file_count = load_iso_files(files, 100);
                        file_selected = 0;
                        scroll_offset = 0;
                        state = STATE_FILESELECT;
                    }
                    else
                    {
                        switch (selected)
                        {
                            case 0 : // "fddload - Load floppy disk image",
                                break;
                            case 1 : // "cdload - Load CD-ROM image",
                                break;
                            case 2 : // "rdiskload - Load removable disk image",
                                break;
                            case 3 : // "cartload - Load cartridge image",
                                break;
                            case 4 : // "moload - Load MO image",
                                break;
                            case 5 : // "fddeject - eject disk from floppy drive",
                                break;
                            case 6 : // "cdeject - eject disc from CD-ROM drive",
                                break;
                            case 7 : // "rdiskeject - eject removable disk",
                                break;
                            case 8 : // "carteject - eject cartridge",
                                break;
                            case 9 : // "moeject - eject image from MO drive",
                                break;
                            case 10 : // "hardreset - hard reset the emulated system",
                                break;
                            case 11 : // "pause - pause the the emulated system",
                                break;
                            case 12 : // "fullscreen - toggle fullscreen",
                                break;
                            case 13 : // "version - print version and license information",
                                break;
                            case 14 : // "exit - exit 86Box",
                                break;
                            case 15 : // "close OSD"
                                break;
                        }

                        printf("Hai scelto: %s\n", menu_items[selected]);
                    }
                    break;
            }
        } else if (state == STATE_FILESELECT) {
            if (file_count == 0) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    state = STATE_MENU;
                }
            } else {
                switch (event.key.keysym.sym) {
                    case SDLK_UP:
                        if (file_selected > 0) {
                            file_selected--;
                            if (file_selected < scroll_offset) {
                                scroll_offset--;
                            }
                        }
                        break;
                    case SDLK_DOWN:
                        if (file_selected < file_count - 1) {
                            file_selected++;
                            if (file_selected >= scroll_offset + max_visible) {
                                scroll_offset++;
                            }
                        }
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        strncpy(selected_file, files[file_selected], 255);
                        selected_file[255] = '\0';
                        printf("Selezionato file: %s\n", selected_file);
                        state = STATE_MENU;
                        break;
                    case SDLK_ESCAPE:
                        state = STATE_MENU;
                        break;
                }
            }
        }
    }

    // Keep it open
    return 1;
}
