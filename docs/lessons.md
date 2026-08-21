# Lessons log

What has bitten this project, why, and the rule each bite taught. The
full narratives live in the commit messages (keep writing them there —
that practice is the best thing this repo does); this file is the index
that makes them findable without `git log -p`, so the same mistake
doesn't need to be paid for twice.

**When to add an entry:** any fix whose commit message tells a
root-cause story, any comb finding (see
[observability.md](observability.md)) that turned into an "oh, *that's*
why", any physical-hardware surprise. Format:

```
## YYYY-MM-DD · Title that names the mistake
What happened · Root cause · The rule now · Guards (commits, tests, fixtures) · Watch for
```

Keep entries under ~12 lines. If a guard doesn't exist yet, say so and
point at the backlog item.

---

## 2026-08-20 · agent-status stack overflow before first paint of data

**What happened:** Mini UI came back, then rebooted as soon as Wi-Fi
connected. Serial: `stack overflow in task agent-status` right after
`agentstatuspollning startad`.
**Root cause:** Grok added a third `tk_agent_snapshot` provider (four jobs
each). The parse result lived on the 6144-byte task stack next to cJSON
and the HTTP client. Overflow → reboot loop → dashes forever.
**The rule:** parsed snapshots and token/tracker structs stay in `.bss`
(`static`), same as the HTTP body. Do not put a third provider on a 6 KB
stack. Any `TK_AGENT_PROVIDER_COUNT` array must list every provider or it
NULLs on apply (`LoadProhibited` in `bounded_job_count`).
**Guards:** `test_agent_net_wiring.py` requires `static tk_agent_snapshot`
and an 8192-byte task. **Watch for:** a fourth provider, or a two-slot
`providers[]` initializer left behind.

## 2026-08-20 · Mini white screen from 420 tracker cells

**What happened:** after the Grok flash the T-Display-S3 sat on a white
ST7789. Serial showed `task_wdt` on `main` / IDLE0, backtrace in
`usage_screen_create` → `lv_obj_create` / `theme_apply`.
**Root cause:** three Max Tracker pages × 140 `lv_obj` cells (plus theme
apply on each) ran under the LVGL lock on `app_main`, so the idle task
never fed the watchdog. Backlight was already on, panel never painted.
**The rule:** Mini heatmaps use the AMOLED pattern — one widget, rects in
`LV_EVENT_DRAW_MAIN`. Never one LVGL object per cell on 320×170.
**Guards:** `test_tdisplay_screens.py` forbids `cells[TK_MT_DAYS]` and
requires `tracker_grid_draw`. **Watch for:** any new per-day widget.

## 2026-08-20 · Max Tracker snapshot dropped Grok

**What happened:** `/api/tokens` had `grokDayTokens` but `/api/max-tracker`
had no `grok` key, so the Mini heatmap stayed empty.
**Root cause:** `observe_volume("grok")` wrote `EXTRA_PROVIDERS` state, then
`MaxTrackerStore.snapshot` copied only `PROVIDERS` (`claude`, `codex`). A
second bug would have stacked the same Grok daily total on every glance
because those numbers are already summed, not incremental log lines.
**The rule:** extra providers must ride the snapshot copy, and already-summed
totals use `set_volume`, never `observe_volume`.
**Guards:** `test/test_grok_usage.py` (`set_volume` does not stack; tokenserver
calls `set_volume("grok")`). **Watch for:** adding a fourth provider and
forgetting `EXTRA_PROVIDERS` in snapshot/prune.

## 2026-08-15 · kickstart restarts the process, not the plist

**What happened:** the tokenserver kept dying with `unrecognized arguments:
--plan claude=100`, restart after restart, taking the whole panel down.
**Root cause:** the running launchd process was pre-GitHub code that predated a
CLI change; editing the plist and running `launchctl kickstart -k` restarted it
with launchd's *cached* ProgramArguments, never re-reading the edited file.
**The rule:** `kickstart -k` is for code changes (same args, updated
`tokenserver.py`); argument/plist edits need `bootout` + `bootstrap`.
**Guards:** none yet — the plist story lives in
[github-pulse.md](github-pulse.md); CLAUDE.md's OTA note still only mentions
kickstart. **Watch for:** any plist edit that "doesn't take" after a restart.

## 2026-08-15 · An optional feature was enabled in the wrong layer

