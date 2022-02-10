#include "mash.h"
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

FILE *open_config(struct passwd *PASSWD) {
	char *config_path;

	// Get user's config dir
	char *env_xdg = getenv("XDG_CONFIG_HOME");
	if (env_xdg == NULL) {
		char *env_home = getenv("HOME");
		if (env_home == NULL)
			env_home = PASSWD->pw_dir;
		config_path = calloc(strlen(env_home) + 26, sizeof (char));
		strcpy(config_path, env_home);
		strcat(config_path, "/.config");
	}
	else {
		config_path = calloc(strlen(env_xdg) + 18, sizeof (char));
		strcpy(config_path, env_xdg);
	}
	// Add mash dir
	strcat(config_path, "/mash/");

	// Check if directory can be read
	if (access(config_path, R_OK) != 0) {
		free(config_path);
		return NULL;
	}

	// Add mash config filename
	strcat(config_path, "config.mash");
	FILE *file = fopen(config_path, "r");
	free(config_path);
	if (file == NULL)
		fprintf(stderr, "%m\n");
	return file;
}

FILE *open_history(struct passwd *PASSWD) {
	char *history_path;

	// Try HISTFILE variable
	char *env_histfile = getenv("HISTFILE");
	if (env_histfile == NULL) {
		char *env_xdg = getenv("XDG_CONFIG_HOME");
		if (env_xdg == NULL) {
			char *env_home = getenv("HOME");
			if (env_home == NULL)
				env_home = PASSWD->pw_dir;
			history_path = calloc(strlen(env_home) + 22, sizeof (char));
			strcpy(history_path, env_home);
			strcat(history_path, "/.config");
		}
		else {
			history_path = calloc(strlen(env_xdg) + 14, sizeof (char));
			strcpy(history_path, env_xdg);
		}
		// Add mash dir
		strcat(history_path, "/mash/");

		// Check if directory can be read
		if (access(history_path, R_OK) != 0) {
			free(history_path);
			return NULL;
		}

		// Add mash history filename
		strcat(history_path, "history");
	}
	else
		history_path = env_histfile;

	// Open file
	FILE *file = fopen(history_path, "a");
	if (env_histfile == NULL)
		free(history_path);
	if (file == NULL) {
		fprintf(stderr, "%m\n");
		fprintf(stderr, "Could not open file at `%s', history not saved.\n", history_path);
	}
	return file;
}
