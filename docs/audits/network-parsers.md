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
| `cl_network.c` `CL_ConnectionlessPacket` `Cmd_Argv(1)` | tokenized | `strtol` → `cls.challenge`; echoed | numeric / `Netchan_OutOfBandPrint` with fixed format | ok |
| `cl_network.c` `cmd` connectionless | `MSG_ReadString` | `Cbuf_AddText` | gated on `NET_IsLocalAddress` | ok (by design for local) |
| `cl_main.c` `CL_ParseStatusMessage` → `M_AddToServerList` | `MSG_ReadString` | `Q_strlcpy` into `local_server_names[]` | `m_num_servers == MAX_LOCAL_SERVERS` guard; `Q_strlcpy` truncates | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` stat indices | `COM_Parse`+`strtol` of a configstring | `stats[index]`, `configstrings[CS_IMAGES+value]`, `clientinfo[value]` | each checked (`index` vs `MAX_STATS`, `value` vs `MAX_IMAGES`/`MAX_CLIENTS`/`MAX_CONFIGSTRINGS`) | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"num"` → `stats[strtol(token)]` | layout number | `stats[...]` index | **unchecked** index into `stats[MAX_STATS]` → Defect D3 | defect (D3) |
| `cl_screen.c` `DrawHUDStringScaled` `line[width++]` | configstring/token chars to `\n` | index into `line[1024]` | **no bound** on `width` → Defect D4 | defect (D4) |
| `cl_entities.c` `CL_AddPacketEntities` `cl.model_draw[s1->modelindex]` | modelindex byte | model pointer | `s1->number` checked `0..cl_numentities`; **modelindex/2/3/4 not range-checked** before `model_draw[...]` → Defect D5 | defect (D5) |
| `cl_entities.c` `ci = &cl.clientinfo[s1->skinnum & 0xff]` | skinnum | clientinfo index | masked `& 0xff`, `MAX_CLIENTS == 256` | ok |
| `cl_entities.c` `bfg_lightramp[s1->frame]` | frame | index into `[6]` | reached only under `RF_FRAMELERP`/modelindex2==255 path; **`frame` not bounded to 6** → Defect D5 (same class) | defect (D5) |
| `cl_prediction.c` `cl.model_clip[ent->modelindex]` | modelindex | clip model | guarded by `ent->solid==31`; **modelindex unchecked** → Defect D5 | defect (D5) |
| `sound/sound.c` `S_BuildSoundList` `sounds[i]` | loop to `num_entities` | write into caller `sounds[MAX_EDICTS]` | bound is `i <= MAX_CL_ENTNUM` (32767), **not `MAX_EDICTS` (1024)** → Defect D1 | defect (D1) |
| `sound/sdl.c` `SDL_AddLoopSounds` `sounds[i]` / loop | `i < num_entities` | read/zero `sounds[MAX_EDICTS]` | **no bound vs `MAX_EDICTS`** → Defect D1 | defect (D1) |
| `sound/sound.c` `cl.sound_precache[sounds[i]]` | sound id from `ent->sound` (byte) | precache index | `ent->sound` is a byte, `MAX_SOUNDS == 256` | ok |

## Server (reads `clc_*` from a client, plus connectionless and userinfo)

