/*
 * tests/supervise/supervise_test.c -- the cases of scripts/lib/supervise.c
 * (TASK218), run by tests/check-supervise.sh.
 *
 *   supervise_test [--no-clock] <supervise> <fake> <shell> [case...]
 *
 * Each case starts <shell> -c '... exec "$@"' with the supervisor and
 * tests/supervise/fake.c under it, so the caller is a real bash 3.2 or sh.
 * Every wait is on a line: an event on the supervisor's --events
 * descriptor (3), a line or end of file on a fake process's private
 * channel, or a fake's hello. A timeout is a byte on the test clock
 * (SUPERVISE_TEST_CLOCK_FD, 4), written only when the supervisor cannot
 * go on without it, and the byte P is a barrier: its "probe" event comes
 * once the supervisor has handled everything the kernel posted before it.
 * Nothing waits on time and nothing reads ps. A case that fails closes the
 * channels, and every fake exits when its channel closes, so a failure
 * ends too. With --no-clock (the release build) the cases that need the
 * clock are skipped. Prints "ok <case> (<shell>)" or "FAIL <case>
 * (<shell>): <reason>"; exits 1 when any case failed.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXCH 8

struct chan
{
	int fd;
	pid_t pid, ppid;
	char hello[256];
	char buf[512];
	size_t len;
	char line[256];
};

struct run
{
	pid_t shell;
	int ev, clk, hel, err;
	char evq[4096];
	size_t evqlen;
	int ev_eof;
	char log[16384];
	size_t loglen;
	char last[256];
	struct chan ch[MAXCH];
	int nch;
	pid_t pgid;
	int status;
	char errtxt[4096];
	size_t errlen;
	char why[512];
	int finished;
};

static const char *sv_bin, *fake_bin, *shell;
static int use_clock = 1;
static const char *cur;

static int
fail(struct run *r, const char *fmt, ...)
{
	va_list ap;

	if (r->why[0] == '\0')
	{
		va_start(ap, fmt);
		vsnprintf(r->why, sizeof(r->why), fmt, ap);
		va_end(ap);
	}
	return 1;
}

static void
cloexec(int fd)
{
	(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
}

static ssize_t
rd(int fd, void *b, size_t n)
{
	ssize_t r;

	while ((r = read(fd, b, n)) < 0 && errno == EINTR)
	{
	}
	return r;
}

/* start: the caller shell with the supervisor and the fake (or cmd) under it */
static int
start(struct run *r, const char *form, const char **opts, const char *cmd, const char **args)
{
	int evp[2], clkp[2], errp[2], hs[2], i, n = 0;
	const char *argv[32];
	const char *script = "exec \"$@\"";

	memset(r, 0, sizeof(*r));
	r->ev = r->clk = r->hel = r->err = -1;
	if (strcmp(form, "intign") == 0)
	{
		script = "(trap '' INT; exec \"$@\") & wait \"$!\"";
	}
	else if (strcmp(form, "closed") == 0)
	{
		script = "exec \"$@\" <&- >&- 2>&-";
	}
	else if (strcmp(form, "sandbox") == 0)
	{
		script = "exec /usr/bin/sandbox-exec -p '(version 1)(allow default)(deny signal)' \"$@\"";
	}
	argv[n++] = shell;
	argv[n++] = "-c";
	argv[n++] = script;
	argv[n++] = "sh";
	argv[n++] = sv_bin;
	argv[n++] = "--events";
	argv[n++] = "3";
	for (i = 0; opts != NULL && opts[i] != NULL; i++)
	{
		argv[n++] = opts[i];
	}
	argv[n++] = "--";
	argv[n++] = cmd != NULL ? cmd : fake_bin;
	for (i = 0; args != NULL && args[i] != NULL; i++)
	{
		argv[n++] = args[i];
	}
	argv[n] = NULL;
	if (pipe(evp) < 0 || pipe(clkp) < 0 || pipe(errp) < 0 || socketpair(AF_UNIX, SOCK_DGRAM, 0, hs) < 0)
	{
		return fail(r, "pipe: %s", strerror(errno));
	}
	cloexec(evp[0]);
	cloexec(clkp[1]);
	cloexec(errp[0]);
	cloexec(hs[0]);
	r->shell = fork();
	if (r->shell < 0)
	{
		return fail(r, "fork: %s", strerror(errno));
	}
	if (r->shell == 0)
	{
		int e = fcntl(evp[1], F_DUPFD, 20), c = fcntl(clkp[0], F_DUPFD, 20);
		int h = fcntl(hs[1], F_DUPFD, 20), o = fcntl(errp[1], F_DUPFD, 20);
		int z = open("/dev/null", O_RDONLY);

		if (e < 0 || c < 0 || h < 0 || o < 0 || z < 0 || dup2(z, 0) < 0 || dup2(o, 1) < 0 || dup2(o, 2) < 0 ||
			dup2(e, 3) < 0 || dup2(c, 4) < 0 || dup2(h, 5) < 0)
		{
			_exit(120);
		}
		for (i = 6; i < 256; i++)
		{
			close(i);
		}
		if (use_clock)
		{
			setenv("SUPERVISE_TEST_CLOCK_FD", "4", 1);
		}
		else
		{
			unsetenv("SUPERVISE_TEST_CLOCK_FD");
		}
		setenv("FAKE_HELLO_FD", "5", 1);
		signal(SIGPIPE, SIG_DFL);
		execv(shell, (char *const *)argv);
		_exit(121);
	}
	close(evp[1]);
	close(clkp[0]);
	close(errp[1]);
	close(hs[1]);
	r->ev = evp[0];
	r->clk = clkp[1];
	r->err = errp[0];
	r->hel = hs[0];
	return 0;
}

