# ESP32 Bluetooth WAV Player — Handoff Spec

**Current version:** v1.10.4 · file `Working_No_Speaker_v1_10_4.ino` (~4320 lines, single sketch)
**Size:** 1,372,769 B app (43.6% of a 3 MB OTA slot), 56,220 B globals.
**Checker:** `verify_sketch.py` — run it after every bulk edit; it encodes the failure classes below.
**Status:** Bluetooth connect/switch path verified working against serial log. v1.9.3–v1.9.5 changed **no Bluetooth behaviour** except the CoD mask in our own scan filter and the removal of `end()` from deep sleep. See "Open Issues" for what is *not* resolved.

**Sketch folder must match the filename** — it is renamed on every version bump.

This document exists because several bugs in this project took multiple wrong attempts to fix, and the reasons are **not visible from reading the code**. Sections marked ⚠️ record failures that looked correct and were not. Read those before touching Bluetooth.

---

## 1. Hardware

| Item | Detail |
|---|---|
| MCU | ESP32-WROVER-E, 8MB flash, 4MB PSRAM (**PSRAM required**) |
| Board FQBN | `esp32:esp32:esp32wroverkit`, PSRAM=enabled. ⚠️ **Partition table now comes from `partitions.csv` in the sketch folder** (two 3 MB OTA slots), which overrides the Tools menu. `huge_app` has no second app slot and makes SD update impossible. |
| Toolchain | ESP-IDF v5.5.4 / Arduino-ESP32 3.3.10 |
| Display | 2.9" e-paper 296×152, GxEPD2, HSPI: SCK=14 MOSI=13 CS=15 DC=26 RST=4 BUSY=27 |
| SD | VSPI: SCK=18 MISO=19 MOSI=23 CS=5, 10 MHz |
| Audio | Bluetooth A2DP **source** via pschatzmann `ESP32-A2DP` (`BluetoothA2DPSource`) |

### Buttons — do not "fix" these
```
GPIO 32(1) 33(2) 36(3) 39(4) 34(5) 35(Home)   KCOM -> GND
```
⚠️ **This pinout is dictated by the user's PCB layout and is not negotiable.** An earlier attempt "corrected" it to match a stale header comment and was rejected. GPIO 34/35/36/39 are **input-only** — they have no internal pull-up silicon, so `INPUT_PULLUP` is a silent no-op on them and they rely on external pull-ups. `initButtons()` handles this via a `p>=34 && p<=39` test. The deep-sleep `ext0` wake pin **must** stay in sync with `BTN_PINS[4]` (GPIO34).

---

## 2. Architecture

**Three execution contexts. Know which one you are in.**

| Task | Core | Purpose |
|---|---|---|
| Arduino `loop()` | 1 | UI, menus, e-paper, all button handling |
| `audioFillTask` | 1 | SD → 512 KB PSRAM ring buffer, prio 2 |
| `guardTask` | 0 | **Reset combo + loop-stall watchdog**, prio 1 |
| (library) BT app task | — | A2DP/AVRC/GAP callbacks; `get_audio_data` pulls from ring |

- `SdLock` = recursive mutex around **all** SD access. `audioFillTask` and `loop()` both take it.
- PSRAM: 512 KB ring buffer + 2888 B artwork buffer, both `ps_malloc`.
- Internal heap after boot is ~100 KB. Watch it; BT start alone costs ~80 KB.

### guardTask — why it exists
⚠️ v1.8 polled the 1+Home reset combo from `readButton()` and from a `busyDelay()` helper — **both on the loop task**. When a library call deadlocked the loop task, the combo stopped being polled with it, i.e. it failed in exactly the scenario it was built for. It now runs on its own task and also reboots if `loopHeartbeat` stops advancing for 45 s. Do not move combo polling back onto the loop task.

---

## 3. SD card layout

