/*
 * Generic FIFO component, implemented as a circular buffer.
 *
 * Copyright (c) 2012 Peter A. G. Crosthwaite
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <86box/86box.h>
#include <86box/fifo8.h>

void fifo8_create(Fifo8 *fifo, uint32_t capacity)
{
    fifo->data = (uint8_t *)malloc(capacity);
	memset(fifo->data, 0, capacity);
    fifo->capacity = capacity;
    fifo->head = 0;
    fifo->num = 0;
}

void fifo8_destroy(Fifo8 *fifo)
{
    if (fifo->data) {
		free(fifo->data);
		fifo->data = NULL;
	}
}

void fifo8_push(Fifo8 *fifo, uint8_t data)
{
    assert(fifo->num < fifo->capacity);
    fifo->data[(fifo->head + fifo->num) % fifo->capacity] = data;
    fifo->num++;
}

void fifo8_push_all(Fifo8 *fifo, const uint8_t *data, uint32_t num)
{
    uint32_t start, avail;

    assert(fifo->num + num <= fifo->capacity);

    start = (fifo->head + fifo->num) % fifo->capacity;

    if (start + num <= fifo->capacity) {
        memcpy(&fifo->data[start], data, num);
    } else {
        avail = fifo->capacity - start;
        memcpy(&fifo->data[start], data, avail);
        memcpy(&fifo->data[0], &data[avail], num - avail);
    }

    fifo->num += num;
}

uint8_t fifo8_pop(Fifo8 *fifo)
{
    uint8_t ret;

    assert(fifo->num > 0);
    ret = fifo->data[fifo->head++];
    fifo->head %= fifo->capacity;
    fifo->num--;
    return ret;
}

const uint8_t *fifo8_pop_buf(Fifo8 *fifo, uint32_t max, uint32_t *num)
{
    uint8_t *ret;

    assert(max > 0 && max <= fifo->num);
    *num = MIN(fifo->capacity - fifo->head, max);
	ret = &fifo->data[fifo->head];
    fifo->head += *num;
    fifo->head %= fifo->capacity;
    fifo->num -= *num;
    return ret;
}

void fifo8_reset(Fifo8 *fifo)
{
    fifo->num = 0;
    fifo->head = 0;
}

int fifo8_is_empty(Fifo8 *fifo)
{
    return (fifo->num == 0);
}

int fifo8_is_full(Fifo8 *fifo)
{
    return (fifo->num == fifo->capacity);
}

uint32_t fifo8_num_free(Fifo8 *fifo)
{
    return fifo->capacity - fifo->num;
}

uint32_t fifo8_num_used(Fifo8 *fifo)
{
    return fifo->num;
}
