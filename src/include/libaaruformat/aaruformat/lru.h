//
// Created by claunia on 2/10/22.
//

#ifndef LIBAARUFORMAT_LRU_H
#define LIBAARUFORMAT_LRU_H

#include <stdint.h>
#include <uthash.h>

/** \struct CacheEntry
 *  \brief Single hash entry in the in-memory cache.
 *
 *  This structure is managed by uthash and represents one key/value association
 *  tracked by the cache. Keys are native 64-bit integers, hashed directly by
 *  uthash without string conversion. Callers do not allocate or free individual
 *  entries directly; use the cache API helpers.
 *
 *  Lifetime & ownership:
 *   - value is an opaque pointer supplied by caller; the cache does not take
 *     ownership unless a free_func is registered on the CacheHeader.
 */
struct CacheEntry
{
    uint64_t       key;    ///< 64-bit integer key (unique within the cache).
    void          *value;  ///< Opaque value pointer associated with key.
    UT_hash_handle hh;     ///< uthash handle (must remain per uthash docs).
};

/** \struct CacheHeader
 *  \brief Cache top-level descriptor encapsulating the hash table root and capacity limit.
 *
 *  The cache enforces an upper bound (max_items) on the number of tracked entries. On insert,
 *  the oldest entry is evicted when the limit is reached (LRU via uthash insertion order).
 *
 *  Fields:
 *   - max_items: Maximum number of entries allowed; 0 means unlimited.
 *   - cache:     uthash root pointer; NULL when the cache is empty.
 *   - free_func: Optional callback to free cached values on eviction/clear.
 */
struct CacheHeader
{
    uint64_t max_items;         ///< Hard limit for number of entries.
    struct CacheEntry *cache;   ///< Hash root (uthash). NULL when empty.
    void (*free_func)(void *);  ///< Optional callback to free cached values. NULL if not needed.
};

void *find_in_cache_uint64(struct CacheHeader *cache, uint64_t key);
void  add_to_cache_uint64(struct CacheHeader *cache, uint64_t key, void *value);
void  free_cache(struct CacheHeader *cache);

#endif  // LIBAARUFORMAT_LRU_H
