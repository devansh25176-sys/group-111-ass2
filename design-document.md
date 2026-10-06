# SimpleShell: Design Document

**Course:** CSE231: Operating Systems
**Assignment:** SimpleShell
**Group ID:** 111

| Member | Roll Number |
|---|---|
| Devansh Gurawa | 2025176 |
| Arnav Taxak | 2025100 |

**Private GitHub repository:** `https://github.com/devansh25176-sys/group-111-ass2`

---

## 1. Contribution of Each Member

The work was shared across design, implementation, testing, and documentation.

Members and contribution:-
Devansh Gurawa (2025176) - create process and run, testing, and documentation
Arnav Taxak (2025100) - shell loop,main and launch

---

## 2. Overview

SimpleShell is a Unix shell written in C (`simple-shell.c`). It prints a prompt, reads a command, executes it, and repeats until the user terminates it with Ctrl-C or Ctrl-D at end of input. On termination, it prints execution details for every command entered during that session.

```
shell_loop():
    do {
        print prompt
        command = read_user_input()
        status  = launch(command)
    } while (status)
```

### Supported features

- Any external command found on `PATH`, with arguments separated by whitespace (e.g. `ls -l`, `wc -l fib.c`, `grep printf helloworld.c`, `./fib 40`).
- Pipelines of any number of commands, up to 32 (e.g. `cat helloworld.c | grep print | wc -l`).
- The `history` command, which also works as a pipeline stage (`history | grep ls`).
- The `cd` builtin (`cd`, `cd <dir>`).
- An execution summary on exit: command, pid(s), start time, and duration.

---

## 3. Implementation Details

### 3.1 Main loop and input (`shell_loop`, `read_user_input`)

- The prompt is `simple-shell> `. It is flushed with `fflush(stdout)` so it appears before the shell blocks on input.
- Input is read with `fgets` into a 1024-byte buffer. The trailing newline is stripped.
- A line longer than the buffer is rejected with a "command too long" message, and the rest of that line is discarded so it is not misread as the next command.
- Leading and trailing whitespace is trimmed. Empty lines are ignored and are not added to history.
- EOF (Ctrl-D) or Ctrl-C ends the loop.

### 3.2 History and the exit summary

Every non-empty command is recorded in a fixed array of `HistoryEntry` structs:

```c
typedef struct {
    char   cmd[MAX_LINE];     // the command exactly as typed
    pid_t  pids[MAX_STAGES];  // pid of every process that ran it
    int    npids;
    time_t start_time;        // wall-clock time, from time()
    double duration;          // seconds, from clock_gettime()
} HistoryEntry;
```

- The start time uses `time()` so it can be printed as a readable date and time with `localtime_r` and `strftime`.
- The duration uses `clock_gettime(CLOCK_MONOTONIC)` taken just before and after `launch()`. A monotonic clock is not affected by system clock changes. The measured duration includes waiting for all children to finish.
- Only the current session's commands are stored, as the assignment requires.
- When the shell terminates, `print_summary()` prints the command, pid(s), start time, and duration for each entry.
- If the history array fills up (1000 commands), the shell prints a message and exits normally, showing the summary.

### 3.3 Launching commands (`launch`, `create_process_and_run`)

`launch()` decides how to run a command:
1. If it is `cd` (with or without arguments, and no pipe), run the `cd` builtin inside the shell process.
2. If it is exactly `history`, print the history from inside the shell process.
3. Otherwise, call `create_process_and_run()`.

For `cd` and `history` run directly, the recorded pid is the shell's own pid, since no child is created.

`create_process_and_run()` does the following:

1. **Split into stages.** The line is copied and split on `|` into at most 32 segments. Each segment is tokenised on whitespace with `strtok_r` (at most 127 arguments). An empty segment is reported as a syntax error.
2. **Create pipes.** For N stages, N-1 pipes are created with `pipe()`. They are stored in one array: `fd[2*i]` is the read end and `fd[2*i+1]` is the write end of pipe `i`.
3. **Fork one child per stage.** `fflush(stdout)` is called before each `fork()` so buffered output is not duplicated into the children. In each child:
   - if not the first stage, `dup2` connects stdin to the previous pipe's read end;
   - if not the last stage, `dup2` connects stdout to the next pipe's write end;
   - all pipe file descriptors are closed;
   - `execvp` replaces the child with the requested program (the lecture's exec step). If `execvp` fails, an error with `strerror(errno)` is printed and the child exits with status 127.
   - A `history` stage prints the history and exits instead of calling `execvp`.
4. **Parent closes its copies of all pipe fds.** This is essential: a reader such as `wc -l` only sees end-of-file once every write end is closed.
5. **Parent waits for every child** with `waitpid` (the lecture's "good parenting"), so no zombies are left. The wait is retried if it is interrupted by a signal (`EINTR`). If a child did not exit normally (killed by a signal other than SIGINT or SIGPIPE), an "Abnormal termination" message with its pid is printed.

### 3.4 The `cd` builtin

`cd` is handled by the shell itself rather than a child. A forked child only changes its own working directory, so running `cd` in a child would have no effect on the shell. With no argument, `cd` goes to `$HOME`. Too many arguments, a missing `HOME`, and `chdir` failures are reported with a message.

### 3.5 Signal handling

- A SIGINT handler is installed with `sigaction` (as in Lecture 08). It sets a `volatile sig_atomic_t` flag and writes a newline using `write()`, an async-signal-safe function (`printf` is not safe inside a handler).
- The handler is installed without `SA_RESTART`, so Ctrl-C interrupts a blocking `fgets` or `waitpid`, and the shell can terminate and print its summary.
- Ctrl-C reaches the foreground child as well. Exec resets the handler to its default, so the child terminates normally. Killed-by-SIGINT children are not reported as abnormal.
- SIGPIPE terminations are ignored in the report, since a pipeline stage ending early (e.g. `head`) is normal.
- SIGTSTP (Ctrl-Z) is ignored by the shell itself so the shell is not suspended, and restored to the default in children.

### 3.6 Error checking

Every system call that can fail is checked: `fork`, `pipe`, `dup2`, `execvp`, `waitpid`, `chdir`, `sigaction`, and input reading. Failures print a descriptive message using `perror` or `strerror(errno)`. If `fork` fails partway through a pipeline, the shell stops forking, closes all pipe fds, and still waits for the children it already created.

---

## 4. Limitations

SimpleShell supports commands separated by whitespace, with pipes, `cd`, and `history`. The following are not supported, with the reasons:

| Not supported | Example | Reason |
|---|---|---|
| Quotes and backslashes | `echo "a  b"`, `echo hello\ world` | The assignment explicitly excludes them. Arguments are split purely on whitespace, so quote characters would be passed to the program literally. |
| I/O redirection | `ls > out.txt`, `sort < file.txt`, `>>` | Requires parsing redirection operators and opening files with `open` + `dup2`. The assignment's scope is pipes only. `>` and `<` are passed to the program as ordinary arguments. |
| Background execution and job control | `./fib 40 &`, `fg`, `bg`, `jobs` | The shell waits for each command to finish before showing the next prompt. Job control needs process groups, terminal ownership (`tcsetpgrp`), and tracking stopped children, which is beyond the assignment. For the same reason, a command suspended with Ctrl-Z is not resumed by the shell. |
| Builtins other than `cd` and `history` | `export`, `alias`, `unset`, `source` | A builtin must change the shell's own state (environment, aliases), which a forked child cannot do. Only `cd` and `history` were required. There is no `exit` command; the shell terminates with Ctrl-C or Ctrl-D, as the assignment specifies. |
| `cd` inside a pipeline | `cd /tmp \| ls` | In a pipeline each stage runs in a separate child, so the directory change would not affect the shell. This matches the behavior of real shells, where it has no lasting effect. |
| `history` with arguments | `history 10` | Only the plain `history` command (alone or as a pipeline stage) is implemented. |
| Shell expansions and syntax | `ls *.c`, `echo $HOME`, `cd ~`, `a; b`, `a && b`, `$(cmd)` | Wildcards, variables, `~`, command separators, and substitution are interpreted by a shell's parser, not by the programs. They are not part of the specified whitespace-only command format, so these characters are passed to the program literally. |
| Very large inputs | Lines over 1023 characters, more than 32 pipeline stages, more than 127 arguments to one command, more than 1000 commands in a session | The shell uses fixed-size arrays. Each limit is checked and reported with an error. When the history array is full, the shell exits and shows its summary. |

---

## 5. Building and Testing

### Build

The shell and the two test programs are compiled with `gcc`:

```
gcc -Wall -Wextra -O2 -o simple-shell simple-shell.c
gcc -o fib fib.c
gcc -o helloworld helloworld.c
```

`fib` and `helloworld` must be in the same directory as `simple-shell`, because the shell is run from that directory and commands such as `./fib 40` are relative to it.

### Run

```
./simple-shell
```

### Automated test

`tests/commands.txt` contains the commands listed in the assignment, plus `cd` and `history` tests. Run it with:

```
./simple-shell < tests/commands.txt
```

### Test commands covered

| Command | What it checks |
|---|---|
| `ls`, `ls /home`, `ls -R`, `ls -l` | Basic commands with and without arguments |
| `echo you should be aware of the plagiarism policy` | Argument splitting |
| `wc -l fib.c`, `wc -c fib.c`, `grep printf helloworld.c`, `sort fib.c`, `uniq file.txt` | Common utilities |
| `./fib 40`, `./helloworld` | Running local executables |
| `cat fib.c \| wc -l` | Two-stage pipe |
| `cat helloworld.c \| grep print \| wc -l` | Three-stage pipe |
| `cd /tmp`, `cd` | The `cd` builtin |
| `history`, `history \| grep ls` | History, alone and in a pipeline |
| Ctrl-C / Ctrl-D | Termination and the execution summary |