/* ev_fill: more of the events into the queue; 0 at end of file */
static int
ev_fill(struct run *r)
{
	ssize_t n;

	if (r->ev_eof || r->evqlen >= sizeof(r->evq))
	{
		return 0;
	}
	n = rd(r->ev, r->evq + r->evqlen, sizeof(r->evq) - r->evqlen);
	if (n <= 0)
	{
		r->ev_eof = 1;
		return 0;
	}
	r->evqlen += (size_t)n;
	return 1;
}

/* ev_wait: the next event line that starts with the word w (logging every line), or NULL at end of file */
static const char *
ev_wait(struct run *r, const char *w)
{
	for (;;)
	{
		char *nl = memchr(r->evq, '\n', r->evqlen);

		if (nl != NULL)
		{
			size_t n = (size_t)(nl - r->evq), wl = strlen(w);

			if (r->loglen + n + 2 < sizeof(r->log))
			{
				memcpy(r->log + r->loglen, r->evq, n + 1);
				r->loglen += n + 1;
				r->log[r->loglen] = '\0';
			}
			snprintf(r->last, sizeof(r->last), "%.*s", (int)n, r->evq);
			memmove(r->evq, nl + 1, r->evqlen - n - 1);
			r->evqlen -= n + 1;
			if (strncmp(r->last, w, wl) == 0 && (r->last[wl] == ' ' || r->last[wl] == '\0'))
			{
				return r->last;
			}
			continue;
		}
		if (!ev_fill(r))
		{
			return NULL;
		}
	}
}

/* at: the offset in the log of the first line that is (or starts with) s, or -1 */
static long
at(struct run *r, const char *s)
{
	const char *p = r->log;
	size_t n = strlen(s);

	while (*p != '\0')
	{
		if (strncmp(p, s, n) == 0 && (p[n] == '\n' || p[n] == ' '))
		{
			return (long)(p - r->log);
		}
		p = strchr(p, '\n');
		if (p == NULL)
		{
			break;
		}
		p++;
	}
	return -1;
}

