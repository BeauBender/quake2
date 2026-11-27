# scripts/lib/supervise.sh -- builds the process supervisor on first use
# (TASK218). Sourced; POSIX sh, safe on bash 3.2.
#
#   . scripts/lib/supervise.sh
#   sv=$(sv_path) || exit 1
#   "$sv" --timeout 60 --events 9 -- cmd args... 9> events.log
#
# sv_path prints the physical path of the binary built from supervise.c
# beside this file (no symbolic link in it), building it when it is not
# there. The binary lives in the tool cache, $SUPERVISE_CACHE, else
# $XDG_CACHE_HOME/quake2-tools, else
# $HOME/.cache/quake2-tools, under a name keyed by the SHA-256 of the
# compiler's version line, the flags and the source: a changed source or
# compiler builds a new binary, and an unchanged one is never built again.
# With no shasum on PATH the key is two POSIX cksum CRCs of the same text
# (as scripts/bench's hash falls back), under the name supervise-crc-<key>.
#
# The cache is trusted only when it is safe. A SUPERVISE_CACHE that is not an
# absolute path is refused; an XDG_CACHE_HOME that is not one is ignored, as
# the XDG spec says. The directory is made with mode 0700. The directory and
# the binary must be owned by this user and not writable by group or others,
# and the binary must be a regular file, not a symbolic link; neither may
# have an ACL, and every directory above the cache's physical path must be
# owned by this user or root and not writable by group or others unless
# sticky; else sv_path refuses it. The cache is resolved to its physical
# path first, and only that path is checked, built in and printed. The
# build runs cc directly, unsupervised, into a temporary name that is
# moved into place, so a concurrent first use never runs a half-written
# binary; its log is a temporary file too.
#
# Any failure is a failure: sv_path prints no path, says why on standard
# error (lines starting "supervise: ", which name no path) and returns 1.
# There is no fallback to a shell supervisor.

SV_SRC_DIR=$(cd "$(dirname "${BASH_SOURCE:-$0}")" 2> /dev/null && pwd -P) || SV_SRC_DIR=
SV_CFLAGS="-O2 -Wall -Wextra"

