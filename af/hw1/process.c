#include "process.h"
#include "shell.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include "parse.h"


void resolve_path(char* args[]) {
  char* path = getenv("PATH");
  tok_t *path_tok = getToks(path);
  DIR *directory;
  struct dirent *entry;
  int i;
  char* dir = NULL;

  for(i=0; i < sizeof(path_tok); i++) {
    directory = opendir(path_tok[i]);
    while ((entry = readdir(directory)) != NULL) {
      if (strcmp(args[0], entry->d_name) == 0) {
        dir = path_tok[i];
        break;
      }
    } 
    closedir(directory);
    if (dir) {
      char full_path[strlen(args[0])+strlen(dir)+2];
      strcpy(full_path, dir);
      strcat(full_path, "/");
      strcat(full_path, args[0]);
      args[0] = full_path;
      execv(full_path, args);
      break;
    }
  }
}

/**
 * Executes the process p.
 * If the shell is in interactive mode and the process is a foreground process,
 * then p should take control of the terminal.
 */
void launch_process(process *p)
{
    signal (SIGQUIT, SIG_DFL);
    signal (SIGTSTP, SIG_DFL);
    signal (SIGTTIN, SIG_DFL);
    signal (SIGTTOU, SIG_DFL);
    signal (SIGCHLD, SIG_DFL);
    signal (SIGINT, SIG_DFL);
    
  	if(setpgid(p->pid, p->pid) < 0){
        perror("Couldn't put the child in its own process group");
        exit(1);
    }
    pid_t cpgid = getpgrp();
    tcsetpgrp(STDIN_FILENO, cpgid);

    if (p->stdin != STDIN_FILENO) {
      dup2(p->stdin, STDIN_FILENO);
      close(p->stdin);
    } if (p->stdout != STDOUT_FILENO) {
      dup2(p->stdout, STDOUT_FILENO);
      close(p->stdout);
    }

    execv(p->argv[0], p->argv);
    resolve_path(p->argv);
    perror("execv");
    exit(1);
}