| File | Purpose |
|---|---|
| ~~`/playlist.txt`~~ | ⚠️ **Does not exist.** No code path opens it — verified v1.10.3. The "playlist" is reservoir-sampled from `catalog.txt` at boot, `cfg.playlistSize` entries, then Fisher-Yates shuffled. This row was wrong in the original spec. |
| `/catalog.txt` | Up to 2000 song paths. ⚠️ **Lines ≥ `MAX_PATH_LEN` are dropped** by both `loadPlaylist()` and `buildCatalogIndex()` — the song exists on the card but is invisible to the player. The limit was **128** through v1.10.3 and the skip was **silent**; v1.10.4 raises it to **256** and logs `[PLAYLIST] SKIPPED (n chars…)`. If you are chasing "a song that never plays", check this first. |
| `/settings.txt` | `key=value`, includes `vol_<devicename>=NN` per-device volume |
| `/btdevices.txt` | Up to 8 saved BT device names, most-recent-first |
| `/session.txt` | v1.9.4 deep-sleep session: queue, index, resume offset. Written on deep sleep, read **only** on ext0 wake, deleted after restore |
| `/Sleep.bmp`, `/DeepSleep.bmp` | v1.9.8 full-screen sleep backgrounds, 296×152 1-bit. Optional — missing/invalid falls back to plain black |
| `/firmware.bin` | v1.10.0 firmware image. Offered at boot, exact filename only, deleted after a successful install |
| `/firmware.md5` | Optional checksum for the above. Absence is warned about, not silent |

Audio is **44.1 kHz 16-bit MONO WAV only** (`BYTES_PER_SEC = 44100*2 = 88200`). No decoding.

⚠️ **This line said "stereo" until v1.10.3 and was wrong.** `get_audio_data()` pops `want*2` bytes and copies ONE 16-bit sample to both output channels, and 88200 B/s is mono 16-bit — stereo would be 176400. The preprocessing script's `ffmpeg -ac 1` is correct; "fixing" it to stereo per the old wording makes the player read L,R,L,R as consecutive mono samples, i.e. double speed and wrong pitch. A PCM frame here is **2 bytes**, not 4 (the session-resume comment in the sketch still says 4 — the `&~3u` mask is a safe superset of 2-byte alignment, so the code is fine and only the comment is wrong).

---

## 4. Bluetooth — READ THIS SECTION BEFORE CHANGING ANYTHING

Five separate bugs lived here. The library's behaviour is counter-intuitive in ways that cost several iterations. The rules below are all **empirically derived from serial logs**, not from documentation.

### Rule 1 — Never call `a2dp_source.end()`
⚠️ `end(false)` **deadlocks**. Log evidence: output stops dead at `[BT] step: end`, no `step: start`, no `step: done`, while the BT app task keeps dispatching `evt 0xff00` every 10 s forever. `end()` waits for a task shutdown that never completes. This froze the UI *and* killed the reset combo.
**There is no need to restart the stack to change devices.** `start()` once in `setup()`, never again.

### Rule 2 — The library only discovers while DISCONNECTED
⚠️ Once a link is up, every `0xff00` heartbeat routes to `bt_app_av_state_connected_hdlr` ("media ready checking") and **no `DISC_RES` event ever arrives again**. A device scan that passively listens to the library's discovery therefore returns zero results while connected. v1.9 shipped exactly that bug.
→ The scan screen **must run its own** `esp_bt_gap_start_discovery()`.

### Rule 3 — If you borrow the GAP callback, you must give it back
⚠️ `esp_bt_gap_register_callback(gapCallback)` **replaces** the library's handler. v1.8 never restored it, so after one scan the library was permanently blind — it logged "Discovery started" forever while structurally incapable of seeing a result. Log tell: **zero `ccall_app_gap_callback` lines** after the first `[SCAN]`.
→ `startScan()`/`endScan()` are a matched pair. `endScan()` re-registers `ccall_app_gap_callback` (declared `extern "C"` in the sketch). Every exit path from the scan screen routes through `endScan()`; the loop releases GAP as soon as the inquiry ends rather than waiting for the user to press Home.

