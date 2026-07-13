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

/**
 * @file static_lru_hash_map.h
 * @brief Static-memory hash map with LRU-like eviction for fixed RAM usage.
 *
 * This hash map variant has the following characteristics:
 * - Fixed memory allocation set at creation time (never grows)
 * - Tracks access frequency for each entry
 * - Automatically evicts least-frequently-used entries when approaching capacity
 * - Maintains a minimum percentage of free slots for new insertions
 * - Uses approximate LFU (Least Frequently Used) with aging to prevent stale entries
 *
 * Use this instead of hash_map_t when you need predictable, bounded memory usage
 * and can tolerate eviction of less-frequently-accessed entries.
 */

#ifndef LIBAARUFORMAT_STATIC_LRU_HASH_MAP_H
#define LIBAARUFORMAT_STATIC_LRU_HASH_MAP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * @brief Default configuration constants for the static LRU hash map.
 *
 * These can be overridden by defining them before including this header.
 */
#ifndef STATIC_LRU_EVICTION_LOAD_FACTOR
#define STATIC_LRU_EVICTION_LOAD_FACTOR 0.90  ///< Trigger eviction when 90% full
#endif

#ifndef STATIC_LRU_TARGET_LOAD_FACTOR
#define STATIC_LRU_TARGET_LOAD_FACTOR 0.75  ///< Evict down to 75% capacity
#endif

#ifndef STATIC_LRU_AGING_INTERVAL
#define STATIC_LRU_AGING_INTERVAL 100000  ///< Age access counts every N operations
#endif

#ifndef STATIC_LRU_MIN_SIZE
#define STATIC_LRU_MIN_SIZE 1024  ///< Minimum map size to prevent edge cases
#endif

/** \struct lru_kv_pair_t
 *  \brief Single key/value slot with access tracking for the static LRU hash map.
 *
 *  Extends the basic kv_pair_t with an access counter to enable LFU-based eviction.
 *  The access_count saturates at 255 and is periodically aged (halved) to prevent
 *  stale "hot" entries from permanently occupying the cache.
 *
 *  An empty slot is represented by key == 0. This means 0 cannot be used as a valid key.
 */
typedef struct
{
    uint64_t key;           ///< Stored key (64-bit). 0 indicates an empty slot.
    uint64_t value;         ///< Associated value payload (64-bit).
    uint8_t  access_count;  ///< Access frequency counter (0-255, saturates at 255).
    uint8_t  _padding[7];   ///< Padding for 8-byte alignment (24 bytes total per entry).
} lru_kv_pair_t;

/** \struct static_lru_hash_map_t
 *  \brief Fixed-size hash map with LRU-like eviction for bounded memory usage.
 *
 *  This map allocates a fixed amount of memory at creation and never grows.
 *  When the map approaches capacity (exceeds eviction threshold), it automatically
 *  evicts the least-frequently-accessed entries to make room for new insertions.
 *
 *  Fields:
 *   - table:        Pointer to contiguous array of lru_kv_pair_t entries.
 *   - size:         Total number of slots allocated (FIXED, never changes).
 *   - count:        Number of occupied (non-empty) slots currently in use.
 *   - max_count:    Eviction is triggered when count exceeds this value.
 *   - target_count: After eviction, entries are removed until count <= this value.
 *   - age_counter:  Operations counter for periodic aging of access counts.
 *
 *  Memory usage: sizeof(static_lru_hash_map_t) + (size * sizeof(lru_kv_pair_t))
 *  With default 24-byte entries: ~24 bytes per entry + 48 bytes for the struct.
 */
typedef struct
{
    lru_kv_pair_t *table;         ///< Array of key/value slots of length == size.
    size_t         size;          ///< Allocated slot capacity (FIXED at creation).
    size_t         count;         ///< Number of active (filled) entries.
    size_t         max_count;     ///< Eviction trigger threshold (size * EVICTION_LOAD_FACTOR).
    size_t         target_count;  ///< Target count after eviction (size * TARGET_LOAD_FACTOR).
    uint32_t       age_counter;   ///< Operations since last aging.
    uint32_t       _padding;      ///< Padding for alignment.

    // Optional statistics (can be compiled out if not needed)
#ifdef STATIC_LRU_ENABLE_STATS
    uint64_t total_lookups;    ///< Total number of lookup operations.
    uint64_t cache_hits;       ///< Number of successful lookups.
    uint64_t total_inserts;    ///< Total number of insert operations.
    uint64_t eviction_count;   ///< Number of entries evicted.
    uint64_t eviction_cycles;  ///< Number of times eviction was triggered.
#endif
} static_lru_hash_map_t;

/**
 * @brief Creates a new static LRU hash map with fixed size.
 *
 * Allocates and initializes a new hash map structure with the given size.
 * This is the ONLY allocation that will ever occur for this map - the size
 * is fixed and will never grow. When the map fills up, old entries are
 * evicted instead of allocating more memory.
 *
 * @param size Fixed size of the hash table. Enforced minimum is STATIC_LRU_MIN_SIZE.
 *             Choose this based on your memory budget and expected working set.
 *
 * @return Returns a pointer to the newly created hash map, or NULL if allocation fails.
 *
 * @note Memory usage: approximately (size * 24) bytes for the table.
 * @note The caller is responsible for freeing the returned hash map using static_lru_free_map().
 * @note A key value of 0 is reserved to indicate empty slots and cannot be used as a valid key.
 *
 * @see static_lru_free_map()
 */
