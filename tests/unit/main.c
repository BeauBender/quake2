/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * tests/unit/main.c -- the unit-test runner (see test.h). Replaces
 * src/backends/unix/main.c in the dedicated server's object list: it
 * starts the engine's early subsystems the way Qcommon_Init does (zone,
 * argv, command buffer, commands, cvars, filesystem on a scratch data
 * directory) and then runs every registered test.
 *
 * Usage: yq2test [--update-pins] [--pins DIR] [--verbose] [--list] [NAME...]
 *   NAME selects the tests whose name contains it (default: all).
 *
 * =======================================================================
 */

#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test.h"

static t_case_t *t_cases;
static int t_ncases;

unsigned t_child_timeout = 10;

/* The process that created t_tmp; only it removes the directory (a forked
   child runs the same atexit handlers when it exits). */
static pid_t t_owner;

/* Set in a t_expect_exit child: t_fail also counts into t_child_fails, a
   page shared with the parent, so a check that fails in the child fails
   the test even though the child's own counters die with it. */
static int t_in_child;
static volatile int *t_child_fails;

static int t_update;
static int t_verbose;
static const char *t_pinsdir = "tests/pins/unit";
static char t_tmp[MAX_OSPATH];

static const t_case_t *t_current;
static int t_current_failures;
static jmp_buf t_test_jmp;
static jmp_buf t_runner_abortframe;
static int t_pins_written;

/* Console capture: rd_buf is the engine's redirect buffer, flushed into cap. */
static char t_rd_buf[8192];
static t_text_t t_cap;

void
t_register(t_case_t *tc)
{
	tc->next = t_cases;
	t_cases = tc;
	t_ncases++;
}

void
t_fail(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	t_current_failures++;

	if (t_in_child && t_child_fails)
	{
		(*t_child_fails)++;
	}

	fprintf(stderr, "FAIL %s (%s:%d): ", t_current ? t_current->name : "?", file, line);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void
t_abort_test(void)
{
	longjmp(t_test_jmp, 1);
}

void
t_rearm_abortframe(void)
{
	memcpy(abortframe, t_runner_abortframe, sizeof(jmp_buf));
}

/* ---- text buffers ---- */

void
t_text_printf(t_text_t *t, const char *fmt, ...)
{
	va_list ap;
	int n;

	for (;;)
	{
		size_t avail = t->size - t->len;

		va_start(ap, fmt);
		n = vsnprintf(t->data ? t->data + t->len : NULL, avail, fmt, ap);
		va_end(ap);

		if (n < 0)
		{
			return;
		}

		if ((size_t)n < avail)
		{
			t->len += n;
			return;
		}

		t->size = (t->size + n + 1) * 2;
		t->data = realloc(t->data, t->size);

		if (!t->data)
		{
			fprintf(stderr, "yq2test: out of memory\n");
			exit(2);
		}
	}
}

void
t_text_hex(t_text_t *t, const void *data, size_t len)
{
	const unsigned char *p = data;
	size_t i;

	for (i = 0; i < len; i++)
	{
		t_text_printf(t, "%02x%s", p[i], ((i % 16) == 15 || i == len - 1) ? "\n" : " ");
	}

	if (len == 0)
	{
		t_text_printf(t, "(empty)\n");
	}
}

void
t_text_free(t_text_t *t)
{
	free(t->data);
	t->data = NULL;
	t->len = t->size = 0;
}

/* ---- console capture ---- */

static void
t_rd_flush(int target, char *buffer)
{
	(void)target;
	t_text_printf(&t_cap, "%s", buffer);
}

void
t_console_reset(void)
{
	t_rd_buf[0] = '\0';
	t_cap.len = 0;

	if (t_cap.data)
	{
		t_cap.data[0] = '\0';
	}
}

void
t_console_rearm(void)
{
	Com_BeginRedirect(1, t_rd_buf, sizeof(t_rd_buf), t_rd_flush);
}

const char *
t_console(void)
{
	t_rd_flush(1, t_rd_buf);
	t_rd_buf[0] = '\0';
	return t_cap.data ? t_cap.data : "";
}

/* ---- scratch files ---- */

const char *
t_tmpdir(void)
{
	return t_tmp;
}

static void
t_mkdirs(char *path)
{
	char *p;

	for (p = path + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = '\0';
			mkdir(path, 0700);
			*p = '/';
		}
	}
}