### Rule 4 — Do not cancel an inquiry that already ended
⚠️ Our inquiry normally ends by itself (`gapCallback` logs "Discovery stopped"). A redundant `esp_bt_gap_cancel_discovery()` afterwards emitted a second GAP event that the just-restored library callback read as *"Device discovery failed, continue to discover..."* — so it began discovering **while connected**, found another saved device, and **silently switched the user's audio to it**. `endScan()` now cancels only if `!scanDone`.

### Rule 5 — The library announces scanning it never performs
⚠️ After a disconnect it prints, every 10 s:
```
handle_reconnect_logic: Auto-reconnect disabled, not attempting to reconnect
Heartbeat: reconnect retries exhausted, fallback to scanning
```
…and **does not start an inquiry**. Observed four times over 40 s. Any design that waits for the library to reconnect will hang.
→ `MS_BT_CONNECTING` **drives** it: `esp_bt_gap_start_discovery()` every 12 s, max 4 tries, screen counts `(n/4)`. We deliberately use the library's own GAP→`ssid_callback`→`esp_a2d_connect` path rather than calling `esp_a2d_source_connect()` ourselves, so its internal peer address stays correct for later `disconnect()`.

### Rule 6 — `onSsidFound()` is the connection gate
```
btTargetName set   -> accept ONLY that name  ("Accepting target: X")
btTargetName empty -> accept a saved device ONLY if not already connected
already connected  -> accept nothing
```
⚠️ Without the last clause the library will abandon a working link for whichever other saved device its next inquiry sees first.

### Rule 7 — Pointer aliasing in `connectToDevice()`
Callers pass `&savedDevices[i]` or `&scanNames[i]`. `addSavedDevice()` `memcpy`s a rebuilt list **over** `savedDevices`, so any later read of that pointer returns a *different device's* string. Copy into `btTargetName[]` **first**. The same buffer is what gets handed to the library, because `BluetoothA2DPSource` keeps the **pointers** from the name vector, not copies of the strings.

Also: capture `btPrevDevice` **before** overwriting `connectedDevice`, or the "am I already on this device?" test compares the target against itself and always disconnects. (That bug once killed an in-flight connect to the very device the user had just picked.)

### Verified-good sequence (v1.9.2 log)
```
[SCAN] Starting BT discovery
[SCAN] Found: JBL Tune 720BT (CoD=0x240404)
[SCAN] Discovery stopped
[SCAN] GAP returned to library
[BT] Target set: JBL Tune 720BT
[BT] Discovery kick 1/4 for JBL Tune 720BT
[BT] Accepting target: JBL Tune 720BT
[BT] Connected
[BT] Connected to JBL Tune 720BT after 1 try
```
Device→device switching (JBL→C17A) also verified, with playback resuming (`[PROG] 40% (69s)`).

---

## 5. Display / e-paper

**Panel refreshes are the scarce resource.** A full refresh is ~4 s and causes wear/ghosting. Design decisions follow from this.

