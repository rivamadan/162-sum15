#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

// ~~~ Added by Group1 ~~~
#include "filesys/off_t.h"
#include "threads/vaddr.h"

struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  // ~~~ Added by Group1 ~~~
  lock_init(&filesys_lock);
  // ~~~ Added by Group1 ~~~
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{

  uint32_t* args = ((uint32_t*) f->esp);
  uint32_t* pointer = f->esp;

  // ~~~ Added by Group1 ~~~

   if (is_safe(pointer)) {
     switch (* (int *) f->esp)
     {
       case SYS_EXIT:
       {
         if (is_safe(pointer+1))
            exit(args[1]);
         break;
       }
       case SYS_WRITE:
       {
         if (is_safe(pointer+1) && is_safe(pointer+2) && is_safe(pointer+3))
            f->eax = write(args[1], (const void *) args[2], (unsigned) args[3]);     
         break;
       }
       case SYS_NULL:
       {
         if (is_safe(pointer+1))
            f->eax = null(args[1]);
         break;
       }
       case SYS_HALT:
       {
         halt();
         break;
       }
       case SYS_WAIT:
       {
         if (is_safe(pointer+1))
            f->eax = wait(args[1]);
         break;
       }
       case SYS_EXEC:
       {
         if (is_safe(pointer+1))
            f->eax = exec((const char *) args[1]);
         break;
       }
       case SYS_CREATE:
       {
         if (is_safe(pointer+1) && is_safe(pointer+2))
            f->eax = create((const char *)args[1], (unsigned) args[2]);
         break;
       }
       case SYS_REMOVE:
       {
         if (is_safe(pointer+1))
            f->eax = remove((const char *) args[1]);
         break;
       }
       case SYS_OPEN:
       {
         if (is_safe(pointer+1))
            f->eax = open((const char *) args[1]);
         break;
       }
       case SYS_FILESIZE:
       {
         if (is_safe(pointer+1))
            f->eax = filesize(args[1]);
         break;
       }
       case SYS_READ:
       {
         if (is_safe(pointer+1) && is_safe(pointer+2) && is_safe(pointer+3))
            f->eax = read(args[1], (void *) args[2], (unsigned) args[3]);
         break;
       }
       case SYS_SEEK:
       {
          if (is_safe(pointer+1) && is_safe(pointer+2))
              seek(args[1], (unsigned) args[2]);
          break;
       }
       case SYS_TELL:
       {
         if (is_safe(pointer+1))
            f->eax = tell(args[1]);
         break;
       }
       case SYS_CLOSE:
       {
         if (is_safe(pointer+1))
            close(args[1]);
         break;
       }
     }
   }
  // ~~~ Added by Group1 ~~~
}

// ~~~ Added by Group1 ~~~
void halt (void)
{
  shutdown_power_off();
}

int wait (tid_t child_tid)
{
  return process_wait(child_tid);
}

tid_t exec (const char *cmd_line)
{
   if (is_safe(cmd_line)) {
      tid_t child_tid = process_execute(cmd_line);
      struct child_process* child_process = get_child_process(child_tid);

      if (child_process->loaded == LOAD_FAIL)
      {
       return ERROR;
      }
       return child_tid;
   }
}

int write (int fd, const void *buffer, unsigned size)
{
  if (fd == STDIN_FILENO) {
    exit (-1);
  }
  else if (is_safe(buffer) && is_safe(buffer + size)) {
     if (fd == STDOUT_FILENO)
     {
       putbuf(buffer, size);
       return size;
     } else {
        lock_acquire(&filesys_lock);
        struct file *f = process_get_file(fd);
        if (!f)
        {
          lock_release(&filesys_lock);
          exit (-1);
        }
        int written_size = file_write(f, buffer, size);
        lock_release(&filesys_lock);
        return written_size;
    }
  }
}
bool create (const char *file, unsigned initial_size)
{
  if (is_safe (file)) {
     lock_acquire(&filesys_lock);
     bool success = filesys_create(file, initial_size);
     lock_release(&filesys_lock);
     return success;
   }
}

bool remove (const char *file)
{
   if (is_safe(file)) {
     lock_acquire(&filesys_lock);
     bool success = filesys_remove(file);
     lock_release(&filesys_lock);
     return success;
   }
}

