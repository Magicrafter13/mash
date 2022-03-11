#define _POSIX_C_SOURCE 200809L // mkstemp
#include "mash.h"
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/stat.h>
#include <unistd.h>

FILE *open_config(struct passwd *PASSWD, char *arg0) {
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
	if (file == NULL && errno != ENOENT) // Ignore file not exist error
		fprintf(stderr, "%s: error reading config file: %m\n", arg0);
	return file;
}

FILE *open_history(struct passwd *PASSWD, char *arg0, Variables *vars) {
	char *history_path;

	// Try HISTFILE variable
	char *env_histfile = getvar(vars, "HISTFILE");
	if (env_histfile == NULL) {
		char *env_xdg = getvar(vars, "XDG_STATE_HOME");
		if (env_xdg == NULL) {
			char *env_home = getvar(vars, "HOME");
			if (env_home == NULL)
				env_home = PASSWD->pw_dir;
			history_path = calloc(strlen(env_home) + 27, sizeof (char));
			strcpy(history_path, env_home);
			strcat(history_path, "/.local/state");
		}
		else {
			history_path = calloc(strlen(env_xdg) + 14, sizeof (char));
			strcpy(history_path, env_xdg);
		}
		// Add mash dir
		strcat(history_path, "/mash/");

		// Check if directory can be read
		if (access(history_path, R_OK) == -1) { // TODO: possibly make use of NAME_MAX for arrays
			/*_Bool failure = 1;
			if (errno == ENOENT)
				if (fprintf(stderr, "what\n"), mkdir(history_path, 0700) == 0)
					failure = 0;*/
			//if (failure) {
				fprintf(stderr, "%s: error opening state directory: %m\n", arg0);
				fprintf(stderr, "Could not open directory at `%s', history not saved.\n", history_path);
				free(history_path);
				return NULL;
			//}
		}

		// Add mash history filename
		strcat(history_path, "history");
	}
	else
		history_path = env_histfile;

	// Open file
	FILE *file = fopen(history_path, "a");
	if (file == NULL) {
		fprintf(stderr, "%s: error reading history file: %m\n", arg0);
		fprintf(stderr, "Could not open file at `%s', history not saved.\n", history_path);
	}
	if (env_histfile == NULL)
		free(history_path);
	return file;
}

int mktmpfile(_Bool hidden, char **path, Variables *vars) {
	size_t path_size = 13 + (hidden ? 1 : 0);
	char *temp_dir = getvar(vars, "TMPDIR");
	if (temp_dir == NULL)
		path_size += 4;
	else
		path_size += strlen(temp_dir);

	char *template = calloc(path_size, sizeof (char));
	template[0] = '\0';
	strcat(template, temp_dir == NULL ? "/tmp" : temp_dir);
	strcat(template, hidden ? "/.mash.XXXXXX" : "/mash.XXXXXX");

	int sub_stdout = mkstemp(template); // Create temporary file, which we will redirect the output to.
	if (sub_stdout == -1) {
		fprintf(stderr, "%m\n"); // TODO: print shell/script name
		free(template);
	}
	else
		*path = template;
	return sub_stdout;
}

int openInputFiles(CmdIO *io, Source *source, Variables *vars, uint8_t *cmd_exit) {
	// File we will return (which will contain the concatenated contents of all requested files)
	io->in_file = tmpfile();
	_Bool error = 0;
	for (size_t i = 0; i < io->in_count; ++i) {
		// Get filename (path)
		char *ipath;
		if (expandArgument(&ipath, io->in_arg[i], source, vars, cmd_exit) == -1)
			return -1; // Child process with error
		if (ipath == NULL) {
			fprintf(stderr, "%s: error expanding argument, possibly related error message: %m\n", source->argv[0]);
			error = 1;
			break;
		}

		// Open file
		FILE *ifile = fopen(ipath, "r");
		if (ifile == NULL) {
			fprintf(stderr, "%s: %m: %s\n", source->argv[0], ipath);
			free(ipath);
			error = 1;
			break;
		}
		free(ipath);

		// Copy contents to temporary file
		char buffer[TMP_RW_BUFSIZE];
		size_t bytes_read;
		while (bytes_read = fread(buffer, sizeof (char), TMP_RW_BUFSIZE, ifile), bytes_read != 0)
			fwrite(buffer, sizeof (char), bytes_read, io->in_file);
		fclose(ifile);
	}
	rewind(io->in_file);
	if (error) {
		fclose(io->in_file);
		io->in_file = NULL;
	}

	return error ? 1 : 0;
}

