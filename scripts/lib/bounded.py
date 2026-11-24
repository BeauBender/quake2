"""scripts/lib/bounded.py -- the bounded-run primitive for the Python scripts (FIX186, slice 2).

The one place in the Python under scripts/ that stops a command with TERM, a grace and KILL, as
bench_stop_groups is for the shell (scripts/bench/core/common.sh). tests/check-bounded-run.sh
refuses a KILL and a subprocess call with a timeout= anywhere else in scripts/, and its disposition
probe (pyprobe) runs this file. Standard library only; Python 3.9 (the macOS python3).

  spawn(cmd, **popen_kw) -> Child   the command in a session (and so a process group) of its own,
                                    its identity (pid and start time) read at once
  signal_group(child, sig) -> bool  sig to the child's group, only while it is still the child's
  alive(child) -> bool              something of the child's group is left
  stop(child, grace=GRACE, pump=None) -> bool
                                    TERM and CONT to the group, up to <grace> seconds for it to go,
                                    KILL to what is left, up to AFTER_KILL seconds for it to go,
                                    then the reap of the child; False when nothing was signalled
  run(cmd, timeout=None, grace=GRACE, **popen_kw) -> Result
                                    subprocess.run's shape: spawn, collect the output, and on the
                                    timeout or any exception (a Ctrl-C, a SIGTERM or SIGHUP) stop()

Result.how is "exit" (the command exited by itself), "signal" (a signal this helper did not send
ended it; Result.signal is its number) or "timeout" (this helper stopped it when its time ran out;
the output is what was read by then, or None). A stop because of the caller's own signal is no
Result: the exception goes on once the group is stopped.

The group is signalled only while it is the child's. Until the child is reaped its pid, and so the
id of the group it leads, cannot go to another process, and a ps start time other than the one read
at spawn means the pid was reaped behind Popen's back. Once it is reaped, a group with its id is
still the child's while it has a member and no process has the pid (the system gives no new process
the id of a process group that has a member); a process that has the pid is another one, so nothing
is sent. What is left is the check-then-signal window of the shell primitive: the group's last
member ends, and its id goes to a new session leader, between the check and the signal.

What it does not reach: a process that moved itself into another group or session (setsid), as
with the shell primitive. A stop holds SIGINT, SIGTERM and SIGHUP until it is over, so a second one
cannot cut the grace short; and spawn holds them from before the fork until the child is recorded,
so that a signal in between still stops it (the child starts with the caller's mask)."""

import os
import signal
import subprocess
import time

GRACE = 10       # seconds between TERM and KILL, as BENCH_STOP_GRACE's default
AFTER_KILL = 2   # seconds for the group to go after the KILL, as bench_stop_groups
POLL = 0.1
STOP_SIGNALS = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)


def proc_start(pid):
    """pid's start time (ps -o lstart=, in UTC, its words one blank apart); "" when no process has
    pid; None when this process cannot tell (pid answers, ps does not)."""
    try:
        p = subprocess.Popen(["/bin/ps", "-o", "lstart=", "-p", str(pid)], env=dict(os.environ, TZ="UTC0"),
                             stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             universal_newlines=True)
        out = p.communicate()[0]
    except OSError:
        out = ""
    s = " ".join(out.split())
    if s:
        return s
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return ""
    except PermissionError:
        pass
    return None


class Child:
    """A command spawn() started: popen, pid (also its group id) and start (proc_start at spawn)."""

    def __init__(self, popen):
        self.popen = popen
        self.pid = popen.pid
        self.start = proc_start(popen.pid)
        self.output = None  # run(): (stdout, stderr) once read to the end
        self.reading = False


def spawn(cmd, grace=GRACE, _box=None, **kw):
    """Popen(cmd, **kw) in a session of its own, recorded as a Child. A SIGINT, SIGTERM or SIGHUP
    that comes before it is recorded is held, and stops it (stop(grace)) once it is. With _box (a
    list; run's), the Child goes into it before that signal can land, and the caller stops it."""
    kw.pop("start_new_session", None)
    held = signal.pthread_sigmask(signal.SIG_BLOCK, STOP_SIGNALS)
    c = None
    try:
        p = subprocess.Popen(cmd, start_new_session=True,
                             preexec_fn=lambda: signal.pthread_sigmask(signal.SIG_SETMASK, held), **kw)
        c = Child(p)
        if _box is not None:
            _box.append(c)
    finally:
        try:
            signal.pthread_sigmask(signal.SIG_SETMASK, held)  # a signal held meanwhile lands here
        except BaseException:
            if c is not None and _box is None:
                stop(c, grace)
            raise
    return c


