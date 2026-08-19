# Known gaps, 2026-08-20

Written at the end of the now-playing page's bring-up. Everything below is
either shipped-but-unverified, still broken, or a pattern worth carrying into
the next piece of work.

## Shipped but never seen on a panel

These are on `main`, pass the host tests, and build clean. **No human has
watched them run.** They are listed separately from the verified work because
"the tests are green" has already been wrong five times on this feature.

- **Locally derived elapsed time.** The page counts up on the board's clock
  between the sender's infrequent progress messages, re-anchoring whenever one
  arrives and freezing while paused. The arithmetic has host tests; the
  behaviour on a real session does not.
- **The once-a-second republish from `feed_audio()`.** The design reasoning is
  in the code: `audio_stream_write()` runs first and unlocked, the common path
  is one timestamp comparison with no lock, and `net_log`'s own contract says
  it never blocks. **That is reasoning, not measurement.** If audio write gaps
  grow, this is the first thing to suspect, and the fix is to move the
  republish to an `esp_timer` task off the audio path.
- **The sender's name on screen.** Reads the mDNS hostname with hyphens turned
  into spaces.

## Still broken

- **Artwork never arrives.** `esp-raop-receiver`'s `util.c` drops any HTTP body
  over 8192 bytes - it reads the remainder to nowhere and sets `*body = NULL` -
  so `raop.c`'s artwork branch never calls `cmd_cb` and `RAOP_EVENT_ARTWORK`
  has not fired once. Confirmed on hardware:
  `SET_PARAMETER content-type='image/jpeg' body=NULL len=0`.

  Raising that ceiling is not sufficient on its own: `util.c` mallocs the body
  from internal RAM, which measured 29,807 bytes free with a **5,120 byte
  largest block** during a TLS fetch. A JPEG-sized contiguous internal
  allocation would fail intermittently rather than cleanly, which is the worst
  of the three outcomes. Source it from PSRAM.

- **A module cannot be told to quiesce before an OTA write.** Recorded in
  `2026-08-20-quiescing-modules-before-an-ota-write.md`. Pushing firmware
  during playback produces loud speaker noise and, once, a failed push.

## The pattern worth carrying forward: these bugs were silent, not intermittent

Six defects were found in one day across this feature and the audio work
running alongside it. **Not one of them would have been caught by writing more
tests**, because in every case there was nothing to assert on - the system
produced no output distinguishing right from wrong.

| Defect | Why it was invisible |
| --- | --- |
| `!dmap_parse(...)` inverted | A discarded parse and a successful one produce identical silence |
| DMAP tag passed as `NULL` | The handler dropped every string; no error path exists for "tag unknown" |
| Sender does not push progress | A frozen number looks exactly like a number that has not changed yet |
| OTA write during playback | No instrument was ever pointed at the speaker during a write |
| `worst_error` never reset | An all-time maximum reads as a trend when compared across captures |
| `raop_create`'s log wording | Post-allocation figures read as a live allocation failure |

Two of these cost a wrong conclusion that was acted on: one session had a
change queued to spend 13.5 KB of permanent `.bss` fixing a working path, and
this one blamed two different AirPlay senders for a receiver-side bug across
three separate testing rounds.

What actually found them, in every case, was **adding an instrument that did
not exist yet** - a log line at a decision point, printing the value the code
was branching on. The `SET_PARAMETER` entry log found two of the six on its
first run.

The practical rule this suggests: when behaviour is wrong and the code reads
correct, stop re-reading the code. Print what the branch actually saw.

## Method notes for the next feature

- **The panel's own `ui_geometry` check caught a layout bug the tests passed
  through.** The tests compared absolute layout coordinates against an
  absolute content box, which is not what the renderer does - it positions
  children relative to a parent already at the canvas origin. Layout tests
  should assert *origin-invariance*, and the replacements here were verified
  by deliberately breaking the translation and confirming they fail.
- **Build the firmware every task, not at the end.** Two defects reached
  commits by passing host tests and failing only the firmware build: a `switch`
  over `PageId` in a file host tests never compile, and `%u` against a
  `uint32_t` that is `unsigned int` on the host and `unsigned long` on xtensa.
- **`remote.sh logs N` takes seconds, not lines**, and streams live rather
  than replaying history. Three capture attempts were wasted before that was
  noticed. Start the capture while a session is already playing.