int openOutputFiles(CmdIO *io, Source *source, Variables *vars, uint8_t *cmd_exit) {
	io->out_file = calloc(io->out_count + 1, sizeof (FILE*));
	_Bool error = 0;
	// Open output files if applicable
	size_t i;
	for (i = 0; i < io->out_count; ++i) {
		// Get filename (path)
		char *opath;
		if (expandArgument(&opath, io->out_arg[i], source, vars, cmd_exit) == -1)
			return -1; // Child process with error
		if (opath == NULL) {
			fprintf(stderr, "%s: error expanding argument, possibly related error message: %m\n", source->argv[0]);
			error = 1;
			break;
		}

		// Open file
		FILE *ofile = fopen(opath, "w");
		if (ofile == NULL) {
			fprintf(stderr, "%s: %m: %s\n", source->argv[0], opath);
			free(opath);
			error = 1;
			break;
		}
		free(opath);

		// Add to array
		io->out_file[i] = ofile;
	}
	if (error) {
		for (size_t j = 0; j < i; ++j)
			fclose(io->out_file[j]);
		free(io->out_file);
		io->out_file = NULL;
	}
	else
		io->out_file[i] = tmpfile();

	return error ? 1 : 0;
}

int openIOFiles(CmdIO *io, Source *source, Variables *vars, uint8_t *cmd_exit) {
	int res = 0;
	if (io->in_count > 0 && io->in_file == NULL) {
		res = openInputFiles(io, source, vars, cmd_exit);
		if (res == -1)
			return -1;
	}
	if ((io->out_count > 0 || io->out_pipe) && io->out_file == NULL) {
		res = openOutputFiles(io, source, vars, cmd_exit);
		if (res == -1) {
			if (io->in_file != NULL) {
				fclose(io->in_file);
				io->in_file = NULL;
				return -1;
			}
		}
		/*if (io->out_pipe)
			cmd->c_next->c_io.in_file = cmd->c_io.out_file[cmd->c_io.out_count];*/
	}
	return res;
}

void closeIOFiles(CmdIO *io) {
	if (io->in_file != NULL) {
		fclose(io->in_file);
		io->in_file = NULL;
	}
	if (io->out_file != NULL) {
		if (io->out_count > 0) {
			rewind(io->out_file[io->out_count]);
			char buffer[TMP_RW_BUFSIZE];
			size_t bytes_read;
			while (bytes_read = fread(buffer, sizeof (char), TMP_RW_BUFSIZE, io->out_file[io->out_count]), bytes_read > 0)
				for (size_t i = 0; i < io->out_count; ++i)
					fwrite(buffer, sizeof (char), bytes_read, io->out_file[i]);
			for (size_t i = 0; i < io->out_count; ++i)
				fclose(io->out_file[i]);
		}
		fclose(io->out_file[io->out_count]);
		free(io->out_file);
		io->out_file = NULL;
	}
}

FILE *getParentInputFile(Command *cmd) {
	while (cmd->c_parent != NULL) {
		cmd = cmd->c_parent;
		if (cmd->c_io.in_file != NULL)
			break;
	}
	return cmd->c_io.in_file;
}

FILE *getParentOutputFile(Command *cmd) {
	while (cmd->c_parent != NULL) {
		cmd = cmd->c_parent;
		if (cmd->c_io.out_file != NULL)
			break;
	}
	return cmd->c_io.out_count > 0 ? cmd->c_io.out_file[cmd->c_io.out_count] : NULL;
}
