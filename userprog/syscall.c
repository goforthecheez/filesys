#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "lib/kernel/hash.h"
#include "lib/user/syscall.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

#define ARG_ONE ((int *)f->esp + 1)
#define ARG_TWO ((int *)f->esp + 2)
#define ARG_THREE ((int *)f->esp + 3)

static void syscall_handler (struct intr_frame *);
void halt (void);
pid_t exec (const char *);
int wait (pid_t);
bool create (const char *, unsigned);
bool remove (const char *);
int open (const char *);
int filesize (int);
int read (int, void *, unsigned);
int write (int, const void*, unsigned);
void seek (int, unsigned);
unsigned tell (int);
void close (int);
bool chdir (const char *);
bool mkdir (const char *);
bool readdir (int, char *);
bool isdir (int);
int inumber (int);
char *abs_path (const char *);
void check_args (void *, void *, void *);
struct inode *lookup_fd (int);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Check that the stack pointer is valid. */
  if (pagedir_get_page (thread_current ()->pagedir, f->esp) == NULL)
    exit (-1);

  switch (*(int *)f->esp)
    {
      /* For each syscall, check that its arguments are valid, then call
         the appropriate handler, writing the return value to EAX. */
      case SYS_HALT:
        shutdown_power_off ();
        break;
      case SYS_EXIT:
        check_args (ARG_ONE, NULL, NULL);
        exit (*ARG_ONE);
        break;
      case SYS_EXEC:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = exec (*(char **) ARG_ONE);
        break;
      case SYS_WAIT:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = wait (*(unsigned *) ARG_ONE);
        break;
      case SYS_CREATE:
        check_args (ARG_ONE, ARG_TWO, NULL);
        f->eax = create (*(char **) ARG_ONE, *(unsigned *) ARG_TWO);
        break;
    case SYS_REMOVE:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = remove (*(char **) ARG_ONE);
        break;
      case SYS_OPEN:
        check_args (ARG_ONE, NULL, NULL);
	f->eax = open (*(char **) ARG_ONE);
        break;
      case SYS_FILESIZE:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = filesize (*ARG_ONE);
        break;
      case SYS_READ:
        check_args (ARG_ONE, ARG_TWO, ARG_THREE);
	f->eax = read (*ARG_ONE, *(char **) ARG_TWO, *(unsigned *) ARG_THREE);
        break;
      case SYS_WRITE:
        check_args (ARG_ONE, ARG_TWO, ARG_THREE);
	f->eax = write (*ARG_ONE, *(char **) ARG_TWO, *(unsigned *) ARG_THREE);
        break;
      case SYS_TELL:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = tell (*ARG_ONE);
        break;
      case SYS_SEEK:
        check_args (ARG_ONE, ARG_TWO, NULL);
        seek (*ARG_ONE, *(unsigned *) ARG_TWO);
        break;
      case SYS_CLOSE:
        check_args (ARG_ONE, NULL, NULL);
        close (*ARG_ONE);
        break;
      case SYS_CHDIR:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = chdir (*(char **) ARG_ONE);
        break;
      case SYS_MKDIR:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = mkdir (*(char **) ARG_ONE);
	break;
      case SYS_READDIR:
        check_args (ARG_ONE, ARG_TWO, NULL);
        f->eax = readdir (*ARG_ONE, *(char **) ARG_TWO);
        break;
      case SYS_ISDIR:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = isdir (*ARG_ONE);
        break;
      case SYS_INUMBER:
        check_args (ARG_ONE, NULL, NULL);
        f->eax = inumber (*ARG_ONE);
        break;
      default:
        exit (-1);
    }
}

/* Terminates the current user program, returning STATUS to the kernel.
   a status of 0 indicates success and nonzero values indicate errors. */
void
exit (int status)
{
  struct thread *t = thread_current ();

  lock_acquire (&t->parent->child_lock);
  struct child c;
  c.pid = t->tid;;
  struct child *found_child = hash_entry (hash_find (t->parent->children,
                                                     &c.elem),
                                          struct child, elem);
  found_child->done = true;
  found_child->exit_status = status;
  cond_signal (&t->parent->child_cond, &t->parent->child_lock);
  lock_release (&t->parent->child_lock);

  thread_exit ();
}

