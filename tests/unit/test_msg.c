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
 * Network message writer and reader (src/common/movemsg.c, szone.c):
 * every MSG_Write* / MSG_Read* pair round-trips, the wire bytes are
 * pinned (they are the protocol), and reads past the end of a message
 * return -1 (or an empty string) without touching memory past cursize.
 *
 * =======================================================================
 */

#include <limits.h>

#include "test.h"

static void
init_msg(sizebuf_t *sb, byte *data, int len)
{
	SZ_Init(sb, data, len);
	memset(data, 0, len);
}

TEST(msg_scalar_roundtrip)
{
	sizebuf_t sb;
	byte buf[256];
	t_text_t pin = {0};

	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteChar(&sb, -5);
	MSG_WriteChar(&sb, 127);
	MSG_WriteByte(&sb, 0);
	MSG_WriteByte(&sb, 255);
	MSG_WriteShort(&sb, -32768);
	MSG_WriteShort(&sb, 0x1234);
	MSG_WriteLong(&sb, -2);
	MSG_WriteLong(&sb, 0x7f00ff01);
	MSG_WriteFloat(&sb, 1.5f);
	MSG_WriteFloat(&sb, -0.0f);
	MSG_WriteString(&sb, "hello");
	MSG_WriteString(&sb, NULL);
	MSG_WriteCoord(&sb, 100.125f);
	MSG_WriteCoord(&sb, -4095.875f);
	MSG_WriteAngle(&sb, 90.0f);
	MSG_WriteAngle(&sb, -45.0f);
	MSG_WriteAngle16(&sb, 359.0f);

	MSG_BeginReading(&sb);
	T_EQ_INT(MSG_ReadChar(&sb), -5);
	T_EQ_INT(MSG_ReadChar(&sb), 127);
	T_EQ_INT(MSG_ReadByte(&sb), 0);
	T_EQ_INT(MSG_ReadByte(&sb), 255);
	T_EQ_INT(MSG_ReadShort(&sb), -32768);
	T_EQ_INT(MSG_ReadShort(&sb), 0x1234);
	T_EQ_INT(MSG_ReadLong(&sb), -2);
	T_EQ_INT(MSG_ReadLong(&sb), 0x7f00ff01);
	T_EQ_FLT(MSG_ReadFloat(&sb), 1.5f);
	T_EQ_FLT(MSG_ReadFloat(&sb), 0.0f);
	T_EQ_STR(MSG_ReadString(&sb), "hello");
	T_EQ_STR(MSG_ReadString(&sb), "");
	/* Coords travel as 1/8 units in a short. */
	T_EQ_FLT(MSG_ReadCoord(&sb), 100.125f);
	T_EQ_FLT(MSG_ReadCoord(&sb), -4095.875f);
	/* Angles travel as 256ths and 65536ths of a turn. */
	T_EQ_FLT(MSG_ReadAngle(&sb), 90.0f);
	T_EQ_FLT(MSG_ReadAngle(&sb), -45.0f);
	/* MSG_ReadAngle16 reads a signed short: 359 degrees comes back as
	   about -1 (the same direction). */
	T_EQ_FLT(MSG_ReadAngle16(&sb), SHORT2ANGLE((short)ANGLE2SHORT(359.0f)));
	T_EQ_INT(sb.readcount, sb.cursize);

	t_text_hex(&pin, sb.data, sb.cursize);
	t_pin("msg-scalars", pin.data);
	t_text_free(&pin);
}

/*
 * A message whose backing store continues past cursize with 0xaa sentinel
 * bytes: a read that looked past the end would return them.
 */
static void
init_short_msg(sizebuf_t *sb, byte *data, int maxlen, const byte *bytes, int len)
{
	SZ_Init(sb, data, maxlen);
	memset(data, 0xaa, maxlen);
	memcpy(data, bytes, len);
	sb->cursize = len;
	MSG_BeginReading(sb);
}

TEST(msg_truncated_reads)
{
	static const byte three[] = { 0x01, 0x02, 0x03 };
	sizebuf_t sb;
	byte buf[64];
	vec3_t pos;

	/* Each fixed-size read that does not fit returns -1 and still
	   advances readcount, so later reads keep failing. */
	init_short_msg(&sb, buf, sizeof(buf), three, 3);
	T_EQ_INT(MSG_ReadShort(&sb), 0x0201);
	T_EQ_INT(MSG_ReadShort(&sb), -1);
	T_EQ_INT(sb.readcount, 4);
	T_EQ_INT(MSG_ReadByte(&sb), -1);
	T_EQ_INT(MSG_ReadChar(&sb), -1);

	init_short_msg(&sb, buf, sizeof(buf), three, 3);
	T_EQ_INT(MSG_ReadLong(&sb), -1);
	T_EQ_INT(sb.readcount, 4);

	init_short_msg(&sb, buf, sizeof(buf), three, 3);
	T_EQ_FLT(MSG_ReadFloat(&sb), -1.0f);

	init_short_msg(&sb, buf, sizeof(buf), three, 3);
	T_EQ_FLT(MSG_ReadCoord(&sb), 0x0201 * 0.125f);
	T_EQ_FLT(MSG_ReadCoord(&sb), -0.125f);

	init_short_msg(&sb, buf, sizeof(buf), three, 3);
	MSG_ReadPos(&sb, pos);
	T_EQ_FLT(pos[0], 0x0201 * 0.125f);
	T_EQ_FLT(pos[1], -0.125f);
	T_EQ_FLT(pos[2], -0.125f);

	init_short_msg(&sb, buf, sizeof(buf), three, 3);
	T_EQ_FLT(MSG_ReadAngle16(&sb), SHORT2ANGLE(0x0201));
	T_EQ_FLT(MSG_ReadAngle16(&sb), SHORT2ANGLE(-1));
	T_EQ_FLT(MSG_ReadAngle(&sb), -1.40625f);

	/* An empty message. */
	init_short_msg(&sb, buf, sizeof(buf), three, 0);
	T_EQ_INT(MSG_ReadByte(&sb), -1);
}

