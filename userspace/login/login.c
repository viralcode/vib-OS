/*
 * vib-OS - Login Program
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_USER_LEN 32
#define MAX_PASS_LEN 32

/* Simple hardcoded credentials for now */
struct user_cred {
  const char *username;
  const char *password;
  uid_t uid;
  gid_t gid;
  const char *home;
  const char *shell;
};

static struct user_cred users[] = {
    {"root", "secret", 0, 0, "/root", "/bin/sh"},
    {"guest", "", 1000, 1000, "/home/guest", "/bin/sh"},
    {NULL, NULL, 0, 0, NULL, NULL}};

static void get_password(char *buf, size_t len) {
  /* For now, just read with echo enabled since we lack termios */
  printf("(visible) ");
  fgets(buf, len, stdin);

  /* Remove newline */
  size_t slen = strlen(buf);
  if (slen > 0 && buf[slen - 1] == '\n') {
    buf[slen - 1] = '\0';
  }
}

static struct user_cred *authenticate(const char *user, const char *pass) {
  for (int i = 0; users[i].username; i++) {
    if (strcmp(user, users[i].username) == 0) {
      /* Empty password for guest */
      if (strlen(users[i].password) == 0)
        return &users[i];

      if (strcmp(pass, users[i].password) == 0) {
        return &users[i];
      }
    }
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  char username[MAX_USER_LEN];
  char password[MAX_PASS_LEN];
  struct user_cred *cred = NULL;

  (void)argc;
  (void)argv;

  /* Clear screen */
  printf("\033[2J\033[H");

  printf("Welcome to vib-OS v0.5.0\n");
  printf("Kernel 0.5.0-arm64 on an aarch64\n\n");

  while (1) {
    printf("vib-os login: ");
    fflush(stdout);

    if (fgets(username, sizeof(username), stdin) == NULL) {
      continue;
    }

    size_t len = strlen(username);
    if (len > 0 && username[len - 1] == '\n') {
      username[len - 1] = '\0';
    }

    if (strlen(username) == 0)
      continue;

    printf("Password: ");
    fflush(stdout);
    get_password(password, sizeof(password));

    cred = authenticate(username, password);

    if (cred) {
      printf("\nLogin successful.\n");
      printf("Last login: Mon Jan 20 2026 on tty1\n\n");

      /* Set UID/GID (Stub - need syscalls) */
      // setgid(cred->gid);
      // setuid(cred->uid);

      /* Set env */
      // setenv not in minimal libc, use putenv if available or skip
      // setenv("HOME", cred->home, 1);

      chdir(cred->home);

      /* Execute shell */
      char *shell_argv[] = {(char *)cred->shell, "-l", NULL};

      // execv(cred->shell, shell_argv); --> use execve
      char *envp[] = {"HOME=/root", "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
                      "SHELL=/bin/sh", "TERM=linux", NULL};
      execve(cred->shell, shell_argv, envp);

      printf("Failed to execute shell: %s\n", cred->shell);
      exit(1);
    } else {
      printf("\nLogin incorrect\n\n");
      sleep(2);
    }
  }

  return 0;
}
