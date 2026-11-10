#define MAX_ARGS 64
#define PATH_MAX 4096
#define CMD_MAX 4096

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

#define EINTR 4

#define X_OK 1

#define WIFEXITED(status) (((status) & 0x7F) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)
#define WTERMSIG(status) (status & 0x7F)
#define WIFSIGNALED(status)                                                    \
	(((status & 0x7F) != 0) && ((status & 0x7F) != 0x7F))

#define TIOCGWINSZ 0x5413

#define TCGETS 0x5401
#define TCSETS 0x5402

#define ICANON 0x00000002
#define ECHO 0x00000008

#define ISIG 0x00000001
#define IEXTEN 0x00008000

#define VTIME 5
#define VMIN 6

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

struct utsname {
	char sysname[65];
	char nodename[65];
	char release[65];
	char version[65];
	char machine[65];
	char domainname[65];
};

// heavily documented because i have already forgotten how this works
#define NCCS 32

struct termios {
	unsigned int c_iflag;	  // Input modes
	unsigned c_oflag;	  // Output modes
	unsigned int c_cflag;	  // Control modes
	unsigned int c_lflag;	  // Local modes
	unsigned char c_line;	  // Line discipline
	unsigned char c_cc[NCCS]; // Control characters array
	unsigned int c_ispeed;	  // Input speed
	unsigned int c_ospeed;	  // Output speed
};

static struct termios orig_term;
static struct termios edit_term;
static char nullsh_line[CMD_MAX];

/*****************************************************
 *    ______  ______________   __   __               *
 *   / __/\ \/ / __/ ___/ _ | / /  / /               *
 *  _\ \   \  /\ \/ /__/ __ |/ /__/ /__              *
 * /___/_  /_/___/\___/_/_|_/____/____/__  _____  __ *
 *   / _ \/ / / / _ \/ ___/ _ /_  __/ __ \/ _ \ \/ / *
 *  / ___/ /_/ / , _/ (_ / __ |/ / / /_/ / , _/\  /  *
 * /_/   \____/_/|_|\___/_/ |_/_/  \____/_/|_| /_/   *
 *****************************************************/

// sys_read -- 0 (3)
// sys_write -- 1 (3)
// sys_ioctl -- 16 (3)
// sys_access -- 21 (2)
// sys_pipe -- 22 (1)
// sys_dup2 -- 33 (2)
// sys_fork -- 57 (0)
// sys_execve -- 59 (3)
// sys_exit -- 60 (1)
// sys_wait4 -- 61 (4)
// sys_uname -- 63 (1)
// sys_getcwd -- 79 (2)
// sys_chdir -- 80 (1)

// NOTE: Syscall wrappers

static long base_syscall(long number, long arg1, long arg2, long arg3,
			 long arg4, long arg5, long arg6) {
	long result;

	register long r10 asm("r10") = arg4;
	register long r8 asm("r8") = arg5;
	register long r9 asm("r9") = arg6;

	asm volatile("syscall"
		     : "=a"(result)
		     : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10),
		       "r"(r8), "r"(r9)
		     : "rcx", "r11", "memory");

	return result;
}

int sys_read(int fd, char *buf, unsigned long count) {
	return base_syscall(0, fd, (long)buf, count, 0, 0, 0);
}

int sys_write(int fd, char *buf, unsigned long count) {
	return base_syscall(1, fd, (long)buf, count, 0, 0, 0);
}

int sys_ioctl(int fd, unsigned long request, void *arg) {
	return base_syscall(16, fd, request, (long)arg, 0, 0, 0);
}

int sys_access(const char *pathname, int mode) {
	return base_syscall(21, (long)pathname, mode, 0, 0, 0, 0);
}

int sys_pipe(int pipefd[2]) {
	return base_syscall(22, (long)pipefd, 0, 0, 0, 0, 0);
}

int sys_dup2(int oldfd, int newfd) {
	return base_syscall(33, oldfd, newfd, 0, 0, 0, 0);
}

int sys_fork(void) {
	return base_syscall(57, 0, 0, 0, 0, 0, 0);
}

