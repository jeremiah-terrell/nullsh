*** nullsh: A barebones, freestanding shell ***

Features:
- Written entirely in a single C file without headers, must be compiled without libc
  - `gcc -ffreestanding -fno-stack-protector -nostdlib -nodefaultlibs nullsh_vX.X.X.c`
- Currently takes no command line arguments, nor does any inline evaluation
- Very much a work in progress, but technically works for now

Upcoming features:
- pipes and redirection
- history (read from file, .nullsh_history)
- tab completion
- support for '~' for user's home directory
- .nullshrc configuration file
- Other things that I eventually realize should be standard in a shell
