# 0005. Rendered frames and short clips may be published as pull-request evidence

Status: accepted by the owner 2026-11-13; landed as TASK73 (#163).

## Context

The renderer pins (`scripts/pins.sh`, [the user page](../../doc/100_renderpins.md))
are made from the retail game data on the owner's machine. Until now
nothing made from that data left the machine: not the frames, not the
manifests, not the logs. A pull request for a renderer change could
therefore show only a score table, and a reviewer had to trust numbers
without seeing a picture.

## Decision

Rendered frames and short clips of the game may be published on this
repository as evidence for a pull request, through one path:

- `scripts/pins.sh evidence` reads two pins already in the store and
  writes, to the git-ignored `evidence/` folder of the checkout, a
  side-by-side still (reference, candidate, difference), a short clip as
  animated PNG and as GIF, and a manifest with the command, the commit,
  the pin ids, the scores and the SHA-256 of every file. Every file stays
  under a size budget (4 MB by default).
- `scripts/pins.sh evidence-publish` uploads only the files that manifest
  lists, after checking each one's name, type, place and digest, as
  assets of the single pre-release `pr-evidence` (not marked latest), and
  prints the Markdown for the pull-request body. The upload is done by
  the script, never by a typed command.

What may never be published, by this path or any other: the game data
files (`.pak`, `.pk3`), demos (`.dm2`), cinematics (`.cin`), renderer
traces (`.q2r`), the pin store or any of its files (the raw `.tga`
frames, `manifest.txt`, `engine.log`, `timedemo.txt`, the `complete`
marker), and any file outside the evidence folder. `evidence-publish`
refuses each of these by name and by content, and its self-test
(`tests/check-evidence.sh`) shows every refusal. Published files and
the manifest carry no host name, user name or local path.

## Consequences

- A renderer pull request shows its effect inline: the still, the clip
  and the per-frame scores.
- Pictures of the game now leave the machine. They are small, scaled
  renders of a few frames, not data the game can load, and they are
  public for as long as the assets exist.
- The release assets are served with `application/octet-stream` and
  `content-disposition: attachment`; they show inline because browsers
  read the image type from the bytes inside an `<img>` element. The
  runbook records what was observed.
- The pre-release grows with every published set; old sets are deleted
  by hand when their pull requests are closed and no longer need them.

## Reversal

Delete the `pr-evidence` pre-release (`gh release delete pr-evidence
--cleanup-tag`), which removes every published image at once and breaks
the images in old pull requests; remove `evidence` and
`evidence-publish` from `scripts/pins.sh` with `scripts/pins/evidence.py`
and `tests/check-evidence.sh`; and restore the sentence on the user page
that said nothing made from the data is uploaded.