int sys_execve(const char *filename, char *const argv[], char *const envp[]) {
	return base_syscall(59, (long)filename, (long)argv, (long)envp, 0, 0,
			    0);
}

int sys_exit(int status) {
	return base_syscall(60, status, 0, 0, 0, 0, 0);
}

int sys_wait4(int pid, int *wstatus, int options, void *rusage) {
	return base_syscall(61, pid, (long)wstatus, options, (long)rusage, 0,
			    0);
}

int waitpid(int pid, int *wstatus, int options) {
	return sys_wait4(pid, wstatus, options, 0);
}

int sys_uname(struct utsname *buf) {
	return base_syscall(63, (long)buf, 0, 0, 0, 0, 0);
}

int sys_getcwd(char *buf, unsigned long size) {
	return base_syscall(79, (long)buf, size, 0, 0, 0, 0);
}

int sys_chdir(const char *path) {
	return base_syscall(80, (long)path, 0, 0, 0, 0, 0);
}

// finally, no more suffering

// NOTE: Signal handling

char *signal_name(int sig) {
	switch (sig) {
	case 1:
		return "Hangup";
	case 2:
		return "Interrupt";
	case 3:
		return "Quit";
	case 4:
		return "Illegal instruction";
	case 6:
		return "Aborted";
	case 7:
		return "Bus error";
	case 8:
		return "Floating point exception";
	case 9:
		return "Killed";
	case 11:
		return "Segmentation fault";
	case 13:
		return "Broken pipe";
	case 14:
		return "Alarm clock";
	case 15:
		return "Terminated";
	default:
		return "Unknown signal";
	}
}

// NOTE: Minimal string library

// 1/6 reinventing strings, basically strlen
unsigned long linelen(char *str) {
	int len = 0;
	while (str[len] != '\0') {
		++len;
	}
	return len;
}

// print prints
void print(int fd, char *str) {
	unsigned long len = linelen(str);
	unsigned long written = 0;

	while (written < len) {
		long ret = sys_write(fd, str + written, len - written);

		if (ret <= 0) {
			break;
		}

		written += ret;
	}
}

// 2/6 reinventing strings, converts integer to string
char *itos(int num, char buf[11]) {
	int i = 10;
	buf[i] = '\0';
	int is_neg = 0;

	if (num == 0) {
		--i;
		buf[i] = '0';
		return &buf[i];
	}

	unsigned int abs = num;
	if (num < 0) {
		is_neg = 1;
		abs = -num;
	}

	while (abs) {
		--i;
		buf[i] = abs % 10 + '0';
		abs /= 10;
	}

	if (is_neg) {
		--i;
		buf[i] = '-';
	}

	return &buf[i];
}

// 3/6 reinventing strings, checks str for a char c
int contains(const char *str, char c) {
	while (*str) {
		if (*str == c) {
			return 1;
		}
		str++;
	}
	return 0;
}

// 4/6 reinventing strings, checks string equivalence
int lineeq(const char *s1, const char *s2) {
	while (*s1 && *s2) {
		if (*s1 != *s2) {
			return 1;
		}
		s1++;
		s2++;
	}
	return *s1 != *s2;
}

// 5/6 reinventing strings, old janky version of lineeq, checks prefix and
// postfix
int sublineeq(const char *s1, const char *s2) {
	while (*s1 && *s2) {
		if (*s1 != *s2) {
			return 1;
		}
		s1++;
		s2++;
	}
	return 0;
}

// 6/6 reinventing strings, basically strtok
char *linetok(char *line, char delimiter) {
	static char *next = 0;

	if (line != 0)
		next = line;
	if (next == 0 || *next == '\0')
		return 0;

	while (*next == delimiter)
		next++;

	if (*next == '\0')
		return 0;

	char *token = next;

	while (*next != '\0' && *next != delimiter)
		next++;

	if (*next != '\0') {
		*next = '\0';
		next++;
	}

	return token;
}

int linegraph(char *str) {
	for (int i = 0; i < linelen(str); ++i)
		if (str[i] != ' ' && str[i] != '\t' && str[i] != '\n' &&
		    str[i] != '\r')
			return 0;
	return 1;
}

