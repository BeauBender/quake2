/*
 * scripts/lib/supervise.c -- the harness's process supervisor (TASK218).
 *
 *   supervise [--timeout S] [--grace S] [--after-kill S] [--events FD]
 *             [--name LABEL] -- cmd args...
 *
 * Runs cmd as the leader of a process group of its own (posix_spawn with
 * POSIX_SPAWN_SETPGROUP, every signal at its default, an empty mask) and
 * owns its reap: the leader stays unreaped (waitid with WNOWAIT) until no
 * other member of its group is left, so its pid, and so the group's id,
 * cannot go to another process while anything of the group runs. One
 * kqueue watches the leader and every member it can see (EVFILT_PROC,
 * NOTE_EXIT and NOTE_FORK), the deadline (EVFILT_TIMER) and INT, TERM and
 * HUP (EVFILT_SIGNAL; the three are blocked here). The members are listed
 * with proc_listpgrppids, never with ps.
 *
 * A stop sends TERM (or the forwarded signal) and CONT to the group, waits
 * --grace seconds (10) for it to empty, sends KILL to what is left and
 * waits --after-kill seconds (2) more. A stop starts at the deadline
 * (--timeout; none by default), at a forwarded signal, and when the leader
 * exits while members of its group are left. A process that moved itself
 * into another session or group is named (escaped) and never signalled; a
 * process that does so before this program sees it is not seen at all.
 * A signal ignored when this program starts stays ignored: it is named
 * (ignored) and not forwarded.
 *
 * Status: the command's own (128+n when a signal this program did not send
 * ended it); 124 when the deadline stopped it; 128+n when a forwarded
 * signal n stopped it; 125 when it cannot be run (or the usage is wrong);
 * 126 when the group is not empty --after-kill seconds after the KILL, or
 * this program cannot go on watching: the command may still run, and the
 * caller must not free what it holds.
 *
 * Events, with --events FD, one line each: "spawned pid=P pgid=P",
 * "exited status=S" (the leader's exit, seen), "deadline",
 * "signal name=N", "ignored name=N", "term-sent name=N", "grace-end",
 * "kill-sent", "escaped pid=Q", "group-empty", "group-left count=K" and
 * "reaped status=S". On standard error it writes only its own lines, each
 * starting "supervise: ", and never a job notice: it exits with a status
 * and is never ended by INT, TERM or HUP. A closed standard descriptor is
 * opened on /dev/null first, for this program and the command.
 *
 * The test build (-DSUPERVISE_TEST) reads the timers from the descriptor
 * SUPERVISE_TEST_CLOCK_FD when it is set: the byte D ends the deadline, G
 * the grace and A the after-kill wait (a byte that comes before its timer
 * is set is kept for it), and P answers "probe" once everything the kernel
 * posted before it has been handled. Built on first use by
 * scripts/lib/supervise.sh; tested by tests/check-supervise.sh.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/proc_info.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

#define SV_MAXPIDS 4096
#define SV_DEADLINE 124

enum { T_DEADLINE, T_GRACE, T_AFTER, T_COUNT };
enum { PH_RUN, PH_GRACE, PH_AFTER };

static const int sv_watch[] = { SIGINT, SIGTERM, SIGHUP };

static char sv_label[65] = "supervise";
static int sv_events = -1;
static int sv_kq = -1;
static pid_t sv_leader;
static long long sv_ms[T_COUNT] = { 0, 10000, 2000 };
static int sv_has_timeout;
static int sv_fired[T_COUNT];
static int sv_ignored[NSIG];
static int sv_phase = PH_RUN;
static int sv_why;         /* 0, SV_DEADLINE, or 128+n: what started the stop */
static int sv_exited;      /* the leader's exit has been seen */
static int sv_status;      /* and its status: the exit code, or 128+n */

/* the processes watched: the leader and the members seen; gone once their exit is seen */
static pid_t sv_known[SV_MAXPIDS];
static unsigned char sv_gone[SV_MAXPIDS];
static unsigned char sv_named[SV_MAXPIDS];
static int sv_nknown;

#ifdef SUPERVISE_TEST
static int sv_clock = -1;
static int sv_armed[T_COUNT];
static int sv_pending[T_COUNT];
static int sv_probes;
#endif

