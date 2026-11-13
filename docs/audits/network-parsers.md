# Audit of the network message parsers (TASK17)

Static, hostile-peer audit of the code that reads bytes off the wire: the
client-side server-message parser, the server-side client-message parser, the
connectionless (out-of-band) handlers on both sides, and the shared message,
netchannel and sizebuf code. The threat model is CLAUDE.md "Security-relevant
boundaries": a malicious server sending a connected client arbitrary
`svc_*` data and connectionless replies, and a malicious client sending a
server `clc_*` data, connectionless requests and userinfo.

Base: branch `task17-network-audit` off `main` (727a6280). Earlier
trust-boundary fixes are referenced where a site is already fixed or already
carded: TASK1 (#8, download chunk / savedir), TASK2 (#9, areabits and asset
loaders), FIX1 (#40), FIX3 (#44), FIX8 (#56), FIX10 (#73), FIX4/#46 (challenge
entropy).

## Method and coverage

Enumeration was by grep for every `MSG_Read*`, `Cmd_Argv`, `Info_ValueForKey`,
`strcpy`/`strcat`/`sprintf`/`memcpy` on peer data, and array indexing by a
parsed value, across the files in scope; then each function that reads the
network was read in full. Read in full: `src/common/movemsg.c`,
`src/common/netchan.c`, `src/common/szone.c`, `src/common/crc.c`
(`COM_BlockSequenceCRCByte`), `src/common/cmdparser.c` (`Cmd_TokenizeString`,
`Cmd_Argv`), the Info_* and `COM_Parse` functions in
`src/common/shared/shared.c`, `src/client/cl_parse.c`,
`src/client/cl_tempentities.c`, `src/client/cl_effects.c`
(`CL_AddMuzzleFlash`, `CL_AddMuzzleFlash2`), `src/client/cl_inventory.c`,
`src/client/cl_download.c`, `src/client/cl_network.c`
(`CL_ConnectionlessPacket`, `CL_ParseStatusMessage`),
`src/client/cl_screen.c` (`SCR_ExecuteLayoutString`, `DrawHUDStringScaled`,
`SCR_CenterPrint`), the sound frontend sinks (`S_BuildSoundList`,
`SDL_AddLoopSounds`, `S_PickChannel`, `GetEntitySoundOrigin`),
`src/client/cl_entities.c`, `src/client/cl_prediction.c`,
`src/server/sv_user.c`, `src/server/sv_conless.c`,
`src/server/sv_main.c` (`SV_ReadPackets`, `SV_UserinfoChanged`,
`SV_CalcPings`), and the backend packet receivers in
`src/backends/unix/network.c`. The game-module userinfo consumers
(`ClientUserinfoChanged`, `ClientConnect`, `SV_FilterPacket`) were read because
the server hands them peer userinfo. Grepped but not relevant: the renderers
(fed only from already-bounded client arrays), demo-file reading.

Two suspected defects were confirmed with standalone ASan harnesses that link
the real parser against stubs; their results are quoted in the Defects
section. The harnesses are in the auditor's scratch dir, not committed.

`MSG_Read*` short-read behaviour (relied on throughout): at end of message
`MSG_ReadByte`/`ReadShort`/`ReadLong`/`ReadChar` return `-1`, `MSG_ReadFloat`
returns `-1.0`, `MSG_ReadString`/`ReadStringLine` stop and NUL-terminate.
`readcount` is still advanced, so after an over-read `readcount > cursize` and
the top-level parse loops (`CL_ParseServerMessage`, `SV_ExecuteClientMessage`,
`CL_ParsePacketEntities`) detect it and drop. The `-1` return is the only
value an attacker can force by truncating; where a `-1` is used as a count,
index or size without a check, that is the bug to look for.

## Shared code

| Site (file / function / expr) | Reads | Used as | Check | Verdict |
|---|---|---|---|---|
| `movemsg.c` `MSG_ReadByte`/`Short`/`Long`/`Char` | 1/2/4 bytes | return value | `readcount + n > cursize` → returns `-1`, still `readcount += n` | ok |
| `movemsg.c` `MSG_ReadString`/`StringLine` `string[l]` | bytes to NUL/`\n` | index into `static char[2048]` | loop bound `l < sizeof(string)-1`; `-1`/`0` break | ok |
| `movemsg.c` `MSG_ReadData` `((byte*)data)[i]` | `len` bytes | write into caller buffer | **no bound here**; `len` is the caller's responsibility | ok-by-construction (sole caller `CL_ParseFrame` checks `len <= sizeof areabits`, see client table) |
| `movemsg.c` `MSG_ReadDir` `bytedirs[b]` | 1 byte `b` | index into `bytedirs[NUMVERTEXNORMALS]` | `b < 0` and `b >= NUMVERTEXNORMALS` both `Com_Error(ERR_DROP)` | ok |
| `movemsg.c` `MSG_ReadDeltaUsercmd` | bits byte then fields | writes into caller `usercmd_t` | fixed struct fields, no array indexed by data | ok |
| `netchan.c` `Netchan_Process` `sequence`,`sequence_ack` | 2×`MSG_ReadLong` | sequence math, `dropped = sequence-(incoming+1)` | top bit stripped; stale `sequence <= incoming` dropped; `dropped` consumed only as a loop count in `SV_ExecuteClientMessage`, there bounded `< 20` | ok |
| `netchan.c` `Netchan_Process` qport | `MSG_ReadShort` (server) | discarded | value unused | ok |
| `szone.c` `SZ_GetSpace` | `length` | `cursize + length > maxsize` | overflow without `allowoverflow` → `Com_Error(ERR_FATAL)`; writes come from engine, not peer | ok |
| `szone.c` `SZ_Write`/`SZ_Print` | `length`/`strlen` | `memcpy` len | engine-originated lengths only | ok |
| `crc.c` `COM_BlockSequenceCRCByte` `memcpy(chkb,base,length)` | `length` from caller | copy into `chkb[64]` | `length > 60` clamped to 60 before copy; `sequence < 0` → `Sys_Error` | ok |
| `cmdparser.c` `Cmd_TokenizeString` / `Cmd_Argv` | tokenized peer string | `cmd_argv[]`, count | `cmd_argc < MAX_STRING_TOKENS` on fill; `Cmd_Argv((unsigned)arg >= cmd_argc)` returns `""` | ok |
| `shared.c` `COM_Parse` `com_token[len]` | token chars | index into `com_token[MAX_TOKEN_CHARS]` | `len < MAX_TOKEN_CHARS` guards each write; `len == MAX_TOKEN_CHARS` reset to 0 — see Hardening H1 (off-by-one terminator) | hardening |
| `shared.c` `Info_ValueForKey` | key/value scan | `Q_strlcpy` into `value[2][MAX_INFO_VALUE]` | `vlen` clamped to `MAX_INFO_VALUE`; `Q_strlcpy` truncates | ok |
| `shared.c` `Info_SetValueForKey` / `Info_RemoveKey` | key/value | builds into `s` | `Info_ValidateKeyValue` bounds key/value < MAX_INFO_KEY/VALUE; `Q_strlcpy_ascii` with `MAX_INFO_STRING - slen` and overflow check | ok |
| `backends/unix/network.c` `NET_GetPacket` `recvfrom(..., maxsize, ...)` | datagram | `cursize = ret` | `ret == maxsize` → "Oversize packet" dropped; buffer is `MAX_MSGLEN` | ok |
| `backends/unix/network.c` `NET_GetLoopPacket` `memcpy(data, loop->msgs[i].data, datalen)` | loopback | `cursize` | local loopback, length set by `NET_SendLoopPacket` from engine | ok |