**What happened:** the GitHub screen rendered in QA but never on the glass, then
came up with no data. **Root cause:** two separate opt-ins were missing —
`TK_GITHUB_SCREEN_ENABLED` (the view compiled out) and `TK_GITHUB_URL` (the poll
task compiled out). Both belong in the per-install `secrets.h` (see
`secrets.h.example`), but the screen flag was fixed by hardcoding it into
`components/app_tokens/CMakeLists.txt` (`b5c5a7a`) instead. **The rule:** enable
per-install feature flags where the design put them — local `secrets.h`, not the
shared firmware CMake; a build define collides with a template-based secrets.h
and silently wins the wrong way. **Guards:** `secrets.h.example` already carries
the full block; cleanup tracked in
[github-pulse.md](github-pulse.md#known-follow-ups). **Watch for:** any
`target_compile_definitions` that duplicates a `#ifndef`-guarded config default.

## 2026-08-13 · The expired token that outranked a fresh login

**What happened:** the screen sat on `usage_http_401` for hours after a
perfectly good re-login. **Root cause:** the probe read the OAuth token
from a running process's environment — which reflects launch time, not
now. A Claude Desktop child process outlived its token and kept
"winning" over the fresh keychain credential. A second bug compounded
it: the probe demanded an active session window, so between windows it
discarded valid weekly data and reported the *fallback's* 401 as the
whole story — blaming auth while auth was fine. **The rule:** report
the status of the source that actually decided, and never require more
data than the answer needs. **Guards:** keychain fallback + candidate
ordering (`7d213ec`), probe succeeds without a session window
(`8d3b4b3`), runbook row updated (`2002725`). **Watch for:** any new
credential source silently outranking a fresher one.

## 2026-08-13 · The 429 night: the probe fed its own penalty

**What happened:** a debugging evening ended rate-limited, repeatedly.
**Root cause:** on 429 the probe fell through to the header fallback
(one more request) and retried the full multi-request cycle two minutes
later — each retry extending the penalty it was caught in. A dead token
had the same shape: two doomed requests every 120 s for hours. **The
rule: failure must slow you down.** Every poller needs backoff, and a
rate-limit response is an instruction, not an error to retry. **Guards:**
10-min cooldown honoring `Retry-After`, cycle aborts on 429
(`c5510b5`); failure-streak slowdown 120→240→480 s (`8f6b8bd`); tests in
`test_tokenserver.py`. **Watch for:** the firmware pollers, which never
got this medicine — fixed cadences, agent-status at 1 Hz (OBS-13). Also:
don't restart the server to "fix" a 429 — that resets the cooldown and
repeats the mistake.

## 2026-08-13 · A stale worktree served old code for an hour

**What happened:** an hour of process archaeology because the launchd
service was quietly running from a different checkout than the one being
edited. **Root cause:** the plist hardcodes its `WorkingDirectory`; no
artifact said which code was live. **The rule:** every long-running
artifact must be able to answer "what revision are you?" in one command.
**Guards:** `GET /` reports `rev` + `startedAt` (`8f6b8bd`) and a
startup `srcFingerprint` (content hash — catches dirty worktrees and
post-start edits that share HEAD with the checkout); the smoke test
compares both against your checkout; the firmware boot banner logs its
version and reset reason (OBS-01, done). **Watch for:** the plist's
hardcoded `WorkingDirectory` is still the root cause waiting to recur —
the guards make it visible, not impossible.

## 2026-08-13 · Stale data replayed as breaking news

**What happened:** after a tokenserver outage, the revived agent feed's
hours-old "waiting for you" states each seized the whole screen as
full-screen alerts. Separately, stale waiting/error records displaced
newly observed work. **Root cause:** alerting and eviction logic trusted
*content* without checking *age*. **The rule:** freshness is part of the
data, and anything that interrupts the user must prove it first.
**Guards:** alerts gated on freshness incl. post-boot (`89f161f`),
cached quotas rendered stale (`ca55c30`), expired records evicted first
(`ef85bf3`). **Watch for:** the device's freshness clock is fed by one
endpoint only — other feeds can still show `LIVE` while dead (OBS-09).

## 2026-08-13 · Upstream data is hostile: the NUL-truncation escalation

**What happened:** four commits in one arc as each fix revealed the next
hole: quota labels arriving NUL-truncated could confuse parsing, then
detection, then forecast trust, then key matching. **Root cause:** the
JSONL and API payloads are *someone else's* output format, unversioned,
and they change and break mid-write; anything not explicitly validated
will eventually lie. **The rule:** parsers are contract-strict — reject
the whole payload on any violation, keep last-known-good, and when a
value can't be trusted, don't display a "probably". **Guards:**
`bbcd5ec` → `254f774` → `3d3825c` → `1d371dc`; empty Codex limit names
(`fa44802`); named vs general quota separation so Spark can never
replace WEEK (`0a0e98e`); hostile-input C tests in `test/test_tokens.c`.
**Watch for:** strictness without diagnostics — a rejection today logs
nothing about *what* offended (OBS-22).

## 2026-08-13 · One byte over budget froze the display

**What happened:** the screen went permanently stale against a healthy
server. **Root cause:** the worst-case v2 payload was 1 058 bytes; the
firmware's all-or-nothing body cap was 1 024. Nobody had ever computed
the worst case. **The rule:** an all-or-nothing contract needs a
capacity gate — a test that constructs the worst-case payload and proves
it fits, so growth fails in CI, not on the shelf. **Guards:** headroom
raised + capacity gate (`c0016d9`, `a90b6f2`,
`test/test_token_body_capacity.py`). **Watch for:** adding any payload
field without touching the capacity test.

## 2026-08-13 · Round before you serialize

**What happened:** the device rejected entire max-tracker responses.
**Root cause:** the server emitted float day-peaks; the device parses
them into `int8_t` and — correctly, per contract — rejected the whole
payload. Truth must be shaped *before* the wire, not after. **Guards:**
server rounds day peaks (`97dd531`); the recorded-live-shape fixture
`sim-fixtures/max-tracker-live-shape.json` locks the real contract into
the sim and tests. **The rule:** when a bug comes from real recorded
data, freeze that data as a fixture forever.

## 2026-08-13 · The log-tail state machine took four rounds

**What happened:** the max-tracker backfill (offsets, watermarks,
carried lines) was "fixed" four times; round 2's fix was rejected by two
new empirical repros. Failure modes included permanent starvation
(oversized line never consumed, offset frozen) and double-counting
(keying on `(inode, size)`). **The rule:** incremental-file-reader state
is the hardest state in this codebase — every claimed fix needs a
reproducing test *before* the fix, and reviewers should assume the next
hole exists. **Guards:** `6e8ebba`/`733bf9c`, `c5eae88`, `09badc2`,
`c7c95e5` → `4a5d761`, each with its repro test. **Watch for:** the
backfill loop still swallows runtime exceptions at 2 Hz (OBS-19).

## 2026-08-13 · Threads shared a list without a story

**What happened:** `ThreadingHTTPServer` request threads shared the
usage-history store with unguarded swap/sort/rollback. **The rule:**
every store touched by the HTTP threads needs an explicit lock story,
and slow work (disk, recompute) moves off the request path. **Guards:**
thread-safe history (`6966957`), nonblocking cache reads (`c0fddcf`),
async persistence (`4041f16`). **Watch for:** `_probe_status` is still
read torn-able without its lock (OBS-18c).

## 2026-08-13 · Inverted defaults shipped a stranger someone else's app

**What happened:** an outside user flashed "VibePulse" and got Swedish
electricity prices — and their board began polling the maintainer's
other project's website every 30 s (~2 880 req/day per device). **Root
cause:** companion apps were opt-out instead of opt-in; the governance
test passed both ways, so it guarded nothing. **The rule: the defaults
are the product.** A fresh clone must build exactly one app, and any
test guarding a default must *fail* on the harmful configuration —
tighten the test in the same commit as the fix. **Guards:** `57e00ba`
(defaults flipped + registry test tightened), `2ec791d` (the two-pass
CMake guard divergence that made "macro on, include dir off" possible).
**Watch for:** any new build-time gate needs both halves guarded — the
compile definition *and* the sources it implies.

## 2026-08-13 · A computer USB port cannot run this panel

**What happened:** flashing worked but the running board bounced off the
USB bus or hung; it looked exactly like a flaky cable or bad firmware.
**Root cause:** the AMOLED draw exceeds what a computer port reliably
supplies. **The rule:** flash in download mode (screen dark), run from a
dedicated PSU; interpret enumeration bouncing as a power symptom first.
**Guards:** documented in `docs/agent-setup.md` and README
(`9ddf387`); full narrative in
`docs/superpowers/reviews/2026-08-13-max-tracker-physical-static.md`.
**Watch for:** serial-monitoring a *running* board is still unverified —
it needs a powered hub or PSU/data split, which also caps how much the
firmware log can help until solved.

## 2026-08-13 · CI's fresh VM exposed a boot-blind throttle

**What happened:** the first CI run of the new logging failed a test
that passed everywhere locally: the throttled save-error log never
fired. Codex's PR review flagged the same line independently. **Root
cause:** the throttle used `0.0` as "long ago" with `time.monotonic()`
— which counts from *boot*. On any machine up less than the 5-minute
window (CI VMs always; a Mac right after restart), `now - 0.0` never
reaches the threshold, so the **first error after boot is exactly the
one that gets swallowed** — in production, not just in tests. **The
rule:** "never happened yet" is a state, not a timestamp zero; with
monotonic clocks use a `None` sentinel, because monotonic's epoch is
arbitrary and *recent* on fresh machines. **Guards:** `None` sentinels
in both error-log throttles; the writer test now exercises the
first-error path on CI's short-uptime VMs by construction. **Watch
for:** any new `last_*_logged` / `last_*_at` throttle seeded with `0.0`.

## 2026-08-13 · Docs drifted from code five times in two days

**What happened:** five separate correction commits: setup steps telling
users to uncomment a block that ships active (at the most expensive
possible step), a README advertising a page that didn't exist, an
under-claimed privacy surface, stale AGENTS.md claims confusing a
stranger's agent, and a verification step that "always passes and proves
nothing". **Root cause:** prose claims have no failure mode — nothing
red happens when they rot. **The rule:** prefer doc claims a command can
verify; when code changes behavior, grep the docs for the old claim in
the same commit; a verification that cannot fail is not a verification.
**Guards:** `439481a`, `3e2eb3c`, `289fea1`, `e353371`, `668f4c0`; the
two doc-content tests (`test_shared_amoled_skill.py`,
`tools/test_hardware_registry.py`) are the only mechanical ones — and
`design-qa.md` has already drifted again (OBS-26). **Watch for:** every
new doc in this observability set is prose too; comb step 7 includes
checking that these files still tell the truth.