void
t_write_file(const char *relpath, const void *data, size_t len)
{
	char path[MAX_OSPATH];
	FILE *f;

	if (!t_tmp[0])
	{
		fprintf(stderr, "yq2test: no scratch directory, refusing to write %s\n", relpath);
		exit(2);
	}

	snprintf(path, sizeof(path), "%s/%s", t_tmp, relpath);
	t_mkdirs(path);

	f = fopen(path, "wb");

	if (!f || fwrite(data, 1, len, f) != len)
	{
		fprintf(stderr, "yq2test: cannot write %s: %s\n", path, strerror(errno));
		exit(2);
	}

	fclose(f);
}

/* Removes a directory tree (no nftw: glibc hides it by default). */
static void
t_rm_tree(const char *path)
{
	struct stat st;
	DIR *d;
	struct dirent *e;

	if (lstat(path, &st) != 0)
	{
		return;
	}

	if (S_ISDIR(st.st_mode) && (d = opendir(path)) != NULL)
	{
		while ((e = readdir(d)) != NULL)
		{
			char sub[MAX_OSPATH];

			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			{
				continue;
			}

			snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
			t_rm_tree(sub);
		}

		closedir(d);
	}

	remove(path);
}

/* ---- forked expectations ---- */

int
t_expect_exit(void (*fn)(void *), void *arg, char *out, size_t outlen)
{
	int fds[2];
	pid_t pid;
	int status;
	size_t got = 0;
	ssize_t n;

	fflush(stdout);
	fflush(stderr);
	out[0] = '\0';

	if (!t_child_fails)
	{
		void *page = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANON, -1, 0);

		if (page == MAP_FAILED)
		{
			t_fail(__FILE__, __LINE__, "mmap: %s", strerror(errno));
			return -1;
		}

		t_child_fails = page;
	}

	*t_child_fails = 0;

	if (pipe(fds) != 0)
	{
		t_fail(__FILE__, __LINE__, "pipe: %s", strerror(errno));
		return -1;
	}

	pid = fork();

	if (pid < 0)
	{
		t_fail(__FILE__, __LINE__, "fork: %s", strerror(errno));
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	if (pid == 0)
	{
		t_in_child = 1;
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		dup2(fds[1], STDERR_FILENO);
		close(fds[1]);

		/* A child that hangs must not hang the suite (and the gate):
		   SIGALRM's default action ends it. */
		alarm(t_child_timeout);

		/* Engine output goes to the real console (the pipe) from here. */
		Com_EndRedirect();

		/* An ERR_DROP in the child must not resume the parent's run loop. */
		if (YQ2_SETJMP(abortframe))
		{
			printf("yq2test-child: Com_Error(ERR_DROP)\n");
			fflush(stdout);
			exit(3);
		}

		fn(arg);
		fflush(stdout);
		exit(0);
	}

	close(fds[1]);

	while (got + 1 < outlen && (n = read(fds[0], out + got, outlen - got - 1)) > 0)
	{
		got += n;
	}

	/* Drain whatever does not fit. */
	{
		char sink[512];

		while (read(fds[0], sink, sizeof(sink)) > 0)
		{
		}
	}

	out[got] = '\0';
	close(fds[0]);

	if (waitpid(pid, &status, 0) != pid)
	{
		t_fail(__FILE__, __LINE__, "waitpid: %s", strerror(errno));
		return -1;
	}

	if (*t_child_fails)
	{
		/* The child's FAIL lines went into its output, not to stderr. */
		t_fail(__FILE__, __LINE__, "%d check(s) failed in the forked child; its output:\n%s",
			*t_child_fails, out);
	}

	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
	{
		/* Said in the output, which a failing caller prints, rather than
		   on stderr: the runner self-test expects the timeout. */
		snprintf(out + got, outlen - got, "yq2test: forked child timed out after %u s\n",
			t_child_timeout);
		return T_EXIT_TIMEOUT;
	}

	if (!WIFEXITED(status))
	{
		return -1;
	}

	return WEXITSTATUS(status);
}

