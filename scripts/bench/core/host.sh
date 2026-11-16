# scripts/bench/core/host.sh: host identity, from system facts only.
#
# The facts, in a fixed order, one key=value line each:
#   cpu    the CPU model (sysctl machdep.cpu.brand_string; /proc/cpuinfo
#          "model name", else uname -m)
#   gpu    the GPU model(s), joined by "+" (system_profiler
#          SPDisplaysDataType "Chipset Model", time-boxed; the NVIDIA proc
#          file; else "unknown")
#   mem    the memory size in bytes (sysctl hw.memsize; /proc/meminfo)
#   os     the OS name and version (sysctl kern.osproductversion; uname -s -r)
#   build  the OS build (sysctl kern.osversion; uname -m)
#   vm     "vm", "physical" or "unknown" (sysctl kern.hv_vmm_present; the
#          "hypervisor" CPU flag in /proc/cpuinfo)
# A fact that cannot be read is "unknown". No host name, user name, path or
# serial number is read.
#
# The token is "h-" and 12 hex digits of the digest of the facts. The class
# line is "<cpu>, <gpu> GPU, <os>, <vm>". Neither may hold this machine's
# host name (hostname -s), user name (whoami) or home path ($HOME), the
# values tests/check-no-local-identity.sh refuses (each of 3 characters or
# more, compared case-insensitively): in the class line each is replaced by
# "***"; a token that happens to spell one is hashed again with a salt line
# added, until it does not (so the token stays stable on one machine).

BENCH_HOST_FACTS=
BENCH_HOST_TOKEN=
BENCH_HOST_CLASS=

# bench_oneline: standard input as one line, blanks squeezed.
bench_oneline() {
	tr '\n\r\t' '   ' | awk '{ $1 = $1; print }'
}

# bench_host_facts <displays file>: prints the facts; on macOS the GPU model
# is read from <displays file>, the output of system_profiler
# SPDisplaysDataType that bench_host_init saved.
bench_host_facts() {
	_hf_sys=$(uname -s 2> /dev/null) || _hf_sys=unknown
	_hf_cpu=
	_hf_gpu=
	_hf_mem=
	_hf_os=
	_hf_build=
	_hf_vm=unknown
	if [ "$_hf_sys" = Darwin ]; then
		_hf_cpu=$(sysctl -n machdep.cpu.brand_string 2> /dev/null) || _hf_cpu=
		_hf_gpu=$(sed -n 's/^[[:space:]]*Chipset Model:[[:space:]]*//p' "$1" 2> /dev/null | paste -s -d + -) || _hf_gpu=
		_hf_mem=$(sysctl -n hw.memsize 2> /dev/null) || _hf_mem=
		_hf_os=$(sysctl -n kern.osproductversion 2> /dev/null) || _hf_os=
		_hf_os="macOS ${_hf_os:-unknown}"
		_hf_build=$(sysctl -n kern.osversion 2> /dev/null) || _hf_build=
		case $(sysctl -n kern.hv_vmm_present 2> /dev/null || true) in
		1) _hf_vm=vm ;;
		0) _hf_vm=physical ;;
		esac
	else
		if [ -r /proc/cpuinfo ]; then
			_hf_cpu=$(sed -n 's/^model name[[:space:]]*:[[:space:]]*//p' /proc/cpuinfo | sed -n 1p)
			if grep -q '^flags.*[[:space:]]hypervisor' /proc/cpuinfo; then _hf_vm=vm; else _hf_vm=physical; fi
		fi
		[ -n "$_hf_cpu" ] || _hf_cpu=$(uname -m 2> /dev/null || true)
		for _hf_f in /proc/driver/nvidia/gpus/*/information; do
			[ -r "$_hf_f" ] || continue
			_hf_gpu="$_hf_gpu${_hf_gpu:++}$(sed -n 's/^Model:[[:space:]]*//p' "$_hf_f" | sed -n 1p)"
		done
		if [ -r /proc/meminfo ]; then
			_hf_mem=$(awk '/^MemTotal:/ { printf "%.0f\n", $2 * 1024; exit }' /proc/meminfo)
		fi
		_hf_os="$_hf_sys $(uname -r 2> /dev/null || echo unknown)"
		_hf_build=$(uname -m 2> /dev/null || true)
	fi
	for _hf_k in cpu gpu mem os build vm; do
		case $_hf_k in
		cpu) _hf_v=$_hf_cpu ;;
		gpu) _hf_v=$_hf_gpu ;;
		mem) _hf_v=$_hf_mem ;;
		os) _hf_v=$_hf_os ;;
		build) _hf_v=$_hf_build ;;
		vm) _hf_v=$_hf_vm ;;
		esac
		_hf_v=$(printf '%s\n' "$_hf_v" | bench_oneline)
		printf '%s=%s\n' "$_hf_k" "${_hf_v:-unknown}"
	done
}

