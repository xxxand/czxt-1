#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 80
#define HISTORY_SIZE 10

static char history[HISTORY_SIZE][MAX_LINE + 1];
static int total = 0;

static void add_history(const char* cmd) {
    strcpy(history[total % HISTORY_SIZE], cmd);
    total++;
}

static const char* get_history(int n) {
    if (n < 1 || n > total || n <= total - HISTORY_SIZE) return NULL;
    return history[(n - 1) % HISTORY_SIZE];
}

static void print_history(void) {
    for (int n = total; n >= 1 && n > total - HISTORY_SIZE; n--)
        printf("%d %s\n", n, get_history(n));
}

static int parse(char* line, char* args[], int* background) {
    int argc = 0;
    char* tok = strtok(line, " \t\n");
    while (tok != NULL && argc < MAX_LINE / 2) {
        args[argc++] = tok;
        tok = strtok(NULL, " \t\n");
    }
    args[argc] = NULL;

    *background = 0;
    if (argc > 0) {
        size_t len = strlen(args[argc - 1]);
        if (args[argc - 1][len - 1] == '&') {
            *background = 1;
            if (len == 1)
                args[--argc] = NULL;
            else
                args[argc - 1][len - 1] = '\0';
        }
    }
    return argc;
}

static void reap_background(void) { while (waitpid(-1, NULL, WNOHANG) > 0); }

int main(void) {
    char* args[MAX_LINE / 2 + 1];
    int should_run = 1;
    char input[MAX_LINE + 2];
    char line[MAX_LINE + 1];

    while (should_run) {
        reap_background();
        printf("osh> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break; /* EOF */
        }
        if (strchr(input, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            printf("Command too long (at most %d characters).\n", MAX_LINE);
            continue;
        }
        input[strcspn(input, "\n")] = '\0';

        /* skip blank lines */
        if (input[strspn(input, " \t")] == '\0') continue;

        /* history retrieval: !! and !N */
        char* p = input + strspn(input, " \t");
        if (p[0] == '!') {
            const char* cmd = NULL;
            if (strcmp(p, "!!") == 0) {
                if (total == 0) {
                    printf("No commands in history.\n");
                    continue;
                }
                cmd = get_history(total);
            } else {
                char* end;
                errno = 0;
                long n = strtol(p + 1, &end, 10);
                if (end == p + 1 || *end != '\0' || errno != 0 || n > total ||
                    (cmd = get_history((int)n)) == NULL) {
                    printf("No such command in history.\n");
                    continue;
                }
            }
            strcpy(input, cmd);
            printf("%s\n", input);
        }

        add_history(input);

        strcpy(line, input);
        int background;
        int argc = parse(line, args, &background);
        if (argc == 0) continue;

        if (strcmp(args[0], "exit") == 0) {
            should_run = 0;
            continue;
        }
        if (strcmp(args[0], "history") == 0) {
            print_history();
            continue;
        }

        fflush(stdout);
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
        } else if (pid == 0) {
            execvp(args[0], args);
            perror(args[0]);
            _exit(127);
        } else if (background) {
            printf("[background process pid %d]\n", pid);
        } else {
            waitpid(pid, NULL, 0);
        }
    }
    return 0;
}
