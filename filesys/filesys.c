#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_flush ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name_, off_t initial_size, bool isdir) 
{
  if (strcmp (name_, "/") == 0)
    return false;

  char *name = malloc (strlen (name_) + 1);
  strlcpy (name, name_, strlen (name_) + 1);

  struct dir *dir = dir_open_root ();
  char *token, *save_ptr;
  struct inode *inode = malloc (sizeof inode);

  for (token = strtok_r (name, "/", &save_ptr); token != NULL;
       token = strtok_r (NULL, "/", &save_ptr))
    {
      bool found = dir_lookup (dir, token, &inode);
      if (!found)
        {
          block_sector_t inode_sector = 0;
          bool success = (dir != NULL
                          && free_map_allocate (1, &inode_sector)
                          && inode_create (inode_sector, initial_size)
                          && dir_add (dir, token, inode_sector, isdir));
          if (!success && inode_sector != 0) 
            free_map_release (inode_sector, 1);
          dir_close (dir);
          return success;
	}
      dir = dir_open (inode);
    }

  return false;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct inode *
filesys_open (const char *name_)
{
  char *name = malloc (strlen (name_) + 1);
  strlcpy (name, name_, strlen (name_) + 1);

  struct dir *dir = dir_open_root ();
  char *token, *save_ptr;
  struct inode *inode = malloc (sizeof inode);

  char *stop_here = strrchr (name, (int) '/') + 1;
  if (strchr (name, '/') == NULL)
    {
      if (dir != NULL)
        dir_lookup (dir, name, &inode);
      dir_close (dir);

      return inode;
    }
  else
    {
      for (token = strtok_r (name, "/", &save_ptr);
           token != NULL && stop_here != token;
           token = strtok_r (NULL, "/", &save_ptr))
        {
          if (strcmp (token, "") == 0)
            continue;
          bool found = dir_lookup (dir, token, &inode);
          if (!found)
              return NULL;

          dir = dir_open (inode);
        }

      dir_lookup (dir, token, &inode);
      dir_close (dir);
      return inode;
    }
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name_) 
{
  char *name = malloc (strlen (name_) + 1);
  strlcpy (name, name_, strlen (name_) + 1);

  struct dir *dir = dir_open_root ();
  char *token, *save_ptr;
  struct inode *inode = malloc (sizeof inode);

  char *stop_here = strrchr (name, (int) '/') + 1;
  if (strchr (name, '/') == NULL)
    {
      bool success = dir_remove (dir, name);
      dir_close (dir);

      return success;
    }
  else
    {
      for (token = strtok_r (name, "/", &save_ptr);
           token != NULL && stop_here != token;
           token = strtok_r (NULL, "/", &save_ptr))
        {
          bool found = dir_lookup (dir, token, &inode);
          if (!found)
            return false;
 
          dir = dir_open (inode);
        }
 
      //if (dir != NULL)
      //dir_lookup (dir, token, &inode);
      if (token == NULL)
        return false;
      bool success = dir != NULL && dir_remove (dir, token);
      dir_close (dir); 

      return success;
    }
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