TEST(msg_truncated_strings)
{
	static const byte unterminated[] = { 'a', 'b', 'c' };
	static const byte lines[] = { 'o', 'n', 'e', '\n', 't', 'w', 'o', 0, 'x' };
	sizebuf_t sb;
	byte buf[64];
	byte data[8];

	/* A string without its terminator ends at cursize. */
	init_short_msg(&sb, buf, sizeof(buf), unterminated, 3);
	T_EQ_STR(MSG_ReadString(&sb), "abc");
	T_EQ_INT(sb.readcount, 4);
	T_EQ_STR(MSG_ReadString(&sb), "");

	init_short_msg(&sb, buf, sizeof(buf), lines, sizeof(lines));
	T_EQ_STR(MSG_ReadStringLine(&sb), "one");
	T_EQ_STR(MSG_ReadStringLine(&sb), "two");
	T_EQ_STR(MSG_ReadStringLine(&sb), "x");
	T_EQ_STR(MSG_ReadStringLine(&sb), "");

	/* MSG_ReadData stores (byte)-1 for every byte past the end. */
	init_short_msg(&sb, buf, sizeof(buf), unterminated, 3);
	memset(data, 0, sizeof(data));
	MSG_ReadData(&sb, data, 5);
	T_EQ_INT(data[0], 'a');
	T_EQ_INT(data[2], 'c');
	T_EQ_INT(data[3], 0xff);
	T_EQ_INT(data[4], 0xff);
	T_EQ_INT(data[5], 0);
}

TEST(msg_long_string_is_cut)
{
	/* MSG_ReadString returns at most 2047 characters; the rest of the
	   string stays in the message and is read as the next string. */
	static byte big[3000];
	static byte store[3100];
	sizebuf_t sb;
	char *s;

	memset(big, 'q', sizeof(big) - 1);
	big[sizeof(big) - 1] = 0;
	init_short_msg(&sb, store, sizeof(store), big, sizeof(big));
	s = MSG_ReadString(&sb);
	T_EQ_INT(strlen(s), 2047);
	T_EQ_INT(sb.readcount, 2047);
	T_EQ_INT(strlen(MSG_ReadString(&sb)), sizeof(big) - 1 - 2047);
	T_EQ_INT(sb.readcount, sb.cursize);
}

TEST(msg_dir_roundtrip)
{
	static const byte bad[] = { 200 };
	sizebuf_t sb;
	byte buf[16];
	vec3_t up = { 0, 0, 1 }, out;

	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteDir(&sb, up);
	MSG_WriteDir(&sb, NULL);
	T_EQ_INT(sb.cursize, 2);

	MSG_BeginReading(&sb);
	MSG_ReadDir(&sb, out);
	T_EQ_FLT(out[0], 0.0f);
	T_EQ_FLT(out[1], 0.0f);
	T_EQ_FLT(out[2], 1.0f);
	/* NULL is written as index 0, which is a real normal. */
	MSG_ReadDir(&sb, out);
	T_CHECK(out[2] != 1.0f);

	/* Past the end, and an index outside the table, drop. */
	T_EXPECT_DROP(MSG_ReadDir(&sb, out));
	T_CONSOLE_HAS("MSG_ReadDir: unexpected message end");

	init_short_msg(&sb, buf, sizeof(buf), bad, 1);
	T_EXPECT_DROP(MSG_ReadDir(&sb, out));
	T_CONSOLE_HAS("MSG_ReadDir: out of range");
}

