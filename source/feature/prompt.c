#define _POSIX_C_SOURCE 200809L // fileno
#define _GNU_SOURCE // strchrnul
#include "compatibility.h"
#include "mash.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

char DEFAULT_PROMPT[4] = "\\$ ";

void printPrompt(Variables *vars, Source *source, struct passwd *PASSWD, uid_t UID) {
	// Set various variables which may be used
	time_t timep = time(NULL);
	struct tm *tm = localtime(&timep);

	// Get prompt instructions
	char *PS1 = getvar(vars, "PS1");
	if (PS1 == NULL)
		PS1 = DEFAULT_PROMPT;

	// Parse PS1 to determine string length
	size_t var_len = strlen(PS1), pmt_len = var_len;
	for (size_t i = 0; i < var_len; ++i) {
		if (PS1[i] == '\\') {
			switch (PS1[++i]) {
				case 'a':
				case 'e':
				case 'n':
				case 'r':
				case '$':
				case '\\':
					--pmt_len;
					break;
				case 'd': {
					char datestr[128];
					pmt_len += strftime(datestr, 128, "%a %b %d", tm) - 2;
					break;
				}
				case 'D':
					if (PS1[i + 1] == '{') {
						size_t len = strchrnul(&PS1[i + 2], '}') - &PS1[i + 2];
						char substr[len + 1];
						memset(substr, '\0', len + 1);
						strncpy(substr, &PS1[i + 2], len);
						char datestr[128];
						pmt_len += strftime(datestr, 128, substr, tm);
					}
					break;
				case 'h': {
					pmt_len -= 2;
					char hostname[HOST_NAME_MAX];
					if (gethostname(hostname, HOST_NAME_MAX) != -1) {
						char *last_dot = strrchr(hostname, '.');
						pmt_len += strlen(last_dot == NULL ? hostname : &last_dot[1]);
					}
					break;
				}
				case 'H': {
					pmt_len -= 2;
					char hostname[HOST_NAME_MAX];
					if (gethostname(hostname, HOST_NAME_MAX) != -1)
						pmt_len += strlen(hostname);
					break;
				}
				case 'j': // TODO job count (when jobs are added...)
					--pmt_len;
					break;
				case 'l': {
					pmt_len -= 2;
					char *name = ttyname(fileno(stdin));
					if (name != NULL)
						pmt_len += strlen(name);
					break;
				}
				case 's': {
					pmt_len -= 2;
					if (source->argc > 0) {
						char *last_slash = strrchr(source->argv[0], '/');
						pmt_len += strlen(last_slash == NULL ? source->argv[0] : &last_slash[1]);
					}
					break;
				}
				case 't':
				case 'T':
				case '@':
					pmt_len += 3;
				case 'A':
					pmt_len += 3;
					break;
				case 'u':
					pmt_len += strlen(PASSWD->pw_name) - 2;
					break;
				/*case 'V':
					pmt_len += 12;*/
				case 'v':
					pmt_len += (_VMAJOR > 99 ? 3 : _VMAJOR > 9 ? 2 : 1) + (_VMINOR > 99 ? 3 : _VMINOR > 9 ? 2 : 1) - 1;
					break;
				case 'w': {
					char *PWD = getvar(vars, "PWD");
					pmt_len += (PWD == NULL ? 0 : strlen(PWD)) - 2;
					break;
				}
				case 'W': {
					pmt_len -= 2;
					char *PWD = getvar(vars, "PWD");
					if (PWD != NULL) {
						char *last_slash = strrchr(PWD, '/');
						pmt_len += last_slash == NULL ? strlen(PWD) : strlen(&last_slash[1]);
					}
					break;
				}
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
					if (strspn(&PS1[i + 1], "01234567") > 1)
						pmt_len -= 3;
					break;
				case '[': // TODO currently ignoring these, but might want to implement them? they seem pointless though
				case ']':
					pmt_len -= 2;
					break;
			}
		}
	}

	// Construct prompt
	char prompt[pmt_len + 1];
	memset(prompt, '\0', pmt_len + 1);
	for (size_t r = 0, w = 0; r < var_len; ++r) {
		char *next_slash = strchrnul(&PS1[r], '\\');
		size_t clean_len = next_slash - &PS1[r];
		strncpy(&prompt[w], &PS1[r], clean_len);
		r += clean_len;
		w += clean_len;
		if (PS1[r] != '\\')
			continue;
		switch (PS1[++r]) {
			case 'a':
				prompt[w++] = '\a';
				break;
			case 'd': {
				char datestr[128];
				size_t len = strftime(datestr, 128, "%a %b %d", tm);
				if (len > 0) {
					strncpy(&prompt[w], datestr, len);
					w += len;
				}
				break;
			}
			case 'D':
				if (PS1[r + 1] == '{') {
					size_t r_len = strchrnul(&PS1[r + 2], '}') - &PS1[r + 2];
					char substr[r_len + 1];
					memset(substr, '\0', r_len + 1);
					strncpy(substr, &PS1[r + 2], r_len);
					char datestr[128];
					size_t w_len = strftime(datestr, 128, substr, tm);
					strncpy(&prompt[w], datestr, w_len);
					r += r_len + 2;
					w += w_len;
				}
				break;
			case 'e':
				prompt[w++] = '\033';
				break;
			case 'h': {
				char hostname[HOST_NAME_MAX];
				if (gethostname(hostname, HOST_NAME_MAX) == -1)
					break;
				char *last_dot = strrchr(hostname, '.');
				size_t len = strlen(last_dot == NULL ? hostname : &last_dot[1]);
				strncpy(&prompt[w], last_dot == NULL ? hostname : &last_dot[1], len);
				w += len;
				break;
			}
			case 'H': {
				char hostname[HOST_NAME_MAX];
				if (gethostname(hostname, HOST_NAME_MAX) == -1)
					break;
				size_t len = strlen(hostname);
				strncpy(&prompt[w], hostname, len);
				w += len;
				break;
			}
			case 'j': // TODO job count (when jobs are added...)
				prompt[w++] = '0';
				break;
			case 'l': {
				char *name = ttyname(fileno(stdin));
				if (name == NULL)
					break;
				char *last_slash = strrchr(name, '/');
				size_t len = strlen(last_slash == NULL ? name : &last_slash[1]);
				strncpy(&prompt[w], last_slash == NULL ? name : &last_slash[1], len);
				w += len;
				break;
			}
			case 'n':
				prompt[w++] = '\n';
				break;
			case 'r':
				prompt[w++] = '\r';
				break;
			case 's': {
				if (source->argc < 1)
					break;
				char *last_slash = strrchr(source->argv[0], '/');
				size_t len = strlen(last_slash == NULL ? source->argv[0] : &last_slash[1]);
				strncpy(&prompt[w], last_slash == NULL ? source->argv[0] : &last_slash[1], len);
				w += len;
				break;
			}
			case 't': {
				char timestr[9];
				size_t len = strftime(timestr, 9, "%H:%M:%S", tm);
				if (len > 0) {
					strncpy(&prompt[w], timestr, 8);
					w += 8;
				}
				break;
			}
			case 'T': {
				char timestr[9];
				size_t len = strftime(timestr, 9, "%I:%M:%S", tm);
				if (len > 0) {
					strncpy(&prompt[w], timestr, 8);
					w += 8;
				}
				break;
			}
			case '@': {
				char timestr[9];
				size_t len = strftime(timestr, 9, "%I:%M %p", tm);
				if (len > 0) {
					strncpy(&prompt[w], timestr, 8);
					w += 8;
				}
				break;
			}
			case 'A': {
				char timestr[6];
				size_t len = strftime(timestr, 6, "%H:%M", tm);
				if (len > 0) {
					strncpy(&prompt[w], timestr, 5);
					w += 5;
				}
				break;
			}
			case 'u': {
				size_t len = strlen(PASSWD->pw_name);
				strncpy(&prompt[w], PASSWD->pw_name, len);
				w += len;
				break;
			}
			case 'v':
				w += sprintf(&prompt[w], "%d.%d", _VMAJOR, _VMINOR);
				break;
			/*case 'V':
				w += sprintf(&prompt[w], "%d.%d", _VMAJOR, _VMINOR);
				// TODO build version?
				break;*/
			case 'w': { // TODO: support PROMPT_DIRTRIM
				char *PWD = getvar(vars, "PWD");
				if (PWD != NULL) {
					char *HOME = getvar(vars, "HOME");
					size_t len = strlen(PWD);
					if (HOME != NULL && HOME[0] != '\0') {
						size_t home_len = strlen(HOME);
						if (!strncmp(PWD, HOME, home_len)) {
							prompt[w++] = '~';
							PWD = &PWD[home_len];
							len -= home_len;
						}
					}
					strncpy(&prompt[w], PWD, len);
					w += len;
				}
				break;
			}
			case 'W': {
				char *PWD = getvar(vars, "PWD");
				if (PWD == NULL)
					break;
				char *HOME = getvar(vars, "HOME");
				if (HOME != NULL && !strcmp(PWD, HOME)) {
					prompt[w++] = '~';
					break;
				}
				char *last_slash = strrchr(PWD, '/');
				size_t len = strlen(last_slash == NULL ? PWD : &last_slash[1]);
				strncpy(&prompt[w], last_slash == NULL ? PWD : &last_slash[1], len);
				w += len;
				break;
			}
			case '$':
				prompt[w++] = UID == 0 ? '#' : '$';
				break;
			case '\\':
				prompt[w++] = '\\';
				break;
			case '[': // TODO currently ignoring these, but might want to implement them? they seem pointless though
			case ']':
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				if (strspn(&PS1[r + 1], "01234567") > 1) {
					char octal[4] = {
						PS1[r],
						PS1[r + 1],
						PS1[r + 2],
						'\0'
					};
					r += 2;
					unsigned int character;
					sscanf(octal, "%o", &character);
					prompt[w++] = (char)character;
					break;
				}
			default:
				prompt[w++] = '\\';
				prompt[w++] = PS1[r];
		}
	}

	// Print prompt
	fputs(prompt, stderr);
	fflush(stderr);
}