/* hello: the next fake's hello and its channel; NULL when the supervisor ended with none sent */
static struct chan *
hello(struct run *r)
{
	struct chan *c;
	char cbuf[CMSG_SPACE(sizeof(int))];
	struct msghdr mh;
	struct iovec iov;
	struct cmsghdr *cm;
	struct pollfd pf[2];
	const char *p;
	ssize_t n;

	if (r->nch >= MAXCH)
	{
		return NULL;
	}
	c = &r->ch[r->nch];
	memset(c, 0, sizeof(*c));
	for (;;)
	{
		pf[0].fd = r->hel;
		pf[0].events = POLLIN;
		pf[1].fd = r->ev_eof ? -1 : r->ev;
		pf[1].events = POLLIN;
		if (poll(pf, 2, -1) < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return NULL;
		}
		if (pf[0].revents & POLLIN)
		{
			break;
		}
		if ((pf[1].revents & (POLLIN | POLLHUP)) && !ev_fill(r))
		{
			pf[0].fd = r->hel;
			if (poll(pf, 1, 0) != 1)
			{
				return NULL;
			}
			break;
		}
	}
	memset(&mh, 0, sizeof(mh));
	iov.iov_base = c->hello;
	iov.iov_len = sizeof(c->hello) - 1;
	mh.msg_iov = &iov;
	mh.msg_iovlen = 1;
	mh.msg_control = cbuf;
	mh.msg_controllen = sizeof(cbuf);
	while ((n = recvmsg(r->hel, &mh, 0)) < 0 && errno == EINTR)
	{
	}
	cm = CMSG_FIRSTHDR(&mh);
	if (n <= 0 || cm == NULL || cm->cmsg_type != SCM_RIGHTS)
	{
		return NULL;
	}
	memcpy(&c->fd, CMSG_DATA(cm), sizeof(int));
	cloexec(c->fd);
	c->hello[n] = '\0';
	p = strstr(c->hello, " pid=");
	c->pid = p != NULL ? (pid_t)atoi(p + 5) : 0;
	p = strstr(c->hello, " ppid=");
	c->ppid = p != NULL ? (pid_t)atoi(p + 6) : 0;
	r->nch++;
	return c;
}

static int
has(const struct chan *c, const char *field)
{
	return strstr(c->hello, field) != NULL;
}

/* line: the next line from a fake's channel, or NULL at end of file (it has exited) */
static const char *
line(struct chan *c)
{
	for (;;)
	{
		char *nl = memchr(c->buf, '\n', c->len);
		ssize_t n;

		if (nl != NULL)
		{
			size_t k = (size_t)(nl - c->buf);

			snprintf(c->line, sizeof(c->line), "%.*s", (int)k, c->buf);
			memmove(c->buf, nl + 1, c->len - k - 1);
			c->len -= k + 1;
			return c->line;
		}
		if (c->fd < 0 || c->len >= sizeof(c->buf))
		{
			return NULL;
		}
		n = rd(c->fd, c->buf + c->len, sizeof(c->buf) - c->len);
		if (n <= 0)
		{
			close(c->fd);
			c->fd = -1;
			return NULL;
		}
		c->len += (size_t)n;
	}
}

static void
send_line(struct chan *c, const char *s)
{
	size_t n = strlen(s);

	while (c->fd >= 0 && n > 0)
	{
		ssize_t w = write(c->fd, s, n);

		if (w < 0 && errno == EINTR)
		{
			continue;
		}
		if (w < 0)
		{
			return;
		}
		s += w;
		n -= (size_t)w;
	}
}

/* ask: a command and its answer line */
static const char *
ask(struct chan *c, const char *cmd)
{
	char b[64];

	snprintf(b, sizeof(b), "%s\n", cmd);
	send_line(c, b);
	return line(c);
}

static void
tick(struct run *r, char t)
{
	while (write(r->clk, &t, 1) < 0 && errno == EINTR)
	{
	}
}

/* probe: a barrier; 0 when the supervisor ended first */
static int
probe(struct run *r)
{
	tick(r, 'P');
	return ev_wait(r, "probe") != NULL;
}

/* finish: closes every channel (each fake exits), reads the events and the output to their end, and the status */
static void
finish(struct run *r)
{
	int i, st;
	ssize_t n;

	if (r->finished)
	{
		return;
	}
	r->finished = 1;
	if (r->clk >= 0)
	{
		close(r->clk);
	}
	for (i = 0; i < r->nch; i++)
	{
		if (r->ch[i].fd >= 0)
		{
			close(r->ch[i].fd);
			r->ch[i].fd = -1;
		}
	}
	if (r->hel >= 0)
	{
		close(r->hel);
	}
	while (ev_wait(r, "") != NULL)
	{
	}
	if (r->ev >= 0)
	{
		close(r->ev);
	}
	while (r->err >= 0 && (n = rd(r->err, r->errtxt + r->errlen, sizeof(r->errtxt) - 1 - r->errlen)) > 0)
	{
		r->errlen += (size_t)n;
	}
	r->errtxt[r->errlen] = '\0';
	if (r->err >= 0)
	{
		close(r->err);
	}
	r->status = -1;
	if (r->shell > 0)
	{
		while (waitpid(r->shell, &st, 0) < 0)
		{
			if (errno != EINTR)
			{
				return;
			}
		}
		r->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
	}
}

