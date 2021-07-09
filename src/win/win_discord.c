/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Discord integration module.
 *
 *
 *
 * Authors:	David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *		Copyright 2019 David Hrdlička.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <time.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <86box/win_discord.h>
#include <discord_game_sdk.h>

#define PATH_DISCORD_DLL	"discord_game_sdk.dll"

int	discord_loaded = 0;

static void				*discord_handle = NULL;
static struct IDiscordCore		*discord_core = NULL;
static struct IDiscordActivityManager	*discord_activities = NULL;

static enum EDiscordResult		(*discord_create)(DiscordVersion version, struct DiscordCreateParams* params, struct IDiscordCore** result);

static dllimp_t discord_imports[] = {
  { "DiscordCreate",	&discord_create	},
  { NULL,		NULL		}
};

#ifdef ENABLE_DISCORD_LOG
int discord_do_log = 1;


static void
discord_log(const char *fmt, ...)
{
    va_list ap;

    if (discord_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define discord_log(fmt, ...)
#endif

void
discord_update_activity(int paused)
{
    struct DiscordActivity activity;
    char config_name[1024], cpufamily[1024];
    char *paren;

    if(discord_activities == NULL)
	return;

    discord_log("win_discord: discord_update_activity(paused=%d)\n", paused);

    memset(&activity, 0x00, sizeof(activity));

    plat_get_dirname(config_name, usr_path);

    strncpy(cpufamily, cpu_f->name, sizeof(cpufamily) - 1);
    paren = strchr(cpufamily, '(');
    if (paren)
	*(paren - 1) = '\0';

    if (strlen(plat_get_filename(config_name)) < 100)
    {
	sprintf_s(activity.details, sizeof(activity.details), "Running \"%s\"", plat_get_filename(config_name));
	sprintf_s(activity.state, sizeof(activity.state), "%s (%s/%s)", strchr(machine_getname(), ']') + 2, cpufamily, cpu_s->name);
    }
    else
    {
	strncpy(activity.details, strchr(machine_getname(), ']') + 2, sizeof(activity.details) - 1);
	sprintf_s(activity.state, sizeof(activity.state), "%s/%s", cpufamily, cpu_s->name);
    }

    activity.timestamps.start = time(NULL);

#ifdef RELEASE_BUILD
    strcpy(activity.assets.large_image, "86box-rb");
#else
    strcpy(activity.assets.large_image, "86box");
#endif

    if (paused)
    {
	strcpy(activity.assets.small_image, "status-paused");
	strcpy(activity.assets.small_text, "Paused");
    }
    else
    {
	strcpy(activity.assets.small_image, "status-running");
	strcpy(activity.assets.small_text, "Running");
    }

    discord_activities->update_activity(discord_activities, &activity, NULL, NULL);
}

int
discord_load()
{
    if (discord_handle != NULL)
	return(1);

    // Try to load the DLL
    discord_handle = dynld_module(PATH_DISCORD_DLL, discord_imports);

    if (discord_handle == NULL)
    {
	discord_log("win_discord: couldn't load " PATH_DISCORD_DLL "\n");
	discord_close();

	return(0);
    }

    discord_loaded = 1;
    return(1);
}

void
discord_init()
{
    enum EDiscordResult result;
    struct DiscordCreateParams params;

    if(discord_handle == NULL)
	return;

    DiscordCreateParamsSetDefault(&params);
    params.client_id = 651478134352248832;
    params.flags = DiscordCreateFlags_NoRequireDiscord;

    result = discord_create(DISCORD_VERSION, &params, &discord_core);
    if (result != DiscordResult_Ok)
    {
	discord_log("win_discord: DiscordCreate returned %d\n", result);
	discord_close();
	return;
    }

    discord_activities = discord_core->get_activity_manager(discord_core);

    return;
}

void
discord_close()
{
    if (discord_core != NULL)
	discord_core->destroy(discord_core);

    discord_core = NULL;
    discord_activities = NULL;
}

void
discord_run_callbacks()
{
    if(discord_core == NULL)
	return;

    discord_core->run_callbacks(discord_core);
}
