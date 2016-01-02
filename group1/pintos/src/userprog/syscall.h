#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/off_t.h"
#include <stdbool.h>
#include "threads/thread.h"


#define CLOSE_ALL -1
#define ERROR -1

#define NOT_LOADED 0
#define LOAD_SUCCESS 1
#define LOAD_FAIL 2


void syscall_init (void);

int write (int fd, const void *buffer, unsigned size);
void exit (int status);
int open (const char *file);
bool remove (const char *file);
bool create (const char *file, unsigned initial_size);
tid_t exec (const char *cmd_line);
void close (int fd);
off_t tell (int fd);
void seek (int fd, unsigned int position);

struct process_file {
  struct file *file;
  int fd;
  struct list_elem elem;
};

struct child_process {
  int pid;
  int loaded;
  bool waited;
  bool exited;
  int status;
  struct semaphore* wait_sema;
  struct list_elem elem;
};

struct child_process* add_child_process (int pid);
struct child_process* get_child_process (int pid);
void remove_child_process (struct child_process *cp);
void remove_child_processes (void);

void process_close_file (int fd);
int process_add_file (struct file *f);
struct file* process_get_file (int fd);

#endif /* userprog/syscall.h */
