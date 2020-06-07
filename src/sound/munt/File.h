/* Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
 * Copyright (C) 2011-2020 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MT32EMU_FILE_H
#define MT32EMU_FILE_H

#include <cstddef>

#include "globals.h"
#include "Types.h"

namespace MT32Emu {

class MT32EMU_EXPORT File {
public:
	// Includes terminator char.
	typedef char SHA1Digest[41];

	virtual ~File() {}
	virtual size_t getSize() = 0;
	virtual const Bit8u *getData() = 0;
	virtual const SHA1Digest &getSHA1() = 0;

	virtual void close() = 0;
};

class MT32EMU_EXPORT AbstractFile : public File {
public:
	const SHA1Digest &getSHA1();

protected:
	AbstractFile();
	AbstractFile(const SHA1Digest &sha1Digest);

private:
	bool sha1DigestCalculated;
	SHA1Digest sha1Digest;

	// Binary compatibility helper.
	void *reserved;
};

class MT32EMU_EXPORT ArrayFile : public AbstractFile {
public:
	ArrayFile(const Bit8u *data, size_t size);
	ArrayFile(const Bit8u *data, size_t size, const SHA1Digest &sha1Digest);

	size_t getSize();
	const Bit8u *getData();
	void close() {}

private:
	const Bit8u *data;
	size_t size;
};

} // namespace MT32Emu

#endif // #ifndef MT32EMU_FILE_H
