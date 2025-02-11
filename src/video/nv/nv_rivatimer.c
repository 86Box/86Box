/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Fast, high-frequency, CPU-independent timer.
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *
 *          Copyright 2024-2025 starfrost
 */

/* See vid_nv_rivatimer.h comments for rationale behind not using the regular timer system 

Notes applicable to this file:
Since Windows XP, QueryPerformanceCounter and QueryPerformanceFrequency cannot fail so they are not checked.

*/

#include <86box/nv/vid_nv_rivatimer.h>

#ifdef _WIN32
LARGE_INTEGER performance_frequency;
#endif

rivatimer_t* rivatimer_head;        // The head of the rivatimer list. 
rivatimer_t* rivatimer_tail;        // The tail of the rivatimer list.

/* Functions only used in this translation unit */
bool rivatimer_really_exists(rivatimer_t* rivatimer);   // Determine if a rivatimer really exists in the linked list.

void rivatimer_init(void)
{
    // Destroy all the rivatimers.
    rivatimer_t* rivatimer_ptr = rivatimer_head;

    if (!rivatimer_ptr)
        return;

    while (rivatimer_ptr)
    {
        // since we are destroing it
        rivatimer_t* old_next = rivatimer_ptr->next;
        rivatimer_destroy(rivatimer_ptr);
        
        rivatimer_ptr = old_next;
    }
        

    #ifdef _WIN32
    // Query the performance frequency.
    QueryPerformanceFrequency(&performance_frequency);
    #endif
}

// Creates a rivatimer.
rivatimer_t* rivatimer_create(double period, void (*callback)(double real_time))
{
    rivatimer_t* new_rivatimer = NULL;

    // See i
    if (period <= 0 
    || !callback)
    {
        fatal("Invalid rivatimer_create call: period <= 0 or no callback");
    }

    // If there are no rivatimers, create one
    if (!rivatimer_head)
    {
        rivatimer_head = calloc(1, sizeof(rivatimer_t));
        rivatimer_head->prev = NULL; // indicate this is the first in the list even if we don't strictly need to
        rivatimer_tail = rivatimer_head; 
        new_rivatimer = rivatimer_head;
    }
    else // Otherwise add a new one to the list
    {
        rivatimer_tail->next = calloc(1, sizeof(rivatimer_t));
        rivatimer_tail = rivatimer_tail->next;
        new_rivatimer = rivatimer_tail;
    }
    
    // sanity check
    if (new_rivatimer)
    {
        new_rivatimer->running = false;
        new_rivatimer->period = period;
        new_rivatimer->next = NULL; // indicate this is the last in the list
        new_rivatimer->callback = callback;
    }

    return new_rivatimer;
}

// Determines if a rivatimer really exists.
bool rivatimer_really_exists(rivatimer_t* rivatimer)
{
    rivatimer_t* current = rivatimer_head;

    if (!current)
        return false;

    while (current)
    {
        if (current == rivatimer)
            return true;

        current = current->next;
    }

    return false;
}

// Destroy a rivatimer.
void rivatimer_destroy(rivatimer_t* rivatimer_ptr)
{
    if (!rivatimer_really_exists(rivatimer_ptr))
        fatal("rivatimer_destroy: The timer was already destroyed, or never existed in the first place.");
    
    // Case: We are destroying the head
    if (rivatimer_ptr == rivatimer_head)
    {
        // This is the only rivatimer
        if (rivatimer_ptr->next == NULL)
        {
            rivatimer_head = NULL;
            rivatimer_tail = NULL;
        }
        // This is not the only rivatimer
        else 
        {
            rivatimer_head = rivatimer_ptr->next;
            rivatimer_head->prev = NULL; 
            // This is the only rivatimer and now there is only one
            if (!rivatimer_head->next)
                rivatimer_tail = rivatimer_head;
        }
    }
    // Case: We are destroying the tail
    else if (rivatimer_ptr == rivatimer_tail)
    {
        // We already covered the case where there is only one item above
        rivatimer_tail = rivatimer_ptr->prev;
        rivatimer_tail->next = NULL;
    }
    // Case: This is not the first or last rivatimer, so we don't need to set the head or tail
    else
    {
        // Fix the break in the chain that this 
        if (rivatimer_ptr->next)
            rivatimer_ptr->prev->next = rivatimer_ptr->next;
        if (rivatimer_ptr->prev)
            rivatimer_ptr->next->prev = rivatimer_ptr->prev;
    }

    free(rivatimer_ptr);
    rivatimer_ptr = NULL; //explicitly set to null
}

