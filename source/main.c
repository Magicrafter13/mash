#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <inttypes.h>

const char delim_char = ' ';

int main(int argc, char * argv[]) {
	// Initialize environment variables
	// TODO

	// User prompt (main loop)
	// TODO make a struct for a command - can have an array of command history to call back on!
	int cmd_type;
	size_t cmd_length, cmd_true_length = 0;
	char * cmd_buf;
	int cmd_argc;
	char ** cmd_argv;

	pid_t cmd_pid;
	int cmd_stat;
	uint8_t cmd_exit;

	for (;;) {
		// Present prompt and read command
		fprintf(stdout, "$ ");
		if ((cmd_length = getline(&cmd_buf, &cmd_true_length, stdin)) == -1) {
			if (errno) {
				fprintf(stderr, "%m\n");
				int err = errno;
				free(cmd_buf);
				return err;
			}
			break;
		}

		// Parse input
		if (cmd_buf[cmd_length - 1] == '\n') // If final character is a new line, replace it with a null terminator
			cmd_buf[cmd_length-- - 1] = '\0';
		if (cmd_buf[0] == '\0')              // Blank input, just ignore and print another prompt.
			continue;
		cmd_argc = 1;
		for (size_t i = 0; i < cmd_length; i++)
			if (cmd_buf[i] == delim_char)
				cmd_argc++;
		cmd_argv = calloc(cmd_argc + 1, sizeof(char*));
		cmd_argv[0] = strtok(cmd_buf, &delim_char);
		for (size_t t = 1; t < cmd_argc; t++)
			cmd_argv[t] = strtok(NULL, &delim_char);
		cmd_argv[cmd_argc] = NULL;

		// Execute command
		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			/*fprintf(stdout, "Going to execute:\n\033[1;31m0\033[0m%s", cmd_argv[0]);
			for (size_t t = 1; t < cmd_argc; t++)
				fprintf(stdout, " \033[1;31m%lu\033[0m%s", t, cmd_argv[t]);
			fprintf(stdout, "\033[1;31m<LF>\033[0m\n");*/
			execv(cmd_argv[0], cmd_argv);
			fprintf(stderr, "%m\n");
			exit(errno);
		}
		// While the main process waits for the child to exit
		else {
			waitpid(cmd_pid, &cmd_stat, 0);
			cmd_exit = WEXITSTATUS(cmd_stat);
			fprintf(stdout, "Command exited with %" PRIu8 ".\n", cmd_exit);
		}

		free(cmd_argv);
		free(cmd_buf);
	}

	return 0;
}