## Client (reads `svc_*` from a server, plus connectionless)

| Site (file / function / expr) | Reads | Used as | Check | Verdict |
|---|---|---|---|---|
| `cl_parse.c` `CL_ParseServerMessage` `cmd` | `MSG_ReadByte` | `switch`; `default` drops | `readcount > cursize` tested each loop; `cmd == -1` ends | ok |
| `cl_parse.c` `CL_ParseEntityBits` `number` | byte or short | returned as entity number | caller `CL_ParsePacketEntities` checks `newnum > MAX_CL_ENTNUM` → drop | ok |
| `cl_parse.c` `CL_ParsePacketEntities` ring | `oldframe->parse_entities + i` | index `cl_parse_entities[... & (MAX_PARSE_ENTITIES-1)]` | masked | ok |
| `cl_parse.c` `CL_DeltaEntity` `cl_parse_entities[cl.parse_entities & (MAX_PARSE_ENTITIES-1)]` | — | ring write; `frame->num_entities++` | ring masked; **`num_entities` itself is never capped to `MAX_EDICTS`** → feeds Defect D1 | defect (D1, in a sink) |
| `cl_parse.c` `CL_DeltaEntity` `CL_AllocEntity(newnum)` | newnum | realloc'd `cl_entities[newnum]` | `CL_AllocEntity` checks `0..MAX_CL_ENTNUM`, grows array via `NextPow2gt`; NULL → dummy | ok |
| `cl_parse.c` `CL_ParseDelta` modelindex/frame/skin/… | byte/short/long | stored in `entity_state_t` fields | consumed later with masks/bounds at the sinks (`cl.model_draw[modelindex]` — see Hardening H2) | ok / hardening |
| `cl_parse.c` `CL_ParsePlayerstate` `state->stats[i]` | long `statbits`, shorts | `stats[i]`, `i` in `0..MAX_STATS` loop | `i < MAX_STATS` | ok |
| `cl_parse.c` `CL_ParseFrame` `cl.frame.deltaframe` | `MSG_ReadLong` | `cl.frames[deltaframe & UPDATE_MASK]` | masked; validity checks | ok |
| `cl_parse.c` `CL_ParseFrame` areabits `len` | `MSG_ReadByte` | `MSG_ReadData` into `areabits[MAX_MAP_AREAS/8]` | `len > sizeof(areabits)` → `Com_Error(ERR_DROP)` (fixed in TASK2/#9) | ok (already carded/fixed) |
| `cl_parse.c` `CL_ParseServerData` `cl.playernum` | `MSG_ReadShort` | `cl.playernum+1` used as entity number across client | **no range check at read** → Defect D2 | defect (D2) |
| `cl_parse.c` `CL_ParseServerData` gamedir/levelname | `MSG_ReadString` | `Q_strlcpy` into `cl.gamedir`; `Cvar_Set("game", str)` | `Q_strlcpy` truncates; `FS_BuildGameSpecificSearchPath` rejects `..`, `/`, `\` | ok |
| `cl_parse.c` `CL_ParseConfigString` `i` | `MSG_ReadShort` | index `cl.configstrings[i]` and sub-ranges | `i < 0 || i >= MAX_CONFIGSTRINGS` → print+return; statusbar vs normal length checked | ok |
| `cl_parse.c` `CL_ParseStartSoundPacket` `sound_num` | `MSG_ReadByte` | `cl.sound_precache[sound_num]` | `sound_num < 0` drop; `>= MAX_SOUNDS` print+return | ok |
| `cl_parse.c` `CL_ParseStartSoundPacket` `ent` | `channel = MSG_ReadShort; ent = channel>>3` | passed to `S_StartSound` | `ent < 0` print+return; `S_StartSound`/`S_PickChannel` clamp — see Hardening H3 | ok / hardening |
| `cl_parse.c` svc_layout `MSG_ReadString` | string | `Q_strlcpy` into `cl.layout[1024]` | truncates | ok |
| `cl_parse.c` svc_print `i` | `MSG_ReadByte` | only compared to `PRINT_CHAT` | `i < 0` → break | ok |
| `cl_inventory.c` `CL_ParseInventory` `cl.inventory[i]` | `MSG_ReadShort` ×`MAX_ITEMS` | `inventory[i]` | `i` in `0..MAX_ITEMS` loop | ok |
| `cl_effects.c` `CL_AddMuzzleFlash` `i` | `MSG_ReadShort` | `CL_AllocEntity(i)` | `i < 1 || i > MAX_CL_ENTNUM` → `Com_Error` | ok |
| `cl_effects.c` `CL_AddMuzzleFlash` `weapon` | `MSG_ReadByte` | `switch`, no array index | `-1` hits default | ok |
| `cl_effects.c` `CL_AddMuzzleFlash2` `flash_number` | `MSG_ReadByte` | `monster_flash_offset[flash_number]` (216 entries) | `flash_number > 210` → print+return; byte so `<= 255`; 211..215 pass but within table — Hardening H4 | hardening |
| `cl_tempentities.c` `CL_ParseTEnt` `type` | `MSG_ReadByte` | `switch`; `default` `Com_Error` | ok | ok |
| `cl_tempentities.c` splash `r` | `MSG_ReadByte` | `splash_color[r]` (7 entries) | `r > 6 || r < 0` → `color=0` else index | ok |
| `cl_tempentities.c` beam/lightning/steam `ent`/`srcEnt`/`destEnt` | `MSG_ReadShort` | stored in beam `entity`/`dest_entity`, compared only | `CL_ParseLightning` drops on `-1`; beams store ints, used in equality tests not as indices | ok |
| `cl_tempentities.c` sustain `id` | `MSG_ReadShort` | `s->id`, compared only | not an index | ok |
| `cl_tempentities.c` `TE_LASER_SPARKS`/`TE_WELDING_SPARKS` `color`, `CL_ParseSteam` `r` | `MSG_ReadByte` | particle colour `color + (randk() & 7)`, up to 262 | none; reaches the renderers unmasked → Defect D9 | defect (D9) |
| `cl_tempentities.c` `TE_TUNNEL_SPARKS`, `TE_FORCEWALL` `color` | `MSG_ReadByte` | particle colour used as is (0..255) | in range | ok |
| `sw_part.c` `R_DrawParticle` `vid_alphamap[(color << 8) + pdest]` / `[color + (pdest << 8)]` | `particle_t.color` | translucency table index | **no mask** → Defect D9 | defect (D9) |
| `gl1_main.c` `R_DrawParticles2` `colortable[p->color]` | `particle_t.color` | `d_8to24table[256]` index | **no mask** (the point-sprite path and gl3 mask `& 0xFF`) → Defect D9 | defect (D9) |
| `cl_network.c` `CL_ConnectionlessPacket` `Cmd_Argv(1)` | tokenized | `strtol` → `cls.challenge`; echoed | numeric / `Netchan_OutOfBandPrint` with fixed format | ok |
| `cl_network.c` `cmd` connectionless | `MSG_ReadString` | `Cbuf_AddText` | gated on `NET_IsLocalAddress` | ok (by design for local) |
| `cl_main.c` `CL_ParseStatusMessage` → `M_AddToServerList` | `MSG_ReadString` | `Q_strlcpy` into `local_server_names[]` | `m_num_servers == MAX_LOCAL_SERVERS` guard; `Q_strlcpy` truncates | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"pic"`/`"stat_string"`/`"if"` stat index | `COM_Parse`+`strtol` of the layout | `stats[index]` | `index < 0 \|\| index >= MAX_STATS` rejected in each | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"pic"` `configstrings[CS_IMAGES + value]` | `value = stats[index]` (server `short`) | configstring row, then picture name | only `value >= MAX_IMAGES` rejected; **negative `value` passes** → Defect D8 | defect (D8) |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"stat_string"` `configstrings[value]` | `stats[index]` | configstring row | `value < 0 \|\| value >= MAX_CONFIGSTRINGS` rejected | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"client"`/`"ctf"` `clientinfo[value]` | `strtol` of the layout | clientinfo index | `value < 0 \|\| value >= MAX_CLIENTS` rejected; `ctf` `sprintf` into `block[80]` with `%3d %3d %-12.12s`: two `int` fields of at most 11 characters each plus a 12-character name, under 80 | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"num"` → `stats[strtol(token)]` | layout number | `stats[...]` index | **unchecked** index into `stats[MAX_STATS]` → Defect D3 | defect (D3) |
| `cl_screen.c` `DrawHUDStringScaled` `line[width++]` | `COM_Parse` token chars to `\n` | index into `line[1024]` | no bound on `width` in the function; its only live callers (`cstring`, `cstring2` layout tokens) pass a `COM_Parse` token, at most `MAX_TOKEN_CHARS - 1` = 1023 characters, so `line[1023]` is the furthest write | hardening (D4, not reachable today) |
| `cl_entities.c` `CL_AddPacketEntities` `cl.model_draw[s1->modelindex]` (and `modelindex2/3/4`) | modelindex bytes | model pointer | no explicit check; `CL_ParseDelta` reads each with `MSG_ReadByte`, so 0..255 against `model_draw[MAX_MODELS]` (256); `-1` only on a short read, which drops the connection before the frame is used (see D5) | ok (by the byte type; H2) |
| `cl_entities.c` `ci = &cl.clientinfo[s1->skinnum & 0xff]` | skinnum | clientinfo index | masked `& 0xff`, `MAX_CLIENTS == 256` | ok |
| `cl_entities.c` custom weapon `ci->weaponmodel[s1->skinnum >> 8]` | skinnum (`int`, byte/short/long on the wire) | model pointer | only `i > MAX_CLIENTWEAPONMODELS - 1` rejected; **negative `i` passes** → Defect D10 | defect (D10) |
| `cl_entities.c` `ent.skinnum = s1->skinnum` → `gl1_mesh.c`/`gl3_mesh.c` `currentmodel->skins[skinnum]` | skinnum | image pointer | only `skinnum >= MAX_MD2SKINS` tested; **negative passes** → Defect D10 (`sw_alias.c` tests both sides) | defect (D10) |
| `cl_entities.c` beam `ent.skinnum = (s1->skinnum >> (...)) & 0xff` → `d_8to24table[e->skinnum & 0xFF]` | skinnum | palette index | masked at both ends | ok |
| `cl_entities.c` `ent.frame = s1->frame` → `files/surf.c` `R_TextureAnimation` `frame % numframes` | frame (signed short) | count of `tex->next` steps | **negative frame gives a negative count**; the loop runs about 2^32 steps or reaches `NULL` → Defect D11 (alias models test both sides) | defect (D11) |
| `cl_entities.c` `cl.model_draw[ps->gunindex]` | `gunindex` (`MSG_ReadByte` in `CL_ParsePlayerstate`) | gun model | 0..255 by the byte type vs `MAX_MODELS` | ok (by the byte type) |
| `cl_entities.c` `bfg_lightramp[s1->frame]` | frame (`U_FRAME8` byte or `U_FRAME16` signed short) | index into `static int[6]` | reached when `effects & EF_BFG` is set without `EF_ANIM_ALLFAST`; **no check on either side** → Defect D5 | defect (D5) |
| `cl_prediction.c` `cl.model_clip[ent->modelindex]` | modelindex byte | clip model | reached when `ent->solid == 31`; 0..255 by the byte type against `model_clip[MAX_MODELS]` | ok (by the byte type; H2) |
| `sound/sound.c` `S_BuildSoundList` `sounds[i]` | loop to `num_entities` | write into caller `sounds[MAX_EDICTS]` | bound is `i <= MAX_CL_ENTNUM` (32767), **not `MAX_EDICTS` (1024)** → Defect D1 | defect (D1) |
| `sound/sdl.c` `SDL_AddLoopSounds` `sounds[i]` / loop | `i < num_entities` | read/zero `sounds[MAX_EDICTS]` | **no bound vs `MAX_EDICTS`** → Defect D1 | defect (D1) |
| `sound/sound.c` `cl.sound_precache[sounds[i]]` | sound id from `ent->sound` (byte) | precache index | `ent->sound` is a byte, `MAX_SOUNDS == 256` | ok |

## Server (reads `clc_*` from a client, plus connectionless and userinfo)

| Site (file / function / expr) | Reads | Used as | Check | Verdict |
|---|---|---|---|---|
| `sv_main.c` `SV_ReadPackets` `*(int*)net_message.data == -1` | first 4 bytes | connectionless test | `cursize` can be 0..1399 (`NET_GetPacket` accepts a zero-length datagram); the 4 bytes are always inside the 1400-byte buffer, stale for a datagram shorter than 4 bytes; a stale marker only sends an empty string to `SV_ConnectionlessPacket` | ok (in bounds) |
| `sv_main.c` `SV_ReadPackets` qport | `MSG_ReadShort & 0xffff` | client match | value only compared | ok |
| `sv_user.c` `SV_ExecuteClientMessage` `c` | `MSG_ReadByte` | `switch`; `default` drops | `readcount > cursize` tested each loop; `c == -1` ends | ok |
| `sv_user.c` clc_userinfo `MSG_ReadString` | string | `Q_strlcpy` into `cl->userinfo[MAX_INFO_STRING]` then `SV_UserinfoChanged` | truncates; `SV_UserinfoChanged` masks name high bit, clamps rate | ok |
| `sv_user.c` clc_move `checksum`, CRC over `net_message.data + checksumIndex + 1` | `MSG_ReadByte`; CRC length `readcount - checksumIndex - 1` | compared to `COM_BlockSequenceCRCByte(...)` | length clamped to 60 inside, but **computed before any `readcount > cursize` test**: a `clc_move` at the end of a full-size packet reads up to 55 bytes past `net_message_buffer` → Defect D12 | defect (D12) |
| `sv_user.c` clc_move `lastframe` | `MSG_ReadLong` | `cl->frames[lastframe & UPDATE_MASK]`, `frame_latency[lastframe & (LATENCY_COUNTS-1)]` | both masked; only read when `lastframe > 0` | ok |
| `sv_user.c` clc_move `net_drop = cl->netchan.dropped` | from netchan | loop count of `SV_ClientThink` | `net_drop < 20` guard | ok |
| `sv_user.c` clc_move usercmds | `MSG_ReadDeltaUsercmd` ×3 | `SV_ClientThink` | `msec` consumed as time; `commandMsec` underflow guarded by `sv_enforcetime` | ok |
| `sv_user.c` clc_stringcmd `MSG_ReadString` | string | `SV_ExecuteUserCommand` | `stringCmdCount < MAX_STRINGCMDS` rate-limited | ok (command dispatch by design) |
| `sv_user.c` `SV_Configstrings_f`/`SV_Baselines_f` `start` | `Cmd_Argv`→`strtol` | loop start / `sv.baselines[i]` | `start < 0` clamped to 0; loops bound by `MAX_CONFIGSTRINGS`/`numbaselines` | ok |
| `sv_user.c` `SV_BeginDownload_f` `name` | `Cmd_Argv(1)` | `FS_LoadFile(name)`, path | denylist `..`, `\`, `:`, leading `.`/`/`, must contain `/` | ok (see By design: denylist, not the client allowlist) |
| `sv_user.c` `SV_BeginDownload_f` `offset` | `Cmd_Argv(2)`→`strtol` | `downloadcount = offset`; then `SV_NextDownload_f` reads `download + downloadcount - r` | clamp only on the `>` side: `if (offset > downloadsize) downloadcount = downloadsize`; **negative `offset` passes** → Defect D6 | defect (D6) |
| `sv_conless.c` `SVC_DirectConnect` `version`/`qport`/`challenge` | `Cmd_Argv`→`strtol` | compared / stored | version and challenge checked; qport stored | ok |
| `sv_conless.c` `SVC_DirectConnect` userinfo | `Cmd_Argv(4)` | `Q_strlcpy` into `userinfo[MAX_INFO_STRING]`; `Info_SetValueForKey` | truncates; `Info_SetValueForKey` validates | ok |
| `sv_conless.c` `SVC_Info` `version` | `Cmd_Argv(1)`→`strtol` | compared; `Com_sprintf(string, 64, ...)` | **bug-adjacent**: wrong-version branch calls `Com_sprintf(string, sizeof(string), "%s: wrong version\n", hostname->string, sizeof(string))` — extra arg, benign; Hardening H6 | hardening |
| `sv_conless.c` `SVC_RemoteCommand` `net_message.data + 4` | raw packet tail | `Com_Printf("%s", ...)` before the password check | **nothing NUL-terminates the receive buffer**; the print can run past `net_message_buffer` → Defect D13 | defect (D13) |
| `sv_conless.c` `SVC_RemoteCommand` args | `Cmd_Argv(i)` | `Q_strlcat` into `remaining[1024]`; `Cmd_ExecuteString` | `Q_strlcat` bounded; gated on `Rcon_Validate` password | ok (rcon by design) |
| `sv_conless.c` `SV_ConnectionlessPacket` `s` | `MSG_ReadStringLine` | `Cmd_TokenizeString` | string bounded 2048; tokenizer bounded | ok |
| `sv_main.c` `SV_UserinfoChanged` `Info_ValueForKey(userinfo,"rate")` | userinfo value | `strtol` → clamped 100..15000 | clamped | ok |
| game `g_svcmds.c` `StringToFilter` `num[j++]` | `gi.argv(2)` of `sv addip` / `sv removeip` (server console or rcon; a token of up to 1023 characters) | write into `char num[128]` | **`j` unbounded** while the input is digits → Defect D7 | defect (D7) |
| game `g_svcmds.c` `SV_FilterPacket` `m[i]` | userinfo `ip` (set by the engine from `net_from` in `SVC_DirectConnect`) | octet bytes `m[4]` | `i < 4` loop bound; does not call `StringToFilter` | ok |
| game `client.c` `ClientUserinfoChanged` skin → `configstring(CS_PLAYERSKINS+playernum, va("%s\\%s", name, s))` | userinfo | configstring | `playernum = ent - g_edicts - 1`, engine-assigned; name/skin validated by `Info_Validate` | ok |

## By design (noted, not filed)

- `svc_stufftext` → `Cbuf_AddText` (cl_parse.c) lets a server queue arbitrary
  client console commands; connectionless `"cmd"` likewise but only from a
  local address. CLAUDE.md states this is intentional and unfiltered. A hostile
  server can therefore drive any client command (quit, `exec`, cvar changes);
  this is the documented trust model, not a parser defect. The parser's job is
  only to keep the bytes in bounds, which it does.
- `clc_stringcmd` → `SV_ExecuteUserCommand` → `ge->ClientCommand` lets a client
  invoke game commands; rate-limited by `MAX_STRINGCMDS` and tokenized with
  macro expansion disabled (the "Security Fix" comment). By design.
- `rcon` executes arbitrary server commands, gated on `rcon_password`. By
  design; note the password compare is a non-constant-time `strcmp`
  (timing side-channel), out of scope for a parser audit.
- Download path on the server is a denylist (`..`, `\`, `:`, leading `.`/`/`),
  not the client's allowlist. It is the long-standing id design and is not a
  memory-safety issue; FIX1/#40 already tracks tightening the *client* and
  savedir sides.

## Defects

Severity is applied to the attacker in the threat model (a hostile server
against a client; a hostile client or unauthenticated peer against a server):

- critical: an out-of-bounds write or a file write driven by a remote peer.
- high: a remote crash or hang; an out-of-bounds read whose bytes reach the
  attacker or that can fault; an unauthenticated takeover of a connection.
- medium: resource exhaustion; an unvalidated peer value that is in bounds
  today but seeds state used across the module; memory corruption reachable
  only by an operator or rcon holder.
- low: an out-of-bounds read that stays in mapped memory next to the buffer
  and whose result is only compared or logged locally; hardening.

### D1: `num_entities` can exceed `MAX_EDICTS`; `S_BuildSoundList` writes past `sounds[MAX_EDICTS]` (critical, high confidence) [FIX11 (#107)]

`CL_ParsePacketEntities` / `CL_DeltaEntity` increment `frame->num_entities`
once per entity in a frame and never cap it to `MAX_EDICTS` (1024). The store
`cl_parse_entities` is a 1024-entry ring accessed with a mask, so a frame with
more than 1024 entities is counted but aliases in the ring.
`S_BuildSoundList` (`sound.c`) loops `for (i = 0; i < num_entities &&
i <= MAX_CL_ENTNUM; i++) sounds[i] = ...`: the guard is `MAX_CL_ENTNUM`
(32767), not the size of the caller's array. Its one caller,
`SDL_AddLoopSounds` (`sdl.c`), declares `int sounds[MAX_EDICTS]` on the stack
and then reads `sounds[i]` and `sounds[j]` in its own two loops, also bounded
only by `num_entities` (the stale values read past the end are then used as
an index into `cl.sound_precache[]`). When `num_entities > 1024` the write
`sounds[i]` runs off the end of the 4 KB stack array.

Hostile input and reachability: `CL_ParsePacketEntities` has no ordering or
uniqueness check on `newnum`. With no delta frame (`oldnum = 99999`) every
entity record is a "delta from baseline" and is appended, so the same small
entity number can be repeated; a record with no field bits and an 8-bit
number costs 2 bytes. One packet (at most `MAX_MSGLEN - 1` = 1399 bytes, less
the netchan and frame headers) therefore carries about 680 entities. A second
frame that deltas from the first carries all of those over unchanged (the
"unchanged" loop runs for every old entry below the first new number) and
appends about 680 more, so the frame exceeds 1024 entities after two packets
from the server. The values written are each entity's `sound` byte, chosen by
the server, at stack slots past the array: server-driven stack memory
corruption on the client, which the scale rates critical.

Confirmation (ASan harness linking the real `S_BuildSoundList` with
`num_entities = 1380`): AddressSanitizer reports a `stack-buffer-overflow`
WRITE of size 4 in `S_BuildSoundList` at `sound.c:1368`. The faulting address
is reported at frame offset 4128, which with ASan's 32-byte left redzone is
the first element past the end of the 4096-byte `sounds` array, i.e.
`sounds[MAX_EDICTS]`.

Minimal fix: bound the frame's entity count at parse time (drop with
`Com_Error(ERR_DROP, ...)` when a frame would exceed `MAX_EDICTS`), which
also protects the other `num_entities` consumers (`cl_entities.c`,
`cl_prediction.c`, which mask the ring index but iterate the inflated count);
clamping the sound loops to `MAX_EDICTS` as well is cheap defence in depth.

### D2: server `playernum` is not range-checked on the client (medium, high confidence) [FIX12 (#108)]

`CL_ParseServerData` does `cl.playernum = MSG_ReadShort(...)` with no bound;
only the value `-1` (cinematic or pic) is special. Every consumer found
compares rather than indexes: `cl.playernum + 1` is compared with entity
numbers (`cl_entities.c`, `cl_prediction.c`, `cl_tempentities.c`, `sound.c`,
`sdl.c`), `cl_screen.c` "ctf" compares `value == cl.playernum`, and
`S_StartSound(NULL, cl.playernum + 1, ...)` stores the number, which
`CL_GetEntitySoundOrigin` and `CL_GetEntitySoundVelocity` bound against
`cl_numentities`. So it is not an out-of-bounds access today, but it is an
unvalidated network value seeding the client's notion of its own entity
across the module, which the scale rates medium. Fix: reject
`playernum < -1 || playernum >= MAX_CLIENTS` with `ERR_DROP` at the read.
Confidence that it is unchecked: high; that it is exploitable today: low.

### D3: the `"num"` layout token indexes `stats[]` with an unchecked parsed value (high, high confidence) [FIX13 (#109)]

In `SCR_ExecuteLayoutString` (`cl_screen.c`), the `"pic"`, `"stat_string"`
and `"if"` handlers validate their parsed stat index against `0..MAX_STATS`,
but the `"num"` handler does `value =
cl.frame.playerstate.stats[(int)strtol(token, NULL, 10)]` with no check. It is
the only handler that indexes `stats[]` by a parsed value (`hnum`, `anum` and
`rnum` read fixed `STAT_*` slots). The layout string is
`cl.configstrings[CS_STATUSBAR]` or `cl.layout`, both set by the server
(`svc_configstring`, `svc_layout`). A layout whose `"num"` stat argument is
outside `0..MAX_STATS - 1` reads `player_state_t.stats[]` (`short[MAX_STATS]`,
32 entries) at a server-chosen offset of up to about 2^31 elements either
side; the value is drawn on the HUD, and a far index faults. Severity high
(server-driven out-of-bounds read that can crash the client). Fix: bound the
`"num"` stat index to `0..MAX_STATS - 1` like the sibling handlers.

### D4: `DrawHUDStringScaled` fills `line[1024]` with no width bound (hardening, not reachable today) [FIX13 (#109)]

`DrawHUDStringScaled` (`cl_screen.c`) copies one line of its input into
`char line[1024]` with `while (*string && *string != '\n') line[width++] =
*string++;` and no cap on `width`, so in isolation it overflows on any line
longer than 1023 characters. Its only live callers are the `"cstring"` and
`"cstring2"` layout handlers, which pass a `COM_Parse` token. `COM_Parse`
stores at most `MAX_TOKEN_CHARS` (1024) characters and resets a token that
reaches that length to empty, so a token is at most 1023 characters and the
furthest write is the terminator at `line[1023]`. (`DrawHUDString`, the
non-scaled wrapper, has no callers; `SCR_DrawCenterString` does not use this
function.) Not reachable today; bounded by the tokenizer, not by the function.
Fix (hardening): cap `width` at `sizeof(line) - 1`.

### D5: entity `frame` indexes `bfg_lightramp[6]` unchecked (high, high confidence) [FIX14 (#110)]

In `CL_AddPacketEntities` (`cl_entities.c`), when an entity has `EF_BFG` in
`effects` and not `EF_ANIM_ALLFAST`, the light intensity is
`bfg_lightramp[s1->frame]`, a `static int[6]`. `s1->frame` comes from
`CL_ParseDelta`: a `U_FRAME8` byte (0..255) or a `U_FRAME16` signed short
(-32768..32767). Both `effects` and `frame` are server-chosen, so a crafted
entity reads up to 32767 elements past or 32768 elements before the table
(about 128 KB either way); the value becomes the `V_AddLight` intensity, and a
far index can fault. Severity high. Fix: use the table only for
`0 <= frame < 6` (or clamp) before the lookup.

The `modelindex`, `modelindex2/3/4` reads (`cl.model_draw[]` in
`CL_AddPacketEntities`, `cl.model_clip[]` in `cl_prediction.c`) and
`gunindex` (`cl.model_draw[ps->gunindex]`) were checked and are in range by
the byte type: each is read with `MSG_ReadByte` (0..255) into an array of
`MAX_MODELS` (256). They are recorded as ok in the tables, not as part of D5.

Short reads: on a truncated message every one of these fields can be `-1`
(`MSG_ReadByte`/`MSG_ReadShort` return `-1` at end of message). No path
renders such a state: the next read in `CL_ParsePacketEntities`
(`CL_ParseEntityBits`, followed by the `readcount > cursize` test) or the
`readcount > cursize` test at the top of the `CL_ParseServerMessage` loop
raises `Com_Error(ERR_DROP)` before control returns to the frame loop, the
drop disconnects (`cls.state` leaves `ca_active`, so `CL_AddPacketEntities` is
not called), and the next `svc_serverdata` runs `CL_ClearState`, which clears
`cl` and the client entities. The `-1` case therefore does not add to D5; the
negative values that matter are the ones a server sends deliberately.

### D6: negative download `offset` is not rejected on the server (high, high confidence, confirmed) [FIX15 (#111)]

`SV_BeginDownload_f` (`sv_user.c`) sets `downloadcount = offset` from
`Cmd_Argv(2)` and only clamps the upper side (`if (offset >
downloadsize)`). A negative offset argument is not rejected.
`SV_NextDownload_f` then computes `r = downloadsize - downloadcount`, caps it
at 1024 and copies `r` bytes from `download + downloadcount - r` (after adding
`r` to `downloadcount`), i.e. from `download + offset`. The server therefore
sends the client memory from before the start of the loaded file buffer, 1024
bytes per chunk, starting at the offset and continuing forward on each
`nextdl`. Any connected client can do this for any file the server allows it
to download. Severity high (out-of-bounds read of server heap memory, contents
sent to the attacker).

Confirmation (ASan harness linking the real `SV_ExecuteUserCommand` →
`SV_BeginDownload_f` → `SV_NextDownload_f`, with `FS_LoadFile` stubbed to
return a 2000-byte heap block and a download command with a small negative
offset): AddressSanitizer reports a `heap-buffer-overflow` READ of size 1024
in `SZ_Write` (`szone.c`), the source located before the 2000-byte region
returned by the stub. A larger magnitude faults instead.

Fix: reject `offset < 0` (keep the existing `> downloadsize` clamp) before
using it. This is adjacent to, but distinct from, TASK1/#8 (which fixed the
client-side `svc_download` chunk size) and FIX1/#40 (download names and the
savedir); neither touched the server-side negative offset.

### D7: `StringToFilter` in the game module overflows `num[128]` (medium, high confidence) [FIX16 (#112)]

`StringToFilter` (`src/game/g_svcmds.c`) copies the decimal digits of each
octet into `char num[128]` with `num[j++] = *s++` and no bound on `j`. Its only
callers are `SVCmd_AddIP_f` and `SVCmd_RemoveIP_f`, which pass `gi.argv(2)`:
the address argument of the `sv addip` / `sv removeip` server commands
(`ServerCommand`, reached from the engine's `sv` command). `SV_FilterPacket`,
which checks connecting clients' `ip` userinfo, does not call it. The argument
is a `COM_Parse` token of up to 1023 characters, so an argument with 128 or
more consecutive digits writes past `num` on the game module's stack.

Reachability: the `sv` command runs from the server console, a config file
the server executes, or an rcon command that passed the password check. A
client's `clc_stringcmd` goes to `SV_ExecuteUserCommand`, whose table does not
include `sv`. So the attacker is an operator or rcon holder, not an
unauthenticated peer. Such a party already runs arbitrary console commands,
but this turns that into stack memory corruption in the game module; the
scale rates operator-only memory corruption medium. Fix: bound `j` to
`sizeof(num) - 1` (reject the address if an octet is longer).

### D8: the `"pic"` layout token indexes `configstrings[]` with a negative server stat (high, high confidence) [FIX13 (#109)]

The `"pic"` handler in `SCR_ExecuteLayoutString` (`cl_screen.c`) bounds its
parsed stat index to `0..MAX_STATS - 1`, then reads `value =
cl.frame.playerstate.stats[index]` and tests only `value >= MAX_IMAGES`
before using `cl.configstrings[CS_IMAGES + value]`. `stats[]` is
`short[MAX_STATS]`, filled by `CL_ParsePlayerstate` with `MSG_ReadShort`, so
the server chooses any value in -32768..32767. With `CS_IMAGES` = 544, a
negative stat below -544 indexes `cl.configstrings` (64-byte rows) up to
about 2 MB before the array. The row is first tested for a non-empty string
and then passed as a picture name to `Draw_GetPicSize` and `Draw_PicScaled`,
which read it as a NUL-terminated string. Severity high (server-driven
out-of-bounds read that can fault). Same class as D3 (a layout handler that
checks one side of a server value). Fix: also reject `value < 0`, as the
`"stat_string"` handler already does for its stat value.

### D9: server-chosen particle colours above 255 index the software renderer's translucency table out of bounds (high, medium confidence) [CARD_R]

Trace: `CL_ParseTEnt` (`cl_tempentities.c`) reads a colour with
`MSG_ReadByte` (0..255) for `TE_LASER_SPARKS` and `TE_WELDING_SPARKS`
(passed to `CL_ParticleEffect2`, `cl_particles.c`) and for `TE_STEAM`
(`CL_ParseSteam`, passed to `CL_ParticleSteamEffect` or stored for the
sustained `CL_ParticleSteamEffect2`, `cl_effects.c`). Each of those sets
`p->color = color + (randk() & 7)`, so a particle's colour reaches 262. No
step masks it: `CL_AddParticles` passes it to `V_AddParticle`
(`cl_view.c`), which stores it in `particle_t.color` (`int`), and the
refresh module receives it in `refdef.particles`.

Sinks:

- Software renderer, `R_DrawParticle` (`sw_part.c`): for 33% particles it
  reads `vid_alphamap[color + (pdest << 8)]` and for 66% particles
  `vid_alphamap[(color << 8) + pdest]`, with no mask (the opaque case only
  writes `color` into a byte pixel, which truncates). `vid_alphamap` is
  `vid_colormap + 64 * 256` (`sw_main.c`), and `vid_colormap` is the
  `pics/colormap.pcx` image as loaded by `LoadPCX`, a heap block of exactly
  width times height bytes.
- OpenGL 1 renderer, `R_DrawParticles2` (`gl1_main.c`), the path used when
  point parameters are unavailable or stereo split is on: reads
  `colortable[p->color]` with `colortable = d_8to24table`, a static
  `unsigned[256]`, unmasked. The point-sprite path in the same file and the
  OpenGL 3 renderer mask with `& 0xFF`.

Assumption about the table extent: the stock `colormap.pcx` is 256 by 320
pixels (81920 bytes), 64 light-level rows followed by the 256 by 256
translucency table, so `vid_alphamap` ends exactly at the end of the heap
block. `LoadPCX` does not check the image size, so the code itself guarantees
no extent; with the stock file, a colour of 256..262 reads up to 1792 bytes
past the end of the block in the 66% case and up to 7 bytes in the 33% case,
and gl1 reads up to 7 entries (28 bytes) past `d_8to24table`. The values read
become pixel colours (seen only by the victim). Whether the 66% read faults
depends on where the allocator places the block (an 80 KB block can end at a
page boundary).

Hostile input: a temporary entity of one of those three types with a colour
byte above 248. Stock game code sends fixed colours below 249, so normal
play is unaffected. Severity high by the scale (server-driven heap
out-of-bounds read that can fault); confidence medium because the fault
depends on allocator placement. Fix: mask the colour with `& 0xFF` where the
renderers consume it (as gl3 and the gl1 point path already do), or at
`V_AddParticle`.

### D10: a negative entity `skinnum` passes one-sided checks into `weaponmodel[]` and the GL model skin arrays (high, high confidence) [CARD_R]

`entity_state_t.skinnum` is an `int` read by `CL_ParseDelta` as a byte, a
signed short, or (with both `U_SKIN8` and `U_SKIN16`) a full signed long, so
the server can send any negative value.

- `CL_AddPacketEntities` (`cl_entities.c`), custom weapon path
  (`modelindex2 == 255`): `i = (s1->skinnum >> 8)` is tested only with
  `i > MAX_CLIENTWEAPONMODELS - 1` before `ent.model = ci->weaponmodel[i]`
  (`ci` is `&cl.clientinfo[s1->skinnum & 0xff]`, which is in range). A
  negative `skinnum` gives a negative `i`, which reads a pointer up to about
  64 MB before the array; it is handed to the renderer as a model and
  dereferenced there.
- Normal entity path (`modelindex != 255`, not a beam): `ent.skinnum =
  s1->skinnum`. The OpenGL 1 and 3 alias-model code (`gl1_mesh.c`,
  `gl3_mesh.c`) select `currentmodel->skins[currententity->skinnum]` after
  testing only `skinnum >= MAX_MD2SKINS`; a negative value reads an image
  pointer before the `skins[MAX_MD2SKINS]` array inside the model, and the
  renderer then uses it as an image. The software renderer (`sw_alias.c`)
  tests both `skinnum >= num_skins` and `skinnum < 0`.

Severity high (server-driven out-of-bounds pointer read, then a wild pointer
dereference: crash at least). Fix: reject or zero a negative `i` in the
custom-weapon path and add the `< 0` test to the two GL skin selections, as
`sw_alias.c` already has.

### D11: a negative brush-entity `frame` makes `R_TextureAnimation` walk the texture chain about 2^32 times (high, high confidence) [CARD_R]

On the normal path of `CL_AddPacketEntities` (no `EF_ANIM*` effect) the
entity's `ent.frame` is `s1->frame`, a server-chosen value that can be
negative (`U_FRAME16` signed short). For a brush model (an inline `*N` model
from the server's own map) the OpenGL 1 and 3 surface code calls
`R_TextureAnimation` (`src/client/refresh/files/surf.c`) for every surface
with an animated texture: `c = currententity->frame % tex->numframes; while
(c && tex) { tex = tex->next; c--; }`. In C the remainder of a negative frame
is zero or negative, so `c` starts below zero and the loop decrements it away
from zero: on the circular animation chain built by `Mod_LoadTexinfo` it runs
until `c` wraps (signed overflow, undefined behaviour; in practice about 2^32
iterations per surface per rendered frame), freezing the client; on a chain
that ends in `NULL` it stops with `tex == NULL` and then dereferences it.
The alias-model paths are not affected: they test `frame` and `oldframe`
against `0..num_frames - 1` on both sides. Severity high (remote hang or
crash). Same class as D10 (a server entity field reaching a renderer with a
one-sided assumption). Fix: treat a negative frame as 0, either in
`R_TextureAnimation` or where `ent.frame` is set.

### D12: a truncated `clc_move` makes the command checksum read past `net_message_buffer` (low, high confidence) [CARD_S]

In `SV_ExecuteClientMessage` (`sv_user.c`) the `clc_move` case records
`checksumIndex = net_message.readcount`, reads the checksum byte, a long and
three `MSG_ReadDeltaUsercmd`, then (for a client in `cs_spawned`) computes
`COM_BlockSequenceCRCByte(net_message.data + checksumIndex + 1,
net_message.readcount - checksumIndex - 1, ...)` before the loop's
`readcount > cursize` test runs. On a short read `MSG_ReadByte` returns `-1`,
so each delta usercmd's bit mask has every field bit set and advances
`readcount` by its maximum of 17 bytes; the long adds 4. If the `clc_move`
byte is the last byte of the message, the CRC input starts one byte past the
message end and is 55 bytes long (below the 60-byte clamp in
`COM_BlockSequenceCRCByte`), so `memcpy` reads up to
`net_message.data + cursize + 55`. With a datagram of the largest accepted
size (1399 bytes, `NET_GetPacket` drops one of `MAX_MSGLEN`) that is up to 55
bytes past the 1400-byte static `net_message_buffer` (`netchan.c`). The
result is only compared with the checksum byte, which is `-1` after a short
read and never matches, so the packet's moves are discarded and nothing read
is returned. Reachable by any spawned client. Severity low (read of adjacent
static memory, result only compared). Fix: test `net_message.readcount >
net_message.cursize` (and drop) before computing the checksum.

### D13: `SVC_RemoteCommand` prints the raw packet with `%s` before the password check, and nothing NUL-terminates it (low, high confidence) [CARD_S]

`SVC_RemoteCommand` (`sv_conless.c`) logs every rcon attempt with
`Com_Printf("...%s\n", ..., net_message.data + 4)`, whether or not
`Rcon_Validate` accepts the password. `net_message.data` is the static
1400-byte `net_message_buffer`; `NET_GetPacket` (`backends/unix/network.c`)
writes the datagram with `recvfrom` and sets `cursize`, and nothing writes a
terminator after it (the tokenizer works on the copy that
`MSG_ReadStringLine` made, not on the buffer). The bytes after `cursize` are
left over from earlier datagrams; an oversize datagram, although dropped,
still fills the whole buffer. So an unauthenticated peer can send an rcon
datagram without a NUL after one that filled the buffer, and the print runs
past the end of `net_message_buffer` into the static memory that follows
until it meets a zero byte. The output goes to the server console and log,
not to the peer (the redirect to the sender starts after the print).
Severity low (out-of-bounds read of adjacent static memory, logged locally).
This replaces the earlier hardening note H5, which said both that the buffer
was NUL-terminated and that the print relied on it. Fix: print at most
`cursize - 4` bytes (`%.*s`), or terminate the received data in
`NET_GetPacket` (receive at most `maxsize - 1` bytes and store a NUL at
`cursize`).

## Hardening (low severity, optional)

- H1 `COM_Parse`: on a token of exactly `MAX_TOKEN_CHARS` it resets `len` to 0,
  silently emptying an over-long token rather than truncating; the guard
  `len < MAX_TOKEN_CHARS` means the terminator write is in range, so no
  overflow — behaviour wart only.
- H2 entity `modelindex*`: safe by the byte type today; an assert/bound would
  harden against a future widening of the field.
- H3 `S_StartSound`/`S_PickChannel`: `ent` from `channel>>3` can be large;
  `S_PickChannel` only errors on `entchannel < 0`, and `ent` is used as a sound
  origin lookup with `cl_numentities` bounds at the sinks — no OOB found, but
  an explicit upper bound on `ent` would be clearer.
- H4 `CL_AddMuzzleFlash2`: `flash_number > 210` is rejected though the table has
  216 entries (211..215 are valid); harmless, but the constant should match the
  table size.
- H5: withdrawn. The `SVC_RemoteCommand` print is a defect (D13); the
  `SV_ReadPackets` runt read stays inside the buffer and is recorded as ok in
  the server table.
- H6 `SVC_Info`: the wrong-version `Com_sprintf` passes an extra `sizeof`
  argument that the format ignores; cosmetic, no overflow.

## Cards filed

One card per defect class, filed as locked backlog issues (epic trust-boundary, depends on TASK17).

---

**Card:** FIX11 (#107)
**Title:** Bound client frame `num_entities` to `MAX_EDICTS`; fix the sound-list loops
**Severity:** high
**Sites:** `cl_parse.c` `CL_ParsePacketEntities`/`CL_DeltaEntity`;
`sound/sound.c` `S_BuildSoundList`; `sound/sdl.c` `SDL_AddLoopSounds`.
**Body:** Goal: a server cannot make `cl.frame.num_entities` exceed `MAX_EDICTS`
and overflow the `int sounds[MAX_EDICTS]` stack arrays. Scope: cap
`num_entities` at parse time (drop with `Com_Error(ERR_DROP)` if a frame would
exceed `MAX_EDICTS`), or clamp the two sound loops to `MAX_EDICTS`; prefer the
parse-time cap since other `num_entities` consumers trust it. Acceptance: the
fix plus a standalone harness (pattern in docs/audits, D1) that drives
`S_BuildSoundList` with `num_entities > MAX_EDICTS` and shows no ASan
stack-buffer-overflow; stock play unchanged. Model: sonnet.

---

**Card:** FIX12 (#108)
**Title:** Range-check server-supplied `playernum` on the client
**Severity:** medium
**Sites:** `cl_parse.c` `CL_ParseServerData`.
**Body:** Goal: reject `playernum < -1 || playernum >= MAX_CLIENTS` at the read
with `Com_Error(ERR_DROP)`. Scope: one bound check in `CL_ParseServerData`.
Acceptance: fix plus a trace/comment showing the bound; `-1` cinematic path and
normal joins still work. Regression: hard to automate without the engine
running — document the trace. Model: haiku.

---

**Card:** FIX13 (#109)
**Title:** Bound the `"num"` stat index and the `line[]` width in the HUD layout interpreter
**Severity:** high
**Sites:** `cl_screen.c` `SCR_ExecuteLayoutString` (`"num"` handler),
`DrawHUDStringScaled`.
**Body:** Goal: a server-supplied layout/configstring cannot read `stats[]` out
of bounds or overflow `line[1024]`. Scope: bound the `"num"` stat index to
`0..MAX_STATS` like the sibling handlers (D3); cap `width` to `sizeof(line)-1`
in `DrawHUDStringScaled` (D4). Acceptance: fix plus a trace showing a
`num <w> 999999` token and an over-long cstring are rejected/truncated; normal
HUDs render unchanged. Model: sonnet.

---

**Card:** FIX14 (#110)
**Title:** Bound entity `frame` before indexing `bfg_lightramp[]`
**Severity:** high
**Sites:** `cl_entities.c` `CL_AddPacketEntities` (`bfg_lightramp[s1->frame]`).
**Body:** Goal: a crafted entity `frame` cannot index the 6-element
`bfg_lightramp` out of bounds. Scope: clamp/validate `s1->frame` (0..5) before
the lookup; record that `modelindex*` indices are safe by the byte type.
Acceptance: fix plus a trace; BFG visuals unchanged. Model: haiku.

---

**Card:** FIX15 (#111)
**Title:** Reject a negative download offset on the server
**Severity:** high
**Sites:** `sv_user.c` `SV_BeginDownload_f` / `SV_NextDownload_f`.
**Body:** Goal: a client cannot make the server `SZ_Write` from before the
loaded file. Scope: reject `offset < 0` (keep the `> downloadsize` clamp)
before assigning `downloadcount` (D6). Acceptance: fix plus the ASan harness in
docs/audits (D6) showing `download maps/x.bsp -64` no longer reads out of
bounds; valid resume offsets still work. Model: sonnet.

---

**Card:** FIX16 (#112)
**Title:** Bound the octet buffer in the game-module IP filter
**Severity:** low (game module)
**Sites:** `src/game/g_svcmds.c` `StringToFilter`.
**Body:** Goal: `num[128]` cannot overflow regardless of input length. Scope:
bound `j` to `sizeof(num)-1` in `StringToFilter` (D7); note it is currently
unreachable because `MAX_INFO_VALUE` caps the `ip` value. Acceptance: fix plus
a trace; `sv addip`/filter commands still parse. Touches the frozen game API
file only internally (no struct change). Model: haiku.
