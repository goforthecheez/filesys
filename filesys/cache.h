#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define BUFFER_CACHE_SIZE 64

/* Representation of a single disk block sector in the buffer cache. */
struct cache_entry
  {
    bool valid;                        /* Whether contents are trustworthy. */
    block_sector_t sector;             /* Disk block sector. */
    uint8_t data[BLOCK_SECTOR_SIZE];   /* Contents of the disk block sector. */
    bool dirty;                        /* Whether there are pending writes. */
    bool accessed;                     /* whether recently accessed. */
    struct lock lock;                  /* Lock on the cache entry. */
    int users;                         /* Number of readers & writers. */
  };

/* Buffer cache keeps track of recently used disk block sectors. */
struct buffer_cache
  {
    struct cache_entry cache[BUFFER_CACHE_SIZE];    /* Buffer cache entries. */
    int hand;                                       /* Clock hand; indexes
                                                       cache. */
    struct lock lock;                               /* Buffer cache lock. */
  };

struct buffer_cache buffer_cache;       /* (Global) buffer cache. */

void cache_init (void);
int cache_lookup (block_sector_t);
void cache_operation_done (int);

void cache_flush (void);

#endif /* filesys/cache.h */
