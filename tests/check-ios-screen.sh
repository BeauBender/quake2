#!/bin/sh
# Invariant (TASK172): the iOS code sizes and places the game by the
# window the scene gives it (SDL's window size, pixel size and safe area,
# read on every change), never by UIScreen.main, never by the device's
# model and never by a fixed size. A foldable (the iPhone Duo) moves the
# window between two displays of different sizes and aspects as it folds,
# its inner display does not keep a landscape-only app in landscape, and
# UIScreen.main is ambiguous with two displays: code that asks for the
# main screen, branches on a model or idiom, or compares a size with a
# number lays the game out for one device only.
#
# The iOS paths are: every block of src/ that is compiled only for iOS
# (the true branch of an #if, #ifdef or #elif whose condition names
# TARGET_OS_IPHONE or TARGET_OS_IOS, up to its #else, #elif or #endif),
# the touch overlay (src/client/input/touch.c) and stuff/ios/. Comments
# are not code and are skipped. Refused there:
#   (a) UIScreen.main, mainScreen (anywhere in src/ and stuff/ios/, not
#       only in an iOS block);
#   (b) the device's model or idiom: hw.machine, hw.model, utsname,
#       UIDevice, userInterfaceIdiom, a string literal that starts with
#       iPhone, iPad or iPod, or a model identifier (iPhone19,4);
#   (c) a fixed size: a comparison (==, !=, <, >, <=, >=) with a number
#       of three digits or more, or CGSizeMake or CGRectMake of one.
#
# Before the tree it checks itself: a clean fixture passes, and each
# mutant (one refused line put into the fixture's iOS block, or for (a)
# outside it) fails, while the same size or model line outside an iOS
# block, or in an "#if !TARGET_OS_IPHONE" block, passes.
#
# Usage: tests/check-ios-screen.sh
# Needs only sh and perl.
set -u
cd "$(dirname "$0")/.." || exit 1
root=$(pwd -P)

tmp=$(mktemp -d "${TMPDIR:-/tmp}/check-ios-screen.XXXXXX") || exit 1
trap 'rm -rf "${tmp:?}"' EXIT

