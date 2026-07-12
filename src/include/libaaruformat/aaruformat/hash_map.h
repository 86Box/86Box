/*
 * This file is part of the Aaru Data Preservation Suite.
 * Copyright (c) 2019-2026 Natalia Portillo.
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBAARUFORMAT_HASH_MAP_H
#define LIBAARUFORMAT_HASH_MAP_H

#include <stdbool.h>
#include <stdlib.h>

/** \struct kv_pair_t
 *  \brief Single key/value slot used internally by the open-addressing hash map.
 *
 *  Collision resolution strategy (implementation detail): linear or quadratic probing (see source). An empty
 *  slot is typically represented by a key sentinel (e.g. 0 or another reserved value) – callers never interact
 *  with individual kv_pair_t entries directly; they are managed through the map API.
 */
typedef struct
{
    uint64_t key;    ///< Stored key (64-bit). May use a reserved sentinel to denote an empty slot.
    uint64_t value;  ///< Associated value payload (64-bit) stored alongside the key.
} kv_pair_t;

/** \struct hash_map_t
 *  \brief Minimal open-addressing hash map for 64-bit key/value pairs used in deduplication lookup.
 *
 *  Fields:
 *   - table: Pointer to contiguous array of kv_pair_t entries (capacity == size).
 *   - size:  Total number of slots allocated in table (must be >= 1).
 *   - count: Number of occupied (non-empty) slots currently in use.
 *
 *  Load factor guidance: insert performance degrades as count approaches size; callers may rebuild with a larger
 *  size when (count * 10 / size) exceeds a chosen threshold (e.g. 70 – 80%). No automatic resizing is performed.
 */
typedef struct
{
    kv_pair_t *table;  ///< Array of key/value slots of length == size.
    size_t     size;   ///< Allocated slot capacity of table.
    size_t     count;  ///< Number of active (filled) entries.
} hash_map_t;

hash_map_t *create_map(size_t size);
void        free_map(hash_map_t *map);
bool        insert_map(hash_map_t *map, uint64_t key, uint64_t value);
bool        lookup_map(const hash_map_t *map, uint64_t key, uint64_t *out_value);

#endif  // LIBAARUFORMAT_HASH_MAP_H
