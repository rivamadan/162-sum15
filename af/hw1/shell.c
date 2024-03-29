#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE 80

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

int cmd_quit(tok_t arg[]) {
  printf("Bye\n");
  exit(0);
  return 1;
}

int cmd_cd(tok_t arg[]) {
  char *filename = arg[0];
  int error = chdir(filename);
  if (error == 0) {
    return 0;
  } else {
    perror("cd");
    return 1;
  }
}

int cmd_help(tok_t arg[]);


/* Command Lookup table */
typedef int cmd_fun_t (tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
  {cmd_cd, "cd", "changes current working directory"},
};

int cmd_help(tok_t arg[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    printf("%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}


int lookup(char cmd[]) {
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}

void init_shell()
{
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  /** Note that we cannot take control of the terminal if the shell
      is not interactive */
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){

    /* force into foreground */
    while(tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp()))
      kill( - shell_pgid, SIGTTIN);

    shell_pgid = getpid();
    /* Put shell in its own process group */
    if(setpgid(shell_pgid, shell_pgid) < 0){
      perror("Couldn't put the shell in its own process group");
      exit(1);
    }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);
  }
  first_process = malloc(sizeof(process));
  first_process->pid = shell_pgid;

  signal (SIGQUIT, SIG_IGN);
  signal (SIGTSTP, SIG_IGN);
  signal (SIGTTIN, SIG_IGN);
  signal (SIGTTOU, SIG_IGN);
  signal (SIGCHLD, SIG_IGN);
  signal (SIGINT, SIG_IGN);
}

/**
 * Add a process to our process list
 */
void add_process(process* p)
{
  process *list = first_process;
  while (list->next){
    list = list->next;
  }
  list->next = p;
}

/**
 * Creates a process given the inputString from stdin
 */
process* create_process(char* inputString)
{ 
  process* new_process = (process*) malloc(sizeof(process));
  new_process->stdin = STDIN_FILENO;
  new_process->stdout = STDOUT_FILENO;
  new_process->stderr = STDERR_FILENO;

  char* input = malloc(strlen(inputString)+1);
  strcpy(input, inputString);
  char* args = strtok(input, "<>");
  tok_t* t = getToks(args);
  new_process->argv = t;
  new_process->argc = sizeof(t);

  add_process(new_process);
  return new_process;
}


int shell (int argc, char *argv[]) {
  char *s = malloc(INPUT_STRING_SIZE+1);			/* user input string */
  tok_t *t;			/* tokens parsed from input */
  int lineNum = 0;
  int fundex = -1; 
  pid_t pid = getpid();		/* get current processes PID */
  pid_t ppid = getppid();	/* get parents PID */
  pid_t cpid, tcpid, cpgid;
  char *cwd = (char *) malloc(INPUT_STRING_SIZE+1);

  init_shell();

  printf("%s running as PID %d under %d\n",argv[0],pid,ppid);
  lineNum=0;
  getcwd(cwd, INPUT_STRING_SIZE+1);
  fprintf(stdout, "%d %s: ", lineNum, cwd);

  while ((s = freadln(stdin))){

    process* curr_process = create_process(s);

    char *input = malloc(strlen(s)+1);
    strcpy(input, s);
    char* in = strstr(input, "<");
    char* out = strstr(input, ">");

    t = getToks(s); /* break the line into tokens */
    fundex = lookup(t[0]); /* Is first token a shell literal */
    if(fundex >= 0) cmd_table[fundex].fun(&t[1]);
    else {
      cpid = fork();
      if (cpid == 0) {
        curr_process->pid = getpid();
        if (in) {
          char* infile = strtok(in, " <\n");
          curr_process->stdin = open(infile, O_RDONLY);
        } if (out) {
          char* outfile = strtok(out, " >\n");
          curr_process->stdout = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        }
        launch_process(curr_process);
      } else if (cpid > 0) {
          if(setpgid(cpid, cpid) < 0){
            perror("Couldn't put the parent in childs process group");
            exit(1);
          }
          tcsetpgrp (STDIN_FILENO, getpgid(cpid));
          wait(&(curr_process->status));
          tcsetpgrp(STDIN_FILENO, pid);
          if (WIFEXITED(curr_process->status)) curr_process->completed = TRUE;
          if (WIFSTOPPED(curr_process->status)) curr_process->stopped = TRUE;
      } else {
          perror("fork");
      }
    }
    getcwd(cwd, INPUT_STRING_SIZE+1);
    fprintf(stdout, "%d %s: ", ++lineNum, cwd);
  }
  return 0;
}