# scan ROOT: prints one line per refused line, exits 1 when there is one
scan() {
	perl -e '
	use strict; use warnings;
	use File::Find;
	my $root = shift;
	my @files;
	for my $d ("src", "stuff/ios") {
		next unless -d "$root/$d";
		find({ no_chdir => 1, wanted => sub {
			push @files, $_ if -f $_ && /\.(c|m|h|mm|in|plist)$/;
		} }, "$root/$d");
	}
	my $bad = 0;
	my $scanned = 0;
	for my $f (sort @files) {
		open(my $fh, "<", $f) or die "cannot read $f\n";
		local $/; my $text = <$fh>; close($fh);
		# comments out, newlines kept so that the line numbers hold
		$text =~ s{/\*.*?\*/}{ my $c = $&; $c =~ s/[^\n]//g; $c }gse;
		$text =~ s{//[^\n]*}{}g;
		(my $rel = $f) =~ s{^\Q$root\E/}{};
		my $whole = ($rel eq "src/client/input/touch.c" || $rel =~ m{^stuff/ios/});
		my @stack;    # per #if depth: 1 when that branch is iOS only
		my $n = 0;
		for my $line (split /\n/, $text, -1) {
			$n++;
			if ($line =~ /^\s*#\s*(if|ifdef|ifndef)\b(.*)/) {
				my ($k, $c) = ($1, $2);
				push @stack, ($k ne "ifndef" && $c =~ /TARGET_OS_(IPHONE|IOS)\b/ && $c !~ /!\s*(defined\s*\(?\s*)?TARGET_OS_(IPHONE|IOS)/) ? 1 : 0;
				next;
			}
			if ($line =~ /^\s*#\s*elif\b(.*)/) {
				my $c = $1;
				$stack[-1] = ($c =~ /TARGET_OS_(IPHONE|IOS)\b/ && $c !~ /!\s*TARGET_OS_(IPHONE|IOS)/) ? 1 : 0 if @stack;
				next;
			}
			if ($line =~ /^\s*#\s*else\b/) { $stack[-1] = 0 if @stack; next; }
			if ($line =~ /^\s*#\s*endif\b/) { pop @stack; next; }
			my $ios = $whole || grep { $_ } @stack;
			$scanned++ if $ios;
			my @why;
			push @why, "UIScreen.main" if $line =~ /\bUIScreen\s*\.\s*main\b|\bmainScreen\b/;
			if ($ios) {
				push @why, "the device model" if $line =~ /hw\.machine|hw\.model|\butsname\b|\bUIDevice\b|userInterfaceIdiom|"(iPhone|iPad|iPod)|\b(iPhone|iPad|iPod)[0-9]+,[0-9]+/;
				push @why, "a fixed size" if $line =~ /(?<![-<>=!])(==|!=|<=|>=|<|>)(?![<>])\s*\(?\s*[0-9]{3,}(\.[0-9]*)?f?\b|\b[0-9]{3,}(\.[0-9]*)?f?\s*\)?\s*(==|!=|<=|>=|<(?!<)|>(?!>))|\bCG(Size|Rect)Make\s*\([^)]*\b[0-9]{3,}/;
			}
			for my $w (@why) {
				(my $l = $line) =~ s/^\s+//;
				print "$rel:$n: $w: $l\n";
				$bad = 1;
			}
		}
	}
	print "scanned $scanned\n";
	exit $bad;
	' "$1"
}

fail=0
bad() { echo "check-ios-screen: FAIL $*"; fail=1; }

# ---- the selftest: a fixture and its mutants --------------------------------
fixture() {
	# fixture DIR LINE_IN_IOS_BLOCK LINE_OUTSIDE LINE_IN_NOT_IOS_BLOCK
	mkdir -p "${1:?}/src/client/vid" "${1:?}/stuff/ios" || exit 1
	{
		printf '%s\n' '#include <TargetConditionals.h>' '' 'static void' 'Layout(int w, int h)' '{'
		printf '%s\n' "$3"
		printf '%s\n' '#if defined(__APPLE__) && TARGET_OS_IPHONE'
		printf '%s\n' '	/* a comment may name UIScreen.main, hw.machine and w == 669: it is not code */'
		printf '%s\n' '	SDL_GetWindowSafeArea(window, &r);'
		printf '%s\n' "$2"
		printf '%s\n' '#else'
		printf '%s\n' '	if (w > 640) { w = 640; }'
		printf '%s\n' '#endif'
		printf '%s\n' '#if !TARGET_OS_IPHONE'
		printf '%s\n' "$4"
		printf '%s\n' '#endif'
		printf '%s\n' '}'
	} > "${1:?}/src/client/vid/fixture.c"
	printf '%s\n' '<key>CFBundleName</key>' > "${1:?}/stuff/ios/Info.plist.in"
}

n=0
expect() {
	# expect NAME pass|fail IOS_LINE OUTSIDE_LINE NOT_IOS_LINE
	n=$((n + 1))
	d="${tmp:?}/st$n"
	fixture "$d" "$3" "$4" "$5"
	if scan "$d" > "${tmp:?}/st$n.out" 2>&1; then got=pass; else got=fail; fi
	if [ "$got" != "$2" ]; then
		bad "selftest $1: expected $2, got $got ($(head -n 1 "${tmp:?}/st$n.out"))"
	fi
}

clean='	w = r.w;'
expect clean pass "$clean" '' ''
expect uiscreen-main fail '	CGRect b = UIScreen.main.bounds;' '' ''
expect mainscreen fail '	CGFloat s = [[UIScreen mainScreen] scale];' '' ''
expect uiscreen-main-outside fail "$clean" '	UIScreen *s = UIScreen.main;' ''
expect hw-machine fail '	sysctlbyname("hw.machine", m, &len, NULL, 0);' '' ''
expect utsname fail '	struct utsname u; uname(&u);' '' ''
expect uidevice fail '	if ([[UIDevice currentDevice] userInterfaceIdiom] == UIUserInterfaceIdiomPad) { }' '' ''
expect model-string fail '	if (strcmp(model, "iPhone Duo") == 0) { }' '' ''
expect model-id fail '	if (strcmp(model, iPhone19,4) == 0) { }' '' ''
expect size-eq fail '	if (w == 669) { }' '' ''
expect size-ge fail '	if (pw >= 2007) { }' '' ''
expect size-lit-first fail '	if (951 < h) { }' '' ''
expect size-float fail '	if (r.w > 466.0f) { }' '' ''
expect cgsize fail '	CGSize s = CGSizeMake(669, 951);' '' ''
expect size-outside pass "$clean" '	if (w == 669) { }' ''
expect model-outside pass "$clean" '	if (strcmp(model, "iPhone Duo") == 0) { }' ''
expect size-not-ios pass "$clean" '' '	if (w >= 2007) { }'
expect small-number pass '	if (w > 0 && h < 2 && x == 64) { }' '' ''
expect shift-not-size pass '	v = (w << 1000) >> 200;' '' ''

if [ "$fail" -ne 0 ]; then
	echo "check-ios-screen: the selftest failed; the tree was not checked"
	exit 1
fi

# ---- the tree ----------------------------------------------------------------
if ! scan "$root" > "${tmp:?}/tree.out" 2>&1; then
	cat "${tmp:?}/tree.out"
	bad "the iOS paths ask UIScreen.main, the device model or a fixed size (above)"
fi
lines=$(sed -n "s/^scanned \([0-9]*\)$/\1/p" "${tmp:?}/tree.out")
[ "${lines:-0}" -ge 200 ] || bad "only ${lines:-0} lines of iOS paths found: the scan no longer finds the iOS blocks"

if [ "$fail" -ne 0 ]; then
	exit 1
fi

echo "check-ios-screen: ok (selftest $n cases; $lines lines of iOS paths, no UIScreen.main, device model or fixed size in the iOS paths)"
