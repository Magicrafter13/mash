# MASH
**Ma**tthew's **Sh**ell, aims to be a bash compatible, or at least bash-like shell.

Not necessarily trying to bring any unique features to the table, I just want to write a shell, cause I'm bored.

# Features

## Commands/Builtins
- Change directory with `cd`
- Set environment variables (or move shell variables to the environment) with `export`
- Flow control: `if`, and `while`
- Aliases: `alias` and `unalias`
- Removing environment variables with `unset`
- POSIX `exec` (only for executing commands, does not have file descriptor functionality)

## Others
- Run scripts (can be used as a shebang)
- Version info `--version`
- Environment and shell variables with `$varname`
- Run single command with `-c command`
- Subshells with `$(command)` - if inside double quotes, you will get the exact output contents (otherwise it is tokenized)
- Redirection (`<` and `>`)
- Set prompt with `$PS1`, supports bash prompt expansion tokens. Also supports `$PROMPT_COMMAND` which if set, will always execute before displaying your prompt (for fancier things like powerline).
- Pipes via `|`

# TODO

## Command Parser

- Handle arrow keys (nobody wants their arrow keys to print `^[[A`, they want them to do actions)
- Readline (should help with above)
- For loops
- Read another line if the line ends with a `\`, then concatenate them together

## Behind the Scenes

- Create `$XDG\_CONFIG\_HOME/mash/config.mash`?
- Keep history loaded in memory to allow for `!` statements and possibly arrow keys (up/down).
- Improve syntax error output messages
- Remove builtin array (the format is too limiting)
- Jobs
- Expand aliases before execution (right after tokenizing)
- Split commandExecute into multiple functions, and use those functions where appropriate to improve performance (subshells don't need to parse aliases because there won't be any!)

## Code Improvements

- More comments

# Misc
An idea I had before finding out about `environ`(7):  
Environment variable dictionary using binary search tree (still store in array, but use this to easily find its location if it exists)
