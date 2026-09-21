/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2013 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 * Copyright (C) 2004  Dag Lem <resid@nimrod.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef POTENTIOMETER_H
#define POTENTIOMETER_H

namespace reSIDfp
{

/**
 * Potentiometer representation.
 *
 * This class will probably never be implemented in any real way.
 *
 * @author Ken Händel
 * @author Dag Lem
 */
class Potentiometer
{
public:
    /**
     * Read paddle value. Not modeled.
     *
     * @return paddle value (always 0xff)
     */
    unsigned char readPOT() const { return 0xff; }
};

} // namespace reSIDfp

#endif
