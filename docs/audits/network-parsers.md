# Audit of the network message parsers (TASK17)

Static, hostile-peer audit of the code that reads bytes off the wire: the
client-side server-message parser, the server-side client-message parser, the
connectionless (out-of-band) handlers on both sides, and the shared message,
netchannel and sizebuf code. The threat model is CLAUDE.md "Security-relevant
boundaries": a malicious server sending a connected client arbitrary
`svc_*` data and connectionless replies, and a malicious client sending a
server `clc_*` data, connectionless requests and userinfo.

Base: `main` at 727a6280. Earlier trust-boundary fixes are referenced where a site is already fixed or already
carded: TASK1 (#8, download chunk / savedir), TASK2 (#9, areabits and asset
loaders), FIX1 (#40), FIX3 (#44), FIX8 (#56), FIX10 (#73), FIX4/#46 (challenge
entropy).

## Method and coverage

Enumeration was by grep for every `MSG_Read*`, `Cmd_Argv`, `Info_ValueForKey`,
`strcpy`/`strcat`/`sprintf`/`memcpy` on peer data, and array indexing by a
parsed value, across the files in scope; then each function that reads the
network was read in full, and each value was followed to the arrays and
renderer calls it reaches. Read in full: `src/common/movemsg.c`,
`src/common/netchan.c`, `src/common/szone.c`, `src/common/crc.c`
(`COM_BlockSequenceCRCByte`), `src/common/cmdparser.c` (`Cmd_TokenizeString`,
`Cmd_Argv`), the Info_* and `COM_Parse` functions in
`src/common/shared/shared.c`, `src/client/cl_parse.c`,
`src/client/cl_tempentities.c`, `src/client/cl_effects.c`
(`CL_AddMuzzleFlash`, `CL_AddMuzzleFlash2`, the particle effects fed by
temporary entities), `src/client/cl_particles.c`, `src/client/cl_inventory.c`,
`src/client/cl_download.c`, `src/client/cl_network.c`
(`CL_ConnectionlessPacket`, `CL_ReadPackets` and the console commands that
build packets), `src/client/cl_main.c` (`CL_ParseStatusMessage`,
`CL_AllocEntity`, `CL_ClearState`), `src/client/cl_lights.c`
(`CL_SetLightstyle`), `src/client/cl_screen.c` (`SCR_ExecuteLayoutString`,
`DrawHUDStringScaled`, `SCR_CenterPrint`, `SCR_DrawCenterString`), the sound
frontend sinks (`S_BuildSoundList`, `SDL_AddLoopSounds`, `S_PickChannel`,
`CL_GetEntitySoundOrigin`), `src/client/cl_entities.c`,
`src/client/cl_prediction.c`, `src/server/sv_user.c`,
`src/server/sv_conless.c`, `src/server/sv_main.c` (`SV_ReadPackets`,
`SV_UserinfoChanged`, `SV_CalcPings`, `SV_StatusString`), and the backend
packet receiver in `src/backends/unix/network.c`. The game-module consumers
(`ClientUserinfoChanged`, `ClientConnect`, `SV_FilterPacket`,
`StringToFilter`) were read because the server hands them peer userinfo or
command arguments.

The renderers are not fed only from bounded client arrays: entity `skinnum`
and `frame` and particle colours pass from the server through
`CL_AddPacketEntities` and `V_AddParticle` into `refdef` unchanged. The
renderer code that indexes by those fields was therefore read
(`gl1_mesh.c`, `gl3_mesh.c`, `sw_alias.c` skin and frame selection,
`files/surf.c` `R_TextureAnimation`, the sprite functions in `gl1_main.c`,
`gl3_main.c` and `sw_sprite.c`, `sw_part.c`, the gl1 and gl3 particle
paths); D9, D10, D11 and D16 come from there.

Sweep, at the audit base. `git grep -n 'MSG_Read[A-Za-z0-9]* *(' -- src`
returns 271 hits: 14 are prototypes in `src/common/header/common.h` and 35
are the definitions and internal calls in `src/common/movemsg.c` (covered by
the shared-code rows); the remaining 222 are in the scope files
(`cl_parse.c` 98, `cl_tempentities.c` 96, `sv_user.c` 8, `cl_effects.c` 4,
`cl_network.c` 4, `netchan.c` 3, `sv_main.c` 3, `cl_download.c` 2,
`sv_conless.c` 2, `cl_inventory.c` 1, `cl_main.c` 1). `git grep -n
'Cmd_Argv *(' -- src/server src/client/cl_network.c src/client/cl_parse.c
src/client/cl_download.c` returns 50 hits: 27 in the scope files
(`cl_network.c` 9, `sv_user.c` 9, `sv_conless.c` 8, `cl_download.c` 1) and 23
in `sv_cmd.c` (15) and `sv_save.c` (8). Every one of the 249 in-scope hits is
covered: each sits in a function that has a row below naming the value it
reads.