/* ---- pins ---- */

#define T_PIN_END "# ---\n"

static char *
t_read_all(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long size;

	if (!f)
	{
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	buf = malloc(size + 1);

	if (!buf || fread(buf, 1, size, f) != (size_t)size)
	{
		fclose(f);
		free(buf);
		return NULL;
	}

	buf[size] = '\0';
	fclose(f);
	*len = size;
	return buf;
}

static void
t_pin_diff(const char *name, const char *want, const char *got)
{
	int line = 1;
	const char *w = want, *g = got;

	while (*w && *g && *w == *g)
	{
		if (*w == '\n')
		{
			line++;
		}

		w++;
		g++;
	}

	/* Back up to the start of the differing line. */
	while (w > want && w[-1] != '\n')
	{
		w--;
		g--;
	}

	fprintf(stderr, "  pin %s differs at body line %d\n", name, line);
	fprintf(stderr, "  want: %.*s\n", (int)strcspn(w, "\n"), w);
	fprintf(stderr, "  got:  %.*s\n", (int)strcspn(g, "\n"), g);
}

void
t_pin(const char *name, const char *text)
{
	char path[MAX_OSPATH];
	size_t len;
	char *file;
	const char *body;

	snprintf(path, sizeof(path), "%s/%s.pin", t_pinsdir, name);

	if (t_update)
	{
		FILE *f = fopen(path, "wb");

		if (!f)
		{
			t_fail(__FILE__, __LINE__, "cannot write pin %s: %s", path, strerror(errno));
			return;
		}

		fprintf(f, "# Pin \"%s\", produced by test %s (%s).\n", name,
				t_current->name, t_current->file);
		fprintf(f, "# Generated from real engine output by `make test UPDATE_PINS=1`\n");
		fprintf(f, "# (build/<release|debug>/test/yq2test --update-pins). Do not edit by\n");
		fprintf(f, "# hand: a change to the body below is a behaviour change and its commit\n");
		fprintf(f, "# says why. The body starts after the separator line.\n");
		fputs(T_PIN_END, f);
		fputs(text, f);
		fclose(f);
		t_pins_written++;
		return;
	}

	file = t_read_all(path, &len);

	if (!file)
	{
		t_fail(__FILE__, __LINE__, "pin %s is missing (run `make test UPDATE_PINS=1`)", path);
		return;
	}

	body = strstr(file, "\n" T_PIN_END);

	if (!body)
	{
		t_fail(__FILE__, __LINE__, "pin %s has no provenance header", path);
	}
	else if (strcmp(body + strlen("\n" T_PIN_END), text) != 0)
	{
		t_fail(__FILE__, __LINE__, "pin %s does not match", path);
		t_pin_diff(name, body + strlen("\n" T_PIN_END), text);
	}

	free(file);
}

/* ---- cleanup and crash reporting ---- */

static void
t_cleanup_tmp(void)
{
	if (t_tmp[0] && getpid() == t_owner)
	{
		t_rm_tree(t_tmp);
		t_tmp[0] = '\0';
	}
}

static const int t_crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
static const int t_stop_signals[] = { SIGINT, SIGTERM, SIGHUP };
static struct sigaction t_old_actions[NSIG];

static void
t_write_str(const char *s)
{
	ssize_t r = write(STDERR_FILENO, s, strlen(s));

	(void)r;
}

/*
 * A test that crashes or is interrupted: name the test (write() is safe
 * here, stdio is not), remove the scratch directory (best effort: not
 * async-signal-safe, but the process is ending anyway), then hand the
 * signal on to whatever handled it before (the default action, or the
 * sanitizer's reporter, which then prints its own diagnosis).
 */
static void
t_on_signal(int sig, siginfo_t *info, void *uctx)
{
	int stop = sig == SIGINT || sig == SIGTERM || sig == SIGHUP;
	int sent = info == NULL || info->si_code == SI_USER || info->si_code <= 0;

	(void)uctx;

	t_write_str("yq2test: ");
	t_write_str(t_current ? t_current->name : "(no test running)");
	t_write_str(sig == SIGINT || sig == SIGTERM || sig == SIGHUP ?
		": interrupted\n" : ": died by a signal\n");
	t_cleanup_tmp();
	sigaction(sig, &t_old_actions[sig], NULL);

	/* A real fault re-executes the faulting instruction on return and so
	   reaches the previous handler with its real context. A signal that
	   was sent (kill, raise, abort) is sent again. */
	if (stop || sent)
	{
		raise(sig);

		/* Still here: the signal was blocked or ignored. Never go on
		   without the scratch directory. */
		if (stop)
		{
			_exit(128 + sig);
		}
	}
}

static void
t_install_handlers(void)
{
	struct sigaction sa;
	size_t i;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = t_on_signal;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < sizeof(t_crash_signals) / sizeof(t_crash_signals[0]); i++)
	{
		sigaction(t_crash_signals[i], &sa, &t_old_actions[t_crash_signals[i]]);
	}

	for (i = 0; i < sizeof(t_stop_signals) / sizeof(t_stop_signals[0]); i++)
	{
		struct sigaction old;

		/* A signal the caller ignores (nohup, a background job) stays
		   ignored: catching it would end the run's directory but not the run. */
		if (sigaction(t_stop_signals[i], NULL, &old) == 0 && old.sa_handler == SIG_IGN)
		{
			continue;
		}

		sigaction(t_stop_signals[i], &sa, &t_old_actions[t_stop_signals[i]]);
	}
}