/* Runs the executable whose name is given in cmd_line, passing any given
   arguments, and returns the new process's program id (pid). If the program
   cannot load or run for any reason, returns -1. */
pid_t
exec (const char *cmd_line)
{
  struct thread *t = thread_current ();

  if (pagedir_get_page (t->pagedir, cmd_line) == NULL)
    exit (-1);

  lock_acquire (&t->child_lock);
  pid_t pid = process_execute (cmd_line);
  cond_wait (&t->child_cond, &t->child_lock);
  t->child_ready = false;
  
  struct child c;
  c.pid = pid;
  struct child *found_child = hash_entry (hash_find (t->children, &c.elem),
                                         struct child, elem);
  if (found_child->exit_status == -1)
    pid = -1;
  lock_release (&t->child_lock);

  return pid;
}

/* Waits for a child process PID to die and returns its exit status.
   If PID was terminated by the kernel, returns -1. Returns -1 immediately if
   PID is invalid or if it was a child of the current process, or if wait() has
   already been successfully called for the given PID. */
int
wait (pid_t pid)
{
  int exit_status = process_wait (pid);

  if (exit_status == -1)
    return -1;

  lock_acquire (&thread_current ()->child_lock);
  struct child c;
  c.pid = pid;
  struct hash_elem *deleted_elem = hash_delete (thread_current ()->children,
                                                &c.elem);
  struct child *deleted_child = hash_entry (deleted_elem, struct child, elem);
  free (deleted_child);
  lock_release (&thread_current ()->child_lock);

  return exit_status;
}

/* Creates a new file initially initial_size bytes in size. Returns true if
   successful, false otherwise. Note that creating a file does not open it. */
bool
create (const char *path, unsigned initial_size)
{
  if (pagedir_get_page (thread_current ()->pagedir, path) == NULL)
    exit (-1);

  char *ap = abs_path (path);

  lock_acquire (&filesys_lock);
  bool success = filesys_create (ap, initial_size, false);
  lock_release (&filesys_lock);

  return success;
}

/* Deletes the file or directory PATH. Returns true if successful, false
   otherwise. Note that removing an open object does not close it. */
bool
remove (const char *path)
{
  if (pagedir_get_page (thread_current ()->pagedir, path) == NULL)
    exit (-1);

  char *ap = abs_path (path);

  lock_acquire (&filesys_lock);
  bool success = filesys_remove (ap);
  lock_release (&filesys_lock);

  return success;
}

/* Opens the file FILE and returns its file descriptor, or -1 if the file could
   not be opened. */
int
open (const char *path)
{
  struct thread *t = thread_current ();

  if (pagedir_get_page (t->pagedir, path) == NULL)
    exit (-1);

  if (strchr (path, '\0') == path)
    return -1;

  if (strcmp (path, "/") == 0)
    {
      struct dir *dir = dir_open_root ();

      //if (hash_find (t->open_inodes, &dir->inode->hashelem) != NULL)
      //dir = dir_reopen (dir);

      hash_insert (t->open_inodes, &dir->inode->hashelem);
      printf ("open: fd is %i\n", dir->inode->fd);
      return dir->inode->fd;
    }

  char *ap = abs_path (path);

  lock_acquire (&filesys_lock);
  struct inode *inode = filesys_open (ap);
  lock_release (&filesys_lock);
  if (inode == NULL)
      return -1;

  if (inode->isdir)
    {
      struct dir *dir = dir_open (inode);

      //if (hash_find (t->open_inodes, &inode->hashelem) != NULL)
      //dir = dir_reopen (dir);

      hash_insert (t->open_inodes, &dir->inode->hashelem);
      printf ("open: fd is %i\n", dir->inode->fd);
      return dir->inode->fd;
    }
  else
    {
      struct file *file = file_open (inode);

      //if (hash_find (t->open_inodes, &inode->hashelem) != NULL)
      //file = file_reopen (file);

      hash_insert (t->open_inodes, &file->inode->hashelem);
      printf ("open: fd is %i\n", file->inode->fd);
      return file->inode->fd;
    }
}