| Site (file / function / expr) | Reads | Used as | Check | Verdict |
|---|---|---|---|---|
| `sv_main.c` `SV_ReadPackets` `*(int*)net_message.data == -1` | first 4 bytes | connectionless test | `NET_GetPacket` guarantees `cursize >= 1`; read of 4 bytes of a `MAX_MSGLEN` buffer — Hardening H5 (runt connectionless) | hardening |
| `sv_main.c` `SV_ReadPackets` qport | `MSG_ReadShort & 0xffff` | client match | value only compared | ok |
| `sv_user.c` `SV_ExecuteClientMessage` `c` | `MSG_ReadByte` | `switch`; `default` drops | `readcount > cursize` tested each loop; `c == -1` ends | ok |
| `sv_user.c` clc_userinfo `MSG_ReadString` | string | `Q_strlcpy` into `cl->userinfo[MAX_INFO_STRING]` then `SV_UserinfoChanged` | truncates; `SV_UserinfoChanged` masks name high bit, clamps rate | ok |
| `sv_user.c` clc_move `checksum` | `MSG_ReadByte` | compared to `COM_BlockSequenceCRCByte(... readcount-checksumIndex-1 ...)` | CRC length clamped to 60 inside; mismatch returns | ok |
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
| `sv_conless.c` `SVC_RemoteCommand` `net_message.data + 4` | raw packet tail | `Com_Printf("%s", ...)` of unvalidated bytes | printed as `%s`; relies on `MSG_ReadStringLine` having been tokenized first; data is NUL-terminated within `MAX_MSGLEN` buffer — Hardening H5 | hardening |
| `sv_conless.c` `SVC_RemoteCommand` args | `Cmd_Argv(i)` | `Q_strlcat` into `remaining[1024]`; `Cmd_ExecuteString` | `Q_strlcat` bounded; gated on `Rcon_Validate` password | ok (rcon by design) |
| `sv_conless.c` `SV_ConnectionlessPacket` `s` | `MSG_ReadStringLine` | `Cmd_TokenizeString` | string bounded 2048; tokenizer bounded | ok |
| `sv_main.c` `SV_UserinfoChanged` `Info_ValueForKey(userinfo,"rate")` | userinfo value | `strtol` → clamped 100..15000 | clamped | ok |
| game `g_svcmds.c` `SV_FilterPacket`/`StringToFilter` `num[128]`,`b[4]` | userinfo "ip" octets | `num[j++]` | **`j` unbounded** while digits — Defect D7 (game module, not engine core) | defect (D7) |
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

Severity: critical = remote memory corruption or file write; high = remote
crash or OOB read of memory; medium = resource exhaustion; low = hardening.

### D1 — `num_entities` can exceed `MAX_EDICTS`; sound loops write/read past `sounds[MAX_EDICTS]` (high, high confidence)

`CL_ParsePacketEntities`/`CL_DeltaEntity` increment `frame->num_entities` once
per entity in a frame and never cap it to `MAX_EDICTS` (1024). The underlying
store `cl_parse_entities` is a 1024-entry ring accessed with a mask, so a
frame whose entity count exceeds 1024 is counted but aliases in the ring.
`S_BuildSoundList` (`sound.c`) then loops `for (i = 0; i < num_entities &&
i <= MAX_CL_ENTNUM; i++) sounds[i] = ...` — the guard is `MAX_CL_ENTNUM`
(32767), not the size of the caller's array. Both callers
(`SDL_AddLoopSounds`, and the duplicate loop in it) declare
`int sounds[MAX_EDICTS]`. When `num_entities > 1024` the write `sounds[i]`
runs off the end of a 4 KB stack array.

Hostile input: a server that, over the delta chain, accumulates a frame with
more than 1024 live entities (new high-numbered entities each packet while the
previously-sent low-numbered entities are auto-carried without consuming
bytes). The written values are `ent->sound` (0..255) — partially controlled —
placed at attacker-influenced stack offsets: stack memory corruption on the
client.

Confirmation (ASan harness linking the real `S_BuildSoundList` with
`num_entities = 1380`): AddressSanitizer reports a `stack-buffer-overflow`
WRITE of size 4 in `S_BuildSoundList` at `sound.c:1368`, the access landing at
offset 4128 of the 4096-byte `sounds` array — i.e. the first element past
`sounds[MAX_EDICTS]`. The write is the real loop writing `sounds[i]`.

Reachability requires sustaining >1024 carried entities across frames, so I
rate it high rather than critical; the OOB write itself is certain. Minimal
fix: cap `num_entities` in `CL_ParsePacketEntities` (drop with `ERR_DROP` if it
would exceed `MAX_EDICTS`, or clamp the sound loops to `MAX_EDICTS`). The
cleanest is to bound the frame's entity count at parse time, which also
protects the other `num_entities` consumers (`cl_entities.c`,
`cl_prediction.c`, which happen to mask but iterate the inflated count).

### D2 — server `playernum` is not range-checked on the client (medium/high, high confidence)

`CL_ParseServerData` does `cl.playernum = MSG_ReadShort(...)` with no bound
except the special value `-1` (cinematic/pic). Elsewhere the client uses
`cl.playernum + 1` as an entity number and, in `cl_screen.c` "ctf", compares
`value == cl.playernum`; `sound.c`/`sdl.c` use `plnum = cl.playernum + 1` only
in equality tests. No direct unmasked array index by `playernum` was found
(the dangerous consumers compare rather than index), so this is not currently
an OOB on its own, but it is an unvalidated network value seeding entity
identity and is exactly the "unchecked playernum" the card's rationale cites.
Fix: reject `playernum < -1 || playernum >= MAX_CLIENTS` with `ERR_DROP` at the
read. Low cost, closes the class. Confidence that it is unchecked: high;
confidence it is independently exploitable today: low — hence medium.