static int
sv_write_all(int fd, const char *p, size_t n)
{
	while (n > 0)
	{
		ssize_t w = write(fd, p, n);

		if (w < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return -1;
		}
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

static int
sv_vline(char *line, size_t size, size_t used, const char *fmt, va_list ap)
{
	int m = vsnprintf(line + used, size - used - 1, fmt, ap);

	if (m < 0)
	{
		m = 0;
	}
	if (used + (size_t)m > size - 2)
	{
		m = (int)(size - 2 - used);
	}
	line[used + (size_t)m] = '\n';
	return (int)used + m + 1;
}

/* sv_say: one line on standard error, "supervise: <label>: ..." */
static void __attribute__((format(printf, 1, 2)))
sv_say(const char *fmt, ...)
{
	char line[512];
	va_list ap;
	int n = snprintf(line, sizeof(line), "supervise: %s: ", sv_label);

	va_start(ap, fmt);
	n = sv_vline(line, sizeof(line), (size_t)n, fmt, ap);
	va_end(ap);
	(void)sv_write_all(STDERR_FILENO, line, (size_t)n);
}

/* sv_emit: one event line on --events; a reader that went away ends the events, not the run */
static void __attribute__((format(printf, 1, 2)))
sv_emit(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	int n;

	if (sv_events < 0)
	{
		return;
	}
	va_start(ap, fmt);
	n = sv_vline(line, sizeof(line), 0, fmt, ap);
	va_end(ap);
	if (sv_write_all(sv_events, line, (size_t)n) < 0)
	{
		sv_events = -1;
	}
}

static const char *
sv_signame(int sig)
{
	switch (sig)
	{
		case SIGINT: return "INT";
		case SIGTERM: return "TERM";
		case SIGHUP: return "HUP";
		default: return "?";
	}
}

/* sv_seconds: "N" or "N.f" (9 digits, 3 decimals at most) as milliseconds */
static int
sv_seconds(const char *s, long long *ms)
{
	long long v = 0, f = 100;
	int digits = 0;

	for (; *s >= '0' && *s <= '9'; s++)
	{
		if (++digits > 9)
		{
			return -1;
		}
		v = v * 10 + (*s - '0');
	}
	if (digits == 0)
	{
		return -1;
	}
	v *= 1000;
	if (*s == '.')
	{
		s++;
		if (*s < '0' || *s > '9')
		{
			return -1;
		}
		for (digits = 0; *s >= '0' && *s <= '9'; s++)
		{
			if (++digits > 3)
			{
				return -1;
			}
			v += (*s - '0') * f;
			f /= 10;
		}
	}
	if (*s != '\0')
	{
		return -1;
	}
	*ms = v;
	return 0;
}

/* sv_fd: a descriptor number above 2 that is open, or -1 */
static int
sv_fd(const char *s)
{
	long v = 0;
	const char *p;

	for (p = s; *p >= '0' && *p <= '9' && v < 100000; p++)
	{
		v = v * 10 + (*p - '0');
	}
	if (p == s || *p != '\0' || v < 3 || v >= 100000 || fcntl((int)v, F_GETFD) < 0)
	{
		return -1;
	}
	return (int)v;
}

static int
sv_is_label(const char *s)
{
	size_t n = strlen(s);

	return n >= 1 && n <= 64 && strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._:@+-") == n;
}

static int
sv_index(pid_t pid)
{
	int i;

	for (i = 0; i < sv_nknown; i++)
	{
		if (sv_known[i] == pid)
		{
			return i;
		}
	}
	return -1;
}

/* sv_alive: pid runs (a zombie does not), with its group in *pgid */
static int
sv_alive(pid_t pid, pid_t *pgid)
{
	struct proc_bsdinfo bi;

	if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, (int)sizeof(bi)) != (int)sizeof(bi))
	{
		return 0;
	}
	*pgid = (pid_t)bi.pbi_pgid;
	return 1;
}

/*
 * sv_track: watch pid and every child of it not yet watched (they may have
 * forked before the watch was set). A pid whose watch cannot be set is gone.
 */
