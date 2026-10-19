---
paths:
  - "src/client/cl_parse.c"
  - "src/client/cl_download.c"
  - "src/server/sv_user.c"
  - "src/common/collision.c"
  - "src/common/filesystem.c"
  - "src/client/refresh/files/**"
---

# Code that parses untrusted bytes

These files read network messages, downloaded files, and map, model and
image data that a server or a mod can supply.

- Check every length, count and offset read from the data against the size
  of the buffer it indexes, before use. Reject bad data with the error the
  file already uses (`Com_Error(ERR_DROP, ...)`, or `ri.Sys_Error` in the
  renderers); do not clamp and keep going with partial data.
- `CL_DownloadFilter` in `src/client/cl_download.c` is the only gate on
  download paths. Do not add a second write path around it, and keep its
  rejections (`..`, leading `.` or `/`, `:`, library extensions).
- Any new console command that writes a file validates its path argument
  against `..` itself: `FS_CreatePath` refuses such paths, `Q_fopen` does
  not.
- Build the affected target with `make ASAN=1` (or `UBSAN=1`) and load real
  data before calling a loader change done.
