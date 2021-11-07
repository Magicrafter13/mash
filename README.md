# MASH
**Ma**tthew's **Sh**ell, aims to be a bash compatible, or at least bash-like shell.

Not necessarily trying to bring any unique features to the table, I just want to write a shell, cause I'm bored.

# TODO

## Command Parser

- Allow quoting (currently ***all*** spaces are treated as delimiters)
- Adding/Setting/Removing environment variables

## Behind the Scenes

- Read environment variables on startup, and pass them to programs
- Create/read $XDG\_CONFIG\_HOME/mash/config.mash

## Code Improvements

- Command data structure
- Environment variable dictionary using binary search tree (still store in array, but use this to easily find its location if it exists)
