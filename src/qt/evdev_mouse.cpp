/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Linux/FreeBSD libevdev mouse input module.
 *
 *
 *
 * Authors:	Cacodemon345
 *
 *		Copyright 2021-2022 Cacodemon345
 */
#include "evdev_mouse.hpp"
#include <libevdev/libevdev.h>
#include <unistd.h>
#include <fcntl.h>

#include <vector>
#include <atomic>
#include <string>
#include <tuple>

#include <QThread>

extern "C"
{
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/mouse.h>
}

static std::vector<std::pair<int, libevdev*>> evdev_mice;
static std::atomic<bool> stopped = false;
static QThread* evdev_thread;

static std::atomic<int> evdev_mouse_rel_x = 0, evdev_mouse_rel_y = 0;

void evdev_mouse_poll()
{
    if (!evdev_mice.size() || !mouse_capture)
    {
        evdev_mouse_rel_x = 0;
        evdev_mouse_rel_y = 0;
        return;
    }
    mouse_x = evdev_mouse_rel_x;
    mouse_y = evdev_mouse_rel_y;
    evdev_mouse_rel_x = evdev_mouse_rel_y = 0;
}

void evdev_thread_func()
{
    while (!stopped)
    {
        for (int i = 0; i < evdev_mice.size(); i++)
        {
            struct input_event ev;
            int rc = libevdev_next_event(evdev_mice[i].second, LIBEVDEV_READ_FLAG_NORMAL, &ev);
            if (rc == 0 && ev.type == EV_REL && mouse_capture)
            {
                if (ev.code == REL_X) evdev_mouse_rel_x += ev.value;
                if (ev.code == REL_Y) evdev_mouse_rel_y += ev.value;
            }
        }
    }
    for (int i = 0; i < evdev_mice.size(); i++)
    {
        libevdev_free(evdev_mice[i].second);
        close(evdev_mice[i].first);
    }
    evdev_mice.clear();
}

void evdev_stop()
{
    stopped = true;
    evdev_thread->wait();
}

void evdev_init()
{
    for (int i = 0; i < 256; i++)
    {
        std::string evdev_device_path = "/dev/input/event" + std::to_string(i);
        int fd = open(evdev_device_path.c_str(), O_NONBLOCK | O_RDONLY);
        if (fd != -1)
        {
            libevdev* input_struct = nullptr;
            int rc = libevdev_new_from_fd(fd, &input_struct);
            if (rc <= -1)
            {
                close(fd);
                continue;
            }
            else
            {
                if (!libevdev_has_event_type(input_struct, EV_REL) || !libevdev_has_event_code(input_struct, EV_KEY, BTN_LEFT))
                {
                    libevdev_free(input_struct);
                    close(fd);
                    continue;
                }
                evdev_mice.push_back(std::make_pair(fd, input_struct));
            }
        }
        else if (errno == ENOENT) break;
    }
    if (evdev_mice.size() != 0)
    {
        evdev_thread = QThread::create(evdev_thread_func);
        evdev_thread->start();
        atexit(evdev_stop);
    }
}