static_lru_hash_map_t *static_lru_create_map(size_t size);

/**
 * @brief Frees all memory associated with a static LRU hash map.
 *
 * Deallocates the hash table and the hash map structure itself.
 *
 * @param map Pointer to the hash map to free. Can be NULL (no operation performed).
 *
 * @note This function does not free any memory pointed to by the values stored in the map.
 *
 * @see static_lru_create_map()
 */
void static_lru_free_map(static_lru_hash_map_t *map);

/**
 * @brief Inserts a key-value pair into the static LRU hash map.
 *
 * Adds a new key-value pair to the hash map. If the map is approaching capacity
 * (exceeds STATIC_LRU_EVICTION_LOAD_FACTOR), the least-frequently-used entries
 * are automatically evicted before insertion.
 *
 * If the key already exists, the value is updated and the access count is incremented.
 *
 * @param map   Pointer to the hash map. Must not be NULL.
 * @param key   The key to insert. Must not be 0 (reserved for empty slots).
 * @param value The value to associate with the key.
 *
 * @return Returns the result of the insertion operation.
 * @retval true  Successfully inserted a NEW key-value pair.
 * @retval false Key already existed; value was updated instead.
 *
 * @note New entries start with access_count = 1.
 * @note Updating an existing entry increments its access_count (up to 255).
 * @note Time complexity: O(1) average, O(n) when eviction is triggered.
 *
 * @warning Using 0 as a key value will result in undefined behavior.
 *
 * @see static_lru_lookup_map()
 */
bool static_lru_insert_map(static_lru_hash_map_t *map, uint64_t key, uint64_t value);

/**
 * @brief Looks up a value by key in the static LRU hash map.
 *
 * Searches for the specified key and retrieves its associated value.
 * Unlike the regular hash_map, this function DOES modify the map by
 * incrementing the access_count of the found entry (to track usage frequency).
 *
 * @param map       Pointer to the hash map to search. Must not be NULL.
 * @param key       The key to search for. Must not be 0.
 * @param out_value Pointer to store the found value. Must not be NULL.
 *                  Only modified if the key is found.
 *
 * @return Returns whether the key was found in the map.
 * @retval true  Key found. The associated value is written to *out_value.
 * @retval false Key not found. *out_value is not modified.
 *
 * @note This function increments access_count on successful lookups.
 * @note Time complexity: O(1) average case.
 *
 * @see static_lru_insert_map()
 */
bool static_lru_lookup_map(static_lru_hash_map_t *map, uint64_t key, uint64_t *out_value);

/**
 * @brief Checks if a key exists in the map WITHOUT updating access count.
 *
 * This is useful when you need to check existence without affecting eviction priority.
 *
 * @param map Pointer to the hash map to search. Must not be NULL.
 * @param key The key to search for. Must not be 0.
 *
 * @return Returns whether the key exists in the map.
 * @retval true  Key exists in the map.
 * @retval false Key not found.
 *
 * @note Unlike static_lru_lookup_map(), this does NOT update access_count.
 */
bool static_lru_contains_key(const static_lru_hash_map_t *map, uint64_t key);

/**
 * @brief Manually triggers eviction of least-used entries.
 *
 * Forces an eviction cycle even if the map hasn't reached the eviction threshold.
 * Useful for proactively freeing memory or resetting the cache.
 *
 * @param map             Pointer to the hash map. Must not be NULL.
 * @param entries_to_keep Number of entries to keep after eviction. If 0, uses target_count.
 *
 * @return Number of entries actually evicted.
 */
size_t static_lru_evict(static_lru_hash_map_t *map, size_t entries_to_keep);

/**
 * @brief Manually ages all access counts.
 *
 * Halves all access counts in the map. This is normally done automatically
 * every STATIC_LRU_AGING_INTERVAL operations, but can be called manually
 * to reset frequency tracking.
 *
 * @param map Pointer to the hash map. Must not be NULL.
 */
void static_lru_age_counts(static_lru_hash_map_t *map);

/**
 * @brief Returns the current load factor of the map.
 *
 * @param map Pointer to the hash map. Must not be NULL.
 *
 * @return Current load factor as a value between 0.0 and 1.0.
 */
double static_lru_load_factor(const static_lru_hash_map_t *map);

/**
 * @brief Returns the number of free slots available.
 *
 * @param map Pointer to the hash map. Must not be NULL.
 *
 * @return Number of empty slots (size - count).
 */
size_t static_lru_free_slots(const static_lru_hash_map_t *map);

#ifdef STATIC_LRU_ENABLE_STATS
/**
 * @brief Returns the cache hit rate.
 *
 * @param map Pointer to the hash map. Must not be NULL.
 *
 * @return Hit rate as a value between 0.0 and 1.0, or 0.0 if no lookups performed.
 */
double static_lru_hit_rate(const static_lru_hash_map_t *map);

/**
 * @brief Resets all statistics counters to zero.
 *
 * @param map Pointer to the hash map. Must not be NULL.
 */
void static_lru_reset_stats(static_lru_hash_map_t *map);
#endif

#endif  // LIBAARUFORMAT_STATIC_LRU_HASH_MAP_H