# sv_cache: the tool cache directory, an absolute path
sv_cache() {
	if [ -n "${SUPERVISE_CACHE:-}" ]; then
		case $SUPERVISE_CACHE in
		/*) printf '%s\n' "$SUPERVISE_CACHE" ;;
		*)
			echo "supervise: SUPERVISE_CACHE is not an absolute path" >&2
			return 1
			;;
		esac
	elif case ${XDG_CACHE_HOME:-} in /*) true ;; *) false ;; esac then
		printf '%s/quake2-tools\n' "$XDG_CACHE_HOME"
	elif case ${HOME:-} in /*) true ;; *) false ;; esac then
		printf '%s/.cache/quake2-tools\n' "$HOME"
	else
		echo "supervise: no tool cache (set SUPERVISE_CACHE, XDG_CACHE_HOME or HOME to an absolute path)" >&2
		return 1
	fi
}

# sv_safe: the path is owned by this user and not writable by group or others
sv_safe() {
	_sv_sp=$1
	_sv_ls=$(ls -ldn "$1" 2> /dev/null) || return 1
	# shellcheck disable=SC2086 # the fields of the listing
	set -- $_sv_ls
	[ "${3:-}" = "$(id -u)" ] || return 1
	case $1 in
	?????w* | ????????w*) return 1 ;;
	esac
	# no ACL: ls -lde lists its entries on lines of their own (the + of
	# ls -l is hidden when the path also has extended attributes)
	_sv_n=$(ls -lden "$_sv_sp" 2> /dev/null | sed -n '$=') || return 1
	[ "${_sv_n:-0}" -eq 1 ]
}

# sv_parents <dir>: every directory above <dir> (a physical path) is owned
# by this user or root, and not writable by group or others unless it is
# sticky (as /tmp is). A parent's ACL is not read: macOS home directories
# carry a deny ACL, which grants nothing.
sv_parents() {
	_sp_d=$1
	_sp_u=$(id -u)
	while [ -n "$_sp_d" ] && [ "$_sp_d" != / ]; do
		_sp_d=${_sp_d%/*}
		[ -n "$_sp_d" ] || _sp_d=/
		_sp_l=$(ls -ldn "$_sp_d" 2> /dev/null) || return 1
		# shellcheck disable=SC2086 # the fields of the listing
		set -- $_sp_l
		case ${3:-} in 0 | "$_sp_u") ;; *) return 1 ;; esac
		case $1 in
		?????????[tT]*) ;;
		?????w* | ????????w*) return 1 ;;
		esac
	done
	return 0
}

# sv_crc <text>: a CRC as cksum prints it: decimal digits, no leading zero
# but for 0 itself, at most 10 digits (scripts/bench's bench_is_uint_max
# rule, which this file cannot source) and at most 4294967295, so it is a
# 32-bit number and %08x prints it in 8 hex digits.
sv_crc() {
	case $1 in
	'' | *[!0-9]* | 0?*) return 1 ;;
	esac
	[ "${#1}" -le 10 ] && [ "$1" -le 4294967295 ]
}

# sv_path: the supervisor's path, built on first use
sv_path() {
	_sv_src=$SV_SRC_DIR/supervise.c
	if [ -z "$SV_SRC_DIR" ] || [ ! -f "$_sv_src" ]; then
		echo "supervise: no source beside scripts/lib/supervise.sh" >&2
		return 1
	fi
	command -v cc > /dev/null 2>&1 || {
		echo "supervise: cc is not on PATH" >&2
		return 1
	}
	_sv_dir=$(sv_cache) || return 1
	_sv_cc=$(cc --version 2> /dev/null | sed -n 1p) || _sv_cc=
	[ -n "$_sv_cc" ] || {
		echo "supervise: cc --version printed nothing" >&2
		return 1
	}
	if command -v shasum > /dev/null 2>&1; then
		_sv_key=$( {
			printf '%s\n%s\n' "$_sv_cc" "$SV_CFLAGS"
			cat "$_sv_src"
		} | shasum -a 256) || _sv_key=
		_sv_key=${_sv_key%% *}
		case $_sv_key in
		*[!0-9a-f]* | '')
			echo "supervise: shasum -a 256 gave no digest" >&2
			return 1
			;;
		esac
		_sv_name=supervise-$(printf '%s\n' "$_sv_key" | cut -c1-16)
	else
		# no shasum (a minimal PATH, #758): POSIX cksum, as bench_hash does,
		# two CRCs of the same text with two prefixes; the name says so, so
		# a cksum key never names a shasum key's binary
		_sv_a=$( {
			printf '%s\n%s\n' "$_sv_cc" "$SV_CFLAGS"
			cat "$_sv_src"
		} | cksum) || _sv_a=
		_sv_b=$( {
			printf 'supervise\n%s\n%s\n' "$_sv_cc" "$SV_CFLAGS"
			cat "$_sv_src"
		} | cksum) || _sv_b=
		_sv_a=${_sv_a%% *}
		_sv_b=${_sv_b%% *}
		if ! sv_crc "$_sv_a" || ! sv_crc "$_sv_b"; then
			echo "supervise: neither shasum nor cksum gave a digest" >&2
			return 1
		fi
		_sv_name=supervise-crc-$(printf '%08x%08x' "$_sv_a" "$_sv_b")
	fi
	if [ ! -d "$_sv_dir" ]; then
		(umask 077 && mkdir -p "$_sv_dir") || {
			echo "supervise: cannot make the tool cache" >&2
			return 1
		}
	fi
	# from here on only the physical path is used, checked, built in and
	# printed: a symbolic link on the way (in a directory others may write)
	# could be swapped once it was checked (security I1, round 1)
	_sv_real=$(cd "$_sv_dir" 2> /dev/null && pwd -P) || _sv_real=
	case $_sv_real in
	/*) ;;
	*)
		echo "supervise: the tool cache is refused: its physical path cannot be read" >&2
		return 1
		;;
	esac
	_sv_dir=$_sv_real
	sv_safe "$_sv_dir/." || {
		echo "supervise: the tool cache is refused: not a directory owned by this user, writable by group or others, or with an ACL" >&2
		return 1
	}
	sv_parents "$_sv_dir" || {
		echo "supervise: the tool cache is refused: a directory above it is owned by another user, or writable by group or others and not sticky" >&2
		return 1
	}
	_sv_bin=$_sv_dir/$_sv_name
	if [ -h "$_sv_bin" ] || { [ -e "$_sv_bin" ] && [ ! -f "$_sv_bin" ]; }; then
		echo "supervise: the cached supervisor is refused: not a regular file" >&2
		return 1
	fi
	if [ -e "$_sv_bin" ] && ! sv_safe "$_sv_bin"; then
		echo "supervise: the cached supervisor is refused: not owned by this user, writable by group or others, or with an ACL" >&2
		return 1
	fi
	if [ ! -x "$_sv_bin" ]; then
		_sv_tmp=$(mktemp "$_sv_bin.XXXXXX") || {
			echo "supervise: cannot make a file in the tool cache" >&2
			return 1
		}
		_sv_log=$(mktemp "$_sv_dir/supervise-log.XXXXXX") || {
			rm -f "$_sv_tmp"
			echo "supervise: cannot make a file in the tool cache" >&2
			return 1
		}
		# shellcheck disable=SC2086 # the flag list
		if ! cc $SV_CFLAGS -o "$_sv_tmp" "$_sv_src" > "$_sv_log" 2>&1; then
			sed 's/^/supervise: cc: /' "$_sv_log" >&2
			rm -f "$_sv_tmp" "$_sv_log"
			echo "supervise: cannot build scripts/lib/supervise.c" >&2
			return 1
		fi
		rm -f "$_sv_log"
		chmod 755 "$_sv_tmp" && mv -f "$_sv_tmp" "$_sv_bin" || {
			rm -f "$_sv_tmp"
			echo "supervise: cannot move the binary into the tool cache" >&2
			return 1
		}
	fi
	printf '%s\n' "$_sv_bin"
}

# sv_run runs one supervisor at a time for this shell, in the background,
# and is the only shell code that signals it (TASK221). The supervisor is a
# background job without job control: it starts with SIGINT and SIGQUIT
# ignored, which it keeps (it names them "ignored"), so every stop it is
# sent is a TERM, whatever signal the caller got. sv_run sets no trap: a
# trap it replaced could not be put back without eval. The caller's own
# INT, TERM or HUP trap, which runs in sv_wait's wait at once, calls
# sv_stop, which sends the supervisor TERM and waits for it again; the
# supervisor stops the command's group (TERM and CONT, KILL after --grace)
# and exits once the group is empty.
SV_BIN=
SV_PID=
SV_LAST=
SV_FORK=
SV_STATUS=0

# sv_run <options> -- cmd args...: the supervisor (sv_path, found once per
# shell) on the command, waited for; its status, or 125 when there is no
# supervisor (said on standard error).
sv_run() {
	if [ -z "$SV_BIN" ]; then SV_BIN=$(sv_path) || return 125; fi
	sv_start "$SV_BIN" "$@"
	sv_wait
}

# sv_start <binary> <options> -- cmd args...: forks the supervisor; SV_PID is
# its pid. A trap that runs between the fork and that store finds it as $!
# (sv_pid), by SV_FORK and SV_LAST ($! before the fork).
sv_start() {
	_ss_b=$1
	shift
	case $- in *u*) set +u && SV_LAST=$! && set -u ;; *) SV_LAST=$! ;; esac
	SV_FORK=1
	"$_ss_b" "$@" &
	SV_PID=$!
	SV_FORK=
}

# sv_pid: SV_PID, also for a trap that runs in sv_start's fork window
sv_pid() {
	[ -z "$SV_PID" ] && [ -n "$SV_FORK" ] || return 0
	case $- in *u*) set +u && _sp_p=$! && set -u ;; *) _sp_p=$! ;; esac
	[ "$_sp_p" = "$SV_LAST" ] || SV_PID=$_sp_p
}

# sv_wait: waits for the supervisor, and returns its status (SV_STATUS).
# A wait a trap ended (above 128) is waited again while the supervisor is
# there; once it is not, one more wait collects its status (127 from a
# shell that has none: the status above 128 is kept). A trap that collected
# it itself (sv_stop) and returned leaves its status.
sv_wait() {
	while [ -n "$SV_PID" ]; do
		_sw_r=0
		wait "$SV_PID" 2> /dev/null || _sw_r=$?
		[ -n "$SV_PID" ] || break
		if [ "$_sw_r" -gt 128 ]; then
			kill -0 "$SV_PID" 2> /dev/null && continue # bounded-run: allowed: sv_wait: its own supervisor is there
			_sw_s=0
			wait "$SV_PID" 2> /dev/null || _sw_s=$?
			[ "$_sw_s" -eq 127 ] || _sw_r=$_sw_s
		fi
		SV_STATUS=$_sw_r
		SV_PID=
	done
	return "$SV_STATUS"
}

# sv_term: TERM to the supervisor (it begins its stop), no wait
sv_term() {
	sv_pid
	[ -z "$SV_PID" ] || kill -s TERM "$SV_PID" 2> /dev/null || :
}

# sv_stop: for a trap: sv_term, then sv_wait; 0 when no supervisor runs
sv_stop() {
	sv_term
	[ -n "$SV_PID" ] || return 0
	sv_wait
}
