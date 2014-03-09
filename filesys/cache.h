#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/block.h"

/* Buffer cache keeps track of recently used disk block sectors. */
struct buffer_cache
  {
    cache_entry cache[64];    /* Buffer cache entries. */
    int hand;                 /* Clock hand; indexes cache. */
  };

/* Representation of a single disk block sector in the buffer cache. */
struct cache_entry
  {
    bool valid;                        /* Whether contents are trustworthy. */
    block_sector_t sector;             /* Disk block sector. */
    int fd;                            /* File descriptor.
                                          (For use on file closure.) */
    uint8_t data[BLOCK_SECTOR_SIZE];   /* Contents of the disk block sector. */
    bool dirty;                        /* Whether there are pending writes. */
    bool accessed;                     /* whether recently accessed. */
    struct lock lock;                  /* Lock on the cache entry. */
  };

bool cache_read (block_sector_t block, int fd);
bool cache_write ();

bool cache_flush_file ();
bool cache_flush_all ();

#endif /* filesys/cache.h */
