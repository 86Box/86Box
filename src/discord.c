/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Discord integration module.
 *
 *
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2019 David Hrdlička.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define HAVE_STDARG_H
#include "cpu/cpu.h"
#include <86box/86box.h>
#include <86box/discord.h>
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/plat_dynld.h>
#include <discord_game_sdk.h>

#ifdef _WIN32
#    define PATH_DISCORD_DLL "discord_game_sdk.dll"
#elif defined __APPLE__
#    define PATH_DISCORD_DLL "discord_game_sdk.dylib"
#else
#    define PATH_DISCORD_DLL "discord_game_sdk.so"
#endif

int discord_loaded = 0;

static void                           *discord_handle     = NULL;
static struct IDiscordCore            *discord_core       = NULL;
static struct IDiscordActivityManager *discord_activities = NULL;

static enum EDiscordResult(DISCORD_API *discord_create)(DiscordVersion version, struct DiscordCreateParams *params, struct IDiscordCore **result);

static dllimp_t discord_imports[] = {
    {"DiscordCreate", &discord_create},
    { NULL,           NULL           }
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
#    define discord_log(fmt, ...)
#endif

void
discord_update_activity(int paused)
{
    struct DiscordActivity activity;
    char                   cpufamily[1024];
    char                  *paren;

    if (discord_activities == NULL)
        return;

    discord_log("discord: discord_update_activity(paused=%d)\n", paused);

    memset(&activity, 0x00, sizeof(activity));

    strncpy(cpufamily, cpu_f->name, sizeof(cpufamily) - 1);
    paren = strchr(cpufamily, '(');
    if (paren)
        *(paren - 1) = '\0';

#pragma GCC diagnostic push
#if defined(__GNUC__) && !defined(__clang__)
#    pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
    if (strlen(vm_name) < 100) {
        snprintf(activity.details, sizeof(activity.details), "Running \"%s\"", vm_name);
        snprintf(activity.state, sizeof(activity.state), "%s (%s/%s)", strchr(machine_getname(), ']') + 2, cpufamily, cpu_s->name);
    } else {
        strncpy(activity.details, strchr(machine_getname(), ']') + 2, sizeof(activity.details) - 1);
        snprintf(activity.state, sizeof(activity.state), "%s/%s", cpufamily, cpu_s->name);
    }
#pragma GCC diagnostic pop

    activity.timestamps.start = time(NULL);

    /* Icon choosing for Discord based on 86Box.rc */

#ifdef RELEASE_BUILD
    /* Icon by OBattler and laciba96 (green for release builds)*/
    strcpy(activity.assets.large_image, "86box-green");
#elif BETA_BUILD
    /* Icon by OBattler and laciba96 (yellow for beta builds done by Jenkins)*/
    strcpy(activity.assets.large_image, "86box-yellow");
#elif ALPHA_BUILD
    /* Icon by OBattler and laciba96 (red for alpha builds done by Jenkins)*/
    strcpy(activity.assets.large_image, "86box-red");
#else
    /* Icon by OBattler and laciba96 (gray for builds of branches and from the git master)*/
    strcpy(activity.assets.large_image, "86box");
#endif

    /* End of icon choosing */

    if (paused) {
        strcpy(activity.assets.small_image, "status-paused");
        strcpy(activity.assets.small_text, "Paused");
    } else {
        strcpy(activity.assets.small_image, "status-running");
        strcpy(activity.assets.small_text, "Running");
    }

    discord_activities->update_activity(discord_activities, &activity, NULL, NULL);
}

int
discord_load(void)
{
    if (discord_handle != NULL)
        return 1;

    // Try to load the DLL
    discord_handle = dynld_module(PATH_DISCORD_DLL, discord_imports);

    if (discord_handle == NULL) {
        discord_log("discord: couldn't load " PATH_DISCORD_DLL "\n");
        discord_close();

        return 0;
    }

    discord_loaded = 1;
    return 1;
}

void
discord_init(void)
{
    enum EDiscordResult        result;
    struct DiscordCreateParams params;

    if (discord_handle == NULL)
        return;

    DiscordCreateParamsSetDefault(&params);
    params.client_id = 906956844956782613;
    params.flags     = DiscordCreateFlags_NoRequireDiscord;

    result = discord_create(DISCORD_VERSION, &params, &discord_core);
    if (result != DiscordResult_Ok) {
        discord_log("discord: DiscordCreate returned %d\n", result);
        discord_close();
        return;
    }

    discord_activities = discord_core->get_activity_manager(discord_core);

    return;
}

void
discord_close(void)
{
    if (discord_core != NULL)
        discord_core->destroy(discord_core);

    discord_core       = NULL;
    discord_activities = NULL;
}

void
discord_run_callbacks(void)
{
    if (discord_core == NULL)
        return;

    discord_core->run_callbacks(discord_core);
}