static void
sv_track(pid_t pid)
{
	static pid_t stack[SV_MAXPIDS], kids[SV_MAXPIDS];
	int top = 0, i, n;
	struct kevent ev;

	stack[top++] = pid;
	while (top > 0)
	{
		pid_t p = stack[--top];

		if (p <= 0 || sv_index(p) >= 0 || sv_nknown >= SV_MAXPIDS)
		{
			continue;
		}
		EV_SET(&ev, p, EVFILT_PROC, EV_ADD, NOTE_EXIT | NOTE_FORK, 0, NULL);
		if (kevent(sv_kq, &ev, 1, NULL, 0, NULL) < 0)
		{
			continue;
		}
		sv_known[sv_nknown] = p;
		sv_gone[sv_nknown] = 0;
		sv_named[sv_nknown] = 0;
		sv_nknown++;
		n = proc_listchildpids(p, kids, (int)sizeof(kids));
		for (i = 0; i < n && i < SV_MAXPIDS && top < SV_MAXPIDS; i++)
		{
			stack[top++] = kids[i];
		}
	}
}

/*
 * sv_scan: the members of the group other than the leader that still run,
 * each one watched; names each watched process that runs outside the group.
 * A process whose exit was seen is not counted, even while it is still
 * being torn down. A list that fills the buffer counts one more.
 */
static int
sv_scan(void)
{
	static pid_t list[SV_MAXPIDS];
	int n, i, k, live = 0;
	pid_t g;

	n = proc_listpgrppids(sv_leader, list, (int)sizeof(list));
	if (n < 0)
	{
		return 1;
	}
	for (i = 0; i < n && i < SV_MAXPIDS; i++)
	{
		if (list[i] == sv_leader || list[i] <= 0)
		{
			continue;
		}
		sv_track(list[i]);
		k = sv_index(list[i]);
		if ((k < 0 || !sv_gone[k]) && sv_alive(list[i], &g) && g == sv_leader)
		{
			live++;
		}
	}
	if (n >= SV_MAXPIDS)
	{
		live++;
	}
	for (i = 0; i < sv_nknown; i++)
	{
		if (sv_known[i] == sv_leader || sv_named[i])
		{
			continue;
		}
		if (!sv_alive(sv_known[i], &g))
		{
			if (sv_gone[i])
			{
				/* torn down: forget it, so its pid can be a new process */
				sv_nknown--;
				sv_known[i] = sv_known[sv_nknown];
				sv_gone[i] = sv_gone[sv_nknown];
				sv_named[i] = sv_named[sv_nknown];
				i--;
			}
			continue;
		}
		if (!sv_gone[i] && g != sv_leader)
		{
			sv_named[i] = 1;
			sv_emit("escaped pid=%d", (int)sv_known[i]);
		}
	}
	return live;
}

/* sv_group: sig to the group, with the event that says so */
static void
sv_group(int sig, const char *event)
{
	if (sig != 0 && kill(-sv_leader, sig) < 0 && errno != ESRCH)
	{
		int e = errno;

		/* the group holding the unreaped leader alone answers EPERM */
		if (sv_scan() > 0 || !sv_exited)
		{
			sv_say("cannot signal the group %d: %s", (int)sv_leader, strerror(e));
		}
	}
	if (event != NULL)
	{
		sv_emit("%s", event);
	}
}

static void
sv_arm(int t)
{
	struct kevent ev;

#ifdef SUPERVISE_TEST
	if (sv_clock >= 0)
	{
		if (sv_pending[t] > 0)
		{
			sv_pending[t]--;
			sv_fired[t] = 1;
		}
		else
		{
			sv_armed[t] = 1;
		}
		return;
	}
#endif
	EV_SET(&ev, t, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, (intptr_t)sv_ms[t], NULL);
	if (kevent(sv_kq, &ev, 1, NULL, 0, NULL) < 0)
	{
		sv_say("cannot set a timer: %s", strerror(errno));
		sv_fired[t] = 1;
	}
}

/* sv_peek: the leader's status, leaving it unreaped; 1 once it has exited, 0 not yet (WNOHANG) */
static int
sv_peek(int options)
{
	siginfo_t si;

	memset(&si, 0, sizeof(si));
	while (waitid(P_PID, (id_t)sv_leader, &si, WEXITED | WNOWAIT | options) < 0)
	{
		if (errno != EINTR)
		{
			return -1;
		}
	}
	if (si.si_pid != sv_leader)
	{
		return 0;
	}
	sv_status = (si.si_code == CLD_EXITED) ? si.si_status : 128 + si.si_status;
	if (!sv_exited)
	{
		sv_exited = 1;
		sv_emit("exited status=%d", sv_status);
	}
	return 1;
}