// gets specified environment variable
char *env_get(char *var, char *envp[]) {
	if (!var || !envp)
		return 0;

	for (char **env = envp; *env != 0; env++) {
		char *current = *env;
		if (sublineeq(current, var) == 0 &&
		    current[linelen(var)] == '=') {
			return current + linelen(var) + 1;
		}
	}
	return 0;
}

// finds binary in PATH, used in exec implementation
int find_bin(char *command, char *envp[], char *result) {
	char *path_env = env_get("PATH", envp);

	char path[PATH_MAX];

	if (!path_env)
		return 0;

	int pathlen = linelen(path_env);

	if (pathlen >= PATH_MAX)
		return 0;

	for (int i = 0; i < pathlen; i++)
		path[i] = path_env[i];

	path[pathlen] = '\0';

	char *dir = linetok(path, ':');

	while (dir != 0) {
		int dirlen = linelen(dir);
		int cmdlen = linelen(command);

		if (dirlen + 1 + cmdlen + 1 >= PATH_MAX)
			return 0;

		for (int i = 0; i < dirlen; i++)
			result[i] = dir[i];

		result[dirlen] = '/';

		for (int i = 0; i < cmdlen; i++)
			result[dirlen + 1 + i] = command[i];

		result[dirlen + 1 + cmdlen] = '\0';

		if (sys_access(result, X_OK) == 0) {
			return 1;
		}
		dir = linetok(0, ':');
	}

	return 0;
}

// custom perror, calls print to stderr
void print_error(char *message, int code) {
	char buf[11];
	print(2, itos(code, buf));
	print(2, ": ");
	print(2, message);
	print(2, "\n");
}

void print_signal_error(int signal_number) {
	char *signal_name_str = signal_name(signal_number);
	print(2, "Command terminated by signal ");
	print(2, itos(signal_number + 128, (char[11]){0}));
	print(2, ": ");
	print(2, signal_name_str);
	print(2, "\n");
}

// takes input string (in) and output string array (args), tokenizes input
void parse_input(char *in, char *args[]) {
	unsigned long count = 0;

	char *token = linetok(in, ' ');

	while (token != 0 && count < MAX_ARGS - 1) {
		args[count] = token;
		++count;
		token = linetok(0, ' ');
	}

	args[count] = 0;
}

void print_prompt(char *user, char *hostname, int lastexit, char *path) {
	char buf[11];
	char *exitstr = itos(lastexit, buf);
	print(STDOUT_FILENO, "\n");
	print(STDOUT_FILENO, path);
	print(STDOUT_FILENO, "\n");
	print(STDOUT_FILENO, user);
	print(STDOUT_FILENO, "@");
	print(STDOUT_FILENO, hostname);
	print(STDOUT_FILENO, " | exit: ");
	print(STDOUT_FILENO, exitstr);
	print(STDOUT_FILENO, "\n");
}

void find_home(char *envp[], char *result, char *path) {
	char *home_env = env_get("HOME", envp);

	if (!home_env)
		return;

	int homelen = linelen(home_env);

	if (homelen >= PATH_MAX)
		return;

	if (path[0] == '~') {
		for (int i = 0; i < homelen; i++)
			result[i] = home_env[i];

		int pathlen = linelen(path);

		if (homelen + pathlen - 1 >= PATH_MAX)
			return;

		for (int i = 1; i < pathlen; i++)
			result[homelen + i - 1] = path[i];

		result[homelen + pathlen - 1] = '\0';
	} else {
		for (int i = 0; i < linelen(path); i++)
			result[i] = path[i];
		result[linelen(path)] = '\0';
	}
}