Deliberately out of scope: `sv_cmd.c` and `sv_save.c` (server console
commands such as `map`, `gamemap`, `savegame`, `loadgame`, `serverrecord`;
their input is the operator or an rcon holder, the by-design rcon trust, and
the savedir side is TASK1/#8); demo-file reading (local files); the asset
loaders behind `R_RegisterModel`, `R_RegisterSkin`, `Draw_FindPic`,
`SCR_PlayCinematic` and `CM_InlineModel` (TASK2/#9 and the asset-loader
audit); the Windows backend receiver, which mirrors the Unix one.

Two defects (D1, D6) were confirmed with standalone ASan harnesses that link
the real code against stubs; their results are quoted in the Defects section.
The harnesses are not committed.

`MSG_Read*` short-read behaviour (relied on throughout): at end of message
`MSG_ReadByte`/`ReadShort`/`ReadLong`/`ReadChar` return `-1`, `MSG_ReadFloat`
returns `-1.0`, `MSG_ReadString`/`ReadStringLine` stop and NUL-terminate.
`readcount` is still advanced, so after an over-read `readcount > cursize` and
the top-level parse loops (`CL_ParseServerMessage`, `SV_ExecuteClientMessage`,
`CL_ParsePacketEntities`) detect it and drop. The `-1` return is the only
value an attacker can force by truncating; where a `-1` is used as a count,
index or size without a check, or where work is done on the over-read data
before the loop's test (D12), that is the bug to look for.

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
| `shared.c` `COM_Parse` `com_token[len]` | token chars | index into `com_token[MAX_TOKEN_CHARS]` | `len < MAX_TOKEN_CHARS` guards each write; `len == MAX_TOKEN_CHARS` reset to 0 — see Hardening H1 (over-long token emptied, not truncated) | ok (hardening H1) |
| `shared.c` `Info_ValueForKey` | key/value scan | `Q_strlcpy` into `value[2][MAX_INFO_VALUE]` | `vlen` clamped to `MAX_INFO_VALUE`; `Q_strlcpy` truncates; in bounds. Returns the **first** pair with the key, which the engine-set `ip` key relies on → Defect D17 | defect (D17) |
| `shared.c` `Info_SetValueForKey` / `Info_RemoveKey` | key/value | builds into `s` | `Info_ValidateKeyValue` bounds key/value < MAX_INFO_KEY/VALUE; `Q_strlcpy_ascii` with `MAX_INFO_STRING - slen` and overflow check; in bounds. `Info_RemoveKey` removes only the **first** matching pair, the new pair is appended at the end, and a pair that does not fit is dropped ("Info string length exceeded"), so setting a key does not make it unique or present → Defect D17 | defect (D17) |
| `backends/unix/network.c` `NET_GetPacket` `recvfrom(..., maxsize, ...)` | datagram | `cursize = ret` | `ret == maxsize` → "Oversize packet" dropped; buffer is `MAX_MSGLEN` | ok |
| `backends/unix/network.c` `NET_GetLoopPacket` `memcpy(data, loop->msgs[i].data, datalen)` | loopback | `cursize` | local loopback, length set by `NET_SendLoopPacket` from engine | ok |

## Client (reads `svc_*` from a server, plus connectionless)

| Site (file / function / expr) | Reads | Used as | Check | Verdict |
|---|---|---|---|---|
| `cl_parse.c` `CL_ParseServerMessage` `cmd` | `MSG_ReadByte` | `switch`; `default` drops | `readcount > cursize` tested each loop; `cmd == -1` ends | ok |
| `cl_network.c` `CL_ReadPackets` `*(int *)net_message.data == -1` | first 4 bytes | connectionless test | client twin of the server runt read: `cursize` can be 0, the 4 bytes are always inside the 1400-byte buffer (stale for a datagram under 4 bytes); sequenced packets need `cursize >= 8` and the server's address before `Netchan_Process` | ok (in bounds) |
| `cl_parse.c` `CL_ParseServerData` protocol version | `MSG_ReadLong` | `cls.serverProtocol`; compared | `!= PROTOCOL_VERSION` → `ERR_DROP`, except with a local server (demo hack); the value only selects the protocol-26 frame layout | ok |
| `cl_parse.c` `CL_ParseServerData` `servercount`, `attractloop` | `MSG_ReadLong`, `MSG_ReadByte` | stored, echoed in commands, tested as a flag | not indices | ok |
| `cl_parse.c` `CL_ParseServerData` level name with `playernum == -1` | `MSG_ReadString` | `SCR_PlayCinematic(str)` | string bounded 2048; the cinematic and picture loaders are asset loaders (TASK2 scope) | ok (loader out of scope) |
| `cl_parse.c` `CL_ParseBaseline` `newnum` | `CL_ParseEntityBits` (byte or signed short) | `CL_AllocEntity(newnum)` → `ent->baseline` | `CL_AllocEntity` returns `NULL` outside `0..MAX_CL_ENTNUM`; `CL_ParseDelta` then writes a local dummy | ok |
| `cl_parse.c` `CL_ParseEntityBits` `number` | byte or short | returned as entity number | caller `CL_ParsePacketEntities` checks `newnum > MAX_CL_ENTNUM` → drop | ok |
| `cl_parse.c` `CL_ParsePacketEntities` ring | `oldframe->parse_entities + i` | index `cl_parse_entities[... & (MAX_PARSE_ENTITIES-1)]` | masked | ok |
| `cl_parse.c` `CL_DeltaEntity` `cl_parse_entities[cl.parse_entities & (MAX_PARSE_ENTITIES-1)]` | — | ring write; `frame->num_entities++` | ring masked; **`num_entities` itself is never capped to `MAX_EDICTS`** → feeds Defect D1 | defect (D1, in a sink) |
| `cl_parse.c` `CL_DeltaEntity` `CL_AllocEntity(newnum)` | newnum | realloc'd `cl_entities[newnum]` | `CL_AllocEntity` checks `0..MAX_CL_ENTNUM`, grows array via `NextPow2gt`; NULL → dummy | ok |
| `cl_parse.c` `CL_ParseDelta` `modelindex`, `modelindex2`, `modelindex3`, `modelindex4`, `frame`, `skinnum`, `effects`, `renderfx`, `origin`, `angles`, `old_origin`, `sound`, `event`, `solid` | byte (`modelindex*`, `sound`, `event`), byte or short (`frame`), byte, short or long (`skinnum`, `effects`, `renderfx`), short (`solid`), coords and angles | stored in `entity_state_t` fields as read (`frame` and `skinnum` can be negative) | no check here; each sink must bound the field: `modelindex*` in the `model_draw`/`model_clip` rows (by the byte type, H2), `frame` in the D5, D11 and D16 rows, `skinnum` in the clientinfo, D10 and beam rows, `sound` in the `sound_precache` row; `effects` and `renderfx` are flag tests, `event` a `switch` (`CL_EntityEvent`) and comparisons, `solid` a test and bit fields for the prediction box, positions and angles plain values | see the sink rows |
| `cl_parse.c` `CL_ParsePlayerstate` `state->stats[i]` | long `statbits`, shorts | `stats[i]`, `i` in `0..MAX_STATS` loop | `i < MAX_STATS` | ok |
| `cl_parse.c` `CL_ParsePlayerstate` other fields (`pm_type`, origin, velocity, `pm_time`, `pm_flags`, gravity, `delta_angles`, view and kick angles, `gunframe`, gun offset and angles, blend, `fov`, `rdflags`) | byte/char/short/angle reads | stored as values in `player_state_t` | not used as indices (`gunindex` is covered in the entity rows) | ok |
| `cl_parse.c` `CL_ParseFrame` `serverframe`, `surpressCount`, `svc_playerinfo`/`svc_packetentities` bytes | `MSG_ReadLong`, `MSG_ReadByte` | `cl.frames[serverframe & UPDATE_MASK]`; command bytes compared | masked; a wrong command byte → `ERR_DROP` | ok |
| `cl_parse.c` `CL_ParseFrame` `cl.frame.deltaframe` | `MSG_ReadLong` | `cl.frames[deltaframe & UPDATE_MASK]` | masked; validity checks | ok |
| `cl_parse.c` `CL_ParseFrame` areabits `len` | `MSG_ReadByte` | `MSG_ReadData` into `areabits[MAX_MAP_AREAS/8]` | `len > sizeof(areabits)` → `Com_Error(ERR_DROP)` (fixed in TASK2/#9) | ok (already carded/fixed) |
| `cl_parse.c` `CL_ParseServerData` `cl.playernum` | `MSG_ReadShort` | `cl.playernum+1` used as entity number across client | **no range check at read** → Defect D2 | defect (D2) |
| `cl_parse.c` `CL_ParseServerData` gamedir/levelname | `MSG_ReadString` | `Q_strlcpy` into `cl.gamedir`; `Cvar_Set("game", str)` | `Q_strlcpy` truncates; `FS_BuildGameSpecificSearchPath` rejects `..`, `/`, `\` | ok |
| `cl_parse.c` `CL_ParseConfigString` `i` | `MSG_ReadShort` | index `cl.configstrings[i]` and sub-ranges | `i < 0 || i >= MAX_CONFIGSTRINGS` → print+return; statusbar vs normal length checked | ok |
| `cl_parse.c` `CL_ParseConfigString` → `CL_LoadClientinfo` (`CS_PLAYERSKINS + n`) | configstring (< 64 bytes, checked above) | `Q_strlcpy` into `cinfo[64]`, `name[64]`, `model_name`/`skin_name[64]`; file names by `Com_sprintf` with `sizeof` | `name[t - s]` is inside `name` because `s` is inside `cinfo` of the same size; the skin pointer `s + strlen(model_name) + 1` stays inside `cinfo`; names are only looked up (`R_RegisterModel`, `R_RegisterSkin`, `Draw_FindPic`), never written | ok |
| `cl_lights.c` `CL_SetLightstyle` `map[k]` | `CS_LIGHTS + n` configstring | `cl_lightstyle[n].map[k]`, `k < strlen(s)` | `n < MAX_LIGHTSTYLES` by the caller's range test; `strlen(s) < MAX_QPATH` enforced by `CL_ParseConfigString`, and `map` is `float[MAX_QPATH]` | ok |
| `cl_parse.c` `CL_ParseStartSoundPacket` `sound_num` | `MSG_ReadByte` | `cl.sound_precache[sound_num]` | `sound_num < 0` drop; `>= MAX_SOUNDS` print+return | ok |
| `cl_parse.c` `CL_ParseStartSoundPacket` `ent` | `channel = MSG_ReadShort; ent = channel>>3` | passed to `S_StartSound` | `ent < 0` print+return; `S_StartSound`/`S_PickChannel` clamp — see Hardening H3 | ok (hardening H3) |
| `cl_parse.c` svc_layout `MSG_ReadString` | string | `Q_strlcpy` into `cl.layout[1024]` | truncates | ok |
| `cl_parse.c` svc_print `i` | `MSG_ReadByte` | only compared to `PRINT_CHAT` | `i < 0` → break | ok |
| `cl_parse.c` svc_print text | `MSG_ReadString` | `Com_Printf("%s", ...)` | string bounded 2048, printed as an argument | ok |
| `cl_parse.c` svc_centerprint → `SCR_CenterPrint` | `MSG_ReadString` | `Q_strlcpy` into `scr_centerstring[1024]`; echo to console builds `line[64]` | the echo scans at most 40 characters per line, so `line[i + 1]` is at most index 41; `SCR_DrawCenterString` also scans 40 per line | ok |
| `cl_parse.c` svc_stufftext | `MSG_ReadString` | `Cbuf_AddText` | by design (see By design) | ok (by design) |
| `cl_parse.c` `CL_ParseStartSoundPacket` `flags`, volume, attenuation, offset, position | `MSG_ReadByte`, `MSG_ReadPos` | scaled floats and a vector | not indices | ok |
| `cl_inventory.c` `CL_ParseInventory` `cl.inventory[i]` | `MSG_ReadShort` ×`MAX_ITEMS` | `inventory[i]` | `i` in `0..MAX_ITEMS` loop | ok |
| `cl_download.c` `CL_ParseDownload` `size`, `percent` | `MSG_ReadShort`, `MSG_ReadByte` | chunk length for `fwrite`; progress | `size == -1` is "no such file"; otherwise `size < 0` or larger than the bytes left in the message → `ERR_DROP` (fixed in TASK1/#8); `percent` is only displayed | ok (fixed by TASK1) |
| `cl_download.c` `CL_Download_f` `Cmd_Argv(1)` | console token (user, or a server through `svc_stufftext`; at most 1023 characters) | `Com_sprintf` into `filename[MAX_OSPATH]`; `strcpy` into `cls.downloadname[MAX_OSPATH]`; `COM_StripExtension` into `cls.downloadtempname[MAX_OSPATH]`; `strcat(..., ".tmp")` | `CL_DownloadFilter`; the first copy truncates; **the append is unbounded**: where `MAX_OSPATH` is 256 (Windows) a name that fills the buffer with no extension leaves no room for the suffix → Defect D15; the sibling `CL_CheckOrDownloadFile` uses `Q_strlcpy`/`Q_strlcat` for the same steps | defect (D15) |
| `cl_download.c` `CL_RequestNextDownload` `strcpy(model, p)`, `strcpy(skin, p)` | `cl.configstrings[CS_PLAYERSKINS + i]` (server) | copies into `char model[MAX_QPATH]`, `skin[MAX_QPATH]` | unbounded copies, in bounds only because `CL_ParseConfigString` refuses a string of `MAX_QPATH` or more characters for every row outside the statusbar range, and `p` and the `skin` source point inside that string | ok |
| `cl_effects.c` `CL_AddMuzzleFlash` `i` | `MSG_ReadShort` | `CL_AllocEntity(i)` | `i < 1 || i > MAX_CL_ENTNUM` → `Com_Error` | ok |
| `cl_effects.c` `CL_AddMuzzleFlash` `weapon` | `MSG_ReadByte` | `switch`, no array index | `-1` hits default | ok |
| `cl_effects.c` `CL_AddMuzzleFlash2` `flash_number` | `MSG_ReadByte` | `monster_flash_offset[flash_number]` | `flash_number` is `unsigned`, so `-1` is rejected too; `flash_number > 210` → print+return, a literal below the table size (`flash.c`) — Hardening H4 | ok (hardening H4) |
| `cl_effects.c` `CL_AddMuzzleFlash2` `entnum` | `MSG_ReadShort` | `CL_AllocEntity`-backed entity lookup | `entnum < 1 \|\| entnum > MAX_CL_ENTNUM` → `Com_Error` | ok |
| `cl_tempentities.c` `CL_ParseTEnt` `type` | `MSG_ReadByte` | `switch`; `default` `Com_Error` | ok | ok |
| `cl_tempentities.c` splash `r` | `MSG_ReadByte` | `splash_color[r]` (7 entries) | `r > 6 || r < 0` → `color=0` else index | ok |
| `cl_tempentities.c` beam/lightning/steam `ent`/`srcEnt`/`destEnt` | `MSG_ReadShort` | stored in beam `entity`/`dest_entity`, compared only | `CL_ParseLightning` drops on `-1`; beams store ints, used in equality tests not as indices | ok |
| `cl_tempentities.c` `TE_FLASHLIGHT` `ent` | `MSG_ReadShort` | `CL_Flashlight(ent, pos)` → `CL_AllocDlight(ent)` key | used only as a key compared with `cl_dlights[i].key` over `MAX_DLIGHTS`; never an index | ok |
| `cl_tempentities.c` sustain `id` | `MSG_ReadShort` | `s->id`, compared only | not an index | ok |
| `cl_tempentities.c` `CL_ParseTEnt` and helpers (`CL_ParseLaser`, `CL_ParseNuke`, `CL_ParseWidow`, `CL_ParseHeatBeam`, `CL_ParseBeam`) positions, directions, counts, `magnitude` | `MSG_ReadPos`, `MSG_ReadDir`, `MSG_ReadByte`, `MSG_ReadShort` | vectors, particle counts, speeds | counts are loop bounds over the free-particle list (allocation stops when it is empty); `MSG_ReadDir` bounds its index | ok |
| `cl_tempentities.c` `CL_ParseParticles` | `MSG_ReadPos`, `MSG_ReadDir`, `MSG_ReadByte` ×2 | `CL_ParticleEffect` | the function has no caller (dead code); its colour would be the same class as D9 | ok (unreachable) |
| `cl_tempentities.c` `TE_LASER_SPARKS`/`TE_WELDING_SPARKS` `color`, `CL_ParseSteam` `r` | `MSG_ReadByte` | particle colour plus a small random offset, which can exceed 255 | none; reaches the renderers unmasked → Defect D9 | defect (D9) |
| `cl_tempentities.c` `TE_TUNNEL_SPARKS`, `TE_FORCEWALL` `color` | `MSG_ReadByte` | particle colour used as is (0..255) | in range | ok |
| `sw_part.c` `R_DrawParticle` `vid_alphamap[(color << 8) + pdest]` / `[color + (pdest << 8)]` | `particle_t.color` | translucency table index | **no mask** → Defect D9 | defect (D9) |
| `gl1_main.c` `R_DrawParticles2` `colortable[p->color]` | `particle_t.color` | `d_8to24table[256]` index | **no mask** (the point-sprite path and gl3 mask `& 0xFF`) → Defect D9 | defect (D9) |
| `cl_network.c` `CL_ConnectionlessPacket` `Cmd_Argv(0)` and the logged command name | `MSG_ReadStringLine` → `Cmd_TokenizeString` | dispatch; `Com_Printf("%s: %s\n", ...)` | string bounded 2048; tokenizer bounded | ok |
| `cl_network.c` `client_connect` | command name only | `Netchan_Setup(..., net_from, ...)`, `cls.state = ca_connected`, sends `new` | ignored only in `ca_connected`; **accepted from any source address in `ca_disconnected`, `ca_connecting` and `ca_active`** while the client socket is open, and points the netchan at the sender → Defect D14 (preconditions there) | defect (D14) |
| `cl_network.c` `challenge` `Cmd_Argv(1)` | tokenized | `strtol` → `cls.challenge`; `CL_SendConnectPacket` to `cls.servername` | numeric; the connect goes to the configured server, not the sender; accepted from any source in any state (same class as D14) | defect (D14, lower impact) |
| `cl_network.c` `echo` `Cmd_Argv(1)` | tokenized (≤ 1023 characters) | `Netchan_OutOfBandPrint(..., "%s", ...)` back to the sender | format is fixed; reply is about as large as the request (no amplification) | ok |
| `cl_network.c` `print` | `MSG_ReadString` | `Com_Printf("%s", s)` | string bounded 2048; accepted from any source, console text only (this is how status replies arrive) | ok (by design) |
| `cl_network.c` `ping` | command name only | `Netchan_OutOfBandPrint("ack")` | fixed reply | ok |
| `cl_network.c` console commands with `Cmd_Argv` (`CL_Connect_f`, `CL_Packet_f`, `CL_Rcon_f`, `CL_ForwardToServer_f`, `Cmd_ForwardToServer`) | user console, or a server through `svc_stufftext` | `Q_strlcpy` into `server[256]`; `send[2048]` from a ≤ 1023-character token; `Q_strlcat` into `message[1024]`; forwarded strings | each copy bounded (the `CL_Packet_f` loop writes at most 4 + 1023 + 1 bytes of 2048) | ok |
| `cl_network.c` `cmd` connectionless | `MSG_ReadString` | `Cbuf_AddText` | gated on `NET_IsLocalAddress` | ok (by design for local) |
| `cl_main.c` `CL_ParseStatusMessage` → `M_AddToServerList` | `MSG_ReadString` | `Q_strlcpy` into `local_server_names[]` | `m_num_servers == MAX_LOCAL_SERVERS` guard; `Q_strlcpy` truncates | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"pic"`/`"stat_string"`/`"if"` stat index | `COM_Parse`+`strtol` of the layout | `stats[index]` | `index < 0 \|\| index >= MAX_STATS` rejected in each | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"pic"` `configstrings[CS_IMAGES + value]` | `value = stats[index]` (server `short`) | configstring row, then picture name | only `value >= MAX_IMAGES` rejected; **negative `value` passes** → Defect D8 | defect (D8) |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"stat_string"` `configstrings[value]` | `stats[index]` | configstring row | `value < 0 \|\| value >= MAX_CONFIGSTRINGS` rejected | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"client"`/`"ctf"` `clientinfo[value]` | `strtol` of the layout | clientinfo index | `value < 0 \|\| value >= MAX_CLIENTS` rejected; `ctf` `sprintf` into `block[80]` with `%3d %3d %-12.12s`: two `int` fields of at most 11 characters each plus a 12-character name, under 80 | ok |
| `cl_screen.c` `SCR_ExecuteLayoutString` `"num"` → `stats[strtol(token)]` | layout number | `stats[...]` index | **unchecked** index into `stats[MAX_STATS]` → Defect D3 | defect (D3) |
| `cl_screen.c` `DrawHUDStringScaled` `line[width++]` | `COM_Parse` token chars to `\n` | index into `line[1024]` | no bound on `width` in the function; its only live callers (`cstring`, `cstring2` layout tokens) pass a `COM_Parse` token, at most `MAX_TOKEN_CHARS - 1` = 1023 characters, so `line[1023]` is the furthest write; not reachable today → D4 | ok (hardening D4) |
| `cl_entities.c` `CL_AddPacketEntities` `cl.model_draw[s1->modelindex]` (and `modelindex2/3/4`) | modelindex bytes | model pointer | no explicit check; `CL_ParseDelta` reads each with `MSG_ReadByte`, so 0..255 against `model_draw[MAX_MODELS]` (256); `-1` only on a short read, which drops the connection before the frame is used (see D5); see Hardening H2 | ok (hardening H2) |
| `cl_entities.c` `ci = &cl.clientinfo[s1->skinnum & 0xff]` | skinnum | clientinfo index | masked `& 0xff`, `MAX_CLIENTS == 256` | ok |
| `cl_entities.c` custom weapon `ci->weaponmodel[s1->skinnum >> 8]` | skinnum (`int`, byte/short/long on the wire) | model pointer | with `cl_vwep` non-zero (the default), only `i > MAX_CLIENTWEAPONMODELS - 1` rejected; **negative `i` passes** → Defect D10 | defect (D10) |
| `cl_entities.c` `ent.skinnum = s1->skinnum` → `gl1_mesh.c`/`gl3_mesh.c` `currentmodel->skins[skinnum]` | skinnum | image pointer | only `skinnum >= MAX_MD2SKINS` tested; **negative passes** → Defect D10 (`sw_alias.c` tests both sides) | defect (D10) |
| `cl_entities.c` beam `ent.skinnum = (s1->skinnum >> (...)) & 0xff` → `d_8to24table[e->skinnum & 0xFF]` | skinnum | palette index | masked at both ends | ok |
| `cl_entities.c` `ent.frame = s1->frame` → `files/surf.c` `R_TextureAnimation` `frame % numframes`, called by all three renderers (`gl1_surf.c`, `gl3_surf.c`; soft `sw_edge.c` and `sw_poly.c` through `D_CacheSurface` in `sw_surf.c`) | frame (signed short) | count of `tex->next` steps | **negative frame gives a negative count**; the loop runs about 2^32 steps or reaches `NULL` → Defect D11 (alias models test both sides) | defect (D11) |
| `cl_entities.c` `ent.frame = s1->frame` → sprite models: `gl1_main.c` `R_DrawSpriteModel`, `gl3_main.c` `GL3_DrawSpriteModel`, `sw_sprite.c` `R_DrawSprite`: `frame %= numframes`, then `psprite->frames[frame]` and `currentmodel->skins[frame]` | frame (signed short) | sprite frame record and image pointer | the remainder of a negative frame is zero or negative, so for a sprite with two or more frames **the index is negative** → Defect D16 | defect (D16) |
| `cl_entities.c` `cl.model_draw[ps->gunindex]` | `gunindex` (`MSG_ReadByte` in `CL_ParsePlayerstate`) | gun model | 0..255 by the byte type vs `MAX_MODELS` | ok (by the byte type) |
| `cl_entities.c` `bfg_lightramp[s1->frame]` | frame (`U_FRAME8` byte or `U_FRAME16` signed short) | index into `static int[6]` | reached when `effects & EF_BFG` is set without `EF_ANIM_ALLFAST`; **no check on either side** → Defect D5 | defect (D5) |
| `cl_prediction.c` `cl.model_clip[ent->modelindex]` | modelindex byte | clip model | reached when `ent->solid == 31`; 0..255 by the byte type against `model_clip[MAX_MODELS]`; see Hardening H2 | ok (hardening H2) |
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
| `sv_user.c` clc_move `checksum`, CRC over `net_message.data + checksumIndex + 1` | `MSG_ReadByte`; CRC length `readcount - checksumIndex - 1` | compared to `COM_BlockSequenceCRCByte(...)` | length clamped to 60 inside, but **computed before any `readcount > cursize` test**: a truncated `clc_move` in a large packet reads past `net_message_buffer` → Defect D12 | defect (D12) |
| `sv_user.c` clc_move `lastframe` | `MSG_ReadLong` | `cl->frames[lastframe & UPDATE_MASK]`, `frame_latency[lastframe & (LATENCY_COUNTS-1)]` | both masked; only read when `lastframe > 0` | ok |
| `sv_user.c` clc_move `net_drop = cl->netchan.dropped` | from netchan | loop count of `SV_ClientThink` | `net_drop < 20` guard | ok |
| `sv_user.c` clc_move usercmds | `MSG_ReadDeltaUsercmd` ×3 | `SV_ClientThink` | `msec` consumed as time; `commandMsec` underflow guarded by `sv_enforcetime` | ok |
| `sv_user.c` clc_stringcmd `MSG_ReadString` | string | `SV_ExecuteUserCommand` | `stringCmdCount < MAX_STRINGCMDS` rate-limited | ok (command dispatch by design) |
| `sv_user.c` `SV_Configstrings_f`/`SV_Baselines_f` `start` | `Cmd_Argv`→`strtol` | loop start / `sv.baselines[i]` | `start < 0` clamped to 0; loops bound by `MAX_CONFIGSTRINGS`/`numbaselines` | ok |
| `sv_user.c` `SV_ExecuteUserCommand` `Cmd_Argv(0)` | `clc_stringcmd` string, tokenized without macro expansion | `ucmds[]` dispatch, else `ge->ClientCommand` | table lookup by name | ok (by design) |
| `sv_user.c` `SV_New_f` | no arguments | writes `svc_serverdata` for this client | `state != cs_connected` rejected | ok |
| `sv_user.c` `SV_Begin_f` `Cmd_Argv(1)` | `strtol` | compared with `svs.spawncount`; then `cs_spawned`, `ge->ClientBegin` | spawncount must match; **no test of the client's current state**, so `begin` can be repeated while spawned (game-level effect, no memory issue) → Hardening H7 | ok (hardening H7) |
| `sv_user.c` `SV_Nextserver_f` `Cmd_Argv(1)` | `strtol` | compared with `svs.spawncount`; `SV_Nextserver` | spawncount must match; `SV_Nextserver` refuses during a normal game | ok |
| `sv_user.c` `SV_BeginDownload_f` `name` | `Cmd_Argv(1)` | `FS_LoadFile(name)`, path | denylist `..`, `\`, `:`, leading `.`/`/`, must contain `/` | ok (see By design: denylist, not the client allowlist) |
| `sv_user.c` `SV_BeginDownload_f` `offset` | `Cmd_Argv(2)`→`strtol` | `downloadcount = offset`; then `SV_NextDownload_f` reads `download + downloadcount - r` | clamp only on the `>` side: `if (offset > downloadsize) downloadcount = downloadsize`; **negative `offset` passes** → Defect D6 | defect (D6) |
| `sv_conless.c` `SVC_DirectConnect` `version`/`qport`/`challenge` | `Cmd_Argv`→`strtol` | compared / stored | version and challenge checked; qport stored | ok |
| `sv_conless.c` `SVC_DirectConnect` userinfo, `Info_SetValueForKey(userinfo, "ip", NET_AdrToString(net_from))` | `Cmd_Argv(4)` | `Q_strlcpy` into `userinfo[MAX_INFO_STRING]`; engine forces the `ip` key; passed to `ge->ClientConnect` | copy truncates, in bounds; **nothing makes the engine's `ip` pair the only one, or present** → Defect D17 | defect (D17) |
| `sv_conless.c` `SVC_Info` `version` | `Cmd_Argv(1)`→`strtol` | compared; `Com_sprintf(string, 64, ...)` | **bug-adjacent**: wrong-version branch calls `Com_sprintf(string, sizeof(string), "%s: wrong version\n", hostname->string, sizeof(string))` — extra arg, benign; Hardening H6 | ok (hardening H6) |
| `sv_conless.c` `SVC_RemoteCommand` `net_message.data + 4` | raw packet tail | `Com_Printf("%s", ...)` before the password check | **nothing NUL-terminates the receive buffer**; the print can run past `net_message_buffer` → Defect D13 | defect (D13) |
| `sv_conless.c` `SVC_RemoteCommand` args | `Cmd_Argv(i)` | `Q_strlcat` into `remaining[1024]`; `Cmd_ExecuteString` | `Q_strlcat` bounded; gated on `Rcon_Validate` password | ok (rcon by design) |
| `sv_conless.c` `SV_ConnectionlessPacket` `s` | `MSG_ReadStringLine` | `Cmd_TokenizeString` | string bounded 2048; tokenizer bounded | ok |
| `sv_conless.c` `SV_ConnectionlessPacket` `Cmd_Argv(0)` | tokenized | dispatch, `Com_DPrintf` | compared with fixed names; unknown command printed with `%s` from the bounded copy | ok |
| `sv_conless.c` `SVC_Status` | command name only | `SV_StatusString` reply | `status[MAX_MSGLEN - 16]` filled with `Q_strlcpy`/`Q_strlcat` and a length test before each player line; reply larger than the request (reflection, see By design) | ok |
| `sv_conless.c` `SVC_Ack` | command name only | `Com_Printf` of `net_from` | no packet data printed | ok |
| `sv_conless.c` `SVC_GetChallenge` | command name only | `svs.challenges[MAX_CHALLENGES]`, keyed by address | loop bounded by `MAX_CHALLENGES`; oldest entry reused; value from `Sys_GetRandomBytes` (FIX4/#46) | ok |
| `sv_conless.c` `Rcon_Validate` `Cmd_Argv(1)` | tokenized | `strcmp` with `rcon_password` | empty password disables rcon; timing see By design | ok |
| `sv_main.c` `SV_UserinfoChanged` `Info_ValueForKey(userinfo,"rate")` | userinfo value | `strtol` → clamped 100..15000 | clamped | ok |
| game `g_svcmds.c` `StringToFilter` `num[j++]` | `gi.argv(2)` of `sv addip` / `sv removeip` (server console or rcon; a token of up to 1023 characters) | write into `char num[128]` | **`j` unbounded** while the input is digits → Defect D7 | defect (D7) |
| game `client.c` `ClientConnect` → `g_svcmds.c` `SV_FilterPacket(Info_ValueForKey(userinfo, "ip"))` `m[i]` | userinfo `ip`: the first `ip` pair, which need not be the one the engine set from `net_from` | octet bytes `m[4]`, then `*(unsigned *)m` against the filter list | `i < 4` loop bound keeps the writes in bounds; does not call `StringToFilter`; **the value can be peer-chosen or absent**, and with fewer than four dotted parts (or none) some of `m[]` is never written before it is read → Defect D17 | defect (D17) |
| game `client.c` `ClientUserinfoChanged` skin → `configstring(CS_PLAYERSKINS+playernum, va("%s\\%s", name, s))` | userinfo | configstring | `playernum = ent - g_edicts - 1`, engine-assigned; name/skin validated by `Info_Validate` | ok |

## By design (noted, no card)

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
- Connectionless reflection: `status`, `info` and `ping` on the server and
  `echo` and `ping` on the client answer any source address. `status` answers
  a short request with the serverinfo and player list (a reflection and
  amplification vector, as in every Quake II server); `echo` returns about as
  many bytes as it receives. Rate limiting is a network-level concern outside a
  parser audit.
- Connectionless `print` on the client is printed from any source; it is how
  `status` replies arrive and only produces console text.

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
only by `num_entities` (the values read past the end are the ones
`S_BuildSoundList` has just written there, and they are then used as an index
into `cl.sound_precache[]`). When `num_entities > 1024` the write
`sounds[i]` runs off the end of the 4 KB stack array.

Hostile input and reachability: there is no cap on `num_entities` and no
uniqueness check on the entity numbers in a frame, so a server can make a
frame's count exceed `MAX_EDICTS` within a few packets. The values written are each entity's `sound` byte, chosen by
the server, at stack slots past the array: server-driven stack memory
corruption on the client, which the scale rates critical.

Confirmation (ASan harness linking the real `S_BuildSoundList` with
`num_entities` above the array size): AddressSanitizer reports a `stack-buffer-overflow`
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
is a `COM_Parse` token, which can be longer than `num`, so an octet longer
than the buffer writes past `num` on the game module's stack.

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

### D9: server-chosen particle colours above 255 index the software renderer's translucency table out of bounds (high, medium confidence) [FIX25 (#126)]

Trace: `CL_ParseTEnt` (`cl_tempentities.c`) reads a colour with
`MSG_ReadByte` (0..255) for `TE_LASER_SPARKS` and `TE_WELDING_SPARKS`
(passed to `CL_ParticleEffect2`, `cl_particles.c`) and for `TE_STEAM`
(`CL_ParseSteam`, passed to `CL_ParticleSteamEffect` or stored for the
sustained `CL_ParticleSteamEffect2`, `cl_effects.c`). Each of those adds a
small random offset to the colour, so a colour value near the top of the byte
range ends up beyond 255. No step masks it: `CL_AddParticles` passes it to `V_AddParticle`
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
no extent; with the stock file, a colour beyond 255 reads past the end of the
block in both translucent cases (further in the 66% case, where the colour
selects a row), and gl1 reads past the end of `d_8to24table`. The values read
become pixel colours (seen only by the victim). Whether the 66% read faults
depends on where the allocator places the block (an 80 KB block can end at a
page boundary).

Hostile input: a temporary entity of one of those three types with a colour
value that, after the engine's random offset, exceeds the table's index
range. Stock game code sends fixed colours that stay in range, so normal play
is unaffected. Severity high by the scale (server-driven heap
out-of-bounds read that can fault); confidence medium because the fault
depends on allocator placement. Fix: mask the colour with `& 0xFF` where the
renderers consume it (as gl3 and the gl1 point path already do), or at
`V_AddParticle`.

### D10: a negative entity `skinnum` passes one-sided checks into `weaponmodel[]` and the GL model skin arrays (high, high confidence) [FIX25 (#126)]

`entity_state_t.skinnum` is an `int` read by `CL_ParseDelta` as a byte, a
signed short, or (with both `U_SKIN8` and `U_SKIN16`) a full signed long, so
the server can send any negative value.

- `CL_AddPacketEntities` (`cl_entities.c`), custom weapon path
  (`modelindex2 == 255`, with `cl_vwep` non-zero, the default; `cl_vwep 0`
  forces `i` to 0): `i = (s1->skinnum >> 8)` is tested only with
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

### D11: a negative brush-entity `frame` makes `R_TextureAnimation` walk the texture chain about 2^32 times (high, high confidence) [FIX25 (#126)]

On the normal path of `CL_AddPacketEntities` (no `EF_ANIM*` effect) the
entity's `ent.frame` is `s1->frame`, a server-chosen value that can be
negative (`U_FRAME16` signed short). For a brush model (an inline `*N` model
from the server's own map) the surface code of all three renderers calls
`R_TextureAnimation` (`src/client/refresh/files/surf.c`) for every surface
with an animated texture (OpenGL 1: `gl1_surf.c`; OpenGL 3: `gl3_surf.c`;
software: `D_SolidSurf` in `sw_edge.c` and `R_BuildPolygonFromSurface` in
`sw_poly.c`, both through `D_CacheSurface` in `sw_surf.c`): `c = currententity->frame % tex->numframes; while
(c && tex) { tex = tex->next; c--; }`. In C the remainder of a negative frame
is zero or negative, so `c` starts below zero and the loop decrements it away
from zero: on the circular animation chain built by `Mod_LoadTexinfo` it runs
until `c` wraps (signed overflow, undefined behaviour; in practice about 2^32
iterations per surface per rendered frame), freezing the client; on a chain
that ends in `NULL` it stops with `tex == NULL` and then dereferences it.
The alias-model paths are not affected: in all three renderers they test
`frame` and `oldframe` against `0..num_frames - 1` on both sides and reset an
out-of-range value to 0 (`gl1_mesh.c` and `gl3_mesh.c` in the cull and draw
functions, `sw_alias.c` `R_AliasSetupFrames`). Sprite models are affected in a
different way: they index arrays with the remainder directly (D16). Severity high (remote hang or
crash). Same class as D10 (a server entity field reaching a renderer with a
one-sided assumption). Fix: treat a negative frame as 0, either in
`R_TextureAnimation` or where `ent.frame` is set.

### D12: a truncated `clc_move` makes the command checksum read past `net_message_buffer` (low, high confidence) [FIX26 (#127)]

In `SV_ExecuteClientMessage` (`sv_user.c`) the `clc_move` case records
`checksumIndex = net_message.readcount`, reads the checksum byte, a long and
three `MSG_ReadDeltaUsercmd`, then (for a client in `cs_spawned`) computes
`COM_BlockSequenceCRCByte(net_message.data + checksumIndex + 1,
net_message.readcount - checksumIndex - 1, ...)` before the loop's
`readcount > cursize` test runs. On a short read the reads keep advancing
`readcount` past `cursize`, so the CRC length counts bytes that were never
received, and `memcpy` in `COM_BlockSequenceCRCByte` (clamped to 60 bytes)
reads past the end of the received data. For a datagram near the largest
accepted size that read runs past the end of the 1400-byte static
`net_message_buffer` (`netchan.c`). The
result is only compared with the checksum byte, which is `-1` after a short
read and never matches, so the packet's moves are discarded and nothing read
is returned. Reachable by any spawned client. Severity low (read of adjacent
static memory, result only compared). Fix: test `net_message.readcount >
net_message.cursize` (and drop) before computing the checksum.

### D13: `SVC_RemoteCommand` prints the raw packet with `%s` before the password check, and nothing NUL-terminates it (low, high confidence) [FIX26 (#127)]

`SVC_RemoteCommand` (`sv_conless.c`) logs every rcon attempt with
`Com_Printf("...%s\n", ..., net_message.data + 4)`, whether or not
`Rcon_Validate` accepts the password. `net_message.data` is the static
1400-byte `net_message_buffer`; `NET_GetPacket` (`backends/unix/network.c`)
writes the datagram with `recvfrom` and sets `cursize`, and nothing writes a
terminator after it (the tokenizer works on the copy that
`MSG_ReadStringLine` made, not on the buffer). The bytes after `cursize` are
left over from earlier datagrams. The receive buffer is not terminated, so
the print can run past the received data, and past the end of
`net_message_buffer` into the static memory that follows until it meets a
zero byte; an unauthenticated peer reaches it. The output goes to the server console and log,
not to the peer (the redirect to the sender starts after the print).
Severity low (out-of-bounds read of adjacent static memory, logged locally).
Fix: print at most
`cursize - 4` bytes (`%.*s`), or terminate the received data in
`NET_GetPacket` (receive at most `maxsize - 1` bytes and store a NUL at
`cursize`).

### D14: the client accepts `client_connect` from any address in any state but `ca_connected` (high, high confidence) [FIX27 (#128)]

`CL_ConnectionlessPacket` (`cl_network.c`) handles `client_connect` by
ignoring it only when `cls.state == ca_connected`. In every other state
(`ca_disconnected`, `ca_connecting`, `ca_active`) it calls
`Netchan_Setup(NS_CLIENT, &cls.netchan, net_from, ...)` with the sender's
address, sets `cls.state = ca_connected` and sends `new` to the sender.
Nothing compares `net_from` with the server the client is connecting to
(`cls.servername`). `CL_ReadPackets` hands every connectionless datagram on
the client socket to this function whatever the state. Where the
preconditions below hold, a client in a game on a remote or multiplayer server
is moved to the sender, a client that is connecting can be raced, and an idle
disconnected client is pulled onto a server the user never chose. The new
"server" then holds the server's trust, which by design includes
`svc_stufftext` (arbitrary client console commands) and every `svc_*` parser
in this audit. Severity high (unauthenticated takeover of a connection).

Preconditions, each read in the code:

1. The client's UDP socket must be open. It is opened only by
   `NET_Config(true)`: from `connect`, `rcon` and `packet` (`CL_Connect_f`,
   `CL_Rcon_f`, `CL_Packet_f`), the server-browser ping (`CL_PingServers_f`),
   or a local server started with `maxclients > 1` (`SV_InitGame`). It is
   closed by `NET_Config(false)`: when a single-player game starts
   (`SV_InitGame` with `maxclients` 1), on `killserver` (`SV_KillServer_f`)
   and when the menu changes the game while a server runs. A client that only
   plays single player therefore has no client socket and is not reachable in
   any state, `ca_active` included; only the loopback carries its packets.
2. `CL_Disconnect` does not close the socket. A client that is disconnected
   and idle, but opened the server browser or left a network game earlier in
   the session, is still reachable.
3. The sender needs the client's address and its UDP source port. The port is
   ephemeral (the client socket is bound with `PORT_ANY`), a 16-bit value
   known to any host the client has sent a packet to (a server it played on or
   pinged) and otherwise searchable. Nothing secret is needed, and nothing
   rate-limits or counts wrong guesses.
4. No on-path position and no source spoofing: the datagram comes from the
   sender's own address, and the client then talks to that address. The
   practical barrier is whether a NAT or stateful firewall in front of the
   client admits an unsolicited datagram from a third address.
5. Verified on the Unix backend (`src/backends/unix/network.c`) only; the
   Windows backend was not read (see Method and coverage).

The `challenge` reply has the same shape at lower impact: it is accepted from
any address in any state and overwrites `cls.challenge`, but the connect it
triggers goes to `cls.servername`, not to the sender. In `ca_disconnected`
with a stale `cls.servername` (`CL_Disconnect` does not clear it), an
unsolicited `challenge` makes the client send its userinfo in a `connect`
packet to the last server it used.

Fix (client-side only, no protocol change): accept `client_connect` and
`challenge` only in `ca_connecting` and only when `net_from` equals the
resolved address of `cls.servername`.

### D15: the `download` command appends `.tmp` past `cls.downloadtempname` where `MAX_OSPATH` is 256 (critical by the scale, medium confidence) [FIX29 (#130)]

`CL_Download_f` (`cl_download.c`) formats `Cmd_Argv(1)` into
`filename[MAX_OSPATH]` with `Com_sprintf` (truncating), passes it through
`CL_DownloadFilter`, copies it into `cls.downloadname` with `strcpy`, strips
the extension into `cls.downloadtempname[MAX_OSPATH]` with
`COM_StripExtension` (which copies up to the first `.` and has no size
parameter), and then runs `strcat(cls.downloadtempname, ".tmp")` with no
bound. `MAX_OSPATH` is 256 on Windows builds and 4096 elsewhere
(`src/common/header/shared.h`). On a Windows build, a name that fills
`filename` and has no extension reaches `COM_StripExtension` whole, so the
stripped name already fills `cls.downloadtempname` and the append writes four
bytes past the array, into the adjacent `cls.downloadname` member of the
client state. Where `MAX_OSPATH` is 4096 it is not reachable: a console token
is at most 1023 characters. The sibling `CL_CheckOrDownloadFile` performs the
same steps with `Q_strlcpy` and `Q_strlcat`.

Hostile input and reachability: `download` is a client console command, so a
server reaches it through `svc_stufftext` (by design, see By design); the
filter refuses `..`, `:`, a leading `.` or `/` and library extensions, none
of which the name needs. The written bytes are the fixed suffix, not chosen
by the server, and they land in the next member of the same static struct
(the download name is overwritten), not in stack or heap metadata. The scale
rates any out-of-bounds write driven by a remote peer critical; that is the
rating here, with the impact limited as described. Confidence medium: a
static trace only, since no Windows build is available here. Fix: use the
bounded copies `CL_CheckOrDownloadFile` uses, and refuse the download when
the temporary name does not fit.

### D16: a negative entity `frame` on a sprite model indexes the sprite frame and skin arrays below zero (high, high confidence) [FIX25 (#126)]

The same server-chosen `ent.frame` (`U_FRAME16` signed short, set on the
normal path of `CL_AddPacketEntities`) reaches the sprite drawing code of all
three renderers: `R_DrawSpriteModel` (`gl1_main.c`), `GL3_DrawSpriteModel`
(`gl3_main.c`) and `R_DrawSprite` (`sw_sprite.c`). Each does
`frame %= psprite->numframes`, then reads `&psprite->frames[frame]` and
`currentmodel->skins[frame]`, with no other test. In C the remainder of a
negative value by a positive count is zero or negative, so for a sprite with
two or more frames (the loader caps `numframes` at `MAX_MD2SKINS`) the index
is negative:

- `psprite->frames[frame]` is a `dsprframe_t` record before the array. The
  sprite data starts a few bytes into the block `Hunk_Begin` maps for it
  (`src/backends/unix/shared/hunk.c`), so any negative index reads before the
  start of that mapping; the record's size and origin feed the sprite's
  vertices. This is an out-of-bounds read that faults when the memory below
  the mapping is not mapped.
- `currentmodel->skins[frame]` reads one of the `model_t` members declared
  before `skins[]` as an image pointer. A NULL value falls back to the "no
  texture" image; any other value is used as an image and dereferenced.

A sprite with a single frame always gives index 0 and is not affected.
Hostile input: an entity whose model is a precached sprite with two or more
frames and whose `frame` is negative; any server can send it. Severity high
(server-driven out-of-bounds read that can fault, then a pointer of unknown
origin used as an image). Same class as D10 and D11. Fix: treat a negative
frame as 0 before the remainder in the three sprite functions, or where
`ent.frame` is set (the D11 fix at that point covers both).

### D17: the engine-set userinfo `ip` key is neither unique nor guaranteed present when the game filters on it (medium, high confidence) [FIX30 (#131)]

Site: `SVC_DirectConnect` (`sv_conless.c`) copies the connecting peer's
userinfo and then forces the `ip` key with `Info_SetValueForKey(userinfo,
"ip", NET_AdrToString(net_from))`, so that the game can filter on the source
address. `Info_SetValueForKey` (`shared.c`) calls `Info_RemoveKey`, which
removes only the first pair with that key, and then appends the new pair at
the end of the string; when the new pair does not fit it prints "Info string
length exceeded" and stores nothing. `Info_ValueForKey` returns the first
pair with the key. The game's `ClientConnect` (`src/game/player/client.c`)
passes `Info_ValueForKey(userinfo, "ip")` to `SV_FilterPacket`
(`src/game/g_svcmds.c`).

Property not enforced: that the `ip` pair the game reads is the one the engine
set, i.e. that it is unique and present. The userinfo is chosen by the peer
before the engine's pair is added, and nothing removes every peer-supplied
`ip` pair or refuses the connect when the engine's pair is dropped.

Consequence: a connecting peer can make the game's IP filter see a value it
chose, or no value. With `filterban 1` (the default, a ban list) it evades a
ban on its address; with `filterban 0` (an allow list) it can claim a listed
address. In the no-value case, and for any value with fewer than four dotted
parts, `SV_FilterPacket` reads octet bytes of its stack array `m[4]` that were
never written (an uninitialised read, in bounds). The memory accesses stay in
bounds; the defect is an access-control bypass by an unauthenticated peer.
Severity medium: the scale has no row for a bypass of an operator's address
filter, it is not a takeover of a connection or a memory-safety fault, and the
closest row is an unvalidated peer value that seeds state the game relies on.
Confidence high (static trace). Fix: in `SVC_DirectConnect`, remove every
`ip` pair before setting the engine's and refuse the connect when it cannot be
stored; initialise `m[]` in `SV_FilterPacket`.

## Hardening (low severity)

Carried on FIX28 (#129) except D4, which is on FIX13 (#109).

- H1 `COM_Parse`: when a token reaches `MAX_TOKEN_CHARS` (1024) characters
  it resets `len` to 0, silently emptying the over-long token rather than
  truncating it; the guard `len < MAX_TOKEN_CHARS` keeps every write in range,
  so no overflow, behaviour wart only. (D4 depends on this cap.)
- H2 entity `modelindex*`: safe by the byte type today; an assert/bound would
  harden against a future widening of the field.
- H3 `S_StartSound`/`S_PickChannel`: `ent` from `channel>>3` can be large;
  `S_PickChannel` only errors on `entchannel < 0`, and `ent` is used as a sound
  origin lookup with `cl_numentities` bounds at the sinks — no OOB found, but
  an explicit upper bound on `ent` would be clearer.
- H4 `CL_AddMuzzleFlash2`: `flash_number > 210` is rejected though the
  `monster_flash_offset` table in `src/common/shared/flash.c` has more
  entries; harmless, but the bound should come from the table size.
- H6 `SVC_Info`: the wrong-version `Com_sprintf` passes an extra `sizeof`
  argument that the format ignores; cosmetic, no overflow.
- H7 `SV_Begin_f`: checks only the spawn count, not that the client is in
  `cs_connected`, so a spawned client can send `begin` again and re-run
  `ge->ClientBegin`. No memory issue; a game-level state wart. A state test
  would make the connect sequence strict.
- D4 (`DrawHUDStringScaled` width) is also hardening today; it is carried on
  FIX13 (#109) with D3 and D8 because it is the same function family.

## Cards filed

One card per defect class (epic trust-boundary, depends on TASK17). Every
unfixed finding above is on one of them.

| Card | Issue | Covers | Severity |
|---|---|---|---|
| FIX11 | #107 | D1 | critical |
| FIX12 | #108 | D2 | medium |
| FIX13 | #109 | D3, D8 (defects); D4 (hardening) | high |
| FIX14 | #110 | D5 | high |
| FIX15 | #111 | D6 | high |
| FIX16 | #112 | D7 | medium |
| FIX25 | #126 | D9, D10, D11, D16 | high |
| FIX26 | #127 | D12, D13 | low |
| FIX27 | #128 | D14 | high |
| FIX28 | #129 | H1, H2, H3, H4, H6, H7 | low |
| FIX29 | #130 | D15 | critical (Windows builds only) |
| FIX30 | #131 | D17 | medium |

D9, D10, D11 and D16 share a card because they are one class (a server-supplied
entity or effect field reaching an array index or loop count with a check on
one side only) and one fix pattern (a two-sided bound or a mask at the sink),
in the same files. D5 is the same class and has its own card.
D12 and D13 share a card because both treat the receive buffer as longer or
better terminated than the datagram in it.