/* stderr_only_own: every line of the output is the supervisor's */
static int
stderr_only_own(struct run *r)
{
	const char *p = r->errtxt;

	while (*p != '\0')
	{
		const char *nl = strchr(p, '\n');

		if (strncmp(p, "supervise: ", 11) != 0)
		{
			return fail(r, "the output holds a line that is not the supervisor's: %.*s",
				(int)(nl != NULL ? nl - p : (long)strlen(p)), p);
		}
		if (nl == NULL)
		{
			break;
		}
		p = nl + 1;
	}
	return 0;
}

static int
want_status(struct run *r, int want)
{
	if (r->status != want)
	{
		return fail(r, "status %d, want %d", r->status, want);
	}
	return 0;
}

/* leader: the leader's hello, and the spawned event */
static struct chan *
leader(struct run *r)
{
	struct chan *c = hello(r);
	const char *s;

	if (c == NULL)
	{
		fail(r, "no hello from the command");
		return NULL;
	}
	if ((s = ev_wait(r, "spawned")) == NULL)
	{
		fail(r, "no spawned event");
		return NULL;
	}
	s = strstr(s, "pgid=");
	r->pgid = s != NULL ? (pid_t)atoi(s + 5) : 0;
	if (r->pgid != c->pid)
	{
		fail(r, "the command %d does not lead the group %d", (int)c->pid, (int)r->pgid);
		return NULL;
	}
	return c;
}

/* fork_fake: parent forks a fake; its hello */
static struct chan *
fork_fake(struct run *r, struct chan *parent, const char *role)
{
	char cmd[64];
	const char *a;
	struct chan *c;

	snprintf(cmd, sizeof(cmd), "fork %s", role);
	a = ask(parent, cmd);
	if (a == NULL || strncmp(a, "forked pid=", 11) != 0)
	{
		fail(r, "fork %s answered %s", role, a != NULL ? a : "nothing");
		return NULL;
	}
	c = hello(r);
	if (c == NULL)
	{
		fail(r, "no hello from the %s", role);
	}
	return c;
}

static int
expect_line(struct run *r, struct chan *c, const char *want, const char *who)
{
	const char *l = line(c);

	if (l == NULL || strcmp(l, want) != 0)
	{
		return fail(r, "%s said %s, want %s", who, l != NULL ? l : "nothing (it exited)", want);
	}
	return 0;
}

static int
expect_gone(struct run *r, struct chan *c, const char *who)
{
	const char *l = line(c);

	if (l != NULL)
	{
		return fail(r, "%s is still there: it said %s", who, l);
	}
	return 0;
}

static int
c_exit(struct run *r, const char *code)
{
	const char *args[] = { "exit", code, NULL };
	struct chan *l;
	char want[32];

	if (start(r, "exec", NULL, NULL, args) || (l = leader(r)) == NULL)
	{
		return 1;
	}
	if (!has(l, " dfl=111 mask=000 "))
	{
		return fail(r, "the command did not start with its signals at their defaults, unblocked: %s", l->hello);
	}
	finish(r);
	snprintf(want, sizeof(want), "reaped status=%s", code);
	if (at(r, want) < 0 || at(r, "term-sent") >= 0)
	{
		return fail(r, "events: %s", r->log);
	}
	return want_status(r, atoi(code));
}

static int
c_exit0(struct run *r)
{
	return c_exit(r, "0");
}

static int
c_exit3(struct run *r)
{
	return c_exit(r, "3");
}

static int
c_deadline(struct run *r)
{
	const char *opts[] = { "--timeout", "100", NULL };
	struct chan *l, *c, *g;

	if (start(r, "exec", opts, NULL, NULL) || (l = leader(r)) == NULL || (c = fork_fake(r, l, "child")) == NULL ||
		(g = fork_fake(r, c, "grandchild")) == NULL)
	{
		return 1;
	}
	tick(r, 'D');
	if (ev_wait(r, "deadline") == NULL || ev_wait(r, "term-sent") == NULL)
	{
		return fail(r, "no deadline and term-sent events: %s", r->log);
	}
	if (expect_line(r, l, "sig=TERM", "the leader") || expect_line(r, g, "sig=TERM", "the grandchild") ||
		expect_line(r, c, "sig=TERM", "the child") || expect_gone(r, l, "the leader") ||
		expect_gone(r, c, "the child") || expect_gone(r, g, "the grandchild"))
	{
		return 1;
	}
	if (ev_wait(r, "group-empty") == NULL || ev_wait(r, "reaped") == NULL)
	{
		return fail(r, "no group-empty and reaped events: %s", r->log);
	}
	finish(r);
	if (want_status(r, 124))
	{
		return 1;
	}
	/* a live member answers 0; a zombie launchd has not reaped yet answers EPERM, an empty group ESRCH */
	if (kill(-r->pgid, 0) == 0 || (errno != ESRCH && errno != EPERM))
	{
		return fail(r, "something of the group %d is left", (int)r->pgid);
	}
	return 0;
}