def _ours(c):
    """Whether the group with the child's id is still the child's (see the module's text)."""
    s = proc_start(c.pid)
    if s == "":
        return True
    if s is None:
        return True  # the pid answers and ps does not: as the shell primitive, while no other is seen
    if c.popen.returncode is not None:
        return False  # reaped: a process that has the pid now is another one
    # unreaped, the pid is still the child's unless it was reaped behind Popen's back (another start
    # time); with no start time read at spawn (ps gave none), the unreaped pid is taken as the child's
    return c.start is None or s == c.start


def signal_group(c, sig):
    """sig to the child's process group, only while it is the child's. True once sent."""
    if not _ours(c):
        return False
    try:
        os.killpg(c.pid, sig)
    except OSError:
        return False
    return True


def alive(c):
    """Something of the child's group is left: the child itself (reaped here once it has exited), or
    a member of its group while that group is still the child's."""
    if c.popen.returncode is None and c.popen.poll() is None:
        return True
    if not _ours(c):
        return False
    try:
        os.killpg(c.pid, 0)
    except OSError:
        return False
    return True


def _pump(c, t, pump):
    """One poll: pump(t) when given, else the output of run() read for up to t seconds, else a wait."""
    if pump is not None:
        pump(t)
        return
    if c.reading and c.output is None:
        try:
            c.output = c.popen.communicate(timeout=t)
        except subprocess.TimeoutExpired:
            pass
        except (OSError, ValueError):
            c.reading = False
        return
    time.sleep(t)


def stop(c, grace=GRACE, pump=None):
    """Stops the child's process group: TERM and CONT (so a stopped member acts on the TERM) to it,
    up to <grace> seconds for it to go, then KILL to what is left, up to AFTER_KILL seconds for it to
    go, then the reap of the child. Every signal goes through signal_group, so only while the group is
    the child's; when the TERM is not sent (nothing of it is left, or it is not the child's), nothing
    more is sent and nothing is reaped, and the result is False. pump(t), when given, is called on
    each poll in place of a wait of t seconds (to read the command's output meanwhile)."""
    held = signal.pthread_sigmask(signal.SIG_BLOCK, STOP_SIGNALS)
    try:
        if not signal_group(c, signal.SIGTERM):
            return False
        signal_group(c, signal.SIGCONT)
        end = time.monotonic() + grace
        while alive(c) and time.monotonic() < end:
            _pump(c, POLL, pump)
        if alive(c):
            signal_group(c, signal.SIGKILL)  # bounded-run: allowed: the helper: stop, the KILL after the grace
        end = time.monotonic() + AFTER_KILL
        while alive(c) and time.monotonic() < end:
            _pump(c, POLL, pump)
        if c.popen.returncode is None:
            try:
                c.popen.wait(AFTER_KILL)
            except subprocess.TimeoutExpired:
                pass
        return True
    finally:
        signal.pthread_sigmask(signal.SIG_SETMASK, held)


class Result:
    """What run() gives: returncode, stdout, stderr, how ("exit", "signal" or "timeout"), signal."""

    def __init__(self, returncode, stdout, stderr, how):
        self.returncode, self.stdout, self.stderr, self.how = returncode, stdout, stderr, how
        self.signal = -returncode if how == "signal" else None


def _raise_exit(signum, frame):
    raise SystemExit(128 + signum)


def run(cmd, timeout=None, grace=GRACE, **kw):
    """The command (Popen keywords as subprocess.run takes them, capture_output and text included) in
    a group of its own, its output read to the end, as a Result. On the timeout, the group is stopped
    (stop) and how is "timeout". On any exception meanwhile, a KeyboardInterrupt and a SystemExit
    included, the group is stopped and the exception goes on. While it runs, a SIGTERM or SIGHUP whose
    handler is the default raises SystemExit(128 + the signal), so the caller's own signal stops the
    group too; a handler the caller set is left as it is."""
    if kw.pop("capture_output", False):
        kw["stdout"] = kw["stderr"] = subprocess.PIPE
    if "text" in kw:
        kw["universal_newlines"] = kw.pop("text")
    old = {}
    for s in (signal.SIGTERM, signal.SIGHUP):
        try:
            if signal.getsignal(s) == signal.SIG_DFL:
                old[s] = signal.signal(s, _raise_exit)
        except ValueError:  # not the main thread: the caller's signals are its own
            pass
    box = []
    try:
        c = spawn(cmd, grace, box, **kw)
        c.reading = True
        try:
            out = c.popen.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            stop(c, grace)
            o = c.output or (None, None)
            return Result(c.popen.returncode, o[0], o[1], "timeout")
        rc = c.popen.returncode
        return Result(rc, out[0], out[1], "signal" if rc < 0 else "exit")
    except BaseException:
        if box:
            stop(box[0], grace)
        raise
    finally:
        for s, h in old.items():
            signal.signal(s, h)