/* sv_finish: the group is empty: reap the leader, and the final status */
static int
sv_finish(void)
{
	siginfo_t si;

	sv_emit("group-empty");
	memset(&si, 0, sizeof(si));
	while (waitid(P_PID, (id_t)sv_leader, &si, WEXITED) < 0)
	{
		if (errno != EINTR)
		{
			sv_say("cannot reap the command %d: %s", (int)sv_leader, strerror(errno));
			return 126;
		}
	}
	sv_emit("reaped status=%d", sv_status);
	if (sv_why == SV_DEADLINE)
	{
		return 124;
	}
	if (sv_why != 0)
	{
		return sv_why;
	}
	return sv_status;
}

static void
sv_stop(int sig)
{
	sv_phase = PH_GRACE;
	sv_group(sig, NULL);
	sv_group(SIGCONT, NULL);
	sv_emit("term-sent name=%s", sv_signame(sig));
	sv_arm(T_GRACE);
}

/* sv_step: the state after a batch of events; -1 to go on, else the status */
static int
sv_step(void)
{
	int live = sv_scan();

	if (sv_exited && live == 0)
	{
		return sv_finish();
	}
	if (sv_phase == PH_RUN && (sv_why != 0 || sv_exited))
	{
		sv_stop((sv_why == 0 || sv_why == SV_DEADLINE) ? SIGTERM : sv_why - 128);
	}
	if (sv_phase == PH_GRACE && sv_fired[T_GRACE])
	{
		sv_fired[T_GRACE] = 0;
		sv_emit("grace-end");
		sv_group(SIGKILL, "kill-sent");
		sv_phase = PH_AFTER;
		sv_arm(T_AFTER);
	}
	if (sv_phase == PH_AFTER && sv_fired[T_AFTER])
	{
		live = sv_scan();
		if (live == 0 && sv_exited)
		{
			return sv_finish();
		}
		live += !sv_exited;
		sv_emit("group-left count=%d", live);
		sv_say("%d process%s of the group %d left %lld ms after the KILL: it may still run",
			live, live == 1 ? "" : "es", (int)sv_leader, sv_ms[T_AFTER]);
		return 126;
	}
	return -1;
}

