#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

int status[2] = {0}; // Exit value/signal #, exit method
int pids[512] = {-1}; // Save pids of bg processes
int foreground_mode = 0;

struct cmd_prompt {
    char* buffer;
    char* command;
    char* argv[513];
    char* input;
    char* output;
    int bg_process;
};

// Signal handler for ^Z
void catch_SIGTSTP(int signo) {
    // Switch between modes
    foreground_mode = ++foreground_mode % 2;

    if (foreground_mode) {
        // Print message enterting foreground mode
        char* on = "\nEntering foreground-mode only (& is now ignored)\n";
        write(STDOUT_FILENO, on, 50);
    }
    else {
        // Print message exiting foreground mode
        char* off = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, off, 30);
    }
}

// Initialize struct
struct cmd_prompt* cmd_prompt_init() {
    // Allocate memory
    struct cmd_prompt* prompt = malloc(sizeof(struct cmd_prompt));

    // Initialize to NULL / null terminator for array
    prompt->buffer = NULL;
    prompt->command = NULL;
    memset(prompt->argv, '\0', 513);
    prompt->input = NULL;
    prompt->output = NULL;
    prompt->bg_process = 0;

    return prompt;
}

struct cmd_prompt* parse_command(char cmd[]) {
    // Initialize struct
    struct cmd_prompt* prompt = cmd_prompt_init();

    // Allocate memory for command & duplicate
    prompt->buffer = strdup(cmd);

    char* temp;

    // Get command from buffer
    char* token = strtok_r(prompt->buffer, " ", &temp);
    prompt->command = token;

    int i = 0;

    // First arg is the command
    prompt->argv[i] = prompt->command;
    i++;

    // Get next token
    token = strtok_r(NULL, " ", &temp);

    while (token) {
        // Next token is the input file
        if (!strcmp(token, "<")) {
            token = strtok_r(NULL, " ", &temp);
            prompt->input = token;
        }
        // Next token is the output file
        else if (!strcmp(token, ">")) {
            token = strtok_r(NULL, " ", &temp);
            prompt->output = token;
        }
        // Command is a background process
        else if (!strcmp(token, "&")) {
            // Save pointer to &
            char* amp_temp = token;

            // Get next token
            token = strtok_r(NULL, " ", &temp);
            if (!token) { // If token is NULL, then background processs
                prompt->bg_process = 1;
                break;
            }
            else { // Otherwise, & is treated as an arg
                prompt->argv[i++] = amp_temp;
                continue;
            }
        }
        // If token is not <, >, or &, then it is an argument
        else
            prompt->argv[i++] = token;
        
        // Get next token
        token = strtok_r(NULL, " ", &temp);
    }
    return prompt; // Return the struct
}