# bench_identity_words <file>: writes this machine's identity values to
# <file>, one per line, those of 3 characters or more.
bench_identity_words() {
	: > "$1"
	for _iw in "$(hostname -s 2> /dev/null || true)" "$(whoami 2> /dev/null || true)" "${HOME:-}"; do
		if [ "${#_iw}" -ge 3 ]; then printf '%s\n' "$_iw" >> "$1"; fi
	done
}

# bench_scrub <words file>: copies standard input with every
# case-insensitive occurrence of a word replaced by "***".
bench_scrub() {
	awk -v wf="$1" '
		BEGIN { while ((getline w < wf) > 0) if (length(w) >= 3) word[++n] = tolower(w) }
		{
			s = $0
			for (i = 1; i <= n; i++) {
				out = ""
				while ((p = index(tolower(s), word[i])) > 0) {
					out = out substr(s, 1, p - 1) "***"
					s = substr(s, p + length(word[i]))
				}
				s = out s
			}
			print s
		}'
}

# bench_holds_word <words file>: true when standard input holds a word,
# case-insensitively.
bench_holds_word() {
	awk -v wf="$1" '
		BEGIN { while ((getline w < wf) > 0) if (length(w) >= 3) word[++n] = tolower(w) }
		{ for (i = 1; i <= n; i++) if (index(tolower($0), word[i])) hit = 1 }
		END { exit !hit }'
}

# bench_host_init: sets BENCH_HOST_FACTS, BENCH_HOST_TOKEN and
# BENCH_HOST_CLASS. Call it in the main shell: on macOS it runs
# system_profiler under bench_bounded (30 s; a signal stops it), and exits 2
# when it does not answer in time, rather than make a token without the
# GPU.
bench_host_init() {
	[ -z "$BENCH_HOST_TOKEN" ] || return 0
	bench_tmp
	bench_identity_words "${BENCH_TMP:?}/identity"
	: > "${BENCH_TMP:?}/displays"
	if [ "$(uname -s 2> /dev/null || true)" = Darwin ]; then
		_hi_st=0
		bench_bounded 30 system_profiler SPDisplaysDataType > "${BENCH_TMP:?}/displays" 2> /dev/null || _hi_st=$?
		[ "$_hi_st" -ne 124 ] || bench_die 2 "system_profiler did not answer within 30 s, so the GPU model is unknown"
	fi
	BENCH_HOST_FACTS=$(bench_host_facts "${BENCH_TMP:?}/displays" | bench_scrub "${BENCH_TMP:?}/identity")
	_hi_salt=0
	while :; do
		if [ "$_hi_salt" -eq 0 ]; then
			BENCH_HOST_TOKEN=$(printf '%s\n' "$BENCH_HOST_FACTS" | bench_id h-) || exit 2
		else
			BENCH_HOST_TOKEN=$(printf '%s\nsalt=%s\n' "$BENCH_HOST_FACTS" "$_hi_salt" | bench_id h-) || exit 2
		fi
		printf '%s\n' "$BENCH_HOST_TOKEN" | bench_holds_word "${BENCH_TMP:?}/identity" || break
		_hi_salt=$((_hi_salt + 1))
	done
	BENCH_HOST_CLASS=$(printf '%s, %s GPU, %s, %s\n' \
		"$(bench_kv "$BENCH_HOST_FACTS" cpu)" "$(bench_kv "$BENCH_HOST_FACTS" gpu)" \
		"$(bench_kv "$BENCH_HOST_FACTS" os)" "$(bench_kv "$BENCH_HOST_FACTS" vm)" | bench_scrub "${BENCH_TMP:?}/identity")
}