### D3 — `"num"`/`"hnum"`/`"anum"`/`"rnum"` layout tokens index `stats[]` unchecked (high, high confidence)

In `SCR_ExecuteLayoutString` (`cl_screen.c`), the `"pic"`, `"stat_string"`,
`"if"` handlers all validate `index` against `MAX_STATS`, but the `"num"`
handler does `value = cl.frame.playerstate.stats[(int)strtol(token, NULL, 10)]`
with no check, and the `hnum`/`anum`/`rnum`/`cnum` variants read a hardcoded
stat so are safe. The layout string is `cl.configstrings[CS_STATUSBAR]` /
`cl.layout`, both set from the server (`svc_layout`, `svc_configstring`). A
server sends `num <width> 999999`, producing an out-of-bounds read of
`player_state_t.stats[]` (`int[MAX_STATS]`) at an attacker-chosen offset; the
value is then rendered as a number, so it is an information read, and a large
negative index can fault. Severity high (remote OOB read / crash). Fix: bound
the `"num"` stat index to `0..MAX_STATS` like the sibling handlers.

### D4 — `DrawHUDStringScaled` writes `line[1024]` with no width bound (high, high confidence)

`DrawHUDStringScaled` (`cl_screen.c`) copies a line of the input string into
`char line[1024]` with `while (*string && *string != '\n') line[width++] =
*string++;` and no cap on `width`. It is called from the `cstring`/`cstring2`
layout tokens with a `COM_Parse` token (bounded to `MAX_TOKEN_CHARS` 1024, so
that path is at the edge) and, more dangerously, `SCR_DrawCenterString` /
other callers pass `scr_centerstring` and configstrings. A server-supplied
layout/centerprint line longer than 1024 bytes with no embedded `\n`
overflows `line` on the stack. `COM_Parse` tokens are capped near 1024 so the
cstring path is a 1-byte edge; a direct caller with a longer string is a clear
overflow. Fix: bound `width` to `sizeof(line)-1`. Severity high (stack OOB
write of attacker bytes).

### D5 — entity `modelindex`/`modelindex2-4` and `frame` used as array indices without range checks (high, high confidence)

`CL_AddPacketEntities` (`cl_entities.c`) indexes `cl.model_draw[s1->modelindex]`
(and `modelindex2/3/4`) and `cl_prediction.c` indexes
`cl.model_clip[ent->modelindex]`. `modelindex*` are bytes (0..255) and
`model_draw`/`model_clip` are `[MAX_MODELS]` (256), so those specific indices
are in range by type — *ok by construction*. However `bfg_lightramp[s1->frame]`
indexes a 6-element array with `s1->frame`, which `CL_ParseDelta` reads as a
`U_FRAME16` short (0..65535); the BFG path is entered via `modelindex2 == 255`
and `effects`/`renderfx` bits the server controls, so a crafted entity with a
large `frame` reads far past `bfg_lightramp[6]`. Severity high (OOB read, value
fed to `V_AddLight` intensity). Fix: bound `s1->frame` before indexing
`bfg_lightramp`. (The `modelindex*` cases are listed here only to record they
were checked and are safe by the byte type.) Confidence high for the
`bfg_lightramp` read.

### D6 — negative download `offset` not clamped on the server (high, high confidence — confirmed)

`SV_BeginDownload_f` (`sv_user.c`) sets `downloadcount = offset` from
`Cmd_Argv(2)` and only clamps the upper side (`if (offset > downloadsize)`).
A negative `offset` passes. `SV_NextDownload_f` then computes
`r = downloadsize - downloadcount` (so `r > downloadsize`, capped to 1024) and
`SZ_Write(..., download + downloadcount - r, r)` — i.e. `memcpy` from
`download + offset` for `r` bytes, reading up to 1024 bytes *before* the start
of the loaded file. A malicious client sends `download maps/x.bsp -64` (any
map that exists on the server). The server reads out of bounds and copies those
bytes into the download message sent back to the client — an OOB read of server
heap memory disclosed to the attacker.

