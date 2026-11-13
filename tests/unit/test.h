/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * tests/unit/test.h -- the whole unit-test runner interface. Hand-written,
 * no framework: a registry filled by constructor functions, non-fatal
 * checks (T_CHECK, T_EQ_*), a fatal T_REQUIRE, a way to expect a
 * Com_Error(ERR_DROP) from real engine code (T_EXPECT_DROP), a fork-based
 * expectation for ERR_FATAL paths that exit the process (t_expect_exit),
 * and pins: committed expected text under tests/pins/unit/ that a test
 * produces and compares (t_pin), regenerated with --update-pins.
 *
 * The test binary links the real dedicated server objects (src/common,
 * src/server, the unix backend), built with -DDEDICATED_ONLY, minus
 * src/backends/unix/main.c; tests/unit/main.c supplies main() instead.
 *
 * =======================================================================
 */

#ifndef YQ2_TESTS_UNIT_TEST_H
#define YQ2_TESTS_UNIT_TEST_H

#include <setjmp.h>
#include <stddef.h>

#include "../../src/common/header/common.h"

typedef struct t_case_s
{
	const char *name;
	const char *file;
	void (*fn)(void);
	struct t_case_s *next;
} t_case_t;

void t_register(t_case_t *tc);

/* Defines and registers a test. Names must be unique across the suite. */
#define TEST(NAME) \
	static void NAME(void); \
	static t_case_t t_case_##NAME = { #NAME, __FILE__, NAME, NULL }; \
	__attribute__((constructor)) static void t_reg_##NAME(void) \
	{ \
		t_register(&t_case_##NAME); \
	} \
	static void NAME(void)

/* Failure reporting; the T_* macros below are what tests use. */
void t_fail(const char *file, int line, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
void t_abort_test(void) __attribute__((noreturn));

#define T_CHECK(COND) \
	do { if (!(COND)) { t_fail(__FILE__, __LINE__, "check failed: %s", #COND); } } while (0)

#define T_REQUIRE(COND) \
	do { if (!(COND)) { t_fail(__FILE__, __LINE__, "required: %s", #COND); t_abort_test(); } } while (0)

#define T_EQ_INT(A, B) \
	do { long long t_a_ = (long long)(A), t_b_ = (long long)(B); \
		if (t_a_ != t_b_) { t_fail(__FILE__, __LINE__, "%s == %s: got %lld, want %lld", #A, #B, t_a_, t_b_); } } while (0)

#define T_EQ_STR(A, B) \
	do { const char *t_a_ = (A), *t_b_ = (B); \
		if (!t_a_ || !t_b_ || strcmp(t_a_, t_b_) != 0) { \
			t_fail(__FILE__, __LINE__, "%s == %s: got \"%s\", want \"%s\"", #A, #B, \
				t_a_ ? t_a_ : "(null)", t_b_ ? t_b_ : "(null)"); } } while (0)

#define T_EQ_MEM(A, B, LEN) \
	do { if (memcmp((A), (B), (LEN)) != 0) { \
		t_fail(__FILE__, __LINE__, "%s and %s differ in the first %d bytes", #A, #B, (int)(LEN)); } } while (0)

#define T_EQ_FLT(A, B) \
	do { double t_a_ = (double)(A), t_b_ = (double)(B); \
		if (t_a_ != t_b_) { t_fail(__FILE__, __LINE__, "%s == %s: got %.9g, want %.9g", #A, #B, t_a_, t_b_); } } while (0)

/*
 * Console capture. The runner redirects Com_Printf output of every test
 * into a buffer (Com_BeginRedirect, the same mechanism rcon uses), so a
 * test can assert on what the engine printed.
 */
void t_console_reset(void);
const char *t_console(void);

/* Re-installs the capture after engine code ran its own redirect
   (rcon does; its Com_EndRedirect switches redirection off). */
void t_console_rearm(void);

#define T_CONSOLE_HAS(SUBSTR) \
	do { if (!strstr(t_console(), (SUBSTR))) { \
		t_fail(__FILE__, __LINE__, "console lacks \"%s\"; it was:\n%s", (SUBSTR), t_console()); } } while (0)

/*
 * Expects STMT to raise Com_Error(ERR_DROP or ERR_DISCONNECT): the real
 * Com_Error in src/common/clientserver.c longjmps to the global abortframe,
 * which this macro arms exactly as Qcommon_Frame does. Fails the test when
 * STMT returns normally. The ERR_DROP message is in t_console() afterwards.
 */
extern jmp_buf abortframe;
void t_rearm_abortframe(void);

#define T_EXPECT_DROP(STMT) \
	do { \
		t_console_reset(); \
		if (YQ2_SETJMP(abortframe) == 0) \
		{ \
			STMT; \
			t_rearm_abortframe(); \
			t_fail(__FILE__, __LINE__, "expected Com_Error(ERR_DROP) from %s", #STMT); \
		} \
		else \
		{ \
			t_rearm_abortframe(); \
		} \
	} while (0)

/*
 * Runs fn(arg) in a forked child with the console capture going to the
 * child's stdout, and returns the child's exit status (-1 if it did not
 * exit normally). Engine paths that end in Com_Error(ERR_FATAL) or
 * Sys_Error exit the process; this is how a test observes them. The
 * child's combined stdout and stderr is copied into out (NUL-terminated).
 */
int t_expect_exit(void (*fn)(void *), void *arg, char *out, size_t outlen);

/*
 * Pins. t_pin(name, text) compares text with the body of
 * tests/pins/unit/<name>.pin (everything after the provenance header) and
 * fails the test on any difference. With --update-pins it rewrites the
 * file instead: header plus text. Pins are text so that a deliberate
 * change shows up in review as a line diff.
 */
void t_pin(const char *name, const char *text);

/* Growable text buffer for building pin bodies. */
typedef struct
{
	char *data;
	size_t len, size;
} t_text_t;

void t_text_printf(t_text_t *t, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
void t_text_hex(t_text_t *t, const void *data, size_t len);
void t_text_free(t_text_t *t);

/* A fresh, empty, per-run scratch directory (created by the runner). */
const char *t_tmpdir(void);

/* Writes len bytes to <t_tmpdir()>/<relpath>, creating directories. */
void t_write_file(const char *relpath, const void *data, size_t len);

#endif
