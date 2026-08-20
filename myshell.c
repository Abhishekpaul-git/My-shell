#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_PIPES 10

int my_strlen(const char *str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int my_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void write_str(const char *str) {
    write(STDOUT_FILENO, str, my_strlen(str));
}

void write_int(int num) {
    char buffer[12];
    int i = 0;
    int is_negative = 0;
    
    if (num == 0) {
        write(STDOUT_FILENO, "0", 1);
        return;
    }
    
    if (num < 0) {
        is_negative = 1;
        num = -num;
    }
    
    while (num > 0) {
        buffer[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    if (is_negative) {
        buffer[i++] = '-';
    }
    
    for (int j = i - 1; j >= 0; j--) {
        write(STDOUT_FILENO, &buffer[j], 1);
    }
}

// Tokenize the input command string into words
// Returns the number of words in args[].
int parse_command(char *cmd, char **args) {
    int argc = 0;
    char *token = cmd;
    int in_token = 0;
    
    while (*token != '\0' && *token != '\n') {
        if (*token == ' ' || *token == '\t') {
            if (in_token) {
                *token = '\0';
                in_token = 0;
            }
        } else {
            if (!in_token) {
                args[argc++] = token;
                in_token = 1;
            }
        }
        token++;
    }
    
    *token = '\0';
    args[argc] = NULL;
    return argc;
}

// A structure Command that captures info. 
// such as input and output redirection, etc.
typedef struct {
    char *args[MAX_ARGS];  // command to exec()
    int argc;              // args.len()
    char *input_file;      // input redirection
    char *output_file;     // output redirection
    int append_output;     // write or append mode
} Command;

// Build Command by from args[i]
void build_command_from_args(char **args, int argc, Command *cmd) {
    int i, j;
    
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append_output = 0;
    
    for (i = 0, j = 0; i < argc; i++) {
        if (my_strcmp(args[i], "<") == 0) {
            /* Input redirection */
            if (i + 1 < argc) {
                cmd->input_file = args[i + 1];
                i++;
            }
        } else if (my_strcmp(args[i], ">") == 0) {
            /* Output redirection */
            if (i + 1 < argc) {
                cmd->output_file = args[i + 1];
                cmd->append_output = 0;
                i++;
            }
        } else if (my_strcmp(args[i], ">>") == 0) {
            /* Append output redirection */
            if (i + 1 < argc) {
                cmd->output_file = args[i + 1];
                cmd->append_output = 1;
                i++;
            }
        } else {
            /* Regular argument */
            cmd->args[j++] = args[i];
            cmd->argc++;
        }
    }
    cmd->args[j] = NULL;
}

// set up I/O redirections
void setup_redirections(Command *cmd) {

    if (cmd->input_file != NULL) {
        int in_fd = open(cmd->input_file, O_RDONLY);
        if (in_fd < 0) {
            write_str("Error: cannot open input file\n");
            _exit(1);
        }
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }

    if (cmd->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->append_output ? O_APPEND : O_TRUNC;
        int out_fd = open(cmd->output_file, flags, 0644);
        if (out_fd < 0) {
            write_str("Error: cannot open output file\n");
            _exit(1);
        }
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }
}

// Find and remember positions of the pipe (|) character in args
int split_args_by_pipe(char **args, int argc, int *pipe_positions) {
    int i;
    int num_pipes = 0;
    
    for (i = 0; i < argc; i++) {
        if (my_strcmp(args[i], "|") == 0) {
            pipe_positions[num_pipes++] = i;
        }
    }
    
    return num_pipes;
}

// execute commands (possibly a pipeline of individual commands)
// args[i] are words in input command, and argc is the number of words.
void execute_cmds(char **args, int argc) {
    int pipe_positions[MAX_PIPES];
    int num_pipes;
    int num_cmds;
    int pipefds[MAX_PIPES][2];
    pid_t pid;
    int status;
    Command cmd;
    int i, j;
    int cmd_start, cmd_end;
    
    // find all pipe (|) positions and remember them 
    num_pipes = split_args_by_pipe(args, argc, pipe_positions);
    num_cmds = num_pipes + 1;
    
    // create all pipes (will be executed by the shell process)
    for (i = 0; i < num_pipes; i++) {
        if (pipe(pipefds[i]) < 0) {
            write_str("Error: pipe creation failed\n");
            return;
        }
    }
    
    // execute the pipeline (process each individual cmd, 
    // set up redirections, and execute cmd)
    for (i = 0; i < num_cmds; i++) {

        // determine the start and end of the current cmd
        cmd_start = (i == 0) ? 0 : pipe_positions[i - 1] + 1;
        cmd_end = (i == num_cmds - 1) ? argc : pipe_positions[i];

        // build struct Command for the current command (will
        // be used by the child to set up redirections)
        build_command_from_args(&args[cmd_start], cmd_end - cmd_start, &cmd);

        // fork the child process
        pid = fork();

        if (pid < 0) {
            write_str("Error: fork failed\n");
            continue;
        }

        if (pid == 0) {
            // Inside the child process:

            // set up the pipe redirections appropriately
            if (i > 0) {
                // not the first command: read from previous pipe
                dup2(pipefds[i - 1][0], STDIN_FILENO);
            }
            if (i < num_cmds - 1) {
                // not the last command: write to next pipe
                dup2(pipefds[i][1], STDOUT_FILENO);
            }

            // close the pipefds inherited from the parent
            for (j = 0; j < num_pipes; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }

            // set up input-output file redirections for the current
            // command (see fields of struct Command)
            setup_redirections(&cmd);

            // exec() the child command
            execvp(cmd.args[0], cmd.args);

            // actions to do if exec() fails
            write_str("Error: exec failed for command\n");
            _exit(1);
        }
        // parent continues the loop to fork the next command
    }
    
    // (myshell process has finished launching all commands)
    // myshell closes all pipefds (in preparation for the next prompt)
    for (i = 0; i < num_pipes; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
    
    // wait for all children to finish before printing the prompt
    for (i = 0; i < num_cmds; i++) {
        wait(&status);
    }
   
    // print the return code of the last child to return
    if (num_pipes > 0) {
        write_str("(Pipeline) exit status: ");
    } else {
        write_str("(Child) exit status: ");
    }
    write_int(status);
    write_str("\n");
}

int main() {
    char command[MAX_CMD_LEN];
    char *args[MAX_ARGS];
    ssize_t bytes_read;
    int argc;
    
    while (1) {

        write_str("myshell> ");

        bytes_read = read(STDIN_FILENO, command, MAX_CMD_LEN - 1);
        if (bytes_read <= 0) {
            write_str("\n");
            break;
        }
        
        // sanitize the input string
        command[bytes_read] = '\0';
        if (command[bytes_read - 1] == '\n') {
            command[bytes_read - 1] = '\0';
        }
        if (command[0] == '\0') {
            continue;
        }
        
        // tokenize the input command for easier manipulation,
        // argc holds the number of words in the input command.
        argc = parse_command(command, args);
        if (argc == 0) {
            continue;
        }
        
        // does the user want to exit the shell?
        if (my_strcmp(args[0], "exit") == 0) {
            write_str("Exiting shell...\n");
            break;
        }
        
        // execute commands
        execute_cmds(args, argc);
    }
    
    return 0;
}