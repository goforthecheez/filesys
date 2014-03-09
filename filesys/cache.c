#include "filesys/cache.h"

int cache_evict ();
bool cache_read_disk ();
bool cache_has_empty ();
bool cache_clear ();

/* Looks for a disk block sector SECTOR in the buffer cache. Failing that,
   reads the block sector from disk and writes it to the buffer cache. If
   there are no empty buffer cache entries, chooses an entry to evict and
   writes there.

   The file descriptor FD must be provided, in order to identify buffer
   cache blocks that may be cleared on file closure.*/
bool
cache_read (block_sector_t sector, int fd)
{
  
}
