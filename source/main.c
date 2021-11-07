#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <inttypes.h>

extern char ** environ;

char * const delim_char = " ";

int main(int argc, char * argv[]) {
	// Initialize
	const uid_t UID = getuid();
	struct passwd * PASSWD = getpwuid(UID);
	char * config_file, * temp = getenv("XDG_CONFIG_HOME");
	if (temp == NULL) {
		size_t len = strlen(PASSWD->pw_dir);
		config_file = calloc(len + 32, sizeof (char));
		strcpy(config_file, PASSWD->pw_dir);
		strcpy(&config_file[len], "/.local/config/mash/config.mash");
	}
	else {
		size_t len = strlen(temp);
		config_file = calloc(len + 17, sizeof (char));
		strcpy(config_file, temp);
		strcpy(&config_file[len], "/mash/config.mash");
	}
	int source_config = access(config_file, R_OK) == 0;

	// User prompt (main loop)
	// TODO make a struct for a command - can have an array of command history to call back on!
	int cmd_type;
	size_t cmd_length, cmd_true_length = 0;
	char * cmd_buf = NULL;
	int cmd_argc;
	char ** cmd_argv;

	pid_t cmd_pid;
	int cmd_stat;
	uint8_t cmd_exit;

	FILE * input_source = source_config ? fopen(config_file, "r") : stdin;
	for (;;) {
		// Present prompt and read command
		if (!source_config)
			fprintf(stdout, "$ ");
		if ((cmd_length = getline(&cmd_buf, &cmd_true_length, input_source)) == -1) {
			int err = errno;
			if (err > 11) {
				free(cmd_buf);
				fprintf(stderr, "%s\n", strerror(err));
				return err;
			}
			if (source_config) {
				source_config = 0;
				input_source = stdin;
				continue;
			}
			break;
		}

		// Parse input
		if (cmd_buf[0] == '\n')              // Blank input, just ignore and print another prompt.
			continue;
		if (cmd_buf[cmd_length - 1] == '\n') // If final character is a new line, replace it with a null terminator
			cmd_buf[cmd_length-- - 1] = '\0';
		cmd_argc = 1;
		for (size_t i = 0; i < cmd_length; i++)
			if (cmd_buf[i] == delim_char[0])
				cmd_argc++;
		cmd_argv = calloc(cmd_argc + 1, sizeof (char*));
		cmd_argv[0] = strtok(cmd_buf, delim_char);
		for (size_t t = 1; t < cmd_argc; t++)
			cmd_argv[t] = strtok(NULL, delim_char);
		cmd_argv[cmd_argc] = NULL;

		// Execute command
		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// TODO consider manual search of the path
			execvp(cmd_argv[0], cmd_argv);
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
	}

	if (cmd_buf != NULL)
		free(cmd_buf);

	free(config_file);

	fprintf(stdout, "\n");

	return 0;
}
