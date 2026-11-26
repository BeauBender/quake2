# scripts/lib/supervise.sh -- builds the process supervisor on first use
# (TASK218). Sourced; POSIX sh, safe on bash 3.2.
#
#   . scripts/lib/supervise.sh
#   sv=$(sv_path) || exit 1
#   "$sv" --timeout 60 --events 9 -- cmd args... 9> events.log
#
# sv_path prints the path of the binary built from supervise.c beside this
# file, building it when it is not there. The binary lives in the tool cache,
# $SUPERVISE_CACHE, else $XDG_CACHE_HOME/quake2-tools, else
# $HOME/.cache/quake2-tools, under a name keyed by the SHA-256 of the
# compiler's version line, the flags and the source: a changed source or
# compiler builds a new binary, and an unchanged one is never built again.
#
# The cache is trusted only when it is safe. A SUPERVISE_CACHE that is not an
# absolute path is refused; an XDG_CACHE_HOME that is not one is ignored, as
# the XDG spec says. The directory is made with mode 0700. The directory and
# the binary must be owned by this user and not writable by group or others,
# and the binary must be a regular file, not a symbolic link; else sv_path
# refuses it. The build runs cc directly, unsupervised, into a temporary name
# that is moved into place, so a concurrent first use never runs a
# half-written binary; its log is a temporary file too.
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
	_sv_ls=$(ls -ldn "$1" 2> /dev/null) || return 1
	# shellcheck disable=SC2086 # the fields of the listing
	set -- $_sv_ls
	[ "${3:-}" = "$(id -u)" ] || return 1
	case $1 in
	?????w* | ????????w*) return 1 ;;
	esac
	return 0
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
	command -v shasum > /dev/null 2>&1 || {
		echo "supervise: shasum is not on PATH" >&2
		return 1
	}
	_sv_dir=$(sv_cache) || return 1
	_sv_cc=$(cc --version 2> /dev/null | sed -n 1p) || _sv_cc=
	[ -n "$_sv_cc" ] || {
		echo "supervise: cc --version printed nothing" >&2
		return 1
	}
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
	if [ ! -d "$_sv_dir" ]; then
		(umask 077 && mkdir -p "$_sv_dir") || {
			echo "supervise: cannot make the tool cache" >&2
			return 1
		}
	fi
	sv_safe "$_sv_dir/." || {
		echo "supervise: the tool cache is refused: not a directory owned by this user, or writable by group or others" >&2
		return 1
	}
	_sv_bin=$_sv_dir/supervise-$(printf '%s\n' "$_sv_key" | cut -c1-16)
	if [ -h "$_sv_bin" ] || { [ -e "$_sv_bin" ] && [ ! -f "$_sv_bin" ]; }; then
		echo "supervise: the cached supervisor is refused: not a regular file" >&2
		return 1
	fi
	if [ -e "$_sv_bin" ] && ! sv_safe "$_sv_bin"; then
		echo "supervise: the cached supervisor is refused: not owned by this user, or writable by group or others" >&2
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
