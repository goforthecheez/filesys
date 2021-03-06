#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Global file descriptor counter. This begins counting at 3 because file
   descriptors 0, 1, and 2 are reserved for stdin, stdout, and stderr,
   respectively. */
int fd_counter = 3;

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Number of addresses per block. */
#define ADDRS_PER_BLOCK (BLOCK_SECTOR_SIZE / 4)

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  //  if (pos < inode->data.length)
  //{
      int direct_idx = pos / BLOCK_SECTOR_SIZE;

      if (direct_idx < DIRECT_BLOCKS)
        return inode->data.direct[direct_idx];

      direct_idx -= DIRECT_BLOCKS;
      int indirect_idx = direct_idx / ADDRS_PER_BLOCK;
      int indirect_ofs = direct_idx % ADDRS_PER_BLOCK;
      if (direct_idx < ADDRS_PER_BLOCK * INDIRECT_BLOCKS)
        {
          block_sector_t *sector = malloc (BLOCK_SECTOR_SIZE);
          block_read (fs_device, (block_sector_t) inode->data.indirect[indirect_idx], sector);
          block_sector_t temp = *(sector + indirect_ofs);
          free (sector);
          return temp;
        }
 
      direct_idx -= INDIRECT_BLOCKS * ADDRS_PER_BLOCK;
      int dbl_indirect_idx = direct_idx / ADDRS_PER_BLOCK;
      int dbl_indirect_ofs = direct_idx % ADDRS_PER_BLOCK;
      block_sector_t **dbl_sector = malloc (BLOCK_SECTOR_SIZE);
      block_read (fs_device, (block_sector_t) inode->data.doubly_indirect, dbl_sector);
      block_sector_t *dbl_sector_ofs = malloc (BLOCK_SECTOR_SIZE);
      block_read (fs_device, (block_sector_t) *(dbl_sector + dbl_indirect_idx), dbl_sector_ofs);
      block_sector_t temp = *(dbl_sector_ofs + dbl_indirect_ofs);
      free (dbl_sector);
      free (dbl_sector_ofs);
      return temp;
      //}
      //else
      //return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      if (sectors > 0)
        {
          static char zeros[BLOCK_SECTOR_SIZE];

          /* Direct blocks. */
          unsigned i;
          for (i = 0; i < sectors && i < DIRECT_BLOCKS; i++)
            {
              disk_inode->direct[i] = free_map_allocate_one ();
              block_write (fs_device, disk_inode->direct[i], zeros);
            }

          /* Indirect blocks. */
	  if (i == DIRECT_BLOCKS)
            {
              sectors -= i;
              int j = -1;
              for (i = 0;
                   i < sectors && i < ADDRS_PER_BLOCK * INDIRECT_BLOCKS;
                   i++)
                {
		  int ofs = i % ADDRS_PER_BLOCK;
                  if (ofs == 0)
                    {
		      if (j >= 0)
                        {
                          block_sector_t *temp = disk_inode->indirect[j];
                          disk_inode->indirect[j] = (block_sector_t *) free_map_allocate_one ();
                          block_write (fs_device, (block_sector_t) disk_inode->indirect[j], temp);
                          free (temp);
                        }
                      j++;
                      disk_inode->indirect[j] = (block_sector_t *) calloc (1, BLOCK_SECTOR_SIZE);
                    }
                  *(disk_inode->indirect[j] + ofs) = free_map_allocate_one ();
                  block_write (fs_device, *(disk_inode->indirect[j] + ofs),
                               zeros);
                }
	      block_sector_t *temp = disk_inode->indirect[j];
              disk_inode->indirect[j] = (block_sector_t *) free_map_allocate_one ();
              block_write (fs_device, (block_sector_t) disk_inode->indirect[j], temp);
              free (temp);
	    }

          /* Doubly indirect blocks. */
	  disk_inode->doubly_indirect = (uint32_t ***) calloc (1, BLOCK_SECTOR_SIZE);
	  if (i == ADDRS_PER_BLOCK * INDIRECT_BLOCKS)
            {
              sectors -= i;
              int j;
              int p;
              for (j = 0; j < ADDRS_PER_BLOCK && i < sectors; j++)
                {
                  *(disk_inode->doubly_indirect + j) = (uint32_t **) calloc (1, BLOCK_SECTOR_SIZE);
                  for (p = 0; p < ADDRS_PER_BLOCK && i < sectors; p++)
                    {
  		      *(*(*(disk_inode->doubly_indirect) + j) + p) = free_map_allocate_one ();
                      block_write (fs_device, *(*(*(disk_inode->doubly_indirect) + j) + p), zeros);
                      i++;
                    }
                  block_sector_t **temp = *(disk_inode->doubly_indirect + j);
                  *(disk_inode->doubly_indirect + j) = (block_sector_t **) free_map_allocate_one ();
                  block_write (fs_device, (block_sector_t) *(disk_inode->doubly_indirect + j), temp);
                  free (temp);
                }
	    }
          block_sector_t ***temp = disk_inode->doubly_indirect;
          disk_inode->doubly_indirect = (block_sector_t ***) free_map_allocate_one ();
          block_write (fs_device, (block_sector_t) disk_inode->doubly_indirect, temp);
          free (temp);
        }
      block_write (fs_device, sector, disk_inode);
      success = true;
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector, bool isdir)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  inode->fd = fd_counter;
  fd_counter++;
  list_push_front (&open_inodes, &inode->elem);
  inode->isdir = isdir;
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  //printf ("closing inode: length is %i\n", inode->data.length);
  block_write (fs_device, inode->sector, &inode->data);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);

          size_t sectors = bytes_to_sectors (inode->data.length);
          unsigned i;
          for (i = 0; i < sectors; i++)
            free_map_release (inode->data.direct[i], 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      int index = cache_lookup (sector_idx);
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Copy a full sector from buffer cache. */
          memcpy (buffer + bytes_read, buffer_cache.cache[index].data,
	          chunk_size);
	}
      else 
        {
          /* Copy a partial sector from buffer cache. */
          memcpy (buffer + bytes_read,
	          buffer_cache.cache[index].data + sector_ofs, chunk_size);
	  }
      cache_operation_done (index);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  int left = bytes_to_sectors (inode->data.length) * BLOCK_SECTOR_SIZE - offset;
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      if (left <= 0)
        {
          struct inode_disk disk_inode = inode->data;
          static char zeros[BLOCK_SECTOR_SIZE];
          size_t num_sectors = bytes_to_sectors (inode->data.length);
          if (num_sectors < DIRECT_BLOCKS)
            {
              inode->data.direct[num_sectors] = free_map_allocate_one ();
              block_write (fs_device, inode->data.direct[num_sectors], zeros);
            }
          else
            {
              num_sectors -= DIRECT_BLOCKS;
	      int indirect_idx = num_sectors / ADDRS_PER_BLOCK;
              int indirect_ofs = num_sectors % ADDRS_PER_BLOCK;
              if (indirect_idx < INDIRECT_BLOCKS)
                {
                  if (indirect_ofs == 0)
                    {
                      // We need to allocate a new indirect block
                      disk_inode.indirect[indirect_idx] = (block_sector_t *) free_map_allocate_one ();
                    }
                  // Then make a new indirect entry
                  block_sector_t *sector = malloc (BLOCK_SECTOR_SIZE);
                  block_read (fs_device, (block_sector_t) disk_inode.indirect[indirect_idx], sector);
                  *(sector + indirect_ofs) = free_map_allocate_one ();
                  block_write (fs_device, *(sector + indirect_ofs), zeros);
                  block_write (fs_device, (block_sector_t) disk_inode.indirect[indirect_idx], sector);
                  free (sector);
                }
              else
                {
                  num_sectors -= ADDRS_PER_BLOCK * INDIRECT_BLOCKS;
                  int dbl_indirect_idx = num_sectors / ADDRS_PER_BLOCK;
                  int dbl_indirect_ofs = num_sectors % ADDRS_PER_BLOCK;
                  // Then make a new doubly indirect entry
                  if (dbl_indirect_idx == 0 && dbl_indirect_ofs == 0)
                    {
                      // We need to allocate the double indirect block
                      disk_inode.doubly_indirect = (block_sector_t ***) free_map_allocate_one ();
                    }
                  if (dbl_indirect_ofs == 0)
                    {
                      // We need to allocate a new indirect block
                      block_sector_t **dbl_sector = malloc (BLOCK_SECTOR_SIZE);
                      block_read (fs_device, (block_sector_t) inode->data.doubly_indirect, dbl_sector);
                      *(dbl_sector + dbl_indirect_idx) = (block_sector_t *) free_map_allocate_one ();
                      block_write (fs_device, (block_sector_t) inode->data.doubly_indirect, dbl_sector);
                      free (dbl_sector);
                    }
                  //Then make a new indirect entry
                  block_sector_t **dbl_sector = malloc (BLOCK_SECTOR_SIZE);
                  block_read (fs_device, (block_sector_t) inode->data.doubly_indirect, dbl_sector);
                  block_sector_t *dbl_sector_ofs = malloc (BLOCK_SECTOR_SIZE);
                  block_read (fs_device, (block_sector_t) *(dbl_sector + dbl_indirect_idx), dbl_sector_ofs);
                  *(dbl_sector_ofs + dbl_indirect_ofs) = free_map_allocate_one ();
                  block_write (fs_device, *(dbl_sector_ofs + dbl_indirect_ofs), zeros);
                  block_write (fs_device, (block_sector_t) *(dbl_sector + dbl_indirect_idx), dbl_sector_ofs);
                  free (dbl_sector);
                  free (dbl_sector_ofs);
                }
            }
          left += BLOCK_SECTOR_SIZE;
        }

      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = left;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (inode->data.length < offset + chunk_size)
        inode->data.length = offset + chunk_size;
      if (chunk_size <= 0)
        break;

      int index = cache_lookup (sector_idx);
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write a full sector to buffer cache. */
          memcpy (buffer_cache.cache[index].data, buffer + bytes_written,
	          chunk_size);
        }
      else 
        {
          /* Write partial sector to buffer cache. */
          memcpy (buffer_cache.cache[index].data + sector_ofs,
                  buffer + bytes_written, chunk_size);
        }
      cache_operation_done (index);

      /* Advance. */
      left -= chunk_size;
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