TEST(msg_usercmd_delta)
{
	sizebuf_t sb;
	byte buf[64];
	usercmd_t base, cmd, got;
	t_text_t pin = {0};

	memset(&base, 0, sizeof(base));
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 16;
	cmd.angles[0] = 100;
	cmd.angles[2] = -7;
	cmd.forwardmove = 400;
	cmd.sidemove = -200;
	cmd.buttons = BUTTON_ATTACK;
	cmd.impulse = 9;
	cmd.lightlevel = 77;

	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteDeltaUsercmd(&sb, &base, &cmd);
	/* Unchanged: only the bits byte, msec and light level. */
	MSG_WriteDeltaUsercmd(&sb, &cmd, &cmd);

	MSG_BeginReading(&sb);
	MSG_ReadDeltaUsercmd(&sb, &base, &got);
	T_EQ_MEM(&got, &cmd, sizeof(cmd));
	MSG_ReadDeltaUsercmd(&sb, &cmd, &got);
	T_EQ_MEM(&got, &cmd, sizeof(cmd));
	T_EQ_INT(sb.readcount, sb.cursize);

	t_text_hex(&pin, sb.data, sb.cursize);
	t_pin("msg-usercmd-delta", pin.data);
	t_text_free(&pin);
}

TEST(msg_entity_delta_bytes)
{
	sizebuf_t sb;
	byte buf[256];
	entity_state_t from, to;
	t_text_t pin = {0};

	memset(&from, 0, sizeof(from));
	memset(&to, 0, sizeof(to));
	to.number = 300;
	to.origin[0] = 64.5f;
	to.origin[2] = -12.0f;
	to.angles[1] = 90.0f;
	to.modelindex = 3;
	to.frame = 260;
	to.skinnum = 2;
	to.effects = EF_ROTATE;
	to.renderfx = RF_GLOW;
	to.sound = 5;
	to.event = EV_ITEM_RESPAWN;
	to.solid = 31;

	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteDeltaEntity(&from, &to, &sb, false, true);
	T_EQ_INT(sb.cursize, (int)MSG_DeltaEntity_Size(&from, &to, false, true));
	t_text_printf(&pin, "new entity 300:\n");
	t_text_hex(&pin, sb.data, sb.cursize);

	/* An event is sent even when nothing else changed. */
	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteDeltaEntity(&to, &to, &sb, false, false);
	t_text_printf(&pin, "unchanged with event:\n");
	t_text_hex(&pin, sb.data, sb.cursize);

	/* No change, no event and not forced. Entity numbers above 255
	   still carry U_NUMBER16, which DeltaEntityBits sets before the
	   "nothing changed" test, so a header goes out; pinned as is. */
	to.event = 0;
	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteDeltaEntity(&to, &to, &sb, false, false);
	t_text_printf(&pin, "unchanged, entity 300, not forced:\n");
	t_text_hex(&pin, sb.data, sb.cursize);

	/* ... while a low entity number writes nothing. */
	to.number = 5;
	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteDeltaEntity(&to, &to, &sb, false, false);
	T_EQ_INT(sb.cursize, 0);

	/* No change but forced: just the header. */
	MSG_WriteDeltaEntity(&to, &to, &sb, true, false);
	t_text_printf(&pin, "forced unchanged:\n");
	t_text_hex(&pin, sb.data, sb.cursize);

	/* Entity numbers above SHRT_MAX are not sent at all. */
	init_msg(&sb, buf, sizeof(buf));
	to.number = SHRT_MAX + 1;
	MSG_WriteDeltaEntity(&from, &to, &sb, true, true);
	T_EQ_INT(sb.cursize, 0);

	t_pin("msg-entity-delta", pin.data);
	t_text_free(&pin);
}

TEST(msg_configstring)
{
	sizebuf_t sb;
	byte buf[64];

	init_msg(&sb, buf, sizeof(buf));
	MSG_WriteConfigString(&sb, CS_NAME, "base1");
	/* The writer emits index and string; the size helper also counts
	   the svc_configstring byte the caller writes in front. */
	T_EQ_INT(sb.cursize + 1, (int)MSG_ConfigString_Size("base1"));

	MSG_BeginReading(&sb);
	T_EQ_INT(MSG_ReadShort(&sb), CS_NAME);
	T_EQ_STR(MSG_ReadString(&sb), "base1");
}

static void
overflow_without_allowoverflow(void *arg)
{
	sizebuf_t sb;
	byte buf[4];

	(void)arg;
	SZ_Init(&sb, buf, sizeof(buf));
	MSG_WriteLong(&sb, 1);
	MSG_WriteByte(&sb, 1);
}

TEST(msg_sizebuf_overflow)
{
	sizebuf_t sb;
	byte buf[4];
	char out[4096];

	/* With allowoverflow the buffer is cleared and flagged. */
	SZ_Init(&sb, buf, sizeof(buf));
	sb.allowoverflow = true;
	MSG_WriteShort(&sb, 1);
	MSG_WriteShort(&sb, 2);
	T_CHECK(!sb.overflowed);
	MSG_WriteByte(&sb, 3);
	T_CHECK(sb.overflowed);
	T_EQ_INT(sb.cursize, 1);
	T_EQ_INT(buf[0], 3);

	/* Without it, the process ends through Com_Error(ERR_FATAL). */
	T_EQ_INT(t_expect_exit(overflow_without_allowoverflow, NULL, out, sizeof(out)), 1);
	T_CHECK(strstr(out, "SZ_GetSpace: overflow without allowoverflow set") != NULL);
}
