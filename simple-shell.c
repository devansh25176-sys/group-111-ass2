#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_LINE     1024
#define MAX_ARGS     128
#define MAX_STAGES   32
#define MAX_HISTORY  1000

// a struct for storing info about command entered, pid, execution time etc
typedef struct {
    char   cmd[MAX_LINE];
    pid_t  pids[MAX_STAGES];
    int    npids;
    time_t start_time;
    double duration;
} HistoryEntry;

static HistoryEntry history[MAX_HISTORY];
static int history_count = 0;
static volatile sig_atomic_t got_sigint = 0;

static void my_handler(int signum)
{
    if (signum == SIGINT) {
        got_sigint = 1;
        ssize_t r = write(STDOUT_FILENO, "\n", 1);
        (void)r;
    }
}

static int install_signal_handler(void)
{
    struct sigaction sig;
    memset(&sig, 0, sizeof(sig));
    sig.sa_handler = my_handler;
    return sigaction(SIGINT, &sig, NULL);
}

static void show_history(void)
{
    for (int i = 0; i < history_count; i++)
        printf("%d  %s\n", i + 1, history[i].cmd);
}
//fxn that displays execution details
static void print_summary(void)
{
    printf("\n---------- Command execution details ----------\n");
    for (int i = 0; i < history_count; i++) {
        HistoryEntry *h = &history[i];
        char tbuf[64];
        struct tm tmv;
        localtime_r(&h->start_time, &tmv);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);

        printf("%d. %s\n", i + 1, h->cmd);
        printf("     pid(s)   : ");
        for (int j = 0; j < h->npids; j++)
            printf("%d%s", (int)h->pids[j], j + 1 < h->npids ? ", " : "");
        printf("\n     started  : %s\n", tbuf);
        printf("     duration : %.6f seconds\n", h->duration);
    }
    printf("------------------------------------------------\n");
}
// split the commands and store them in array
static int parse_args(char *segment, char *argv[])
{
    int argc = 0;
    char *save = NULL;
    char *tok = strtok_r(segment, " \t\r\n", &save);
    while (tok != NULL && argc < MAX_ARGS - 1) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t\r\n", &save);
    }
    argv[argc] = NULL;
    return argc;
}
// fxm that executes the commands and store all details
static int create_process_and_run(char *command)
{
    HistoryEntry *entry = &history[history_count - 1];

    char line[MAX_LINE];
    strncpy(line, command, MAX_LINE - 1);
    line[MAX_LINE - 1] = '\0';

    char *segments[MAX_STAGES];
    int nstages = 0;
    for (char *p = line; ; ) {
        char *bar = strchr(p, '|');
        if (bar) *bar = '\0';
        if (nstages >= MAX_STAGES) {
            fprintf(stderr, "simple-shell: too many piped commands (max %d)\n",
                    MAX_STAGES);
            return 1;
        }
        segments[nstages++] = p;
        if (!bar) break;
        p = bar + 1;
    }

    char *args[MAX_STAGES][MAX_ARGS];
    for (int i = 0; i < nstages; i++) {
        if (parse_args(segments[i], args[i]) == 0) {
            fprintf(stderr, "simple-shell: syntax error near '|'\n");
            return 1;
        }
    }

    int nfds = 2 * (nstages - 1);
    int fd[2 * (MAX_STAGES - 1)];
    for (int i = 0; i < nstages - 1; i++) {
        if (pipe(&fd[2 * i]) < 0) {
            perror("simple-shell: pipe");
            for (int j = 0; j < 2 * i; j++)
                close(fd[j]);
            return 0;
        }
    }

    int status = 1;
    for (int i = 0; i < nstages; i++) {
        fflush(stdout);
        pid_t pid = fork();
        if (pid < 0) {
            perror("simple-shell: fork");
            status = 0;
            break;
        } else if (pid == 0) {
            if (i > 0 && dup2(fd[2 * (i - 1)], STDIN_FILENO) < 0) {
                perror("simple-shell: dup2");
                _exit(1);
            }
            if (i < nstages - 1 && dup2(fd[2 * i + 1], STDOUT_FILENO) < 0) {
                perror("simple-shell: dup2");
                _exit(1);
            }
            for (int j = 0; j < nfds; j++)
                close(fd[j]);

            /* history also works as a pipeline stage: history | grep ls */
            if (strcmp(args[i][0], "history") == 0) {
                show_history();
                fflush(stdout);
                _exit(0);
            }
            signal(SIGTSTP, SIG_DFL);
            execvp(args[i][0], args[i]);
            fprintf(stderr, "simple-shell: %s: %s\n", args[i][0],
                    strerror(errno));
            _exit(127);
        } else {
            entry->pids[entry->npids++] = pid;
        }
    }

    for (int j = 0; j < nfds; j++)
        close(fd[j]);

    for (int i = 0; i < entry->npids; i++) {
        int ret;
        pid_t pid;
        while ((pid = waitpid(entry->pids[i], &ret, 0)) < 0 && errno == EINTR)
            ;
        if (pid < 0) {
            perror("simple-shell: waitpid");
            continue;
        }
        if (!WIFEXITED(ret)) {
            int sig = WTERMSIG(ret);
            if (sig != SIGINT && sig != SIGPIPE)
                fprintf(stderr, "Abnormal termination of %d\n", (int)pid);
        }
    }
    return status;
}
// fxn that stores the command in history and execute it
static int launch(char *command)
{
    int status;
    if (strcmp(command, "history") == 0) {
        history[history_count - 1].pids[history[history_count - 1].npids++] =
            getpid();
        show_history();
        status = 1;
    } else {
        status = create_process_and_run(command);
    }
    return status;
}
//read user input
static char *read_user_input(void)
{
    static char buf[MAX_LINE];

    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        if (!got_sigint && !feof(stdin) && errno != EINTR)
            perror("simple-shell: read");
        return NULL;
    }

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    } else if (len == MAX_LINE - 1) {
        fprintf(stderr, "simple-shell: command too long\n");
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
        buf[0] = '\0';
    }
    return buf;
}
//main shell loop
static void shell_loop(void)
{
    int status = 1;
    do {
        printf("simple-shell> ");
        fflush(stdout);

        char *command = read_user_input();
        if (command == NULL || got_sigint) {
            status = 0;
            continue;
        }

        char *p = command;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            continue;
        for (size_t n = strlen(p); n > 0 && (p[n-1] == ' ' || p[n-1] == '\t' || p[n-1] == '\r'); n--)
            p[n-1] = '\0';

        if (history_count >= MAX_HISTORY) {
            fprintf(stderr, "simple-shell: history full, exiting\n");
            status = 0;
            continue;
        }

        HistoryEntry *entry = &history[history_count++];
        memset(entry, 0, sizeof(*entry));
        snprintf(entry->cmd, MAX_LINE, "%s", p);
        entry->start_time = time(NULL);

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        status = launch(entry->cmd);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (got_sigint)                 
            status = 0;

        entry->duration = (double)(t1.tv_sec - t0.tv_sec) +
                          (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    } while (status);
}

int main(void)
{
    if (install_signal_handler() < 0) {
        perror("simple-shell: sigaction");
        return EXIT_FAILURE;
    }
    signal(SIGTSTP, SIG_IGN);
    shell_loop();
    print_summary();
    return EXIT_SUCCESS;
}