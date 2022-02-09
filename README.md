# MASH
**Ma**tthew's **Sh**ell, aims to be a bash compatible, or at least bash-like shell.

Not necessarily trying to bring any unique features to the table, I just want to write a shell, cause I'm bored.

# Features

- Run scripts (can be used as a shebang)
- Version info `--version`
- Change directory with `cd`
- Environment variables with `$varname` - set with `export`
- Flow control: `if`, and `while`
- Run single command with `-c command`
- Subshells with `$(command)` - if inside double quotes, you will get the exact output contents (otherwise it is tokenized)
- Aliases: `alias` and `unalias`

# TODO

## Command Parser

- Removing environment variables
- Handle arrow keys (nobody wants their arrow keys to print `^[[A`, they want them to do actions)
- Pipes
- Redirection (`<` and `>`)
- For loops

## Behind the Scenes

- Create `$XDG\_CONFIG\_HOME/mash/config.mash`?

## Code Improvements

# Misc
An idea I had before finding out about `environ`(7):  
Environment variable dictionary using binary search tree (still store in array, but use this to easily find its location if it exists)
