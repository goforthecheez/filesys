#include "filesys/cache.h"
#include <stdio.h>
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "userprog/syscall.h"

int cache_has_empty (void);
int cache_evict (void);
void cache_read_disk (block_sector_t, int);
void increment_users (int);
void decrement_users (int);
void cache_clear (int);

/* Initialize the buffer cache. */
void
cache_init ()
{
  lock_init (&buffer_cache.lock);

  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
      buffer_cache.cache[i].valid = false;
      lock_init (&buffer_cache.cache[i].lock);
      buffer_cache.cache[i].users = 0;
    }
}

/* Return the index to disk block sector SECTOR in the buffer cache.
   If SECTOR is not already in the buffer cache, reads the block sector
   from disk and writes it to the buffer cache. If there are no empty 
   buffer cache entries, chooses an entry to evict and writes there.

   Caller must call cache_operation_done() when it is done with SECTOR. */
int
cache_lookup (block_sector_t sector)
{
  lock_acquire (&buffer_cache.lock);
  /* Look for SECTOR in buffer cache. */
  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    {
      if (buffer_cache.cache[i].valid
          && buffer_cache.cache[i].sector == sector)
        {
          increment_users (i);
          lock_release (&buffer_cache.lock);
          return i;
        }
    }

  /* Failing that, we need to read a sector from disk. */
  i = cache_has_empty();
  if (i == -1)
    i = cache_evict ();

  cache_read_disk (sector, i);
  lock_release (&buffer_cache.lock);

  return i;
}

/* Returns index of first empty cache entry, else -1. This operation should
   probably be called while holding the buffer cache lock.*/
int
cache_has_empty ()
{
  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    if (buffer_cache.cache[i].valid == false)
      return i;

  return -1;
}

/* Evict an entry from the buffer cache, returning its index. This operation
   can only be called while holding the buffer cache lock.

   This is a modification to the clock algorithm. A single hand sweeps once,
   marking all sectors with no users as accessed. If after two sweeps through
   the cache, no eviction candidates are found, sleep and try again. */
int
cache_evict ()
{
  int start = buffer_cache.hand;
  bool first = true;

  while (buffer_cache.cache[buffer_cache.hand].accessed && first == true)
    {
      /* Skip the entry or mark it as not accessed. */
      if (buffer_cache.cache[buffer_cache.hand].users > 0)
        continue;
      buffer_cache.cache[buffer_cache.hand].accessed = false;

      /* Increment clock hand. */
      if (buffer_cache.hand == BUFFER_CACHE_SIZE - 1)
        buffer_cache.hand = 0;
      else
        buffer_cache.hand++;

      if (buffer_cache.hand == start)
        {
          if (first == true)
            first = false;
          else
            {
              first = true;
              lock_release (&buffer_cache.lock);
              //timer_sleep ();
              lock_acquire (&buffer_cache.lock);
            }
        }
    }

  cache_clear (buffer_cache.hand);
  return buffer_cache.hand;
}

/* Reads SECTOR from disk into the INDEX'th entry of the buffer cache.
   This function should be called while holding the buffer cache lock. */
void
cache_read_disk (block_sector_t sector, int index)
{
  buffer_cache.cache[index].sector = sector;
  block_read (fs_device, sector, buffer_cache.cache[index].data);
  buffer_cache.cache[index].dirty = false;
  buffer_cache.cache[index].accessed = true;
  buffer_cache.cache[index].users = 1;
  buffer_cache.cache[index].valid = true;
}

/* Flush the entire contents of the buffer cache back to disk. */
void
cache_flush ()
{
  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    cache_clear (i);
}

/* When the calling function is done reading or writing to the cache, it must
   call this function to signal that hte operation is done. */
void
cache_operation_done (int index)
{
  decrement_users (index);
}

/* Safely increment the indexed cache block entry's user count. */
void
increment_users (int index)
{
  lock_acquire (&buffer_cache.cache[index].lock);
  buffer_cache.cache[index].users++;
  lock_release (&buffer_cache.cache[index].lock);
}

/* Safely decrements the indexed cache block entry's user cound. */
void
decrement_users (int index)
{
  lock_acquire (&buffer_cache.cache[index].lock);
  buffer_cache.cache[index].users--;
  lock_release (&buffer_cache.cache[index].lock);
}

void
cache_clear (int index)
{
  buffer_cache.cache[index].valid = false;
  //  if (buffer_cache.cache[index].dirty == true)
    block_write (fs_device, buffer_cache.cache[index].sector,
                 buffer_cache.cache[index].data);
}
