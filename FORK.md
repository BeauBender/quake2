# Fork notes

Portmanbeau Quake II is a long-lived fork of
[Yamagi Quake II](https://github.com/yquake2/yquake2). This file records
where it came from and how it relates to upstream. The README lists how
it differs ([README.md](README.md)); the licence and every copyright
notice are upstream's and are unchanged ([LICENSE](LICENSE)).

## Provenance

* Upstream: Yamagi Quake II, which descends from Icculus Quake II and
  id Software's Quake II source release.
* Fork point: upstream commit `9dbf6992` of 2026-10-15. The tree was
  byte-identical to upstream at that commit.
* Home of the fork: <https://github.com/BeauBender/quake2>. Report problems
  there; the project's issues and pull requests are open to its owner only.

## The upstream remote

The repository has no `upstream` remote by default. To add it:

```sh
git remote add upstream https://github.com/yquake2/yquake2.git
git fetch upstream
```

## Sync policy

* Security fixes in upstream are cherry-picked, one commit at a time,
  and tested with the gate (`scripts/test.sh`).
* No new dependency and no new feature is imported from upstream without a
  card that asks for it. The fork removes dependencies on purpose
  ([docs/DEPENDENCIES.md](docs/DEPENDENCIES.md)); a merge of upstream
  would bring them back.
* Upstream is never merged wholesale.
* The vendored SDL3 tree has its own update procedure:
  [third_party/SDL3/README.md](third_party/SDL3/README.md).

## Comparing against upstream

With the remote added and fetched:

```sh
git log --oneline 9dbf6992..upstream/master    # what upstream did since the fork point
git diff 9dbf6992 HEAD --stat                  # what this fork changed since it
git diff upstream/master HEAD -- src/common    # the fork against upstream's tip, one path
git cherry -v HEAD upstream/master             # upstream commits not yet picked
```

If upstream's default branch has another name than `master`, use that name.
