# mini-shell

A minimal Unix shell written in C, implemented from scratch using only
`fork()`, `execvp()`, `pipe()`, `dup2()`, and `open()` — no libc string
functions or stdio buffering.

## Features
- Command execution via `fork` + `execvp`
- I/O redirection: `<`, `>`, `>>`
- Pipelines: `cmd1 | cmd2 | cmd3 | ...`
- Built-in `exit`

## Build
```
gcc -Wall -Wextra -o myshell myshell.c
```

## Run
```
./myshell
```

## Example
```
myshell> echo hello | tr a-z A-Z > out.txt
```

## Notes
This shell relies on POSIX-only headers (`unistd.h`, `fcntl.h`, `sys/wait.h`)
and system calls like `fork()`, so it must be built and run on Linux, macOS,
or WSL — it will not compile with a native Windows compiler.