// takes string array (line) and executes
int execute(char *line[], char *path, char *envp[]) {
	int pid = sys_fork();

	if (pid == 0) {
		sys_execve(path, line, envp);
		print_error("Command not found ", 127);
		sys_exit(127);
	} else if (pid < 0) {
		print_error("fork failed", 127);
		return 127;
	} else {
		int status;
		int ret;

		do {
			ret = waitpid(pid, &status, 0);
		} while (ret == -EINTR);

		if (ret < 0) {
			print_error("waitpid failed", 127);
			return 127;
		}

		if (WIFEXITED(status)) {
			int exit_status = WEXITSTATUS(status);
			if (exit_status != 0)
				print_error("Command exited", exit_status);
			return exit_status;
		} else if (WIFSIGNALED(status)) {
			int signal_number = WTERMSIG(status);
			print_signal_error(signal_number);
			return signal_number + 128;
		}
	}
	return 1;
}

// new version of execute, i guess it's permanent now
int e(char *argv[], char *envp[]) {
	char path[PATH_MAX];

	if (contains(argv[0], '/'))
		return execute(argv, argv[0], envp);
	if (find_bin(argv[0], envp, path))
		return execute(argv, path, envp);
	print_error("Command not found", 127);
	return 127;
}

static int termios_init(void) {
	long ret = sys_ioctl(STDIN_FILENO, TCGETS, &orig_term);

	if (ret < 0)
		return -1;

	edit_term = orig_term;
	return 0;
}

static int termios_up(void) {
	edit_term.c_lflag &= ~(ICANON | ECHO);

	edit_term.c_cc[VMIN] = 1;
	edit_term.c_cc[VTIME] = 0;

	long ret = sys_ioctl(STDIN_FILENO, TCSETS, &edit_term);

	if (ret < 0)
		return -1;
	return 0;
}

static int termios_restore(void) {
	long ret = sys_ioctl(STDIN_FILENO, TCSETS, &orig_term);

	if (ret < 0)
		return -1;
	return 0;
}

int readline_init(void) {
	if (termios_init() < 0)
		return -1;

	if (termios_up() < 0)
		return -1;

	return 0;
}

void readline_shutdown(void) {
	termios_restore();
}

char *nullsh_readline(const char *prompt) {
	unsigned long len = 0;
	char c;
	unsigned long cursor = 0;

	print(STDOUT_FILENO, (char *)prompt);

	// only interactive if terminal, not file
	int interactive = (termios_init() == 0);

	if (interactive) {
		if (termios_up() < 0) {
			termios_restore();
			interactive = 0;
		}
	}

	while (len < CMD_MAX - 1) {
		long ret = sys_read(STDIN_FILENO, &c, 1);

		if (ret == -EINTR)
			continue;

		if (ret <= 0) {
			if (interactive)
				termios_restore();

			if (ret == 0 && len > 0) {
				nullsh_line[len] = '\0';
				return nullsh_line;
			}

			return 0;
		}

		if (c == '\n' || c == '\r') {
			if (interactive)
				print(STDOUT_FILENO, "\n");

			nullsh_line[len] = '\0';

			if (interactive)
				termios_restore();

			return nullsh_line;
		}

		// ctrl-d
		if (c == 4) {
			if (len == 0) {
				if (interactive)
					print(STDOUT_FILENO, "\n");

				if (interactive)
					termios_restore();

				return 0;
			}

			continue;
		}

		// ESC checking
		if (c == 27) {
			char esc[3];

			sys_read(STDIN_FILENO, &esc[0], 1);
			sys_read(STDIN_FILENO, &esc[1], 1);

			if (esc[0] != '[')
				continue;

			switch (esc[1]) {
			case 'A':
				print(STDOUT_FILENO, "Up arrow, history back");
				break;
			case 'B':
				print(STDOUT_FILENO,
				      "Down arrow, history forward");
				break;
			case 'C':
				if (cursor < len) {
					++cursor;
					print(STDOUT_FILENO, "\033[1C");
				}
				break;
			case 'D':
				if (cursor > 0) {
					--cursor;
					print(STDOUT_FILENO, "\033[1D");
				}
				break;
			case '3':
				if (sys_read(STDIN_FILENO, &esc[2], 1) == 1 &&
				    esc[2] == '~') {
					if (cursor < len) {
						for (unsigned long i = cursor;
						     i < len; ++i) {
							nullsh_line[i] =
							    nullsh_line[i + 1];
						}

						--len;
						nullsh_line[len] = '\0';

						if (interactive) {
							print(STDOUT_FILENO,
							      &nullsh_line
								  [cursor]);
							print(STDOUT_FILENO,
							      " ");

							for (unsigned long i =
								 cursor;
							     i <= len; ++i) {
								print(
								    STDOUT_FILENO,
								    "\b");
							}
						}
					}
				}
				break;
			default:
				continue;
			}
		}

		// backspace logic
		if (c == 127 || c == '\b') {
			if (cursor > 0) {
				--len;
				--cursor;

				for (unsigned long i = cursor; i < len; ++i) {
					nullsh_line[i] = nullsh_line[i + 1];
				}

				nullsh_line[len] = '\0';

				if (interactive) {
					print(STDOUT_FILENO, "\b \b");
					print(STDOUT_FILENO,
					      &nullsh_line[cursor]);
					print(STDOUT_FILENO, " ");

					for (unsigned long i = cursor; i <= len;
					     ++i) {
						print(STDOUT_FILENO, "\b");
					}
				}
			}

			continue;
		}

		if (c >= ' ' && c != 127) {
			for (unsigned long i = len; i > cursor; --i) {
				nullsh_line[i] = nullsh_line[i - 1];
			}
			nullsh_line[cursor] = c;
			++len;
			++cursor;
			nullsh_line[len] = '\0';

			if (interactive) {
				print(STDOUT_FILENO, &nullsh_line[cursor - 1]);

				for (unsigned long i = cursor; i < len; ++i) {
					print(STDOUT_FILENO, "\033[1D");
				}
			}
		}
	}

	nullsh_line[len] = '\0';

	if (interactive)
		termios_restore();

	print(STDOUT_FILENO, "\n");
	return nullsh_line;
}