/* Returns the size, in bytes, of the file open as FD. */
int
filesize (int fd)
{
  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
      exit (-1);

  lock_acquire (&filesys_lock);
  int len = file_length ((struct file *) inode->object);
  lock_release (&filesys_lock);

  return len;
}

/* Reads size bytes from the file open as FD into BUFFER. Returns the number
   of bytes actually read, or -1 if the file could not be read. */
int
read (int fd, void *buffer, unsigned size)
{
  struct thread *t = thread_current ();

  if (pagedir_get_page (t->pagedir, buffer) == NULL ||
      pagedir_get_page (t->pagedir, (char *)buffer + size) == NULL)
    exit (-1);

  if (fd == STDIN_FILENO)
    {
      unsigned i;
      for (i = 0; i < size; i++)
        input_getc ();
      return i;
    }

  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
      exit (-1);

  lock_acquire (&filesys_lock);
  int bytes_read = file_read ((struct file *) inode->object, buffer, size);
  lock_release (&filesys_lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER to the open file descriptor FD. Returns the
   number of bytes actually written, which may be less than SIZE if some bytes
   could not be written. */
int
write (int fd, const void *buffer, unsigned size)
{
  struct thread *t = thread_current ();

  if (pagedir_get_page (t->pagedir, buffer) == NULL ||
      pagedir_get_page (t->pagedir, (char *)buffer + size) == NULL)
    exit (-1);

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      return size;
    }

  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
    exit (-1);
  if (inode->isdir)
    exit (-1);

  lock_acquire (&filesys_lock);
  int bytes_written = file_write ((struct file *) inode->object, buffer, size);
  lock_release (&filesys_lock);

  return bytes_written;
}

/* Changes the next byte to be read or written in open file FD to POSITION,
   expressed in bytes from the beginning of the file. A seek past the current
   end of a file is not an error. */
void
seek (int fd, unsigned position)
{
  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
      exit (-1);

  lock_acquire (&filesys_lock);
  file_seek ((struct file *) inode->object, position);
  lock_release (&filesys_lock);
}

/* Returns the position of the next byte to be read or written in open
   file FD, expressed in bytes from the beginning of the file. */
unsigned
tell (int fd)
{
  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
      exit (-1);

  lock_acquire (&filesys_lock);
  unsigned pos = file_tell ((struct file *) inode->object);
  lock_release (&filesys_lock);

  return pos;
}

/* Closes file descriptor FD. Exiting or terminating a process implicitly
   closes all its open file descriptors, as if by calling this function for
   each one. */
void close (int fd)
{
  struct thread *t = thread_current ();

  /* File descriptors 0, 1, and 2 are reserved and cannot be closed. */
  if (fd == 0 || fd == 1 || fd == 2)
    exit (-1);

  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
      exit (-1);

  /* If the lookup succeeded, delete the inode from open_inodes. */
  struct inode lookup;
  lookup.fd = fd;
  hash_delete (t->open_inodes, &lookup.hashelem);
  
  lock_acquire (&filesys_lock);
  if (inode->isdir)
    dir_close ((struct dir *) inode->object);
  else
    file_close ((struct file *) inode->object);
  lock_release (&filesys_lock);
}

/* Changes the current working directory of the process to PATH, which may be
   relative or absolute. Returns true if successful, false on failure. */
bool
chdir (const char *path)
{
  struct thread *t = thread_current ();

  if (pagedir_get_page (t->pagedir, path) == NULL)
    exit (-1);

  char *ap = abs_path (path);
  //printf ("absolute path: %s\n", ap);

  // Don't forget to remove the final "/"!
  *(t->cwd) = ap;

  return true;
}

/* Creates the directory named PATH, which may be relative or absolute.
   Returns true if successful, false on failure. Fails if dir already
   exists or if any directory name in dir, besides the last, does not
   already exist. That is, mkdir("/a/b/c") succeeds only if /a/b already
   exists and /a/b/c does not. */
bool
mkdir (const char *path)
{
  if (pagedir_get_page (thread_current ()->pagedir, path) == NULL)
    exit (-1);

  char *ap = abs_path (path);
  //printf ("mkdir: abs_path is %s\n", ap);
  struct dir *dir = dir_open_root ();
  char *token, *save_ptr;

  /* Crawl down the absolute path string. */
  struct inode *inode = malloc (sizeof inode);
  for (token = strtok_r (ap, "/", &save_ptr);
       token != NULL; token = strtok_r (NULL, "/", &save_ptr))
    {
      if (!dir_lookup (dir, token, &inode))
        {
	  block_sector_t dir_sector = free_map_allocate_one ();
          dir_create (dir_sector, 0);
          dir_add (dir, token, dir_sector, true);
          return true;
        }

      dir = dir_open (inode);
    }

  return false;
}

/* Reads a directory entry from file descriptor fd, which must represent a directory. If successful, stores the null-terminated file name in name, which must have room for READDIR_MAX_LEN + 1 bytes, and returns true. If no entries are left in the directory, returns false.
   . and .. should not be returned by readdir.*/
bool
readdir (int fd, char *name)
{
  if (pagedir_get_page (thread_current ()->pagedir, name) == NULL)
    exit (-1);

  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
    return false;

  return dir_readdir ((struct dir *) inode->object, name);
}

/* Returns true if fd represents a directory, false if it represents an ordinary file. */
bool
isdir (int fd)
{
  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
    exit (-1);

  return inode->isdir;
}

/* Returns the inode number of the inode associated with fd, which may represent an ordinary file or a directory. */
int
inumber (int fd)
{
  struct inode *inode = lookup_fd (fd);
  if (inode == NULL)
    exit (-1);

  return inode->sector;
}

/* Verify that the passed syscall arguments are valid pointers.
   If not, exit(-1) the user program with an kernel error. */
void
check_args (void *first, void *second, void *third)
{
  uint32_t *pd = thread_current ()->pagedir;

  if (pagedir_get_page (pd, first) == NULL)
    exit (-1);

  if (second != NULL && pagedir_get_page (pd, second) == NULL)
    exit (-1);

  if (third != NULL && pagedir_get_page (pd, third) == NULL)
    exit (-1);
}

/* Given a file descriptor FD, returns its corresponding inode. If no
   inode is found, return NULL). */
struct inode *
lookup_fd (int fd)
{
  struct thread *t = thread_current ();

  struct inode lookup;
  lookup.fd = fd;
  struct hash_elem *e = hash_find (t->open_inodes, &lookup.hashelem);
  if (e == NULL)
    return NULL;

  return hash_entry (e, struct inode, hashelem);
}

/* Given an absolute or relative path PATH, returns the absolute path. */
char *
abs_path (const char *path)
{
  const char *cwd = *thread_current ()->cwd;

  char *abs_path;
  if (strchr (path, '/') == path)
    {
      int length = strlen (path) + 1;
      abs_path = malloc (length);
      strlcpy (abs_path, path, length);
      return abs_path;
    }

  if (strcmp (cwd, "/") == 0)
    {
      int length = strlen (cwd) + strlen (path) + 1;
      abs_path = malloc (length);
      strlcpy (abs_path, cwd, strlen (cwd) + 1);
      strlcat (abs_path + strlen (cwd), path, length);
      return abs_path;
    }

  int length = strlen (cwd) + 1 + strlen (path) + 1;
  abs_path = malloc (length);
  strlcpy (abs_path, cwd, strlen (cwd) + 1);
  strlcpy (abs_path + strlen (cwd), "/", 2);
  strlcat (abs_path + strlen (cwd) + 1, path, length);

  return abs_path;
}
