# MASH
**Ma**tthew's **Sh**ell, aims to be a bash compatible, or at least bash-like shell.

Not necessarily trying to bring any unique features to the table, I just want to write a shell, cause I'm bored.

# TODO

## Command Parser

- Allow quoting (currently ***all*** spaces are treated as delimiters)
- Adding/Setting/Removing environment variables
- Handle arrow keys (nobody wants their arrow keys to print `^[[A`, they want them to do actions)

## Behind the Scenes

- Create `$XDG\_CONFIG\_HOME/mash/config.mash`?

## Code Improvements

# Misc
An idea I had before finding out about `environ`(7):  
Environment variable dictionary using binary search tree (still store in array, but use this to easily find its location if it exists)