void nullsh_main(int argc, char *argv[], char *envp[]) {
	struct utsname uts;
	sys_uname(&uts);

	char *input = 0;
	char *parsed[MAX_ARGS];

	char path[PATH_MAX];
	sys_getcwd(path, PATH_MAX);

	int lastexit = 0;

	char *user;

	user = env_get("USER", envp);
	print_prompt(user, uts.nodename, lastexit, path);

	while ((input = nullsh_readline("$> ")) != 0) {
		if (linegraph(input)) {
			print_prompt(user, uts.nodename, lastexit, path);
			continue;
		}
		// add_history(input);

		parse_input(input, parsed);

		// determine if builtin or run program
		if (lineeq(parsed[0], "exit") == 0) {
			print(STDOUT_FILENO, "Exiting shell...\n");
			sys_exit(0);
		} else if (lineeq(parsed[0], "help") == 0) {
			print(1, "This is help text for nullsh "
				 "v0.1.0\nBuiltins "
				 "are \"exit\", \"help\", "
				 "\"pwd\", and \"cd\"\n\n");
		} else if (lineeq(parsed[0], "pwd") == 0) {
			sys_getcwd(path, PATH_MAX);
			print(1, path);
			print(1, "\n");
		} else if (lineeq(parsed[0], "cd") == 0) {
			lastexit = sys_chdir(parsed[1]);
			if (lastexit < 0)
				print_error("Could not find directory", 1);
			sys_getcwd(path, PATH_MAX);

		} else {
			lastexit = e(parsed, envp);
		}

		print_prompt(user, uts.nodename, lastexit, path);
	}

	sys_exit(0);
}

// overdocumenting this next part becuase i will forget how it works
// first line here makes sure the stack keeps args on top
__attribute__((naked)) void _start(void) {
	__asm__ volatile(
	    // pop and load argc
	    "pop %rdi\n\t"

	    // move argv
	    "mov %rsp, %rsi\n\t"

	    // weird hidden math, for displacement(base, index, scale)
	    // address = base + (index * scale) +
	    // displacement
	    "lea 8(%rsi,%rdi,8), %rdx\n\t"

	    // 16 byte stack stafety, prevents segfaults sometimes
	    "and $-16, %rsp\n\t"

	    "call nullsh_main\n\t");
}