- **Partial window is `y 102..128` only** — progress bar (104–117) and the track/volume line (121–128). The two hint lines (130–137, 140–147) sit **outside** it and are painted once per full redraw. ⚠️ v1.7 had them inside, so every progress tick erased and re-inked two identical lines of static text.
- `bandFit()` centres the 8 px built-in font in a 10 px band → glyphs at `bandY+1 .. bandY+8`. Verify geometry arithmetically, don't eyeball it.
- **Measure-then-constrain**: when two strings share a line, draw the right-aligned one first, capture its width, cap the left one to the remainder (`drawHeader`/`drawFooter` pattern). Giving both the full column width is what let v1.6's header pairs overlap.
- **Two image paths, two different formats — don't confuse them.** `loadScreenBmp()` (sleep backgrounds) parses **real BMP**: honours the data offset at byte 10, the 4-byte row padding (296 px = 37 bytes → 40 stride), bottom-up row order for positive height, and palette polarity (most editors write `palette[0]=black`, so bit 1 = *white*, the opposite of Adafruit's "1 = ink"). `loadArtwork()` (per-song art) does **none** of that — it reads `ART_BYTES` of raw bytes from a file that merely has a `.bmp` extension. ⚠️ If the per-song art files are real BMPs, the header has been rendering as garbage across the top rows all along. Unverified.
- Sleep screens draw SLEEP / DEEP SLEEP (band 26–58) and the wake hint (132–144) over the background, each on a white knock-out box **sized to the measured glyph ink** — `getTextBounds` at the exact draw cursor, padded 4×3. ⚠️ Don't size the box from the band: the band fixes the *baseline* (measured from a reference string) and is far taller than the ink, which is how v1.9.9 ended up with a box twice the height of the letters — same measure-then-constrain rule as `drawHeader`/`drawFooter`. Band geometry (`SLEEP_TITLE_Y/H`, `SLEEP_HINT_Y/H`) is shared by the image path and the plain-black fallback so they can't drift.
- Sleep backgrounds are **preloaded at boot**, not read on demand: `drawSleepScreen()` runs inside `enterDeepSleep()`, exactly when the SD may be wedged. 11.2 KB of PSRAM buys immunity from taking `SdLock` there.
- `GHOST_CLEAR_AFTER = 12` partial updates forces a full redraw. ⚠️ Do **not** "tune" this down. `drawNowPlaying()` is itself a full refresh and resets `partialRefreshCount` on every song change, and a song produces only ~10 progress buckets, so the counter rarely reaches 12 — ghosting is already cleared by the song change. Lowering it *adds* a full refresh per song rather than saving one. (Proposed at 8 in an v1.9.5 review, then withdrawn on this reasoning.)
- AVRC play/pause from the phone forces one full redraw (`lastDrawnPaused`), because the PAUSED badge and hint 2 are outside the partial window.
- `display.init()` is the **last** thing `setup()` does. Boot faults before it cannot draw anything — `fatalHalt()` reports on serial and honours the reset combo instead.

---

## 6. Menu system

21 states in `enum MenuState`; `handleButton(int btn)` is one switch. `btn` is 1–5, `0` = short Home, and a long Home hold sets `homeHoldTriggered` (jump to Now Playing from anywhere).

### The BROWSE/SELECT rule
Lists longer than one page have a BROWSE layer (1/2 = page up/down, 5 = enter SELECT). **Lists that fit one page skip BROWSE entirely** — otherwise 1 and 2 look like row pickers but are dead paging buttons.

⚠️ The entry decision and the Home-back decision **must use the same predicate**, recomputed rather than cached, or the user lands on a one-page browse screen they never passed through. Helpers: `libTotalPages()`, `artTotalPages()`, `btTotalPages()`, `albumTotalPages()`, `artSongTotalPages()`.