static int
c_term_ignored(struct run *r)
{
	const char *opts[] = { "--timeout", "100", NULL };
	struct chan *l, *c;
	const char *a;

	if (start(r, "exec", opts, NULL, NULL) || (l = leader(r)) == NULL || (c = fork_fake(r, l, "child")) == NULL)
	{
		return 1;
	}
	if ((a = ask(c, "ignore-term")) == NULL || strcmp(a, "ok") != 0)
	{
		return fail(r, "ignore-term answered %s", a != NULL ? a : "nothing");
	}
	tick(r, 'D');
	if (ev_wait(r, "term-sent") == NULL)
	{
		return fail(r, "no term-sent event: %s", r->log);
	}
	if (expect_line(r, l, "sig=TERM", "the leader") || expect_gone(r, l, "the leader") ||
		expect_line(r, c, "sig=TERM ignored", "the member"))
	{
		return 1;
	}
	tick(r, 'G');
	if (ev_wait(r, "grace-end") == NULL || ev_wait(r, "kill-sent") == NULL)
	{
		return fail(r, "no grace-end and kill-sent events: %s", r->log);
	}
	if ((a = ask(c, "ping")) != NULL)
	{
		return fail(r, "a member that ignores TERM was alive after kill-sent (it said %s)", a);
	}
	if (ev_wait(r, "group-empty") == NULL || ev_wait(r, "reaped") == NULL)
	{
		return fail(r, "no group-empty and reaped events: %s", r->log);
	}
	finish(r);
	return want_status(r, 124);
}

static int
c_signal(struct run *r, int sig, const char *name)
{
	struct chan *l;
	char want[64];

	if (start(r, "exec", NULL, NULL, NULL) || (l = leader(r)) == NULL)
	{
		return 1;
	}
	kill(l->ppid, sig);
	snprintf(want, sizeof(want), "term-sent name=%s", name);
	if (use_clock)
	{
		char sw[64];

		snprintf(sw, sizeof(sw), "signal name=%s", name);
		if (!probe(r) || at(r, sw) < 0 || at(r, want) < 0)
		{
			return fail(r, "%s to the supervisor was not forwarded (events: %s)", name, r->log);
		}
	}
	else if (ev_wait(r, "term-sent") == NULL || at(r, want) < 0)
	{
		return fail(r, "%s to the supervisor was not forwarded (events: %s)", name, r->log);
	}
	snprintf(want, sizeof(want), "sig=%s", name);
	if (expect_line(r, l, want, "the leader") || expect_gone(r, l, "the leader"))
	{
		return 1;
	}
	if (ev_wait(r, "group-empty") == NULL || ev_wait(r, "reaped") == NULL)
	{
		return fail(r, "no group-empty and reaped events: %s", r->log);
	}
	finish(r);
	return want_status(r, 128 + sig);
}

static int
c_signal_int(struct run *r)
{
	return c_signal(r, SIGINT, "INT");
}

static int
c_signal_term(struct run *r)
{
	return c_signal(r, SIGTERM, "TERM");
}

static int
c_signal_hup(struct run *r)
{
	return c_signal(r, SIGHUP, "HUP");
}

