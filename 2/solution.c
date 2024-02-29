#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

static void execute_command(const struct expr *e, int in_fd, int out_fd);

void execute_single_command(const struct command *cmd, int in_fd, int out_fd) { 
    pid_t pid = fork(); 
    if (pid == 0) { 
        if (in_fd != STDIN_FILENO) { 
            if (dup2(in_fd, STDIN_FILENO) < 0) { 
                fprintf(stderr, "dup2() failed\n"); 
                exit(EXIT_FAILURE); 
            } 
            close(in_fd); 
        } 
        if (out_fd != STDOUT_FILENO) { 
            if (dup2(out_fd, STDOUT_FILENO) < 0) { 
                fprintf(stderr, "dup2() failed\n"); 
                exit(EXIT_FAILURE); 
            } 
            close(out_fd); 
        } 

        char** exec_args = malloc((cmd->arg_count + 2) * sizeof(char*)); 
        exec_args[0] = strdup(cmd->exe); 
        for (uint32_t i = 0; i < cmd->arg_count; i++) { 
            exec_args[i + 1] = strdup(cmd->args[i]); 
        } 
        exec_args[cmd->arg_count + 1] = NULL; 
        execvp(exec_args[0], exec_args); 
        fprintf(stderr, "execvp() failed\n"); 
        exit(EXIT_FAILURE); 

    } else { 
        wait(NULL); 
    } 
} 


static void execute_piped_commands(const struct expr *e, int in_fd, int out_fd) {
    int pipe_fds[2];
    pid_t pid;
    if(pipe(pipe_fds) < 0) {
        perror("pipe() failed");
        return;
    }
    if ((pid = fork()) == 0) {
        close(pipe_fds[0]);
        dup2(in_fd, STDIN_FILENO);
        if (out_fd != STDOUT_FILENO) {
            dup2(pipe_fds[1], STDOUT_FILENO);
        }
        execute_command(e, STDIN_FILENO, STDOUT_FILENO);
        close(pipe_fds[1]);
        _exit(EXIT_SUCCESS);
    }
    close(pipe_fds[1]);
    wait(NULL);
    if (out_fd != STDOUT_FILENO) {
        execute_piped_commands(e->next, pipe_fds[0], out_fd);
    } else {
        execute_piped_commands(e->next, pipe_fds[0], STDOUT_FILENO);
    }
    close(pipe_fds[0]);
}


static void execute_command(const struct expr *e, int in_fd, int out_fd) {
    if (e->type == EXPR_TYPE_COMMAND) {
		// printf("\tCommand: %s", e->cmd.exe);
		// for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
		// 	printf(" %s", e->cmd.args[i]);
		// printf("\n");
		if (strcmp(e->cmd.exe, "exit") == 0 && e->next == NULL) {
			exit(EXIT_SUCCESS);
		}
		
		if (strcmp(e->cmd.exe, "cd") == 0) {
				printf("%s", *e->cmd.args);
				chdir(*e->cmd.args);
				e = e->next;
		}
        execute_single_command(&(e->cmd), in_fd, out_fd);

    } else if (e->type == EXPR_TYPE_PIPE) {
        execute_piped_commands(e, in_fd, out_fd);

    } else {
        fprintf(stderr, "Unknown expr type\n");
        _exit(EXIT_FAILURE);
    }
}


static void execute_command_line(const struct command_line *line) {
    if (line->head == NULL) {
        return;
    }

    int in_fd = STDIN_FILENO;
    int out_fd = STDOUT_FILENO;

    const struct expr *e = line->head;

	if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
		out_fd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		out_fd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
	} 
	// printf("Expressions:\n");
		
	while (e != NULL) {
        if (e->next != NULL && e->next->type == EXPR_TYPE_PIPE) {
            int pipe_fds[2];
            if (pipe(pipe_fds) != 0) {
                fprintf(stderr, "pipe() failed\n");
                break;
            }
            execute_command(e, in_fd, pipe_fds[1]);
            close(pipe_fds[1]);
            in_fd = pipe_fds[0];
			
        } else {
            execute_command(e, in_fd, STDOUT_FILENO);
            break;
        }
        e = e->next;
    }

	if (line->out_type == OUTPUT_TYPE_FILE_NEW || line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		close(out_fd);
	}

}

int main() {
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}