#ifdef SUPERVISE_TEST
/* sv_tick: the bytes on the test clock */
static void
sv_tick(void)
{
	char buf[64];
	ssize_t n, i;
	struct kevent ev;

	n = read(sv_clock, buf, sizeof(buf));
	if (n < 0)
	{
		if (errno == EINTR || errno == EAGAIN)
		{
			return;
		}
		n = 0;
	}
	if (n == 0)
	{
		EV_SET(&ev, sv_clock, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		(void)kevent(sv_kq, &ev, 1, NULL, 0, NULL);
		sv_clock = -2;
		return;
	}
	for (i = 0; i < n; i++)
	{
		int t = buf[i] == 'D' ? T_DEADLINE : buf[i] == 'G' ? T_GRACE : buf[i] == 'A' ? T_AFTER : -1;

		if (buf[i] == 'P')
		{
			sv_probes++;
		}
		else if (t >= 0 && sv_armed[t])
		{
			sv_armed[t] = 0;
			sv_fired[t] = 1;
		}
		else if (t >= 0)
		{
			sv_pending[t]++;
		}
	}
}
#endif

/* sv_batch: one batch of events, the leader's and members' first, the clock's last */
static void
sv_batch(const struct kevent *evs, int n)
{
	int i, pass;

	for (pass = 0; pass < 3; pass++)
	{
		for (i = 0; i < n; i++)
		{
			const struct kevent *e = &evs[i];

			if (pass == 0 && e->filter == EVFILT_PROC)
			{
				pid_t p = (pid_t)e->ident;
				int k = sv_index(p);

				if ((e->fflags & NOTE_FORK) && !(e->fflags & NOTE_EXIT))
				{
					static pid_t kids[SV_MAXPIDS];
					int c, j = proc_listchildpids(p, kids, (int)sizeof(kids));

					for (c = 0; c < j && c < SV_MAXPIDS; c++)
					{
						sv_track(kids[c]);
					}
				}
				if ((e->fflags & NOTE_EXIT) && k >= 0)
				{
					sv_gone[k] = 1;
				}
				if ((e->fflags & NOTE_EXIT) && p == sv_leader)
				{
					(void)sv_peek(0);
				}
			}
			else if (pass == 1 && e->filter == EVFILT_SIGNAL)
			{
				int sig = (int)e->ident;

				if (sv_ignored[sig])
				{
					sv_emit("ignored name=%s", sv_signame(sig));
					continue;
				}
				sv_emit("signal name=%s", sv_signame(sig));
				if (sv_phase == PH_RUN && !sv_exited && sv_why == 0)
				{
					sv_why = 128 + sig;
				}
			}
			else if (pass == 2 && e->filter == EVFILT_TIMER)
			{
				if ((int)e->ident >= 0 && (int)e->ident < T_COUNT)
				{
					sv_fired[e->ident] = 1;
				}
			}
#ifdef SUPERVISE_TEST
			else if (pass == 2 && e->filter == EVFILT_READ && (int)e->ident == sv_clock)
			{
				sv_tick();
			}
#endif
		}
	}
	if (sv_fired[T_DEADLINE])
	{
		sv_fired[T_DEADLINE] = 0;
		if (sv_phase == PH_RUN && !sv_exited && sv_why == 0)
		{
			sv_emit("deadline");
			sv_why = SV_DEADLINE;
		}
	}
}

static int
sv_usage(const char *why)
{
	sv_say("%s; usage: supervise [--timeout S] [--grace S] [--after-kill S] [--events FD] [--name LABEL] -- cmd args...", why);
	return 125;
}

int
main(int argc, char **argv)
{
	posix_spawnattr_t attr;
	posix_spawn_file_actions_t fa;
	sigset_t block, none, dfl;
	struct sigaction sa;
	struct kevent ev;
	const char *base;
	int i, fd, err, st;
	size_t w;

	/* a closed standard descriptor is opened on /dev/null, so nothing below lands on it */
	for (fd = 0; fd <= 2; fd++)
	{
		if (fcntl(fd, F_GETFD) < 0 && errno == EBADF)
		{
			int nfd = open("/dev/null", O_RDWR);

			if (nfd >= 0 && nfd != fd)
			{
				(void)dup2(nfd, fd);
				(void)close(nfd);
			}
		}
	}

	for (i = 1; i < argc && strcmp(argv[i], "--") != 0; i += 2)
	{
		long long *ms = NULL;

		if (i + 1 >= argc)
		{
			return sv_usage("an option has no value");
		}
		if (strcmp(argv[i], "--timeout") == 0)
		{
			ms = &sv_ms[T_DEADLINE];
		}
		else if (strcmp(argv[i], "--grace") == 0)
		{
			ms = &sv_ms[T_GRACE];
		}
		else if (strcmp(argv[i], "--after-kill") == 0)
		{
			ms = &sv_ms[T_AFTER];
		}
		else if (strcmp(argv[i], "--events") == 0)
		{
			if ((sv_events = sv_fd(argv[i + 1])) < 0)
			{
				return sv_usage("--events is no open descriptor above 2");
			}
			continue;
		}
		else if (strcmp(argv[i], "--name") == 0)
		{
			if (!sv_is_label(argv[i + 1]))
			{
				return sv_usage("--name is 1 to 64 of A-Z a-z 0-9 . _ : @ + -");
			}
			snprintf(sv_label, sizeof(sv_label), "%s", argv[i + 1]);
			continue;
		}
		else
		{
			return sv_usage("an unknown option");
		}
		if (sv_seconds(argv[i + 1], ms) < 0)
		{
			return sv_usage("a time is seconds, N or N.fff");
		}
		if (ms == &sv_ms[T_DEADLINE])
		{
			sv_has_timeout = *ms > 0;
		}
	}
	if (i >= argc - 1)
	{
		return sv_usage("no command after --");
	}
	argv += i + 1;
	if (strcmp(sv_label, "supervise") == 0)
	{
		base = strrchr(argv[0], '/');
		base = base != NULL ? base + 1 : argv[0];
		w = strlen(base);
		if (sv_is_label(base) && w < sizeof(sv_label))
		{
			memcpy(sv_label, base, w + 1);
		}
	}

	/* the stop signals: their disposition at entry, then blocked and read from the kqueue */
	sigemptyset(&block);
	sigaddset(&block, SIGINT);
	sigaddset(&block, SIGTERM);
	sigaddset(&block, SIGHUP);
	for (i = 0; i < (int)(sizeof(sv_watch) / sizeof(sv_watch[0])); i++)
	{
		if (sigaction(sv_watch[i], NULL, &sa) == 0 && sa.sa_handler == SIG_IGN)
		{
			sv_ignored[sv_watch[i]] = 1;
		}
	}
	if (sigprocmask(SIG_BLOCK, &block, NULL) < 0)
	{
		sv_say("cannot block the stop signals: %s", strerror(errno));
		return 125;
	}
	signal(SIGPIPE, SIG_IGN);
	if ((sv_kq = kqueue()) < 0)
	{
		sv_say("cannot make a kqueue: %s", strerror(errno));
		return 125;
	}
	for (i = 0; i < (int)(sizeof(sv_watch) / sizeof(sv_watch[0])); i++)
	{
		EV_SET(&ev, sv_watch[i], EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
		if (kevent(sv_kq, &ev, 1, NULL, 0, NULL) < 0)
		{
			sv_say("cannot watch %s: %s", sv_signame(sv_watch[i]), strerror(errno));
			return 125;
		}
	}
#ifdef SUPERVISE_TEST
	if (getenv("SUPERVISE_TEST_CLOCK_FD") != NULL)
	{
		if ((sv_clock = sv_fd(getenv("SUPERVISE_TEST_CLOCK_FD"))) < 0)
		{
			return sv_usage("SUPERVISE_TEST_CLOCK_FD is no open descriptor above 2");
		}
		EV_SET(&ev, sv_clock, EVFILT_READ, EV_ADD, 0, 0, NULL);
		if (kevent(sv_kq, &ev, 1, NULL, 0, NULL) < 0)
		{
			sv_say("cannot watch the test clock: %s", strerror(errno));
			return 125;
		}
	}
#endif

	/* the command: a group of its own, every signal at its default, nothing blocked */
	sigemptyset(&none);
	sigfillset(&dfl);
	sigdelset(&dfl, SIGKILL);
	sigdelset(&dfl, SIGSTOP);
	if ((err = posix_spawnattr_init(&attr)) != 0 || (err = posix_spawn_file_actions_init(&fa)) != 0)
	{
		sv_say("cannot set up the spawn: %s", strerror(err));
		return 125;
	}
	err = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
	if (err == 0)
	{
		err = posix_spawnattr_setpgroup(&attr, 0);
	}
	if (err == 0)
	{
		err = posix_spawnattr_setsigdefault(&attr, &dfl);
	}
	if (err == 0)
	{
		err = posix_spawnattr_setsigmask(&attr, &none);
	}
	if (err == 0 && sv_events >= 0)
	{
		err = posix_spawn_file_actions_addclose(&fa, sv_events);
	}
#ifdef SUPERVISE_TEST
	if (err == 0 && sv_clock >= 0)
	{
		err = posix_spawn_file_actions_addclose(&fa, sv_clock);
	}
#endif
	if (err != 0)
	{
		sv_say("cannot set up the spawn: %s", strerror(err));
		return 125;
	}
	err = posix_spawnp(&sv_leader, argv[0], &fa, &attr, argv, environ);
	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&fa);
	if (err != 0)
	{
		sv_say("cannot run %s: %s", argv[0], strerror(err));
		return 125;
	}

	/* the leader watched before anything is said; one that is already gone is seen by the peek */
	sv_track(sv_leader);
	if (sv_peek(sv_index(sv_leader) < 0 ? 0 : WNOHANG) < 0)
	{
		sv_say("cannot watch the command %d: %s", (int)sv_leader, strerror(errno));
		(void)kill(-sv_leader, SIGKILL);
		return 126;
	}
	if (sv_has_timeout)
	{
		sv_arm(T_DEADLINE);
	}
	sv_emit("spawned pid=%d pgid=%d", (int)sv_leader, (int)sv_leader);

	for (;;)
	{
		struct kevent evs[64];
		int n;

		st = sv_step();
		if (st >= 0)
		{
			return st;
		}
#ifdef SUPERVISE_TEST
		for (; sv_probes > 0; sv_probes--)
		{
			sv_emit("probe");
		}
#endif
		n = kevent(sv_kq, NULL, 0, evs, (int)(sizeof(evs) / sizeof(evs[0])), NULL);
		if (n < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			sv_say("cannot wait on the kqueue: %s; the command is killed", strerror(errno));
			(void)kill(-sv_leader, SIGKILL);
			return 126;
		}
		sv_batch(evs, n);
	}
}
