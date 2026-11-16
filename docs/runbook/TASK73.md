# What the synthetic test established (2026-11-13)

A generated test pattern (colour bars, a moving square, frame numbers,
no game content; `scripts/pins.sh evidence --synthetic`) was published
through `evidence-publish` to `pr-evidence`: a 320x240 PNG still
(1478 bytes), a 24-frame animated PNG (49870 bytes) and the same clip as
GIF (113219 bytes).

1. GitHub's Markdown renderer (`POST /markdown`, mode `gfm`, context this
   repository, the same pipeline as pull-request bodies) keeps the
   `<img>` for all three and leaves the URL as is:
   `https://github.com/<owner>/<repo>/releases/download/pr-evidence/<name>`.
   It does not route it through the image proxy (`camo`), because the
   URL is on github.com. The GIF's `<img>` gets a `data-animated-image`
   attribute (GitHub's play and pause control); the animated PNG's does
   not, because GitHub recognises animation by the `.gif` extension.
2. Fetching that URL gives `302` to `release-assets.githubusercontent.com`
   and then `200` with the exact bytes uploaded (SHA-256 equal to the
   manifest for every file). The final response is served as
   `content-type: application/octet-stream` with
   `content-disposition: attachment`, whatever type the asset was
   uploaded with (the API lists `image/png` and `image/gif`), and
   without `x-content-type-options: nosniff`. The github.com page's
   Content-Security-Policy lists `release-assets.githubusercontent.com`
   under `img-src`, so the page allows images from there.
3. Size: an animated PNG of random noise of 3753869 bytes and one of
   9760144 bytes were published the same way, rendered to an `<img>` by
   the Markdown API and fetched back whole (status 200, digest equal),
   then deleted. No limit was met up to 9.76 MB: release assets are not
   proxied, so the proxy's limits do not apply; the evidence budget
   stays at 4 MB per file to keep a pull-request page light.

Not established, because no browser was driven: that a browser draws
the `application/octet-stream` response inside the `<img>` (browsers
determine an image's type from its bytes there, and nothing in the
headers forbids it), and that the animated PNG animates inline. The
GIF of every clip is published next to it, and the printed section
links it, so the owner can compare the two in the first pull request
and drop the one that does not animate. If release images turn out not
to render at all, the fallback is a branch of images referenced by
commit through `raw.githubusercontent.com`; it was not taken, because
anything committed stays in the history of a public repository, while a
release asset can be deleted.

# First real run (TASK73)

`world` on `soft`, reference `598e4654575f-2420249005` (run 1) and
candidate `598e4654575f-2420249005-r2` (run 2), both from one commit, so
every score is 0 and the difference panel is black: a still of
1928x500 (192319 bytes, full size), an animated PNG of 23 frames at
968x260 (1281289 bytes, half size) and the GIF (689057 bytes, half
size), all within the 4 MB budget at the first size tried. On the build
VM, loaded by other work at the time, the encoding took about three
minutes, most of it in the pure-Python GIF encoder. Published and
fetched back byte for byte.