/* ---- runner ---- */

static int
t_case_cmp(const void *a, const void *b)
{
	const t_case_t *x = *(t_case_t * const *)a;
	const t_case_t *y = *(t_case_t * const *)b;
	int r = strcmp(x->file, y->file);

	return r ? r : strcmp(x->name, y->name);
}

static void
t_engine_init(void)
{
	static char *argv[] = { "yq2test" };

	/* The order of Qcommon_Init. */
	Z_Init();
	COM_InitArgv(1, argv);
	Swap_Init();
	Cbuf_Init();
	Cmd_Init();
	Cvar_Init();

	/* Search path: <tmp>/data/baseq2 and the binary's own directory
	   (FS_BuildRawPath always adds it), never the user's home: the
	   same switch as the -portable command line option. */
	is_portable = true;
	t_write_file("data/baseq2/.keep", "", 0);
	snprintf(datadir, sizeof(datadir), "%s/data", t_tmp);
	FS_InitFilesystem();

	/* The generic search path leaves fs_gamedir at the last raw path
	   (the binary's directory); resetting to baseq2 points it at the
	   first, <tmp>/data/baseq2, so that what the engine writes while
	   baseq2 is the game lands in the scratch directory. Not everything
	   stays there: FS_BuildRawPath always puts the binary's own
	   directory on the search path, and the engine creates
	   <bindir>/baseq2/scrnshot at start-up and, when a test switches to
	   a mod, <bindir>/<mod>/scrnshot (fs_gamedir is <bindir>/<mod> while
	   the mod is active). No source change can avoid that, so these
	   empty directories are left under build/; the tests write no file
	   while a mod is active. scripts/coverage.sh starts from an empty
	   build/coverage so that they cannot make two runs differ. */
	FS_BuildGameSpecificSearchPath("");

	developer = Cvar_Get("developer", "0", 0);
	dedicated = Cvar_Get("dedicated", "1", CVAR_NOSET);
	SV_Init();
}