### Single-item auto-advance
- Artist with 0 or 1 albums → skip the album screen (both rows resolve to the same song list).
- Search narrowing to exactly one artist → open it.
- Bisect group holding one letter → **type it immediately** (it's a keystroke, not a menu).
- **Deliberately NOT applied to a single song result.** Entering a screen is undoable with Home; starting playback replaces what's playing and isn't. Keep the confirming press unless the user asks otherwise.

Pagination: `PAGE=5`, `ALBUMS_PER_PAGE=4` (btn1 = "all songs"), `DEV_PER_PAGE=4` (btn5 = scan).

### Settings pages (rebuilt v1.9.3–v1.9.4)
Row indices are named constants (`SET_*`, `SET2_*`) because the draw code, the partial-row redraw and the button handler all have to agree on them.

| Page 1 | | Page 2 | |
|---|---|---|---|
| 1 | Volume | 1 | Sleep screen now |
| 2 | Playlist size | 2 | Deep sleep now |
| 3 | Volume step (5–20%) | 3 | Screen sleep after |
| 4 | Progress updates (5/10/20/25%) | 4 | Deep sleep after |
| 5 | More settings › | 5 | Deep sleep when (`BT down`/`Always`) |

- **Volume step** is read by *both* Vol+/Vol- call sites (Now Playing and the Volume row's own +/-). Change one and the setting becomes a lie on the other screen.
- **Progress updates** is the progress-bar bucket — how often a partial refresh is spent (20/song at 5%, 4/song at 25%). ⚠️ Values are restricted to divisors of 100: a step like 15 makes the last bucket 90, so the bar never reaches the end under partial refresh. `loop()` and `drawNowPlaying()` must read the same `cfg.progStep`, or the first partial update after a full redraw fires immediately.

- **H is DONE, not cancel.** Edits are live as made (`applyVolume()` has already gone to the sink), so H commits and `3` is the same commit saved immediately rather than on the 2 s deferred write. Every edit marks `settingsDirty`.
- **H on page 2 pops to page 1**, not to the main menu — which is what made a "< Back" row unnecessary.
- Both pages use a **partial single-row redraw** while editing. A full `drawSettings*()` per press costs ~4 s.
- Blank rows use `drawBlankSettingRow()`, not `drawListRow(i,"",…)`, which still prints the row number and invites a press that does nothing.

Search: 39-symbol alphabet, bisected 5 ways per press; word search filters catalog by substring.

---

## 7. Power

- **Screen sleep** — panel hibernates, any button wakes.
- **Deep sleep** — `esp_sleep_enable_ext0_wakeup(GPIO34, 0)`, i.e. button 5. GPIO34 is input-only so `rtc_gpio_pullup_en()` is a no-op there; the board's external pull-up defines the idle level.
- **Auto deep sleep** (v1.9.4) — `deep_sleep_timeout` (0 = off, 15–240 min, default 90) on the same `lastActivityMs` clock as the screen sleep, gated by `deep_sleep_mode`: `DSM_BT_DOWN` (only while no A2DP link) or `DSM_ALWAYS`. In ALWAYS mode the clock is **pure button inactivity — playback does not count as activity**, so it will sleep mid-album if untouched. Deliberate. Held off during `MS_BT_SCAN`, `MS_BT_CONNECTING` and `btConnectBusy`.
- ⚠️ **`enterDeepSleep()` must not call `end()`.** It did until v1.9.4 (`end(true)`), in violation of Rule 1. As a button press a deadlock there was survivable; on a timer it becomes a loop — wedge, `guardTask` reboots at 45 s, session restores, repeat. It is `disconnect()` now, and only when a link exists. `end()` was never needed: `esp_deep_sleep_start()` resets the SoC and powers the controller down anyway.
- **Session resume** (v1.9.4) — `saveSession()` writes `/session.txt` before sleeping, taking `sdMutex` with a **500 ms bound, not `SdLock`** (same rule as the reset combo: a wedged audio task must not hang the sleep path). `restoreSession()` runs only when `esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0`, so cold boot and 1+H still resample — that is the deliberate reshuffle gesture. Wake resumes **playing**.
  ⚠️ Two ways the resume offset is silently wrong, both handled, both easy to reintroduce: it must be **`playedBytes`, not `producedBytes`** (the producer runs ~512 KB / 6 s ahead — saving its count skips six seconds every resume), and it must be **masked to a 4-byte frame boundary** (16-bit stereo; land mid-frame and the channels stay swapped for the rest of the song).
- **Reset combo** — hold **button 1 + Home for 1.5 s**. Surfaced on the Settings page-2 "Back" row. Tries to unmount SD but waits max 300 ms for the mutex (a wedged audio task holding it is a likely *reason* to be resetting).
- Auto-sleep timeout configurable, 0 = off.

---

## 7a. Firmware update from SD (v1.10.0)

`/firmware.bin` at boot → confirm screen → **5 installs, Home skips, 30 s no-press skips**. Never silent.

- ⚠️ **Needs the OTA partition table.** `huge_app` is one app partition; an app cannot overwrite the flash it runs from, so `Update.begin()` fails and no code fixes it. `partitions.csv` gives two 3 MB slots on the 8 MB part. Changing it needs one erase-all USB flash — then turn erase-all **off** again or every upload wipes NVS and the per-device BT volumes.
- **Runs after the SD mount, before BT / `audioFillTask` / remaining PSRAM allocs.** Flash writes disable the instruction cache; doing that with the audio and A2DP tasks live, in a `-mfix-esp32-psram-cache-issue` build, is the hazard being avoided. This is also the **only** sanctioned exception to §8's "EPD init stays last" rule — an update reboots rather than continuing into bring-up, so the panel comes up early here, and `epdReady` stops `setup()` re-initialising it on the skip path.
- Checks in order: first byte `0xE9` (before `Update.begin()` erases anything), size ≥ 64 KB, then optional MD5 via `Update.setMD5()`. Writes go to the **inactive** slot, so any failure before commit leaves the running firmware bootable. Image deleted only after a successful `end()`, before restart — leave it and it re-offers every boot.
- ⚠️ **No bootloader rollback** (Arduino doesn't enable it). An image that crash-loops needs a USB reflash; 1+H won't help.
- ⚠️ **The compile output cannot confirm the partition table.** `huge_app` and the OTA table both report `Maximum is 3145728 bytes` — that number proves nothing. `logPartitionInfo()` reports the truth on every boot; look for `[OTA] Update slot 'app1' available … SD update ENABLED`. If it says `SD UPDATE UNAVAILABLE`, `partitions.csv` isn't in effect.
- ⚠️ **`initDisplayOnce()` is the only way the panel may be brought up.** It owns pin modes, `hspi.begin()`, `epd2.selectSPI()`, `init()` and `setRotation()`, and is idempotent. Calling `display.init()` on its own does *not* put the panel on HSPI — GxEPD2 defaults to the global `SPI` object, which is bound to the **SD card's VSPI bus**. v1.10.0 did exactly that in the update path, which also stranded the whole session's UI on the wrong bus whenever an update was skipped.
- Anything running in `setup()` is on loopTask's **8 KB stack**. The updater's 4 KB copy buffer is heap-allocated for this reason; don't turn it back into a local.

## 8. Open issues / not verified

**Closed since v1.9.2** (kept here because the reasoning matters more than the outcome):

1. ~~Volume-down not working~~ — **not a firmware fault.** Never reproduced in code; the up/down paths are arithmetically symmetric and share `applyVolume()`. Root cause was pressing **1 and 2 simultaneously**: `readButton()` scans buttons in pin order and returns the first one down, so 1+2 always reports 1, which on Now Playing is Vol+. No PCM scaling was added — AVRC absolute volume works with this sink. ⚠️ Do not re-open this hunt from the symptom alone; the scan-order note is in `readButton()`.
2. ~~`isAudioDevice()` CoD mask~~ — **fixed v1.9.5.** It masked `0x08` on the shifted service field = bit 16 = *Positioning*, not bit 21 = Audio (`0x100`). Hidden for two versions by the second clause: the JBL's `CoD=0x240404` has service field `0x120`, so the old test failed and only major-class-4 matched. A headset advertising Audio under a different major class was dropped from every scan.
3. ~~`while(1)` in `loadPlaylist()`~~ — **fixed v1.9.5**, now `fatalHalt()` (forward-declared, since the boot paths precede its definition).
4. ~~Three dead settings rows~~ — **fixed v1.9.3.** `shuffle` and `epd_refresh` had no consumers at all and were deleted; `playlist_size` was wired, which also required moving `loadSettings()` **before** `loadPlaylist()` — it had been running after, so `cfg` still held compiled defaults while the playlist was built.

**Still open:**

1. **`extern "C" void ccall_app_gap_callback(...)`** is declared in the sketch and re-registered by `endScan()`. It links against this library version. If a library upgrade breaks it, the fallback is to re-register via a library call instead.
2. **No auto-reconnect on a dropped link.** The library's is disabled and the discovery kick only runs on the CONNECTING screen. With auto deep sleep in `DSM_BT_DOWN` mode the device will now eventually sleep instead of sitting there decoding into nothing, but that is mitigation, not reconnection.
3. Boot faults (SD mount, empty playlist, playlist alloc) can't display anything because EPD init is last in `setup()`. Reordering was deliberately avoided — it moves PSRAM/BT/audio bring-up.
4. `SCAN_MAX = 5` devices per scan; results are name-only (CoD filtering happens in our `gapCallback`).
5. **The library holds pointers into `savedDevices[]`** from the `start()` call in `setup()`, and `sortConnectedFirst()` / `addSavedDevice()` rewrite that array in place. Investigated v1.9.3 and left alone: the array is static and always NUL-terminated so nothing dangles, and with `set_ssid_callback()` installed the vector is not what gates connections — `onSsidFound()` is. Revisit only if the callback is ever removed.
6. **Nothing here has been compiled.** v1.9.3–v1.9.5 were verified structurally only (delimiter balance, every `case MS_*` has a `break;`, every symbol defined or forward-declared before first use, arithmetic checked against the logged CoD). No toolchain was available.

## 9. Working agreement (from this project's history)

- **The user's PCB and hardware choices are ground truth.** If code and a comment disagree, ask — don't assume the comment is right.
- **Serial logs beat reasoning.** Every real fix here came from log evidence; every wrong fix came from plausible-sounding inference. Ask for a log before proposing a Bluetooth change.
- **Say what you did not fix.** Two rounds were wasted because a confident diagnosis (pointer aliasing) was real but was not the reported symptom's cause.
- **Verify structure after bulk edits**: brace/paren/bracket balance ignoring comments and string literals, every `case MS_*` has a `break;`, and every helper is defined or forward-declared before first use. Several ordering bugs were caught this way and one over-broad deletion was caught before it was written to disk.
- ⚠️ **Two ordering rules the compiler reports in misleading places. `verify_sketch.py` checks both — run it before flashing.**
  1. *File-scope constants.* A `static const` initialiser may only reference names declared **above** it. `BG_W = SCREEN_W` was written into the `ART_BYTES` block, 18 lines above `SCREEN_W`, and failed. Appending to a block that looks topically related is how this happens.
  2. *Prototype hoisting.* The Arduino builder generates prototypes and hoists them above the **first function definition** — so any sketch type named in a signature must be declared before that point. ⚠️ **The generator skips functions that have a default argument** (it can't legally repeat the default), which is the whole reason `bandFit()` and `drawInBand()` have always been safe taking `BandFit`. `drawSleepLabel()` had no default arg, got a prototype, and failed with `'BandFit' does not name a type` — pointing at a struct declared 700 lines above it. `static` does **not** exempt a function. `Metadata` and `BandFit` are both forward-declared at the top of the file as permanent defence; add to that list rather than reordering code.
- ⚠️ **Where a function is defined can break an unrelated file.** The Arduino builder generates a prototype for every **non-static** function in the .ino and inserts the block immediately before the **first function definition in the file**. Any type named in a non-static signature must therefore be declared before that first definition. In v1.9.6 a one-line helper added to the settings section became the first definition, landing 44 lines above `struct Metadata`, and the build failed with `'Metadata' has not been declared` pointing at `loadMetadata()` two thousand lines away — nothing was wrong at the named line. `struct Metadata` is now forward-declared at the top of the file as a permanent defence. `static` functions get no generated prototype, which is why `BandFit` and `TextAlign` have always been safe below the insertion point. **After adding any function, check that the first function definition in the file still sits below the type declarations.**
- ⚠️ **Also scan for a premature `*/` inside the header block comment.** A v1.9.6 changelog line containing the literal `SET_*/SET2_*` closed the header comment 423 lines early and turned the rest of it into code. Balance checking caught it only as a stray paren count; the direct check is "does `grep -n '\*/'` return anything before the header's real terminator".
- Changes carry a dated comment explaining *why*, referencing the log symptom where relevant. Keep this convention — it is why v1.9.2 was diagnosable at all.