/* the merge gate's launch: SIGINT ignored at entry, the supervisor a background job of the caller */
static int
c_int_ignored(struct run *r)
{
	const char *opts[] = { "--timeout", "100", NULL };
	struct chan *l;

	if (start(r, "intign", use_clock ? opts : NULL, NULL, NULL) || (l = leader(r)) == NULL)
	{
		return 1;
	}
	if (!has(l, " dfl=111 mask=000 "))
	{
		return fail(r, "the command did not get INT at its default: %s", l->hello);
	}
	kill(l->ppid, SIGINT);
	if (ev_wait(r, "ignored") == NULL || at(r, "ignored name=INT") < 0)
	{
		return fail(r, "an INT ignored at entry was not seen as ignored: %s", r->log);
	}
	if (use_clock)
	{
		tick(r, 'D');
	}
	else
	{
		kill(l->ppid, SIGTERM);
	}
	if (ev_wait(r, "term-sent") == NULL)
	{
		return fail(r, "the supervisor did not stop the command: %s", r->log);
	}
	if (expect_line(r, l, "sig=TERM", "the leader") || expect_gone(r, l, "the leader"))
	{
		return 1;
	}
	if (ev_wait(r, "group-empty") == NULL || ev_wait(r, "reaped") == NULL)
	{
		return fail(r, "no group-empty and reaped events: %s", r->log);
	}
	finish(r);
	return want_status(r, use_clock ? 124 : 128 + SIGTERM);
}

/* the leader exits while a member lives: the member is stopped, and the leader reaped after group-empty */
static int
c_leader_first(struct run *r)
{
	struct chan *l, *c;
	const char *a;
	long ge, rp;

	if (start(r, "exec", NULL, NULL, NULL) || (l = leader(r)) == NULL || (c = fork_fake(r, l, "child")) == NULL)
	{
		return 1;
	}
	send_line(l, "exit 5\n");
	if (ev_wait(r, "reaped") == NULL)
	{
		return fail(r, "no reaped line: the leader was reaped before its group was empty (events: %s)", r->log);
	}
	ge = at(r, "group-empty");
	rp = at(r, "reaped");
	if (ge < 0 || ge > rp)
	{
		return fail(r, "reaped came before group-empty: %s", r->log);
	}
	a = ask(c, "ping");
	if (a != NULL && strcmp(a, "alive") == 0)
	{
		return fail(r, "the leader was reaped while a member of its group was alive");
	}
	if (a == NULL || strcmp(a, "sig=TERM") != 0)
	{
		return fail(r, "the member said %s, want sig=TERM", a != NULL ? a : "nothing");
	}
	if (expect_gone(r, c, "the member"))
	{
		return 1;
	}
	finish(r);
	if (at(r, "exited status=5") < 0 || at(r, "term-sent name=TERM") < 0 || at(r, "reaped status=5") < 0)
	{
		return fail(r, "events: %s", r->log);
	}
	return want_status(r, 5);
}

/* a member that moved into a session of its own is named and not signalled */
static int
c_escaped(struct run *r)
{
	struct chan *l, *c;
	const char *a;
	char want[64];
	long es;

	if (start(r, "exec", NULL, NULL, NULL) || (l = leader(r)) == NULL || (c = fork_fake(r, l, "child")) == NULL)
	{
		return 1;
	}
	if ((a = ask(c, "setsid")) == NULL || atoi(a + 4) != (int)c->pid)
	{
		return fail(r, "setsid answered %s", a != NULL ? a : "nothing");
	}
	if (!probe(r))
	{
		return fail(r, "no probe event: %s", r->log);
	}
	send_line(l, "exit 0\n");
	if (ev_wait(r, "reaped") == NULL)
	{
		return fail(r, "no reaped event: %s", r->log);
	}
	snprintf(want, sizeof(want), "escaped pid=%d", (int)c->pid);
	es = at(r, want);
	if (es < 0 || es > at(r, "group-empty"))
	{
		return fail(r, "the escaped member was not named before group-empty: %s", r->log);
	}
	if (at(r, "term-sent") >= 0)
	{
		return fail(r, "a stop was sent with no member left in the group: %s", r->log);
	}
	if ((a = ask(c, "ping")) == NULL || strcmp(a, "alive") != 0)
	{
		return fail(r, "the escaped member was signalled (it said %s)", a != NULL ? a : "nothing");
	}
	finish(r);
	return want_status(r, 0);
}

static int
c_closed_std(struct run *r)
{
	const char *args[] = { "exit", "7", NULL };
	struct chan *l;

	if (start(r, "closed", NULL, NULL, args) || (l = leader(r)) == NULL)
	{
		return 1;
	}
	if (!has(l, " std=111"))
	{
		return fail(r, "the closed standard descriptors are not open on /dev/null: %s", l->hello);
	}
	finish(r);
	if (at(r, "reaped status=7") < 0)
	{
		return fail(r, "events: %s", r->log);
	}
	return want_status(r, 7);
}

