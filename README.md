# MASH
**Ma**tthew's **Sh**ell, aims to be a bash compatible, or at least bash-like shell.

Not necessarily trying to bring any unique features to the table, I just want to write a shell, cause I'm bored.

# Features

## Commands/Builtins
- Change directory with `cd`
- Set environment variables (or move shell variables to the environment) with `export`
- Flow control: `if`, and `while` (supports multiple commands in conditional)
- Aliases: `alias` and `unalias`
- Removing environment variables with `unset`
- POSIX `exec` (only for executing commands, does not have file descriptor functionality)
- `shift` to shift out positional parameters (arguments) - most useful in scripts
- `break` and `continue` to stop, or return to the top of a while loop

## Others
- Run scripts (can be used as a shebang)
- Version info `--version`
- Environment and shell variables with `$varname`
- Run single command with `-c command`
- Subshells with `$(command)` - if inside double quotes, you will get the exact output contents (otherwise it is tokenized)
- Redirection (`<` and `>`)
- Set prompt with `$PS1`, supports bash prompt expansion tokens. Also supports `$PROMPT_COMMAND` which if set, will always execute before displaying your prompt (for fancier things like powerline).
- Pipes via `|`
- Cursor around and edit current command text, via GNU Readline
- Math statements with `$((...))`, i.e.: `echo $((num * 5))`.

# TODO

## Command Parser

- For loops
- Read another line if the line ends with a `\`, then concatenate them together
- Chain commands together based on exit status with `&&` and `||`
- Allow for appending to files with `>>` and not just `>`
- Accept strings as input with `<<<`
- More advanced (recursion) math statement parsing, matching the abilities of other shells.

## Behind the Scenes

- Create `$XDG\_CONFIG\_HOME/mash/config.mash`?
- Keep history loaded in memory to allow for `!` statements and possibly arrow keys (up/down).
- Improve syntax error output messages
- Jobs (should also fix issue with defunct processes resulting from pipes...)
- Split commandExecute into multiple functions, and use those functions where appropriate to improve performance (subshells don't need to parse aliases because there won't be any!)
- Output of subshells (`$(...)`), when not in double quotes, should become multiple arguments, not just a single argument - as in, in bash/zsh `for x in $(echo 'hello world'); do "echo $x"; done` will echo hello and world separately, on new lines
- Consider moving away from stdio FILEs and exclusively using unix file descriptors

## Code Improvements

- More comments

## Other
- Create testbed script that runs through as many mash features as possible to verify they still work, or work period on unfamiliar systems

# OS' and Architectures Tested

## x86
- Manjaro, Arch, Ubuntu (Linux) - no reason to believe others wouldn't work, except for Void (haven't tested musl)
- macOS - Big Sur (I think), running in a KVM - compatibility.c contains any GNU libc extensions I used that aren't available in the C library included in macOS
- OpenBSD - 7.0, also in a KVM - Makefile does not work out of the box, presumably due to OpenBSD not shipping with GNU make - compatibility.c may also be required for the same reason as macOS

## ARM
- Android - via Termux - not the most authentic environment, but as best I can tell it is being natively compiled and executed - compatibility.c also required since Termux ships with bionic libc

## PowerPC
- Debian, Lubuntu - running on a Wii and Wii U respectively, yes these are my only ppc machines - outside of being out-of-date versions of these OS', mash compiled and ran fine

# Misc
An idea I had before finding out about `environ`(7):  
Environment variable dictionary using binary search tree (still store in array, but use this to easily find its location if it exists)
Builtins were going to use suffix trees, and did up until commit `0cead82c` where I finally deleted the code. Although janky in its implementation I was still somewhat proud of them.
I was going to manually tweak the terminal and implement my own line editing system, but found readline and decided to use that instead. I may revisit this in the future for flexibility. Zsh and fish both use their own line editor.