void fork_child(struct cmd_prompt* cmd, struct sigaction* SIGINT_action, struct sigaction* SIGTSTP_action) {
    pid_t spawnpid = -2;
    int child_exit_method = 0;
    int target_fd, source_fd, result;
    char input_error[512], output_error[512], cmd_error[512];

    spawnpid = fork(); // Fork
    switch(spawnpid) {
        case -1:
            // Error in fork
            perror("Hull Breached!");
            exit(1);
            break;
        case 0:
            // Change interrupt signal to default action
            SIGINT_action->sa_handler = SIG_DFL;
            sigaction(SIGINT, SIGINT_action, NULL);

            // Ignore SIGTSTP
            SIGTSTP_action->sa_handler = SIG_IGN;
            sigaction(SIGTSTP, SIGTSTP_action, NULL);

            // In child, if there is an input file specified
            if (cmd->input) {
                // Open the input file
                source_fd = open(cmd->input, O_RDONLY, S_IRUSR | S_IWUSR);
                if (source_fd == -1) {
                    // Print error if file cannot be opened
                    sprintf(input_error, "bash: %s", cmd->input);
                    perror(input_error);
                    exit(1);
                }
                // Set stdin to file descriptor
                result = dup2(source_fd, 0);
                if (result == -1) {
                    perror("dup2()");
                    exit(1);
                }
            }
            // If there is an output file specified
            if (cmd->output) {
                // Open the output file
                target_fd = open(cmd->output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (target_fd == -1) {
                    // Print error if file cannot be opened
                    sprintf(output_error, "bash: %s", cmd->output);
                    perror(output_error);
                    exit(1);
                }
                // Set stdout to file descriptor
                result = dup2(target_fd, 1);
                if (result == -1) {
                    perror("dup2()");
                    exit(1);
                }
            }
            // No input specified & command is a background process
            if (!cmd->input && cmd->bg_process) {
                // stdin is set to /dev/null
                source_fd = open("/dev/null", O_RDONLY);
                dup2(source_fd, 0);
            }
            // No output specified & command is a background process
            if (!cmd->output && cmd->bg_process) {
                // stdout is set to /dev/null
                target_fd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                dup2(target_fd, 1);
            }
            // Execute the command
            execvp(cmd->command, cmd->argv);

            // Error and break if exec fails
            sprintf(cmd_error, "bash: %s", cmd->command);
            perror(cmd_error);
            exit(1);
            break;
        default:
            // Command is a background process & foreground mode is not on
            if (cmd->bg_process && !foreground_mode) {
                printf("background pid is %d\n", spawnpid); // Print background pid
                fflush(stdout);

                for (int i = 0; i < 512; i++) { // Add pid to array of bg processes
                    if (pids[i] == 0 || pids[i] == -1) {
                        pids[i] = spawnpid;
                        break;
                    }
                }
            }
            else
                // Wait if foreground
                waitpid(spawnpid, &child_exit_method, 0);

                if (WIFEXITED(child_exit_method)) {
                    status[0] = WEXITSTATUS(child_exit_method); // Exit method
                    status[1] = 0; // Exited normally
                }
                else {
                    status[0] = WTERMSIG(child_exit_method); // Signal
                    status[1] = 1; // Exited by signal

                    printf("terminated by signal %d\n", status[0]); // Print out signal that killed child
                    fflush(stdout);
                }
            break;
    }
}

void exec_cmd(struct cmd_prompt* cmd, struct sigaction* SIGINT_action, struct sigaction* SIGTSTP_action) {
    // Built in command cd
    if (!strcmp(cmd->command, "cd")) {
        // If there is an argument, chdir(arg)
        if (cmd->argv[1] != NULL)
            chdir(cmd->argv[1]);
        // Otherwise, chdir to HOME env var
        else {
            const char* name = "HOME";
            chdir(getenv(name));
        }
    }
    // Built in command status
    else if (!strcmp(cmd->command, "status")) {
        if (status[1]) // Terminated by signal
            printf("terminated by signal %d\n", status[0]);
        else // Terminated normally
            printf("exit value %d\n", status[0]);
        fflush(stdout);
    }
    // Built in command exit
    else if (!strcmp(cmd->command, "exit")) {
        for (int i = 0; i < 512; i++) { // Loop through bg processes
            if (pids[i] == -1)
                break;
            else if (pids[i]) // Kill all processes
                kill(pids[i], SIGKILL);
        }
        exit(0); // Terminate the shell
    }
    else // Not built-in, fork
        fork_child(cmd, SIGINT_action, SIGTSTP_action);
}

void var_expansion(char* prompt) {
    char pid[32];
    sprintf(pid, "%d", getpid()); // pid as string
    size_t pid_len = strlen(pid); // Number of characters in pid

    int insert = 0; // Where to start inserting
    int num = 0;
    char* temp = strdup(prompt); // Duplicate command prompt
    char* start = &temp[0];      // Store start of temp

    // Get pointer for first instance of "$$"
    char* ptr = strstr(temp, "$$");

    // Loop while strstr() is not NULL
    while (ptr) {
        // Set first $ in "$$" to null terminator
        *ptr = 0;

        // Modify prompt in place
        sprintf(prompt + insert, "%s%d%s", temp, getpid(), ptr + 2);

        // temp is moved by 2 to skip over "$$"
        temp = ptr + 2;
        // insert gets # of characters between start of string and $$ + pid_len's (account for getting rid of "$$")
        insert = (ptr - start) + (pid_len) + ((pid_len - 2) * num);

        // Find next instance of "$$"
        ptr = strstr(temp, "$$");

        num++;
    }
    // Free temp
    free(start);
    
    return;
}

void cmd_prompt(struct sigaction* SIGINT_action, struct sigaction* SIGTSTP_action) {
    char prompt[2048];
    memset(prompt, '\0', 2048);

    int pid = -1;
    int child_exit_method = 0;

    // Check if bg processes have terminated before prompt
    for (int i = 0; i < 512; i++) {
        if (pids[i] == -1)
            break;
        else if (pids[i]) {
            if (waitpid(pids[i], &child_exit_method, WNOHANG)) { // Child has terminated
                if (WIFEXITED(child_exit_method) != 0) // Exited normally
                    printf("background pid %d is done: exit value %d\n", pids[i], WEXITSTATUS(child_exit_method));
                else // Terminated by a signal
                    printf("background pid %d is done: terminated by signal %d\n", pids[i], WTERMSIG(child_exit_method));
                fflush(stdout);
                pids[i] = 0; // Set int in array to 0
            }
        }
    }

    // Colon prompt symbol
    printf(": ");
    fflush(stdout); // Flush stdout
    fgets(prompt, 256, stdin);

    // https://stackoverflow.com/a/28462221
    // Remove trailing new line from fgets() input
    prompt[strcspn(prompt, "\n")] = 0;

    // Prompt is not a comment / blank line / only spaces
    if (prompt[0] != '#' && strlen(prompt) != 0) {
        for (int i = 0; i < strlen(prompt); i++) {
            if (!isspace(prompt[i])) {
                var_expansion(prompt);
                // Parse the command & get resulting struct
                struct cmd_prompt* cmd_prompt = parse_command(prompt);
                exec_cmd(cmd_prompt, SIGINT_action, SIGTSTP_action); // Execute the command
                break;
            }
        }
    }
    return;
}

int main() {
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0}; // Signal handler structs

    SIGINT_action.sa_handler = SIG_IGN; // Ignore SIGINT
    sigaction(SIGINT, &SIGINT_action, NULL);

    SIGTSTP_action.sa_handler = catch_SIGTSTP; // Toggle foreground mode for SIGTSTP
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while (1) // Loop the command prompt
        cmd_prompt(&SIGINT_action, &SIGTSTP_action);

    return 0;
}