int
main(int argc, char **argv)
{
	t_case_t **list, *tc;
	int i, n = 0, failed = 0, ran = 0;
	int list_only = 0;
	int filtered = 0;
	const char *tmpbase;

	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--update-pins") == 0)
		{
			t_update = 1;
		}
		else if (strcmp(argv[i], "--verbose") == 0)
		{
			t_verbose = 1;
		}
		else if (strcmp(argv[i], "--list") == 0)
		{
			list_only = 1;
		}
		else if (strcmp(argv[i], "--pins") == 0 && i + 1 < argc)
		{
			t_pinsdir = argv[++i];
		}
		else if (argv[i][0] == '-')
		{
			fprintf(stderr, "usage: %s [--update-pins] [--pins DIR] [--verbose] [--list] [NAME...]\n", argv[0]);
			return 2;
		}
		else
		{
			filtered = 1;
		}
	}

	list = malloc(sizeof(*list) * (t_ncases + 1));

	for (tc = t_cases; tc; tc = tc->next)
	{
		list[n++] = tc;
	}

	qsort(list, n, sizeof(*list), t_case_cmp);

	/* --list is not a test run and exits 0. The gate cannot reach this:
	   scripts/test.sh and scripts/coverage.sh run `make ... TEST_ARGS= test`,
	   so no runner option from the environment gets through. */
	if (list_only)
	{
		for (i = 0; i < n; i++)
		{
			printf("%s\t%s\n", list[i]->name, list[i]->file);
		}

		return 0;
	}

	tmpbase = getenv("TMPDIR");
	snprintf(t_tmp, sizeof(t_tmp), "%s/yq2test.XXXXXX", (tmpbase && tmpbase[0]) ? tmpbase : "/tmp");

	if (!mkdtemp(t_tmp))
	{
		fprintf(stderr, "yq2test: mkdtemp: %s\n", strerror(errno));
		return 2;
	}

	/* Remove the scratch directory however the run ends: atexit for the
	   engine's own exit() paths (Sys_Error, ERR_FATAL), signal handlers
	   for a crash or an interrupt, which also name the running test. */
	t_owner = getpid();
	atexit(t_cleanup_tmp);
	t_install_handlers();

	/* The filesystem stores realpath()s (/var is /private/var on macOS);
	   use the same spelling so tests can match paths in its output. */
	{
		char real[PATH_MAX];

		if (realpath(t_tmp, real) && strlen(real) < sizeof(t_tmp))
		{
			Q_strlcpy(t_tmp, real, sizeof(t_tmp));
		}
	}

	/* Engine start-up errors are fatal to the run. */
	Com_BeginRedirect(1, t_rd_buf, sizeof(t_rd_buf), t_rd_flush);

	if (YQ2_SETJMP(abortframe))
	{
		fprintf(stderr, "yq2test: Com_Error during engine start-up:\n%s\n", t_console());
		return 2;
	}

	t_engine_init();

	for (i = 0; i < n; i++)
	{
		int j, selected = 1;

		for (j = 1; j < argc; j++)
		{
			if (argv[j][0] == '-')
			{
				if (strcmp(argv[j], "--pins") == 0)
				{
					j++;
				}

				continue;
			}

			selected = strstr(list[i]->name, argv[j]) != NULL;

			if (selected)
			{
				break;
			}
		}

		if (!selected)
		{
			continue;
		}

		t_current = list[i];
		t_current_failures = 0;
		t_console_rearm();
		t_console_reset();

		if (YQ2_SETJMP(abortframe))
		{
			t_fail(t_current->file, 0, "unexpected Com_Error(ERR_DROP); console:\n%s", t_console());
		}
		else
		{
			memcpy(t_runner_abortframe, abortframe, sizeof(jmp_buf));

			if (setjmp(t_test_jmp) == 0)
			{
				t_current->fn();
			}
		}

		ran++;

		if (t_current_failures)
		{
			failed++;
			fprintf(stderr, "not ok %s\n", t_current->name);
		}
		else if (t_verbose)
		{
			printf("ok %s\n", t_current->name);
		}

		if (t_verbose)
		{
			fputs(t_console(), stdout);
		}
	}

	Com_EndRedirect();
	t_current = NULL;
	t_cleanup_tmp();
	t_text_free(&t_cap);
	free(list);

	if (t_update)
	{
		printf("yq2test: wrote %d pins to %s\n", t_pins_written, t_pinsdir);
	}

	printf("yq2test: %d tests, %d failed\n", ran, failed);

	/* A run that ran nothing proved nothing: never report it as a pass. */
	if (ran == 0)
	{
		if (filtered)
		{
			fprintf(stderr, "yq2test: no test name matches the filter\n");
		}
		else
		{
			fprintf(stderr, "yq2test: no tests are registered\n");
		}

		return 2;
	}

	return failed ? 1 : 0;
}
