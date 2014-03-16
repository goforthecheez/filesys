#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

/* Number of direct blocks per inode. */ 
#define DIRECT_BLOCKS 100     

/* Number of indirect blocks per inode. */
#define INDIRECT_BLOCKS 25

/* On-disk inode.   
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */        
struct inode_disk   
  {
    off_t length;                            /* File size in bytes. */
    unsigned magic;                          /* Magic number. */
    block_sector_t direct[DIRECT_BLOCKS];    /* Direct blocks. */
    block_sector_t *indirect[INDIRECT_BLOCKS];     /* Indirect block. */
    block_sector_t ***doubly_indirect;             /* Doubly indirect block. */
  };

/* In-memory inode. */
struct inode 
  {
    int fd;                             /* File descriptor. */
    bool isdir;                         /* Whether this is a directory. */
    struct list_elem elem;              /* Element in inode list. */
    struct hash_elem hashelem;          /* Element in per-process open inodes
                                           list */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    void *object;
  };

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t, bool);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