/* a group that cannot be emptied: a sandbox refuses every signal */
static int
c_stuck(struct run *r)
{
	const char *opts[] = { "--timeout", "100", "--name", "stuck-case", NULL };
	struct chan *l;
	const char *a;

	if (start(r, "sandbox", opts, NULL, NULL) || (l = leader(r)) == NULL)
	{
		return 1;
	}
	tick(r, 'D');
	if (ev_wait(r, "term-sent") == NULL)
	{
		return fail(r, "no term-sent event: %s", r->log);
	}
	if ((a = ask(l, "ping")) == NULL || strcmp(a, "alive") != 0)
	{
		return fail(r, "the sandbox let the TERM through (the leader said %s)", a != NULL ? a : "nothing");
	}
	tick(r, 'G');
	if (ev_wait(r, "kill-sent") == NULL)
	{
		return fail(r, "no kill-sent event: %s", r->log);
	}
	if ((a = ask(l, "ping")) == NULL || strcmp(a, "alive") != 0)
	{
		return fail(r, "the sandbox let the KILL through (the leader said %s)", a != NULL ? a : "nothing");
	}
	tick(r, 'A');
	if (ev_wait(r, "group-left") == NULL)
	{
		return fail(r, "no group-left event: %s", r->log);
	}
	finish(r);
	if (want_status(r, 126))
	{
		return 1;
	}
	if (strstr(r->errtxt, "supervise: stuck-case: ") == NULL)
	{
		return fail(r, "the supervisor did not say the group is left");
	}
	return 0;
}

static int
c_stderr(struct run *r)
{
	const char *opts[] = { "--name", "stderr-case", NULL };
	const char *nl;

	if (start(r, "exec", opts, "/nonexistent/supervise-case", NULL))
	{
		return 1;
	}
	finish(r);
	if (want_status(r, 125))
	{
		return 1;
	}
	nl = strchr(r->errtxt, '\n');
	if (strncmp(r->errtxt, "supervise: stderr-case: ", 24) != 0 || nl == NULL || nl[1] != '\0')
	{
		return fail(r, "want one line from the supervisor, got: %s", r->errtxt);
	}
	return 0;
}

static const struct
{
	const char *name;
	int (*fn)(struct run *);
	int clock;
} cases[] = {
	{ "exit0", c_exit0, 0 },
	{ "exit3", c_exit3, 0 },
	{ "deadline", c_deadline, 1 },
	{ "term-ignored", c_term_ignored, 1 },
	{ "signal-int", c_signal_int, 0 },
	{ "signal-term", c_signal_term, 0 },
	{ "signal-hup", c_signal_hup, 0 },
	{ "int-ignored", c_int_ignored, 0 },
	{ "leader-first", c_leader_first, 0 },
	{ "escaped", c_escaped, 1 },
	{ "closed-std", c_closed_std, 0 },
	{ "stuck", c_stuck, 1 },
	{ "stderr", c_stderr, 0 },
};

int
main(int argc, char **argv)
{
	int i, j, failed = 0, ran = 0;

	signal(SIGPIPE, SIG_IGN);
	if (argc > 1 && strcmp(argv[1], "--no-clock") == 0)
	{
		use_clock = 0;
		argc--;
		argv++;
	}
	if (argc < 4)
	{
		fprintf(stderr, "usage: supervise_test [--no-clock] <supervise> <fake> <shell> [case...]\n");
		return 2;
	}
	sv_bin = argv[1];
	fake_bin = argv[2];
	shell = argv[3];
	for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
	{
		static struct run r;
		int bad;

		if (argc > 4)
		{
			for (j = 4; j < argc && strcmp(argv[j], cases[i].name) != 0; j++)
			{
			}
			if (j == argc)
			{
				continue;
			}
		}
		if (cases[i].clock && !use_clock)
		{
			printf("skip %s (%s): needs the test clock\n", cases[i].name, shell);
			continue;
		}
		cur = cases[i].name;
		bad = cases[i].fn(&r);
		finish(&r);
		bad |= stderr_only_own(&r);
		ran++;
		if (bad)
		{
			printf("FAIL %s (%s): %s\n", cur, shell, r.why[0] != '\0' ? r.why : "no reason given");
			failed++;
		}
		else
		{
			printf("ok %s (%s)\n", cur, shell);
		}
		fflush(stdout);
	}
	if (ran == 0)
	{
		printf("FAIL no case ran\n");
		return 1;
	}
	return failed != 0;
}
