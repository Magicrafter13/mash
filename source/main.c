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

#include "command.h"

extern char ** environ;

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
	Command cmd = commandInit();

	pid_t cmd_pid;
	int cmd_stat;
	uint8_t cmd_exit;

	FILE * input_source = source_config ? fopen(config_file, "r") : stdin;
	for (;;) {
		// Present prompt and read command
		if (!source_config)
			fprintf(stdout, "$ ");
		if (commandRead(&cmd, input_source) == -1) {
			if (errno > 11) {
				int err = errno;
				free(cmd.c_buf);
				fprintf(stderr, "%s\n", strerror(err));
				return err;
			}
			// EOF
			if (source_config) {
				source_config = 0;
				input_source = stdin;
				continue;
			}
			break;
		}

		// Execute command
		if (cmd.c_type == CMD_EMPTY)
			continue;

		cmd_pid = fork();
		// Forked process will execute the command
		if (cmd_pid == 0) {
			// TODO consider manual search of the path
			execvp(cmd.c_argv[0], cmd.c_argv);
			fprintf(stderr, "%m\n");
			exit(errno);
		}
		// While the main process waits for the child to exit
		else {
			waitpid(cmd_pid, &cmd_stat, 0);
			cmd_exit = WEXITSTATUS(cmd_stat);
			fprintf(stdout, "Command exited with %" PRIu8 ".\n", cmd_exit);
		}

		free(cmd.c_argv);
	}

	if (cmd.c_buf != NULL)
		free(cmd.c_buf);

	free(config_file);

	fprintf(stdout, "\n");

	return 0;
}