Confirmation (ASan harness linking the real `SV_ExecuteUserCommand` →
`SV_BeginDownload_f` → `SV_NextDownload_f` with `FS_LoadFile` returning a
2000-byte heap block and the command `download maps/x.bsp -64`):
AddressSanitizer reports a `heap-buffer-overflow` READ of size 1024 in
`SZ_Write` (`szone.c`), the source located 64 bytes before the 2000-byte
region returned by the stub `FS_LoadFile` — exactly `download + (-64)`. A
larger magnitude (`-4096`) faults with a `BUS`/segfault instead. Severity high
(remote OOB read of server memory, contents sent to the attacker). Fix: reject
`offset < 0` (and keep the existing `> downloadsize` clamp) before using it.
This is adjacent to, but distinct from, TASK1/#8 (which fixed the *client* side
`svc_download` size) and FIX1/#40 (download header / savedir): neither touched
the server-side negative offset.

### D7 — `StringToFilter` in the game module overflows `num[128]` (medium, medium confidence — game module)

`StringToFilter` (`src/game/g_svcmds.c`, also reached from `SV_FilterPacket`)
copies decimal digits of an IP octet into `char num[128]` with `num[j++]` and
no bound on `j`. The input is the userinfo `"ip"` key. The engine forces
`ip` to `NET_AdrToString(net_from)` in `SVC_DirectConnect` *before*
`ClientConnect`, so the normal path is safe; but `ClientConnect` reads
`Info_ValueForKey(userinfo, "ip")` and the value is bounded to
`MAX_INFO_VALUE` (64) < 128, so `j` cannot reach 128 through the info string.
This is therefore not reachable today (ok-by-construction via `MAX_INFO_VALUE`),
but the loop is unbounded in isolation and lives in the game module (frozen API,
separate repo for addons). Recorded as a hardening-grade defect; confidence it
is exploitable today: low. Listed so the class is not silently dropped.

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
- H5 connectionless runt: `SV_ReadPackets` reads `*(int*)net_message.data`
  after `NET_GetPacket` guarantees only `cursize >= 1`; a 1–3 byte datagram
  reads up to 3 bytes of the fixed `MAX_MSGLEN` buffer (initialised, no OOB) —
  benign but reads uninitialised tail. `SVC_RemoteCommand` prints
  `net_message.data + 4` as `%s` relying on NUL-termination within the buffer.
- H6 `SVC_Info`: the wrong-version `Com_sprintf` passes an extra `sizeof`
  argument that the format ignores; cosmetic, no overflow.

## Cards to file

One card per defect class. The orchestrator files these and fills in ids.

---

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

**Title:** Range-check server-supplied `playernum` on the client
**Severity:** medium
**Sites:** `cl_parse.c` `CL_ParseServerData`.
**Body:** Goal: reject `playernum < -1 || playernum >= MAX_CLIENTS` at the read
with `Com_Error(ERR_DROP)`. Scope: one bound check in `CL_ParseServerData`.
Acceptance: fix plus a trace/comment showing the bound; `-1` cinematic path and
normal joins still work. Regression: hard to automate without the engine
running — document the trace. Model: haiku.

---

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

**Title:** Bound entity `frame` before indexing `bfg_lightramp[]`
**Severity:** high
**Sites:** `cl_entities.c` `CL_AddPacketEntities` (`bfg_lightramp[s1->frame]`).
**Body:** Goal: a crafted entity `frame` cannot index the 6-element
`bfg_lightramp` out of bounds. Scope: clamp/validate `s1->frame` (0..5) before
the lookup; record that `modelindex*` indices are safe by the byte type.
Acceptance: fix plus a trace; BFG visuals unchanged. Model: haiku.

---

**Title:** Reject a negative download offset on the server
**Severity:** high
**Sites:** `sv_user.c` `SV_BeginDownload_f` / `SV_NextDownload_f`.
**Body:** Goal: a client cannot make the server `SZ_Write` from before the
loaded file. Scope: reject `offset < 0` (keep the `> downloadsize` clamp)
before assigning `downloadcount` (D6). Acceptance: fix plus the ASan harness in
docs/audits (D6) showing `download maps/x.bsp -64` no longer reads out of
bounds; valid resume offsets still work. Model: sonnet.

---

**Title:** Bound the octet buffer in the game-module IP filter
**Severity:** low (game module)
**Sites:** `src/game/g_svcmds.c` `StringToFilter`.
**Body:** Goal: `num[128]` cannot overflow regardless of input length. Scope:
bound `j` to `sizeof(num)-1` in `StringToFilter` (D7); note it is currently
unreachable because `MAX_INFO_VALUE` caps the `ip` value. Acceptance: fix plus
a trace; `sv addip`/filter commands still parse. Touches the frozen game API
file only internally (no struct change). Model: haiku.
