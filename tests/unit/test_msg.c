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
