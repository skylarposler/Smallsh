#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
int exit_status = 0; 
int bg_child_pid = 0;
int pid_list[999999]; 
int pid_idx = 0;
void sigint_handler(int sig) {};
sighandler_t og_sig_STP; 
sighandler_t og_sig_INT;

int main(int argc, char *argv[])
{
  /* SIGNALS */

  struct sigaction signal_stop;
  signal_stop.sa_handler = SIG_IGN;

  struct sigaction signal_int;
  signal_int.sa_handler = sigint_handler;

  struct sigaction oldact;
  if (sigaction(SIGINT, &signal_stop, &oldact) == -1) err(1, "SIGINT Err");
  
  struct sigaction sigstop;
  if (sigaction(SIGTSTP, &signal_stop, &sigstop) == -1) err(1, "SIGTSTP Err");

  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;
  for (;;) {
start:;
//
    /* TODO: Manage background processes */

    /* TODO: prompt */
    if (input == stdin) { /* is interactive, otherwise is file */
      char *ps1_default = getenv("PS1");
      if (ps1_default != NULL) fprintf(stderr, ps1_default);
      sigaction(SIGINT, &signal_int, NULL);
    }

    ssize_t line_len = getline(&line, &n, input);
    if (input == stdin) sigaction(SIGINT, &signal_stop, NULL);
    if (line_len < 0){ 
    if (feof(input)) break;
    if (errno == EINTR) {
        fprintf(stderr, "\n");
        clearerr(input);
        errno = 0;
        goto start;
    }
     err(1, "%s", input_fn);
    }

    for (int i = 0; i < pid_idx; i++) {
      if (pid_list[i] != -1) {
      int status = NULL;
      if (waitpid(pid_list[i], &status, WNOHANG | WUNTRACED) == 0) continue;
      if (WIFEXITED(status)) fprintf(stderr, "Child process %d done. Exit status %d.\n", pid_list[i], WEXITSTATUS(status));
      if (WIFSIGNALED(status)) fprintf(stderr, "Child process %d done. Signaled %d.\n", pid_list[i], WTERMSIG(status));
      if (WIFSTOPPED(status)) {fprintf(stderr, "Child process %d stopped. Continuing.\n", pid_list[i]); 
      kill(pid_list[i], SIGCONT);
      }
      pid_list[i] = -1;
      }
    }

    if (feof(input)) {
      exit(exit_status);
    }

    size_t nwords = wordsplit(line);

    /* exit */

    if (nwords == 0) {
      exit(exit_status);
    }

    if (!strcmp(words[0], "exit") && strcmp(words[nwords - 1], "&")) {

      char *endptr = "\0";
      int value = 0;
      if (nwords > 1) {
      value = strtol(words[1], &endptr, 10);
      }
      if (*endptr != '\0' || nwords > 2) {
        fprintf(stderr, "Exit Error\n"); 
        goto prompt;
      }
      else {
      exit(value);
      }
    }

    /* CD */
    if (strcmp(words[0], "cd") == 0) {
      char *temp_dir = words[1];
      if (nwords == 1) {
        temp_dir = getenv("HOME");
      }
      if (nwords > 2) {fprintf(stderr, "Too many CD arguments.\n"); // should error exit program?
      goto prompt;
      }

      if (chdir(temp_dir) == -1 ) { 
        fprintf(stderr, "CD Failed.\n");
      }
      goto prompt;
    } 
    
    int temp = -1;
    int output_index = -1;
    int input_index = -1;
    int app_index = -1;
    /* for debudding purposes */
    for (size_t i = 0; i < nwords; ++i) {
      //fprintf(stderr, "Word %zu: %s\n", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;

      /* Input redirection */
      // background operator
      if (strcmp(words[i], "&") == 0) {
        temp = i;
      }

      // open file for writing stdout
      if (strcmp(words[i], ">") == 0) {
        output_index = i;
        int fd = open(words[i + 1], O_RDWR| O_CREAT, 0777);
        if (fd == -1) fprintf (stderr, "File couldnot be found.");
        truncate(words[output_index +1], 0);
        close(fd);
      }
  
      // open file for reading on stdin
      if (strcmp(words[i], "<") == 0) {
        input_index = i;
      }

      // open file for appending on stdout
      if (strcmp(words[i], ">>") == 0) {
          app_index = i;
      }
    }

    /* Non Built in Commands */
    /* child pid = 0, parent = child pid */
    int pid = fork();

    if (pid < 0) {
      fprintf(stderr, "Fork failed.\n");
      goto prompt;
    }
    else if (pid == 0) { // child
        if (sigaction(SIGTSTP, &sigstop, NULL) == -1) err(1, "Sig Stop Err");
        if (sigaction(SIGINT, &oldact, NULL) == -1) err(1, "Sig Int Err");

      words[temp] = NULL;
      if (output_index >= 0) { // >
        words[output_index] = NULL;
        if (output_index > app_index && freopen(words[output_index + 1], "w", stdout) == NULL)
          errx(errno, "> Error"); 
          } 

      if (input_index > 0) { // <
          words[input_index] = NULL;
          if (freopen(words[input_index + 1], "r", stdin) == NULL)
            errx(errno, "< Error");
      }

      if (app_index > 0){ // >>
          words[app_index] = NULL;
          if (app_index > output_index && freopen(words[app_index + 1], "a", stdout) == NULL)
          errx(errno, ">> Error");
      
      }
      if (!strcmp(words[0], "exit")) {

      char *endptr = "\0";
      int value = 0;
      if (nwords > 1) {
      value = strtol(words[1], &endptr, 10);
      }
      if (*endptr != '\0') {
        fprintf(stderr, "Exit Error\n"); // should program end here? or just error to be thrown?
        return -1;
      }
      else {
      exit(value);
      }
    }
      execvp(words[0], words);
      return errno;
    }

    else {

      /* waiting */
      if (strcmp(words[nwords -1], "&") == 0) {
        bg_child_pid = pid;
        pid_list[pid_idx++] = pid;
        
      }
      else {
        int status = 0;
        waitpid(pid, &status, WUNTRACED);
        if (WIFEXITED(status)) { // exits normally
          exit_status = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status)) {
          exit_status = WTERMSIG(status) + 128;
        }
        else if (WIFSTOPPED(status))
        {
          fprintf(stderr, "Child process %d stopped. Continuing.\n", pid);
          kill(pid, SIGCONT);
          bg_child_pid = pid;
          pid_list[pid_idx++] = pid;
        }
        
      }
    }

    prompt:;
    for (int i = 0; i < nwords; i++) {
      free(words[i]);
      words[i] = NULL;
    }
  }

}

char *words[MAX_WORDS] = {0};


/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */

size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}

/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }

  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
  char const *pos = word;
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') {
      char bg_pid[32];
      sprintf(bg_pid, "%d", bg_child_pid);
      build_str(bg_pid, NULL);
    }
    else if (c == '$') {
      char pid[32];
      sprintf(pid, "%d", getpid()); 
      build_str(pid, NULL);
    
    }
    else if (c == '?') {
      char status_arr[32];
      sprintf(status_arr, "%d", exit_status);
      build_str(status_arr, NULL);
    }
    else if (c == '{') {
      char new_arr[strlen(word) - 3];
      int end = 2;
      while (word[end] != '}') {
          new_arr[end - 2] = word[end];
          ++end;
      }
      new_arr[end - 2]  = '\0';

      build_str(getenv(new_arr), NULL);
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}