void rivatimer_update_all(void)
{
    rivatimer_t* rivatimer_ptr = rivatimer_head;

    if (!rivatimer_ptr)
        return;

    while (rivatimer_ptr)
    {
        // if it's not running skip it
        if (!rivatimer_ptr->running)
        {
            rivatimer_ptr = rivatimer_ptr->next;
            continue;
        }

        #ifdef _WIN32
            LARGE_INTEGER current_time;

            QueryPerformanceCounter(&current_time);

            double microseconds = ((double)current_time.QuadPart / 1000000.0) - (rivatimer_ptr->starting_time.QuadPart / 1000000.0);
        #else
            struct timespec current_time; 

            clock_gettime(CLOCK_REALTIME, &current_time);

            double microseconds = ((double)current_time.tv_sec * 1000000.0) + ((double)current_time.tv_nsec / 1000.0);
        #endif

        rivatimer_ptr->time += microseconds; 

        // Reset the current time so we can actually restart
        #ifdef _WIN32
            QueryPerformanceCounter(&rivatimer_ptr->starting_time);
        #else
            clock_gettime(CLOCK_REALTIME, &rivatimer_ptr->starting_time);
        #endif 

        // Time to fire
        if (microseconds > rivatimer_ptr->period)
        {
            if (!rivatimer_ptr->callback)
            {
                pclog("Eh? No callback in RivaTimer?");
                continue;
            }
                    
            rivatimer_ptr->callback(microseconds);
        }

        rivatimer_ptr = rivatimer_ptr->next;
    }

}

void rivatimer_start(rivatimer_t* rivatimer_ptr)
{
    if (!rivatimer_really_exists(rivatimer_ptr))
        fatal("rivatimer_start: The timer has been destroyed, or never existed in the first place.");

    if (rivatimer_ptr->period <= 0)
        fatal("rivatimer_start: Zero period!");

    rivatimer_ptr->running = true;

    // Start off so rivatimer_update_all can actually update.
    #ifdef _WIN32
        QueryPerformanceCounter(&rivatimer_ptr->starting_time);
    #else
        clock_gettime(CLOCK_REALTIME, &rivatimer_ptr->starting_time);
    #endif 
}

void rivatimer_stop(rivatimer_t* rivatimer_ptr)
{
    if (!rivatimer_really_exists(rivatimer_ptr))
        fatal("rivatimer_stop: The timer has been destroyed, or never existed in the first place.");

    rivatimer_ptr->running = false;
    rivatimer_ptr->time = 0;
}

// Get the current time value of a rivatimer
double rivatimer_get_time(rivatimer_t* rivatimer_ptr)
{
    if (!rivatimer_really_exists(rivatimer_ptr))
        fatal("rivatimer_get_time: The timer has been destroyed, or never existed in the first place.");

    return rivatimer_ptr->time;
}

void rivatimer_set_callback(rivatimer_t* rivatimer_ptr, void (*callback)(double real_time))
{
    if (!rivatimer_really_exists(rivatimer_ptr))
        fatal("rivatimer_set_callback: The timer has been destroyed, or never existed in the first place.");

    if (!callback)
        fatal("rivatimer_set_callback: No callback!");

    rivatimer_ptr->callback = callback;
}

void rivatimer_set_period(rivatimer_t* rivatimer_ptr, double period)
{
    if (!rivatimer_really_exists(rivatimer_ptr))
       fatal("rivatimer_set_period: The timer has been destroyed, or never existed in the first place.");

    rivatimer_ptr->period = period;
}