# tests/lib/proc.sh -- waits on a process's own lines, for the harness checks
# (TASK221). Sourced by a check; it has no check of its own. Needs a shell
# whose read takes -t (bash 3.2 and later, also as sh).
#
# A case waits on a line a process writes, or on end of file once every
# process that holds the write end has exited, and never on time: there is
# no sleep and no ps. PROC_BOUND (60 by default) is only the failure bound,
# whole seconds by the clock: a wait that reaches it fails.
#
# The reader opens a FIFO as the cases do (proc_open), so that neither its
# open nor a writer's blocks, and keeps its own write end only until the
# writer has said its first line (proc_alone).

# proc_open <fifo>: opens <fifo> on fd 5 (read and write, so the open does
# not wait for a writer) and fd 6 (read only, for the reads)
proc_open() {
	exec 5<> "$1" 6< "$1"
}

# proc_alone: closes fd 5, the reader's own write end: from then on, end of
# file on fd 6 means every other writer has gone
proc_alone() {
	exec 5>&-
}

# proc_close: closes both
proc_close() {
	exec 5>&- 6<&-
}

PROC_LINE=

# proc_await <pattern>: reads fd 6 until a line matches the case pattern
# <pattern> (the line in PROC_LINE). False at end of file or at the bound.
proc_await() {
	_pa_end=$(($(date +%s) + ${PROC_BOUND:-60}))
	while :; do
		_pa_left=$((_pa_end - $(date +%s)))
		[ "$_pa_left" -gt 0 ] || return 1
		IFS= read -r -t "$_pa_left" PROC_LINE <&6 || return 1
		# shellcheck disable=SC2254 # the pattern is the caller's
		case $PROC_LINE in $1) return 0 ;; esac
	done
}

# proc_eof: reads fd 6 until end of file: true then. False at the bound.
proc_eof() {
	_pe_end=$(($(date +%s) + ${PROC_BOUND:-60}))
	while :; do
		_pe_left=$((_pe_end - $(date +%s)))
		[ "$_pe_left" -gt 0 ] || return 1
		# bash 3.2's read -t says 1 at the bound as at end of file, so a
		# failed read is end of file only before the bound
		IFS= read -r -t "$_pe_left" PROC_LINE <&6 && continue
		[ "$(date +%s)" -lt "$_pe_end" ]
		return
	done
}
