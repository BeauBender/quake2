# 0007. Rendered frames and short clips may be published as pull-request evidence

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
  under a size budget (4 MB by default; `PINS_EVIDENCE_BUDGET` can raise
  it, and the raised value is written into the manifest and allowed at
  publish).
- `scripts/pins.sh evidence-publish` uploads one such set as assets of
  the single pre-release `pr-evidence` (not marked latest), and prints
  the Markdown for the pull-request body. It never uploads bytes it
  found: it reads each image once, without following a link, checks
  those bytes (the size budget, the SHA-256 the manifest records, and a
  full parse proving the file is exactly the PNG, animated PNG or GIF
  structure the encoder writes, with nothing before, between or after
  it) and stages those same bytes. The manifest it uploads is
  regenerated from fields that each match the pattern the evidence step
  can write, and the Markdown is built from the same fields; anything
  else refuses the whole set. The asset names are derived from the
  validated card, commit, renderer and scenario. The upload is done by
  the script, never by a typed command, and the script says on stderr,
  before it starts, where it uploads, how many files and bytes, and that
  an uploaded asset is public at once and cannot be recalled. One
  exception to "one path": `PINS_EVIDENCE_UPLOADER`, when set, names a
  program that replaces the built-in `gh` upload, including its check
  that `pr-evidence` exists and is a pre-release. It exists for the
  self-test's stand-in uploader and is left unset to publish.

What may never be published, by this path or any other: the game data
files (`.pak`, `.pk3`), demos (`.dm2`), cinematics (`.cin`), renderer
traces (`.q2r`), the pin store or any of its files (the raw `.tga`
frames, `manifest.txt`, `engine.log`, `timedemo.txt`, the `complete`
marker), and any file outside the evidence folder. `evidence-publish`
refuses each of these by name (no such name is one a set can have) and
by content (no such file parses as an image the encoder writes). Its
self-test (`tests/check-evidence.sh`) has a refusing case for each kind
named here by name, listed in the manifest, and for a `.pak` archive,
a trace, a store manifest and a raw frame by content, put in place of
an image and listed with its true size and digest; it also refuses a
real image with data appended, a text chunk, a foreign extension, or
the user name in its bytes. Published files and the manifest carry no
host name, user name or home path. The remaining limit is the pixels:
the checks prove each file is the structure the encoder writes and
holds no identity string in its bytes, not what its pixels show.

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
--cleanup-tag`). That stops further downloads of every published image
at once and breaks the images in old pull requests, but it cannot
recall a copy already fetched, cached or mirrored. Then remove `evidence` and
`evidence-publish` from `scripts/pins.sh` with `scripts/pins/evidence.py`
and `tests/check-evidence.sh`; and restore the sentence on the user page
that said nothing made from the data is uploaded.