int open (const char *file)
{
  if (is_safe(file)) {
     lock_acquire(&filesys_lock);
     struct file *f = filesys_open(file);
     if (!f)
     {
       lock_release(&filesys_lock);
       return ERROR;
     }
   
     int fd = process_add_file(f);
     lock_release(&filesys_lock);
     return fd;
  }
}

int filesize (int fd)
{
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
  {
    lock_release(&filesys_lock);
    return ERROR;
  }
  int size = file_length(f);
  lock_release(&filesys_lock);
  return size;
}

int read (int fd, void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO) {
    exit (-1);
  }
  else if (is_safe (buffer)) { 
     if (fd == STDIN_FILENO)
     {
       return size;
     } 

     lock_acquire(&filesys_lock);
     struct file *f = process_get_file(fd);

     if (!f)
     {
       lock_release(&filesys_lock);
       return ERROR;
     } 

     int bytes = file_read(f, buffer, size);
     lock_release(&filesys_lock);
     return bytes;
  }
}

void seek (int fd, unsigned int position)
{
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
  {
    lock_release(&filesys_lock);
    return;
  }
  file_seek(f, position);
  lock_release(&filesys_lock);
}

off_t tell (int fd)
{
  lock_acquire(&filesys_lock);
  struct file *f = process_get_file(fd);
  if (!f)
  {
    lock_release(&filesys_lock);
    return ERROR;
  }

  off_t offset = file_tell(f);
  lock_release(&filesys_lock);
  return offset;
}

void close (int fd)
{
  lock_acquire(&filesys_lock);
  process_close_file(fd);
  lock_release(&filesys_lock);
}

void exit (int status)
{
  struct thread *current_thread = thread_current();
  current_thread->cp->status = status;
  printf ("%s: exit(%d)\n", current_thread->name, status);
  thread_exit();
}

int process_add_file (struct file *f)
{
  struct process_file *pf = malloc(sizeof(struct process_file));
  pf->file = f;
  pf->fd = thread_current()->fd;
  thread_current()->fd++;
  list_push_back(&thread_current()->file_list, &pf->elem);
  return pf->fd;
}

struct file* process_get_file (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for (e = list_begin (&t->file_list); e != list_end (&t->file_list); e = list_next (e))
  {
    struct process_file *pf = list_entry (e, struct process_file, elem);
    if (fd == pf->fd)
    {
      return pf->file;
    }
  }
  return NULL;
}

void process_close_file (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *next, *e = list_begin(&t->file_list);

  while (e != list_end (&t->file_list))
  {
    next = list_next(e);
    struct process_file *pf = list_entry (e, struct process_file, elem);
    if (fd == pf->fd || fd == CLOSE_ALL)
    {
      file_close(pf->file);
      list_remove(&pf->elem);
      free(pf);
      if (fd != CLOSE_ALL)
        {
          return;
        }
    }
    e = next;
  }
}

struct child_process* add_child_process (tid_t pid)
{
  struct child_process* cp = malloc(sizeof(struct child_process));
  cp->pid = pid;
  cp->loaded = NOT_LOADED;
  cp->waited = false;
  cp->exited = false;
  cp->wait_sema = NULL;

  list_push_back(&thread_current()->child_list, &cp->elem);
  return cp;
}

struct child_process* get_child_process (tid_t pid)
{
  struct thread *t = thread_current();
  struct list_elem *e;

  for (e = list_begin (&t->child_list); e != list_end (&t->child_list); e = list_next (e))
  {
    struct child_process *cp = list_entry (e, struct child_process, elem);
    if (pid == cp->pid)
    {
      return cp;
    }
  }

  return NULL;
}

void remove_child_process (struct child_process *cp)
{
  list_remove(&cp->elem);
  if (!cp->wait_sema)
    free(cp->wait_sema);
  free(cp);
}

void remove_child_processes (void)
{
  struct thread *t = thread_current();
  struct list_elem *next, *e = list_begin(&t->child_list);

  while (e != list_end (&t->child_list))
  {
    next = list_next(e);
    struct child_process *cp = list_entry (e, struct child_process, elem);
    remove_child_process(cp);
    e = next;
  }
}

int null (int i)
{
  return ++i;
}

int is_safe (const void *ptr) {
    struct thread *t = thread_current();
    if (ptr == NULL || !is_user_vaddr (ptr) || pagedir_get_page(t->pagedir, ptr) == NULL) {
       exit(-1);
       return 0;
    } else {
        return 1;
    }
} 

// ~~~ Added by Group1 ~~~