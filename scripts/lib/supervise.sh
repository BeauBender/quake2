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
# The build runs cc directly, unsupervised, into a temporary name that is
# moved into place, so a concurrent first use never runs a half-written
# binary. A build that fails is a failure: sv_path prints no path, says why
# on standard error (lines starting "supervise: ") and returns 1. There is
# no fallback to a shell supervisor.

SV_SRC_DIR=$(cd "$(dirname "${BASH_SOURCE:-$0}")" 2> /dev/null && pwd -P) || SV_SRC_DIR=
SV_CFLAGS="-O2 -Wall -Wextra"

# sv_cache: the tool cache directory
sv_cache() {
	if [ -n "${SUPERVISE_CACHE:-}" ]; then
		printf '%s\n' "$SUPERVISE_CACHE"
	elif [ -n "${XDG_CACHE_HOME:-}" ]; then
		printf '%s/quake2-tools\n' "$XDG_CACHE_HOME"
	elif [ -n "${HOME:-}" ]; then
		printf '%s/.cache/quake2-tools\n' "$HOME"
	else
		return 1
	fi
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
	_sv_dir=$(sv_cache) || {
		echo "supervise: no tool cache (set SUPERVISE_CACHE, XDG_CACHE_HOME or HOME)" >&2
		return 1
	}
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
	_sv_bin=$_sv_dir/supervise-$(printf '%s\n' "$_sv_key" | cut -c1-16)
	if [ ! -x "$_sv_bin" ]; then
		mkdir -p "$_sv_dir" || {
			echo "supervise: cannot make the tool cache $_sv_dir" >&2
			return 1
		}
		_sv_tmp=$(mktemp "$_sv_bin.XXXXXX") || {
			echo "supervise: cannot make a file in $_sv_dir" >&2
			return 1
		}
		# shellcheck disable=SC2086 # the flag list
		if ! cc $SV_CFLAGS -o "$_sv_tmp" "$_sv_src" > "$_sv_tmp.log" 2>&1; then
			sed 's/^/supervise: cc: /' "$_sv_tmp.log" >&2
			rm -f "$_sv_tmp" "$_sv_tmp.log"
			echo "supervise: cannot build $_sv_src" >&2
			return 1
		fi
		rm -f "$_sv_tmp.log"
		chmod 755 "$_sv_tmp" && mv -f "$_sv_tmp" "$_sv_bin" || {
			rm -f "$_sv_tmp"
			echo "supervise: cannot move the binary into $_sv_dir" >&2
			return 1
		}
	fi
	printf '%s\n' "$_sv_bin"
}
