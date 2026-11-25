/*
 * tests/supervise/fake.c -- the command tests/supervise/supervise_test.c
 * runs under scripts/lib/supervise.c (TASK218).
 *
 *   fake [exit N]
 *
 * Each fake process, the leader and each one it forks, makes a private
 * channel (a socket pair) and sends its far end to the runner over the
 * datagram socket FAKE_HELLO_FD, with one line:
 *
 *   hello role=R pid=P ppid=Q pgid=G sid=S dfl=IHT mask=IHT std=012
 *
 * dfl and mask give, for INT, HUP and TERM, 1 when the signal was at its
 * default disposition and when it was blocked at entry; std gives 1 for
 * each standard descriptor that is open. Only this process holds its end
 * of the channel (close-on-exec, closed in each child), so the runner
 * reads end of file there once it has exited, and the process exits when
 * the runner closes its end. "exit N" as arguments exits at once after the
 * hello. Commands on the channel, one per line, each answered by a line:
 *
 *   exit N        exits with status N (no answer)
 *   fork ROLE     forks a fake with that role; answers "forked pid=C"
 *   setsid        moves into a session of its own; answers "sid=S"
 *   ignore-term   TERM is reported and then ignored; answers "ok"
 *   ping          answers "alive"
 *
 * INT, TERM and HUP each write "sig=NAME" on the channel ("sig=TERM
 * ignored" after ignore-term) and then end the process with that signal.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int chan = -1;
static int hello_fd = -1;
static volatile sig_atomic_t ignore_term;

static void
put(const char *s, size_t n)
{
	while (n > 0)
	{
		ssize_t w = write(chan, s, n);

		if (w < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			_exit(70);
		}
		s += w;
		n -= (size_t)w;
	}
}

static void
on_signal(int sig)
{
	if (sig == SIGTERM && ignore_term)
	{
		put("sig=TERM ignored\n", 17);
		return;
	}
	if (sig == SIGINT)
	{
		put("sig=INT\n", 8);
	}
	else if (sig == SIGHUP)
	{
		put("sig=HUP\n", 8);
	}
	else
	{
		put("sig=TERM\n", 9);
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

static int
dfl(int sig)
{
	struct sigaction sa;

	return sigaction(sig, NULL, &sa) == 0 && sa.sa_handler == SIG_DFL;
}

static void
hello(const char *role)
{
	char text[256], cbuf[CMSG_SPACE(sizeof(int))];
	struct msghdr mh;
	struct iovec iov;
	struct cmsghdr *cm;
	struct sigaction sa;
	sigset_t mask;
	int sp[2], n;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) < 0 || fcntl(sp[0], F_SETFD, FD_CLOEXEC) < 0)
	{
		_exit(71);
	}
	sigprocmask(SIG_SETMASK, NULL, &mask);
	n = snprintf(text, sizeof(text), "hello role=%s pid=%d ppid=%d pgid=%d sid=%d dfl=%d%d%d mask=%d%d%d std=%d%d%d",
		role, (int)getpid(), (int)getppid(), (int)getpgrp(), (int)getsid(0),
		dfl(SIGINT), dfl(SIGHUP), dfl(SIGTERM),
		sigismember(&mask, SIGINT), sigismember(&mask, SIGHUP), sigismember(&mask, SIGTERM),
		fcntl(0, F_GETFD) >= 0, fcntl(1, F_GETFD) >= 0, fcntl(2, F_GETFD) >= 0);
	memset(&mh, 0, sizeof(mh));
	memset(cbuf, 0, sizeof(cbuf));
	iov.iov_base = text;
	iov.iov_len = (size_t)n;
	mh.msg_iov = &iov;
	mh.msg_iovlen = 1;
	mh.msg_control = cbuf;
	mh.msg_controllen = sizeof(cbuf);
	cm = CMSG_FIRSTHDR(&mh);
	cm->cmsg_level = SOL_SOCKET;
	cm->cmsg_type = SCM_RIGHTS;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &sp[1], sizeof(int));
	while (sendmsg(hello_fd, &mh, 0) < 0)
	{
		if (errno != EINTR)
		{
			_exit(72);
		}
	}
	close(sp[1]);
	chan = sp[0];
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

/* one command line from the channel; end of file ends the process */
static void
command(char *line, size_t size)
{
	size_t n = 0;

	for (;;)
	{
		char c;
		ssize_t r = read(chan, &c, 1);

		if (r < 0 && errno == EINTR)
		{
			continue;
		}
		if (r <= 0)
		{
			_exit(0);
		}
		if (c == '\n')
		{
			break;
		}
		if (n + 1 < size)
		{
			line[n++] = c;
		}
	}
	line[n] = '\0';
}

int
main(int argc, char **argv)
{
	char line[128], out[64];
	const char *env = getenv("FAKE_HELLO_FD");

	if (env == NULL)
	{
		return 73;
	}
	hello_fd = atoi(env);
	hello("leader");
	if (argc == 3 && strcmp(argv[1], "exit") == 0)
	{
		_exit(atoi(argv[2]));
	}
	for (;;)
	{
		command(line, sizeof(line));
		if (strncmp(line, "exit ", 5) == 0)
		{
			_exit(atoi(line + 5));
		}
		else if (strncmp(line, "fork ", 5) == 0)
		{
			pid_t c = fork();

			if (c == 0)
			{
				close(chan);
				chan = -1;
				hello(line + 5);
				continue;
			}
			snprintf(out, sizeof(out), "forked pid=%d\n", (int)c);
			put(out, strlen(out));
		}
		else if (strcmp(line, "setsid") == 0)
		{
			snprintf(out, sizeof(out), "sid=%d\n", (int)setsid());
			put(out, strlen(out));
		}
		else if (strcmp(line, "ignore-term") == 0)
		{
			ignore_term = 1;
			put("ok\n", 3);
		}
		else if (strcmp(line, "ping") == 0)
		{
			put("alive\n", 6);
		}
		else
		{
			put("unknown\n", 8);
		}
	}
}
