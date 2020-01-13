/************************************************************************

    PCEM: IBM 5150 cassette support

    Copyright (C) 2019  John Elliott <seasip.webmaster@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*************************************************************************/

extern wchar_t cassettefn[256];

extern const device_t cassette_device;

uint8_t cassette_input(void);
void cassette_set_motor(uint8_t on);
void cassette_eject(void);
void cassette_load(wchar_t *filename);
