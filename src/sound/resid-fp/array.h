/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 *  Copyright (C) 2011-2014 Leandro Nini
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef ARRAY_H
#define ARRAY_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <atomic>

/**
 * Counter.
 */
class counter
{
private:
    std::atomic<unsigned int> c;

public:
    counter() : c(1) {}
    void increase() { ++c; }
    unsigned int decrease() { return --c; }
};

/**
 * Reference counted pointer to matrix wrapper, for use with standard containers.
 */
template<typename T>
class matrix
{
private:
    T* data;
    counter* count;
    const unsigned int x, y;

public:
    matrix(unsigned int x, unsigned int y) :
        data(new T[x * y]),
        count(new counter()),
        x(x),
        y(y) {}

    matrix(const matrix& p) :
        data(p.data),
        count(p.count),
        x(p.x),
        y(p.y) { count->increase(); }

    ~matrix() { if (count->decrease() == 0) { delete count; delete [] data; } }

    unsigned int length() const { return x * y; }

    T* operator[](unsigned int a) { return &data[a * y]; }

    T const* operator[](unsigned int a) const { return &data[a * y]; }
};

using matrix_t = matrix<short>;

#endif
