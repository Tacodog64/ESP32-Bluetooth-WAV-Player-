/*
 * ESP32 Bluetooth WAV Player - v1.10.3 (sleep screen tweaks)
 * =====================================================================
 * Hardware:
 *   ESP32-DevKitC-VE (WROVER-E, 8MB PSRAM required)
 *   SD:  VSPI  SCK=18 MISO=19 MOSI=23 CS=5
 *   EPD: HSPI  SCK=14 MOSI=13 CS=15 DC=26 RST=4 BUSY=27
 *   Buttons: GPIO 32(1) 33(2) 36(3) 39(4) 34(5) 35(H)  KCOM->GND
 *
 *   ** ADD BULK DECOUPLING ** 220-470uF + 0.1uF on EPD 3V3/GND,
 *   100uF + 0.1uF on SD 3V3/GND.
 *
 *   GPIO 32,33          -> INPUT_PULLUP (internal)
 *   GPIO 34,35,36,39     -> INPUT + external 10K to 3.3V (input-only pins,
 *                            no internal pull hardware on any of the four)
 *
 * v1.10.3 (2026-08-10): sleep-screen layout, for testing against the BMPs.
 *
 *   TITLE RAISED to the midpoint between its old position and the top edge:
 *   band 52..84 -> 26..58. Gap to the hint grows 48 px -> 74 px, 26 px of top
 *   clearance, no overlap, nothing off-panel. Shared by BOTH sleep screens so
 *   SLEEP and DEEP SLEEP stay aligned with each other.
 *
 *   KNOCK-OUT BOX NOW HUGS THE GLYPH INK rather than the band. It was
 *   fillRect(x0, bandY-1, bw, bandH+2) -- the full 32 px title band plus 2,
 *   around caps that are ~13 px tall. More than double the height of the
 *   letters, which over artwork reads as a floating white slab. The band's job
 *   is to fix the BASELINE (bandFit measures a REFERENCE string so every string
 *   in a band shares one height); it never claimed to describe how much space
 *   the ink occupies, and using it as if it did was the error.
 *
 *   Now the fitted string is measured at the exact cursor drawInBand will use,
 *   and the returned ink rectangle is padded by 4 px / 3 px. getTextBounds
 *   reports an absolute ink box in BOTH font modes, so the 12pt title and the
 *   5x7 hint need no per-font fudge. Title boxes drop to 55% of their old area
 *   (DEEP SLEEP: 158x34 -> 156x19); the hint box was already near-tight at 92%.
 *   bandY/bandH left drawSleepLabel's signature -- they are no longer used.
 *
 * v1.10.2 (2026-08-10): FIXES A BAD ONE IN v1.10.0/.1 -- the update screen was
 *   driving the e-paper over the SD CARD'S SPI BUS. Caught by re-reading the
 *   boot path, not by running it; it would not have failed at compile time.
 *
 *   checkSdFirmwareUpdate() called display.init() directly, but the EPD's pin
 *   setup, hspi.begin() and epd2.selectSPI() all still sat at the BOTTOM of
 *   setup(). GxEPD2 defaults to the global SPI object, and
 *   SPI.begin(SD_SCK,SD_MISO,SD_MOSI) had already bound that to VSPI for the
 *   card -- so the panel would have been clocked onto the SD's wires, with the
 *   card selected on the same bus, inside the one routine that then streams a
 *   firmware image off it. The two devices also disagree on clock and mode.
 *
 *   The larger half of the bug: epdReady=true made setup() SKIP the real init
 *   at the bottom. So merely LEAVING firmware.bin on the card and pressing H to
 *   skip would strand the ENTIRE session's UI on the wrong bus -- a fault in
 *   the update path leaking into normal operation, which is exactly the kind of
 *   blast radius this design was supposed to avoid.
 *
 *   Root cause was duplication: two places brought the display up and only one
 *   of them knew the full sequence. initDisplayOnce() now owns all of it -- pin
 *   modes, hspi.begin(), selectSPI(), init(), setRotation() -- and is
 *   idempotent via epdReady, so the early path and the normal path CANNOT
 *   differ. Same reasoning as settingsValue() and the SET_* row constants: when
 *   two call sites must agree, give them one implementation rather than trusting
 *   them to stay in step.
 *
 *   Also fixed a comment that said "No timeout" directly above a 30 s timeout.
 *
 * v1.10.1 (2026-08-10): two fixes to v1.10.0's updater, both found by reading
 *   it against the real build figures (1,372,769 B app / 56,220 B globals)
 *   rather than by running it.
 *
 *   THE COPY BUFFER WAS ON THE STACK. `uint8_t buf[4096]` as a local, in a
 *   function called from setup(), which runs on loopTask with an 8 KB stack --
 *   half the stack in one array, with the SD library's frames underneath it.
 *   A stack overflow there would land in the middle of the one operation in
 *   this firmware that can leave the device unbootable. Now malloc'd and freed;
 *   internal heap at that point is ~250 KB because BT has not started.
 *
 *   THE COMPILE OUTPUT CANNOT CONFIRM THE PARTITION TABLE. huge_app's single
 *   app partition and this table's OTA slots are both 0x300000, so the IDE
 *   prints "Maximum is 3145728 bytes" either way -- the number that looks like
 *   confirmation proves nothing. logPartitionInfo() now asks the running device
 *   at every boot and says plainly whether a second app slot exists, and
 *   checkSdFirmwareUpdate() bails BEFORE the confirm screen when there is none,
 *   rather than after the user has agreed to install.
 *
 *   Headroom confirmed: 1.31 MB in a 3.00 MB slot, 43.6% used, 1.69 MB spare.
 *   No rebalance needed; the idle spiffs region stays as future space.
 *
 * v1.10.0 (2026-08-10): FIRMWARE UPDATE FROM SD.
 *
 *   ⚠️ REQUIRES A PARTITION TABLE CHANGE AND ONE FULL ERASE. See partitions.csv
 *   in the sketch folder. The project was built PartitionScheme=huge_app, which
 *   is ONE ~3 MB app partition -- self-update is physically impossible there,
 *   because an app cannot overwrite the flash it is executing from.
 *   Update.begin() fails with a partition error and no code works around it.
 *   The new table uses the 8 MB part for two 3 MB OTA slots, keeping the same
 *   per-image headroom. Enable "Erase All Flash Before Sketch Upload" for that
 *   ONE upload, then turn it back off -- leaving it on wipes NVS, and with it
 *   the per-device BT volumes, on every subsequent upload.
 *
 *   TRIGGER: /firmware.bin present at boot -> confirm screen -> 5 installs,
 *   Home skips, 30 s with no press skips. Never silent: a box left on a shelf
 *   should keep running the firmware it has.
 *
 *   PLACED AFTER THE SD MOUNT AND BEFORE BLUETOOTH / audioFillTask / the
 *   remaining PSRAM allocations, which is the whole safety argument. Flash
 *   writes briefly disable the instruction cache; doing that with the audio and
 *   A2DP tasks live, in a build using -mfix-esp32-psram-cache-issue, is asking
 *   for it. At that point in setup() neither task exists.
 *
 *   This is also the one place display.init() may legitimately move earlier.
 *   Section 8's rule is that EPD init stays last because reordering it drags
 *   PSRAM/BT/audio bring-up along -- but an update never reaches that bring-up,
 *   it reboots. So the panel comes up early ONLY here, and epdReady stops
 *   setup() initialising it a second time on the skip path.
 *
 *   INTEGRITY, in the order things actually go wrong:
 *     - first byte must be 0xE9 (every ESP32 image starts with it), checked
 *       BEFORE Update.begin() erases anything -- rejects a wrong or renamed
 *       file cheaply;
 *     - size sanity (>= 64 KB) catches an obviously truncated copy;
 *     - /firmware.md5 optional per the chosen policy, but its ABSENCE is warned
 *       about rather than passing silently. When present it goes to
 *       Update.setMD5() and the library refuses to activate on mismatch.
 *   Nothing is ever written to the running slot, so a failure at any point
 *   before the final commit leaves the current firmware bootable.
 *
 *   The image is deleted only AFTER a successful end(), and before the restart.
 *   Leave it in place and the device offers the same update on every boot
 *   forever. The filename is matched EXACTLY -- no glob on *.bin -- so a
 *   firmware.bin.bak or an old firmware_v1_9_9.bin on the card is inert.
 *
 *   ⚠️ NO BOOTLOADER ROLLBACK. Arduino does not enable it, so an image that
 *   boots into a crash loop needs a USB reflash; 1+H will not save you. Keep
 *   the cable. This is the one failure this design cannot absorb.
 *
 * v1.9.10 (2026-08-10): BUILD FIX x2. v1.9.9 did not compile.
 *
 *   (a) "'SCREEN_W' was not declared in this scope". The BG_* constants were
 *   written into the ART_BYTES block, 18 lines ABOVE the SCREEN_W/SCREEN_H they
 *   are derived from. Moved below them. A file-scope constant can only
 *   reference names already declared above it -- obvious in isolation, easy to
 *   miss when appending to a block that merely looks related.
 *
 *   (b) "'BandFit' does not name a type" at drawSleepLabel(), whose parameter
 *   list names a struct declared 700 lines ABOVE it. Same prototype-hoist
 *   mechanism as the v1.9.7 Metadata failure, plus the detail that explains why
 *   it had never fired before: THE GENERATOR SKIPS ANY FUNCTION WITH A DEFAULT
 *   ARGUMENT, because it cannot legally repeat the default in a prototype.
 *   bandFit() and drawInBand() both have default args, so neither has ever had
 *   a prototype generated -- the only reason BandFit in their signatures was
 *   safe. drawSleepLabel() had none, so it got one, hoisted above the type.
 *   `static` does NOT exempt a function from this. BandFit is now
 *   forward-declared beside Metadata at the top of the file.
 *
 *   Both failure classes are now caught mechanically by verify_sketch.py rather
 *   than by remembering: it checks that file-scope const initialisers reference
 *   only earlier declarations, and that no function without default arguments
 *   names a type declared after the first function definition unless that type
 *   is forward-declared. The prototype check operates on the whole file rather
 *   than line by line -- a line-based version missed drawSleepLabel() outright,
 *   because its signature wraps. Both checks were regression-tested by
 *   reintroducing the two bugs and confirming they are reported by name.
 *
 *   No behaviour change. Sleep screens, backgrounds and labels are as v1.9.9.
 *
 * v1.9.9 (2026-08-10): SLEEP / DEEP SLEEP wording restored over the
 *   backgrounds. v1.9.8 dropped the big word on the assumption the artwork
 *   would carry it; it should not have assumed. Both labels are now drawn in
 *   both paths.
 *
 *   Legibility over arbitrary artwork is handled by knocking out a white box
 *   behind each label, sized to the MEASURED text rather than to the full
 *   screen width -- fitText() and textWidth() are run exactly as drawInBand()
 *   will run them, so the box provably covers the glyphs (measure-then-
 *   constrain, the drawHeader/drawFooter pattern). A full-width band would also
 *   have been legible but would erase a horizontal stripe of the user's image.
 *
 *   Band geometry is now four named constants shared by the image path and the
 *   plain-black fallback, so the two cannot drift: title 52..84, hint 132..144
 *   on a 152 px panel, verified arithmetically rather than by eye -- no
 *   overlap, nothing past the bottom edge, and the widest label
 *   ("press any button to wake", ~144 px) leaves its knock-out box inside the
 *   panel at x 71..225.
 *
 * v1.9.8 (2026-08-10): FULL-SCREEN BACKGROUNDS for the two sleep screens,
 *   from /Sleep.bmp and /DeepSleep.bmp (296x152, 1-bit).
 *
 *   THESE ARE PARSED AS REAL BMPs, unlike loadArtwork(), which reads ART_BYTES
 *   of RAW bytes out of a file that merely has a .bmp extension. Three things
 *   in the real format corrupt a naive raw read, none of them loudly:
 *     - pixel data starts at the offset stored at byte 10 (usually 62), not 0;
 *     - rows are padded to a 4-byte boundary. 296 px = 37 bytes, padded to 40.
 *       Reading 37-byte rows skews the image progressively -- a diagonal tear,
 *       not an obvious failure;
 *     - positive height means rows are stored BOTTOM-UP, so file order paints
 *       the picture upside down.
 *   Plus polarity: in 1-bpp the palette decides what a set bit means, and most
 *   editors write palette[0]=black, i.e. bit 1 = WHITE -- the opposite of what
 *   Adafruit_GFX drawBitmap wants. The palette is read and the buffer
 *   normalised to "1 = black ink" rather than guessed at. A file with no 'BM'
 *   magic falls back to a raw read, so a pre-converted dump also works.
 *   The parse arithmetic was validated off-device against generated BMPs in all
 *   four bottom-up/top-down x palette-polarity combinations before shipping.
 *
 *   ⚠️ CONSEQUENCE FOR ALBUM ART: if the per-song .bmp files are real BMPs
 *   rather than raw dumps, loadArtwork() has been rendering a 62-byte header as
 *   garbage pixels across the top rows and shifting the image ever since it was
 *   written. Unverified -- worth a look on the device.
 *
 *   PRELOADED AT BOOT (11.2 KB of PSRAM for both), not read on demand.
 *   drawSleepScreen() is called from enterDeepSleep(), which runs at exactly
 *   the moment the SD may be wedged; taking SdLock there is the hazard
 *   saveSession() uses a bounded take to dodge. Buffers stay null on any
 *   failure (missing file, wrong size, wrong depth, compressed, alloc fail) and
 *   the screen falls back to the original plain-black version, so a bad file
 *   degrades rather than breaks. Every rejection says why on serial.
 *
 * v1.9.7 (2026-08-10): BUILD FIX. v1.9.6 did not compile:
 *     error: 'Metadata' has not been declared
 *     void loadMetadata(const String and wavPath, Metadata and m)
 *
 *   Nothing was wrong at the line the compiler named. The Arduino builder
 *   auto-generates a prototype for every NON-STATIC function in a .ino and
 *   inserts the whole block immediately before the FIRST FUNCTION DEFINITION in
 *   the file. v1.9.6 added progStepIndex() to the settings section, which made
 *   it the first function definition in the sketch -- 44 lines above the
 *   declaration of struct Metadata. The generated prototype for loadMetadata()
 *   therefore named a type that did not exist yet, two thousand lines from the
 *   helper that actually caused it. Every earlier version compiled because the
 *   first function was printHeap(), which sits below the type declarations.
 *
 *   Fixed twice over: progStepIndex() moved down to the settings row helpers
 *   (restoring printHeap as the first definition), AND struct Metadata is now
 *   forward-declared at the top of the file, so a generated prototype taking a
 *   Metadata reference compiles even if the insertion point moves again -- a
 *   reference to an incomplete type is legal in a declaration.
 *
 *   Audited the rest: BandFit and TextAlign are also declared below the
 *   insertion point, but appear only in static functions, which get no
 *   generated prototype. That is why they never broke. The rule is now written
 *   at the top of the file: a non-static function's signature may only name
 *   types declared before the first function definition.
 *
 * v1.9.6 (2026-08-10): TWO NEW SETTINGS, filling the page-1 rows that v1.9.3
 *   left blank when Shuffle and EPD refresh were deleted. Both were picked for
 *   having a real consumer rather than for filling space; the sleep settings on
 *   page 2 are untouched.
 *
 *   VOLUME STEP (5..20%, default 5). The increment used by Vol+/Vol- on Now
 *   Playing AND by the Volume row's own +/- -- both call sites, or the setting
 *   would be a lie on one screen. 5% meant twenty presses to cross the range on
 *   a device whose entire input is five buttons.
 *
 *   PROGRESS UPDATES (5/10/20/25%, default 10). The progress-bar bucket, i.e.
 *   how often Now Playing spends a PARTIAL refresh: 20 per song at 5%, 4 per
 *   song at 25%. This is the panel-wear control the deleted "EPD refresh" row
 *   only pretended to be, and unlike GHOST_CLEAR_AFTER (see v1.9.5 -- lowering
 *   that ADDS refreshes) it moves the number that actually dominates.
 *   Restricted to divisors of 100 on purpose: a step like 15 makes the last
 *   bucket 90, so the bar would never reach the end under partial refresh.
 *   Both loop() and drawNowPlaying() read cfg.progStep -- if they disagree, the
 *   first partial update after a full redraw fires instantly.
 *
 *   Page 1 is therefore full again (Volume / Playlist size / Volume step /
 *   Progress updates / More settings >), 1-4 edit and 5 opens page 2. The
 *   row-value strings moved into one settingsValue() accessor shared by the
 *   full redraw and the partial row redraw, which is the same drift the
 *   SET_ and SET2_ row constants exist to prevent.
 *
 * v1.9.5 (2026-08-10): the open-issues list, closed.
 *
 *   VOLUME DOWN WAS NEVER BROKEN. Reported not working, never reproduced in
 *   code -- the up/down paths are arithmetically symmetric and share
 *   applyVolume(). Root cause turned out to be the user pressing 1 and 2 at the
 *   same time: readButton() scans the buttons in pin order and returns the
 *   FIRST one down, so a simultaneous 1+2 always reports 1, which on Now
 *   Playing is Vol+. Left alone deliberately -- rejecting the press when both
 *   are down would also swallow fast alternating presses. Noted at the scan
 *   loop so the next person doesn't re-open the hunt. No PCM scaling was added
 *   to get_audio_data(); AVRC absolute volume works with this sink.
 *
 *   CoD MASK FIX in isAudioDevice(): tested bit 16 (Positioning) instead of
 *   bit 21 (Audio). Masked for two versions by the second clause (major device
 *   class == 4), which is what actually matched the JBL: its CoD 0x240404 has
 *   service field 0x120, so the old 0x08 test failed and only the major-class
 *   test passed. A headset advertising the Audio service under a different
 *   major class was silently dropped from every scan.
 *
 *   LAST while(1) GONE. loadPlaylist()'s alloc failure was still a silent
 *   forever-hang with a blank panel, the pattern v1.8 replaced everywhere else.
 *   It is fatalHalt() now, which reports on serial and honours 1+H.
 *
 *   NOT CHANGED, having re-checked two of my own earlier suggestions:
 *   GHOST_CLEAR_AFTER stays at 12 and SAVE_DELAY_MS stays at 2000. Lowering the
 *   ghost-clear counter would ADD a full refresh per song, not save one --
 *   drawNowPlaying() already fires a full refresh on every song change and
 *   resets partialRefreshCount, so with ~10 progress buckets per song the
 *   counter rarely reaches 12 and ghosting is already being cleared by the song
 *   change itself. And settingsDirtyAt is reset on every press, so a held
 *   volume sweep cannot trigger a mid-sequence SD write; the 2 s delay is
 *   already a debounce.
 *
 * v1.9.4 (2026-08-10): AUTO DEEP SLEEP WITH SESSION RESUME.
 *
 *   PREREQUISITE, and it is the reason this went in first: enterDeepSleep()
 *   called a2dp_source.end(true) -- the last surviving violation of "never call
 *   end()". As a button press a deadlock there was merely annoying. On a timer
 *   it becomes a scheduled event: wedge in end(), guardTask reboots at 45 s,
 *   the session restores, and the loop can repeat all night. It is now
 *   disconnect() (the call the v1.9.2 log proved clean) and only when a link
 *   exists. end() was never needed anyway -- esp_deep_sleep_start() resets the
 *   SoC and powers the BT controller down by itself.
 *
 *   SESSION FILE /session.txt. Written in enterDeepSleep() while the SD is
 *   still mounted; read back ONLY when esp_sleep_get_wakeup_cause() is
 *   ESP_SLEEP_WAKEUP_EXT0, so a cold boot or a 1+H reset still resamples the
 *   playlist -- which doubles as the deliberate "reshuffle" gesture. Deleted
 *   after a successful restore, so a later failed save cannot resurrect a stale
 *   queue. Magic line + field count + path count are all checked; any failure
 *   removes the file and falls through to a normal loadPlaylist().
 *
 *   THE RESUME OFFSET HAS TWO WAYS TO BE SILENTLY WRONG, both handled:
 *   it is playedBytes, NOT producedBytes (audioFillTask runs up to 512 KB, ~6 s,
 *   ahead of the speaker -- saving the producer's count would skip six seconds
 *   on every resume), and it is masked to a 4-byte frame boundary (16-bit
 *   stereo; land mid-frame and the channels stay swapped for the rest of the
 *   song, which sounds like a vague smear rather than an obvious fault).
 *   openSongCommon() starts producedBytes AND playedBytes at the resume point,
 *   so the EOF test, the SD-recovery seek and the progress bar all stay right.
 *   Wake resumes PLAYING, not paused.
 *
 *   TWO NEW SETTINGS: deep_sleep_timeout (0 = off, 15..240 min, default 90) and
 *   deep_sleep_mode (0 = only while the BT link is DOWN, 1 = always). Split
 *   rather than encoded into one field because they are independent questions.
 *   BT-down-only is the battery case: link dropped, nothing reconnects it (the
 *   library's auto-reconnect is disabled and our discovery kick only runs on
 *   the CONNECTING screen), and the firmware decodes into nothing for an hour.
 *   In ALWAYS mode the clock is pure button inactivity -- playback does NOT
 *   count as activity, so it will sleep mid-album if untouched. Deliberate.
 *   The trigger holds off during MS_BT_SCAN, MS_BT_CONNECTING and
 *   btConnectBusy: powering the radio down mid-inquiry is exactly the class of
 *   half-finished teardown section 4 of the handoff spec exists to prevent.
 *
 *   PAGE 2 REBUILT to fit them without a page 3. "About" was a dead press whose
 *   only content was the version, now in the page-2 header where it is always
 *   visible; "< Back to page 1" was redundant once H was made to pop ONE level
 *   (to page 1) like every other back-step in the app, instead of jumping past
 *   it to the main menu. 1+H = reset moved to the footer. Page 2 also gained a
 *   partial single-row redraw: every edit press used to cost a ~4 s FULL
 *   refresh, so walking the deep-sleep timeout to 240 was ten of them.
 *
 * v1.9.3 (2026-08-10): NO BLUETOOTH CHANGES AT ALL. Settings audit and dead
 *   code removal only; the connect/scan path is byte-for-byte v1.9.2.
 *
 *   THREE OF THE NINE SETTINGS ROWS DID NOTHING. Traced every cfg field to its
 *   consumers: `shuffle` and `epd_refresh` were read at load, written at save,
 *   drawn twice -- and used by no other line in the sketch. loadPlaylist()
 *   shuffled unconditionally regardless of the toggle, and "EPD refresh: N
 *   songs" could not have meant anything, because a song change already forces
 *   a full drawNowPlaying(). Both rows are DELETED, struct fields and all.
 *   Page 1 is now 3 rows (Volume / Playlist size / More settings >), 1-2 edit
 *   and 3 opens page 2; the two rows past the end are drawn genuinely blank
 *   rather than as numbered rows that ignore a press.
 *
 *   PLAYLIST SIZE IS NOW WIRED, and needed an ORDERING FIX to be wirable:
 *   loadSettings() ran AFTER loadPlaylist(), so cfg held compiled defaults
 *   while the playlist was built. It now runs immediately after the SD mount.
 *   MAX_PLAYLIST 100 -> 200 (allocation ceiling, 25.6 KB PSRAM); the live queue
 *   length is playlistCap, resolved from cfg.playlistSize at boot and also used
 *   by playSongByPath() so library picks cannot creep the queue above the
 *   configured size. Takes effect at boot -- the edit footer says so.
 *
 *   THE EDIT FOOTER STOPPED LYING: "H=CANCEL" -> "H=DONE". H never cancelled
 *   anything -- it left edit mode with the edited value still in place, and for
 *   Volume applyVolume() had already gone out to the sink, so there was nothing
 *   left to cancel by the time the label was read. Rather than build a restore
 *   path for edits that are live by design, the label now matches the
 *   behaviour: H commits, and 3 is the same commit saved immediately instead of
 *   on the 2 s deferred write ("3:save now"). Every edit on both pages now
 *   marks settingsDirty -- Settings' Volume and page 2's timeout never did, so
 *   with H meaning DONE they would otherwise have been silently dropped.
 *   The BT scan screen's H=CANCEL is untouched; that one really does cancel.
 *
 *   DEAD CODE: busyDelay() deleted (no call sites since v1.9 moved combo
 *   polling to guardTask and deleted the blocking teardown it guarded).
 *   scanLastDrawn's unused tri-state collapsed to a bool, scanDoneDrawn --
 *   NOT deleted, since without that latch the finished-scan screen would queue
 *   a ~4 s full refresh every loop pass. Two stale comments corrected.
 *
 *   STILL OPEN, deliberately untouched here: volume-down (needs the 0% test),
 *   the isAudioDevice() CoD mask, end(true) in enterDeepSleep(), and the
 *   while(1) in loadPlaylist()'s alloc failure.
 *
 * v1.9.2: three failures visible in the JBL log, all from trusting the
 *   library's discovery state machine too much:
 *   1. ENDSCAN CANCELLED AN INQUIRY THAT HAD ALREADY ENDED. Our scan usually
 *      finishes by itself (gapCallback logs "Discovery stopped"); the extra
 *      esp_bt_gap_cancel_discovery() then produced a second GAP event, which
 *      the just-restored library callback read as "discovery failed, continue
 *      to discover" -- so it began discovering WHILE CONNECTED, found another
 *      saved device and switched the link to it. That is the unrequested
 *      C17A -> JBL jump at 107s. endScan() now only cancels a live inquiry.
 *   2. onSsidFound() ACCEPTED ANY SAVED DEVICE, EVEN WHILE CONNECTED, so the
 *      library was free to abandon a working link for whatever it saw first.
 *      It now refuses everything while connected unless an explicit target is
 *      set.
 *   3. THE "ALREADY CONNECTED?" TEST WAS DEAD CODE. connectToDevice()
 *      overwrote connectedDevice with the target and then compared the two, so
 *      it always disconnected -- at 123s that killed an in-flight connect to
 *      the very device the user had just picked. The previous name is now
 *      captured first, and picking the current device is a no-op.
 *
 *   - CONNECTING NO LONGER WAITS ON THE LIBRARY. After the 123s disconnect it
 *     printed "reconnect retries exhausted, fallback to scanning" at 128s,
 *     138s, 148s and 158s and never started an inquiry -- it says it will scan
 *     and then doesn't, which is why the CONNECTING screen sat dead until the
 *     reset. We now start the inquiry ourselves every 12s, up to 4 tries, and
 *     let the library's own GAP path do the connecting so its internal peer
 *     address stays correct. The screen counts the attempts instead of looking
 *     frozen, and a failure restores the previous device name.
 *
 * v1.9 NEW: BLUETOOTH CONNECT FIX -- two separate bugs, both proven by the
 *   serial log rather than guessed at:
 *
 *   1. THE STACK TEARDOWN DEADLOCKED. connectToDevice() called
 *      a2dp_source.end(false). The log ends at "[BT] step: end" with no
 *      "step: start" and no "step: done", while the BT app task carried on
 *      dispatching evt 0xff00 every 10s. end() waits for that task to shut
 *      down; it never does. The Arduino loop task blocked there forever, which
 *      froze the UI AND disabled the 1+H reset, since the combo was being
 *      polled from the loop task that had just died.
 *      FIX: the entire disconnect/end/start sequence is gone. The library is
 *      already sitting in a permanent discovery loop looking for a name to
 *      connect to, so connecting is now just "set btTargetName and let the
 *      next round find it". Nothing blocks. Only disconnect() survives, and
 *      only when a link actually exists -- that call completed cleanly.
 *
 *   2. OUR SCAN BLINDED THE LIBRARY PERMANENTLY. startScan() called
 *      esp_bt_gap_register_callback(gapCallback), replacing the library's own
 *      GAP handler, and never restored it. Count the ccall_app_gap_callback
 *      lines after the first [SCAN] in the log: there are none. The library
 *      kept logging "Discovery started" forever but could not see a single
 *      result, so it could never connect to anything again -- which is why the
 *      device never appeared even though our own scan had just found it.
 *      FIX: gapCallback() and isAudioDevice() are deleted. Discovery belongs
 *      entirely to the library; results arrive through its own ssid callback,
 *      which now both fills the scan list and picks the connect target. We no
 *      longer register a GAP callback, start a discovery, or cancel one
 *      anywhere -- cancelling was also stopping its reconnect attempts.
 *
 *   - THE RESET COMBO NOW RUNS ON ITS OWN TASK (guardTask, core 0). v1.8
 *     polled it from readButton() and busyDelay(), both on the loop task,
 *     making it useless in precisely the situation it was built for. guardTask
 *     also watches a heartbeat and reboots if loop() stops ticking for 45s, so
 *     a hard block reboots itself instead of sitting there dead.
 *   - Connecting is asynchronous, so there's a CONNECTING screen with a real
 *     45s timeout that falls back to "NOT FOUND" rather than sitting forever.
 *
 * v1.8 NEW: MENU FRICTION PASS -- no dead layers, no dead rows:
 *   - ONE-PAGE LISTS SKIP THE BROWSE LAYER. The BROWSE/SELECT split only
 *     earns its keep when there's more than one page. On a single page, 1 and
 *     2 look like they should pick rows 1 and 2 but are paging buttons that
 *     silently do nothing, and you must discover "5=SELECT" before any number
 *     does what it looks like it does. Worst on the Bluetooth screen, where
 *     <=4 saved devices meant pressing 1 to connect just paged instead.
 *     Library / artists / albums / artist-songs / bluetooth now enter SELECT
 *     directly when everything fits one page.
 *   - The entry decision and the H-back decision now share ONE definition of
 *     "does this list need a browse layer" (libTotalPages, btTotalPages,
 *     albumTotalPages, artSongTotalPages, artTotalPages). They're recomputed
 *     rather than cached in a flag, so they cannot drift out of sync and drop
 *     you onto a one-page browse screen you never passed through on the way
 *     in. Library's source toggle (3) re-evaluates too, since catalog->
 *     playlist can take you from 400 pages to 1.
 *   - SINGLE-ITEM SUBMENUS RESOLVE THEMSELVES. An artist with 0 or 1 albums
 *     skips the album screen: "[All songs]" and the lone album resolve to the
 *     same song list, so it was two rows with one outcome. A search narrowing
 *     to exactly one artist opens that artist, in all three paths (word
 *     search, A-Z browse, and mid-bisection).
 *     NOT applied to a single SONG result: stepping into a browse screen is
 *     undoable with H, but starting playback replaces what you're listening
 *     to and isn't, so songs keep their confirming press -- the same line
 *     enterArtist() already drew for its <=PAGE case.
 *   - Footers stopped lying: one-page screens no longer advertise "1<pg 2>pg",
 *     and SELECT mode says "H=back" when there's no browse layer to go back
 *     to. Back-half hints moved into drawFooter's RIGHT argument so they're
 *     measured against the left string instead of being crammed into it.
 *   - BLUETOOTH CONNECT WAS PASSING A POINTER INTO AN ARRAY IT THEN REWROTE.
 *     connectToDevice(name) was called with &savedDevices[i] (from the saved
 *     list) or &scanNames[i] (from a scan), then immediately called
 *     addSavedDevice(name) -- which memcpy's a rebuilt list straight over
 *     savedDevices. Every read of `name` after that line therefore saw a
 *     DIFFERENT device's string: wrong name stored, wrong saved volume loaded,
 *     and the A2DP source asked to connect to a device that may not be there,
 *     so it never connects and the player looks hung. The name is now copied
 *     into btTargetName[] before anything touches the arrays. That buffer is
 *     also what's handed to a2dp_source.start(), because the library keeps the
 *     POINTERS from that vector rather than copying the strings, so the old
 *     code left it holding a pointer into storage the next scan overwrites.
 *     [SUPERSEDED by v1.9 -- kept as history. connectToDevice() no longer
 *     calls start() at all; start() runs once in setup(). The only vector the
 *     library still holds pointers from is the savedDevices[] list built in
 *     setup(), and since set_ssid_callback() is installed, onSsidFound() is
 *     the gate that decides connections, not that vector.]
 *   - The connect sequence now shows a CONNECTING screen before it blocks,
 *     logs a breadcrumb per step (cancel discovery / disconnect / end / start)
 *     so a fault inside the BT stack can be located from the serial log, pauses
 *     the audio feed across the teardown so get_audio_data() can't be called
 *     into a half-dismantled source, and refuses re-entry while in progress.
 *   - EMERGENCY RESET: hold button 1 + Home for 1.5s to reboot. Polled from
 *     readButton() AND from busyDelay() inside the blocking BT teardown, and
 *     from the boot fatal paths (initButtons() moved to the top of setup() to
 *     make that possible). It unmounts the SD card first, but only if it can
 *     take the mutex within 300ms -- a wedged audio task holding sdMutex is a
 *     likely reason to be resetting, so the rescue path must not block on it.
 *     It CANNOT rescue a true hard hang or a crash inside the BT stack, since
 *     nothing is left running to poll the pins; that needs the EN button or a
 *     hardware watchdog.
 *   - The two boot failures (SD mount, empty playlist) were `while(1)delay()`
 *     -- a silent forever-hang with a blank panel, indistinguishable from a
 *     dead board. They now report on serial and honour the reset combo.
 *   - A BISECT GROUP OF ONE LETTER IS A KEYSTROKE, NOT A MENU. Narrowing the
 *     alphabet often lands on a group holding a single letter; that used to
 *     open a one-row picker whose only possible action was "press 1", costing
 *     a full panel refresh to ask a question with one answer, plus another to
 *     get back to the alphabet. It now types the letter directly. The
 *     keystroke logic moved out of the MS_SEARCH_RESULTS handler into
 *     applySymbolPick() so both entry points share one implementation.
 *   - PARTIAL REFRESH WINDOW HALVED (y 98..151 -> 102..128). It used to span
 *     the two static hint lines, so every progress tick erased and re-inked
 *     two identical lines of text -- panel wear and ghosting for no visual
 *     change. The window now covers only what actually moves: the progress
 *     bar, the elapsed time, and the track/volume line. The hints are painted
 *     once per full redraw and never touched in between. Volume moved up onto
 *     the track-counter line to keep every changing pixel inside it, which
 *     also freed hint line 1 to go back to the clearer "1:Vol+ 2:Vol-".
 *   - Consequence of the above: AVRC play/pause from the phone now forces one
 *     full redraw, because the PAUSED badge and hint line 2 are both outside
 *     the partial window and would otherwise not appear until something else
 *     repainted the screen.
 *   - NOW PLAYING SHOWS THE VOLUME. There was previously no volume readout
 *     anywhere on the screen, so a Vol+/Vol- press produced no visible change
 *     at all and a dead button was indistinguishable from a sink ignoring the
 *     volume command. Now a live "VOL 75%" sits on the track-counter line
 *     (laid out right-item-first so it can't collide with "3/47") and both
 *     directions log to serial.
 *
 * v1.7 NEW (A): MEASURED TEXT ENGINE -- every string is now placed by
 *   measurement instead of hand-tuned baselines. Adafruit_GFX anchors the
 *   built-in 5x7 font by its TOP-LEFT corner but anchors GFXfont text by its
 *   BASELINE; v1.6 mixed the two with per-call magic numbers (ry+11, ry+14,
 *   ry+17, ry+20, HDR_H-1, fy+3, y=73, y=90 ...) and they disagreed:
 *     - Two-line list rows drew the title with a baseline of ry+11, putting
 *       cap tops at ry-2: every such row bled 2 px THROUGH its own divider
 *       into the row above. Title descenders (ry+15) also sat 1 px from the
 *       sub-line (ry+17), so "gy" tails kissed the artist name underneath.
 *     - Header text used baseline HDR_H-1 in a 14 px bar. Caps are ~13 px so
 *       they just fit, but descenders land 4 px BELOW the bar -- drawn white
 *       on white, i.e. the tails of 'g'/'p'/'y' in artist and album headers
 *       were silently invisible.
 *     - The word-entry cursor was a literal '_', which also sits below the
 *       baseline, so the caret never rendered at all.
 *     - Header/footer left and right strings were drawn independently, so a
 *       long left string could run under the right one.
 *     - drawWrapped() had no ellipsis and no hard break, so a single long
 *       word ran off the 140 px info panel and over-long titles just vanished.
 *   Replaced with bandFit()/drawInBand(): a band (y,height) is given a
 *   reference string, getTextBounds() supplies the ink offset and height for
 *   whichever anchoring mode applies, and the cursor is solved as
 *       cursorY = bandTop + (bandH - h)/2 - y1
 *   Measuring a REFERENCE string rather than the text keeps one baseline per
 *   band (measuring the real text would make "Now Playing" and "eee" sit at
 *   different heights). If the preferred font can't fit the band, the 5x7 font
 *   is used rather than letting ink cross into a neighbour. Widths are
 *   measured too, and each item is laid out against the width the previous one
 *   actually consumed, so left/right pairs can no longer collide.
 *
 * v1.7 NEW (B): LAYOUT CONSEQUENCES OF (A):
 *   - HDR_H 14 -> 15, FTR_H 13 -> 12 (rows are unchanged at 5 x 25 = 125;
 *     15 + 125 = 140 = footer rule, so the grid still lands exactly).
 *   - Header text is upper-cased. A 15 px bar holds ~13 px caps but not the
 *     ~18 px caps-plus-descenders box, so upper-casing is what makes header
 *     text provably fit instead of clipping.
 *   - List rows are single-line: secondary text moved from a second line
 *     inside the row to a right-aligned 5x7 column on the same line. A 25 px
 *     row physically cannot stack 13 px caps + 4 px descender + 7 px sub-line
 *     with separation; v1.6 "fit" them by overlapping.
 *   - Now Playing's right panel uses fixed bands (title / artist / badge /
 *     next), so those lines sit at the same y for every song instead of
 *     floating on the wrapped title's height.
 *   - One row renderer (drawRowCore) backs every list in the app, so library,
 *     artists, albums, songs, search, bluetooth and scan share a rhythm.
 *
 * v1.7 NEW (C): WORD SEARCH OVER THE WHOLE CATALOG, FROM EVERYWHERE:
 *   - SEARCH menu is now 5 rows: Find Song / Find Artist / Find Anything
 *     (title OR artist) / Browse Titles A-Z / Browse Artists A-Z. All three
 *     "Find" modes are case-insensitive CONTAINS matches over the ENTIRE
 *     catalog index (every song, not the 100-song shuffled playlist).
 *   - "Find Anything" is new and needs the artist of each song, so TitleEntry
 *     gained an artistIdx into artistList, resolved at boot by binary search
 *     over the already-sorted artist list (+2 bytes per catalog entry, no
 *     extra SD pass).
 *   - Button 4 now opens word search directly from the library browser, the
 *     artist browser, the album browser and the artist-song browser, so you
 *     never have to back out to the menu to search. H from search returns to
 *     wherever the search was started (searchReturn), not always the menu.
 *   - The typing screen shows a live match count in the header ("42 hits"),
 *     so you can stop typing as soon as the set is small.
 *   - Song results now show the artist as row metadata, so identically-titled
 *     songs are distinguishable before you play one.
 *   - Symbol tokens relabelled SPACE / DELETE / SEARCH (were "_" / "<-" / "OK").
 *
 * v1.6: HOLD HOME TO JUMP HOME:
 *   - Holding the Home button (GPIO 21) for >=HOME_HOLD_MS (700ms) jumps
 *     straight to the Now Playing screen from ANY menu state or depth --
 *     mid-browse, mid-search, even mid-settings-edit -- without needing to
 *     back out one screen at a time. A normal short press/release still
 *     does whatever it always did on that screen (back/cancel/etc).
 *   - Implemented as hold-tracking on Home only (readButton()); buttons 1-5
 *     are untouched. The long-press fires once, mid-hold (not on release),
 *     and suppresses the short-press action for that same press so you
 *     don't get both. Checked once per loop() BEFORE handleButton(), so it
 *     doesn't need a case added to every menu state's switch.
 *   - Cancels an in-flight BT scan first if one's running, same cleanup the
 *     existing short-H-press handler in MS_BT_SCAN already does.
 *
 * v1.5 NEW: FIND BY WORD (single-word substring search, no keyboard needed):
 *   - SEARCH menu (main menu btn3) now has 4 rows: "Find Song" / "Find
 *     Artist" (new, word-based) and "Browse Titles" / "Browse Artists"
 *     (the v1.4 alphabetical bisection, unchanged, kept for when you DO
 *     know the first letters).
 *   - Find-by-word reuses the SAME 5-way bisection control scheme already
 *     used for browsing, but applied to a 39-symbol alphabet (space, 0-9,
 *     A-Z, a Backspace token, an OK token) instead of to song/artist names.
 *     Composing a word is therefore ~2-3 button presses per letter, with
 *     the word-so-far always visible in the header (e.g. "hone_").
 *   - Pressing OK runs a case-insensitive CONTAINS match (not prefix-only)
 *     against every title or every artist. <=5 matches show directly;
 *     >5 matches hand off to the existing results-bisection screen so you
 *     narrow the same way you would when browsing. 0 matches shows a
 *     dedicated screen; H returns you to the SAME typed word (not a blank
 *     slate) so you can backspace and adjust instead of retyping.
 *   - New PSRAM cost: searchFilterIdx[], a reusable int buffer sized to
 *     max(artistCount,titleCount) that holds match indices for whichever
 *     pool was last filtered (~8KB for a 2000-song catalog).
 *
 * v1.4 NEW: SEARCH (replaces "Browse Artists" on the main menu; the album/
 *   song drill-down screens it used to launch are unchanged and still used
 *   once a search resolves to one artist):
 *   - Main menu btn3 -> MS_SEARCH_TYPE: choose "Search Artists" or "Search
 *     Titles" first (titles search the full 2000-song catalog, not just
 *     the 100-song shuffled playlist).
 *   - No on-screen typing. Each press 5-way bisects the REMAINING sorted
 *     candidates (not the alphabet) -- e.g. 1612 songs narrows to <=5
 *     candidates in just 4 fast PARTIAL refreshes. Each button shows the
 *     letter range and count it covers, e.g. "1: Aaron - Carly (322)".
 *   - Once <=5 candidates remain, a direct picker shows them by name;
 *     selecting an artist drills into the existing album/song browser,
 *     selecting a title inserts it after the current track and plays it
 *     (same mechanism as the library browser).
 *   - H pops back ONE bisection step at a time (small stack, mirrors every
 *     other browse screen's back behavior) rather than aborting to the menu.
 *   - New PSRAM cost: titleIndex[], a title-sorted catalog index built once
 *     at boot (~164KB for a 2000-song catalog; reuses catalogOffsets so it's
 *     a single linear pass, no extra SD re-scan).
 *
 * v1.3 CHANGE:
 *   FIX K (wired)  Bluetooth screen previously only displayed an overflow
 *          COUNT ("3 more saved") for devices beyond the first 4 -- those
 *          devices were not actually selectable, only reachable by re-scan.
 *          Now has real pagination: DEV_PER_PAGE=4 slots/page (same pattern
 *          as ALBUMS_PER_PAGE), with a BROWSE/SELECT split identical to
 *          every other list screen (1<pg/2>pg to page, 5=SELECT to enter
 *          select mode, numbers connect/scan). New MS_BT_SELECT state.
 *          All SAVED_MAX=8 saved devices are now reachable from the UI.
 *
 * v1.2 UI FIXES (carried forward from prior audit):
 *   FIX A  Button hints never erased (v1.7 redrew them inside
 *          drawProgressRegion; v1.8 moved them outside the partial window).
 *   FIX B  drawListRow sub cursor ry+22 -> ry+17; no row-boundary overflow.
 *   FIX C  Hint line 2: y=144 -> y=140; 4 px bottom clearance.
 *   FIX D  1 px separator line at x=152 between artwork and info panel.
 *   FIX E  Artist/album capped at maxLines=1; clears space above "Next:".
 *   FIX F  PAUSED badge moved from artwork overlay to the right info panel.
 *   FIX G  Header cursor HDR_H-2 -> HDR_H-1; 2 px top clearance.
 *   FIX H  drawListRow no-sub cursor ry+20 -> ry+17 (vertical centering).
 *   FIX I  Track counter "3/47" added below progress bar inside partial win.
 *   FIX J  Settings2 footer now labels all four buttons including "4:abt".
 *   FIX L  Album pages uniform: row 0 = [All songs] everywhere, rows 1-4 =
 *          albums (ALBUMS_PER_PAGE=4), SELECT maps btn1->all, btn2-5->album.
 */

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <Update.h>       // 2026-08-10: SD firmware update
#include <esp_ota_ops.h>  // partition report at boot
#include <SPI.h>
#include <vector>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_bt.h"
#include "BluetoothA2DPSource.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// ============================================================
// ARDUINO PROTOTYPE HAZARD -- READ BEFORE ADDING A FUNCTION HIGH IN THIS FILE
// ============================================================
// The Arduino builder auto-generates a prototype for every non-static function
// in a .ino and inserts them all IMMEDIATELY BEFORE THE FIRST FUNCTION
// DEFINITION in the file. So any type named in a non-static function's
// signature must be declared before that first definition, or its generated
// prototype references a type that does not exist yet.
//
// 2026-08-10, learned the hard way: adding one small helper (progStepIndex())
// to the settings section made IT the first function definition, 44 lines above
// `struct Metadata`, and the build died with
//     error: 'Metadata' has not been declared
// pointing at loadMetadata() two thousand lines away. Nothing was wrong at the
// line the compiler named; the fault was the position of an unrelated helper.
//
// Two defences, both in place:
//   1. the helper was moved back below the type declarations, and
//   2. Metadata is forward-declared HERE, above everything, so a generated
//      prototype taking `Metadata&` compiles even if the insertion point moves
//      again. A reference to an incomplete type is legal in a declaration.
// If you add a function that takes another sketch-defined type in its
// signature, forward-declare that type here too.
//
// 2026-08-10, second instance of the same class: drawSleepLabel() takes a
// `const BandFit&` and broke the build with "'BandFit' does not name a type",
// even though struct BandFit is declared 700 lines ABOVE the function. The
// missing piece is that the generator SKIPS ANY FUNCTION WITH A DEFAULT
// ARGUMENT -- it cannot legally repeat the default in a prototype. bandFit()
// and drawInBand() both have default args, so neither has ever had a prototype
// generated, which is the only reason BandFit in THEIR signatures has always
// been safe. drawSleepLabel() had none, so it got one, hoisted above the type.
// `static` does NOT exempt a function from this.
struct Metadata;
struct BandFit;

// ============================================================
// PIN CONFIG
// ============================================================
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_CS     5
#define EPD_SCK  14
#define EPD_MOSI 13
#define EPD_CS   15
#define EPD_DC   26
#define EPD_RST   4
#define EPD_BUSY 27
static const uint32_t SD_CLOCK_HZ = 10000000;
// Order is button 1,2,3,4,5,Home. GPIO 34,35,36,39 are ESP32 input-only pins
// with no internal pull resistors, so they rely on the board's external
// pull-ups (see initButtons()). Button 5 = GPIO34; this MUST stay in sync
// with the esp_sleep_enable_ext0_wakeup() pin in enterDeepSleep(), since
// that's the only pin capable of waking the board from deep sleep.
static const uint8_t BTN_PINS[6] = { 32, 33, 36, 39, 34, 35 };

// ============================================================
// BLUETOOTH CANDIDATES
// ============================================================
const char* BT_CANDIDATES[] = { "C17A" };
const int BT_CANDIDATE_COUNT = sizeof(BT_CANDIDATES)/sizeof(BT_CANDIDATES[0]);

// ============================================================
// CONSTANTS
// ============================================================
const char* CATALOG_FILE   = "/catalog.txt";
const char* SETTINGS_FILE  = "/settings.txt";
const char* BTDEVICES_FILE = "/btdevices.txt";
// 2026-08-10: written on deep sleep, read back ONLY on an ext0 wake, deleted
// after a successful restore. A cold boot or a 1+H reset therefore always
// resamples the playlist -- which doubles as the deliberate "reshuffle" gesture.
const char* SESSION_FILE   = "/session.txt";
const char* SESSION_MAGIC  = "SESSION 1";
// 2026-08-10: SD firmware update. FW_IMAGE_FILE is matched EXACTLY -- not a
// glob on *.bin -- so a stray backup like firmware.bin.bak or an old
// firmware_v1_9_9.bin sitting on the card can never be flashed by accident.
const char* FW_IMAGE_FILE       = "/firmware.bin";
const char* FW_MD5_FILE         = "/firmware.md5";
const char* SLEEP_BMP_FILE      = "/Sleep.bmp";
const char* DEEPSLEEP_BMP_FILE  = "/DeepSleep.bmp";
// 2026-08-10: one definition. The About row used to hold this and was the only
// place it appeared outside the file header; it now lives in the page-2 header,
// where it is always visible instead of requiring a press that did nothing.
const char* FW_VERSION     = "v1.10.3";

// 2026-08-10: 100 -> 200. This is now the ALLOCATION CEILING only; the live
// queue length is cfg.playlistSize (10..200, default 100), clamped to this and
// cached in playlistCap at boot. The buffer is always allocated at full size
// (200 * 128 B = 25.6 KB of PSRAM, up from 12.8 KB) so changing the setting
// never needs a realloc.
static const int      MAX_PLAYLIST     = 200;
static const int      MAX_CATALOG      = 2000;
static const int      MAX_ARTISTS      = 256;
static const int      ARTIST_LEN       = 64;
static const int      MAX_PATH_LEN     = 128;
static const size_t   ART_BYTES        = 2888;   // 152*152/8
static const int      PAGE             = 5;
// FIX L: 4 album slots per page; row 0 is always "[All songs]".
static const int      ALBUMS_PER_PAGE  = PAGE - 1;   // = 4
// FIX K (wired): 4 device slots per page; row 4 is always "+ Scan for new",
// same pattern as ALBUMS_PER_PAGE so all SAVED_MAX=8 devices stay reachable.
static const int      DEV_PER_PAGE     = PAGE - 1;   // = 4
static const uint32_t BYTES_PER_SEC    = 44100 * 2;
static const int      DEBOUNCE_MS      = 30;
static const uint32_t RESET_HOLD_MS    = 1500;   // v1.8: 1+H hold -> reboot
static const int      FADE_FRAMES      = 2200;

// ============================================================
// DISPLAY LAYOUT
// ============================================================
static const int16_t SCREEN_W = 296;
static const int16_t SCREEN_H = 152;

// 2026-08-10: full-screen 1-bit backgrounds for the two sleep screens.
// 296/8 = 37 bytes per row exactly, x152 rows = 5624 B each, 11.2 KB for both.
// Trivial against 4 MB of PSRAM; preloading at boot is deliberate, see
// loadScreenBmp(). Panel is 296x152, so these must match it exactly.
// MUST STAY BELOW SCREEN_W/SCREEN_H: these were first written up beside
// ART_BYTES, 18 lines ABOVE the dimensions they are derived from, and the build
// died with "'SCREEN_W' was not declared in this scope". A file-scope constant
// can only reference names already declared above it.
static const int16_t  BG_W         = SCREEN_W;
static const int16_t  BG_H         = SCREEN_H;
static const size_t   BG_ROW_BYTES = (size_t)SCREEN_W / 8;   // 37
static const size_t   BG_BYTES     = BG_ROW_BYTES * (size_t)SCREEN_H;
// v1.7: 15 px header / 12 px footer. Rows are untouched (5 x ROW_H(25) = 125)
// and 15 + 125 = 140 = the footer rule, so the grid still divides exactly.
// The extra header pixel is what lets cap-height header text clear the top
// edge instead of touching it.
static const int16_t HDR_H    = 15;
static const int16_t FTR_H    = 12;
static const int16_t ROW_H    = 25;
static const int16_t NUM_W    = 18;
static const int16_t COL_X    = 156;

static const int16_t PROG_X  = COL_X + 2;    // 158
static const int16_t PROG_Y  = 104;
static const int16_t PROG_W  = SCREEN_W - COL_X - 6;  // 134
static const int16_t PROG_H  = 14;

// Panel rows below the artwork. Only the first two CHANGE while a song plays
// (bar fill, elapsed time, track counter, volume), so the partial-refresh
// window is clamped to exactly those and stops one pixel short of HINT1_Y:
//   102 - 128  partial window
//     104 - 117  progress bar
//     121 - 128  track counter "3/47" (right) + volume "75%" (left)   (FIX I)
//   130 - 137  hint line 1   (static -- OUTSIDE the window, never erased)
//   140 - 147  hint line 2   (static -- OUTSIDE the window, never erased)
//
// v1.7 FIX A kept the hints correct by REDRAWING them on every partial update;
// v1.8 keeps them correct by never touching them. Same result on screen, but
// the window is half the height, so a progress tick toggles half as many
// pixels and the static text can no longer ghost from being repainted every
// few seconds for the length of an album.
static const int16_t TRACK_CTR_Y  = PROG_Y + PROG_H + 3;  // 121
static const int16_t HINT1_Y      = 130;
static const int16_t HINT2_Y      = 140;
static const int16_t PARTIAL_Y    = PROG_Y - 2;                 // 102
static const int16_t PARTIAL_H    = (HINT1_Y - 1) - PARTIAL_Y;  // 27

static uint16_t partialRefreshCount = 0;
static const uint16_t GHOST_CLEAR_AFTER = 12;
// v1.8: pause state as last PAINTED, so an AVRC play/pause from the phone can
// be noticed and repainted. Both the PAUSED badge (y=73) and hint line 2 sit
// outside the partial window, so without this nothing on screen moves until
// something else forces a full redraw.
static bool lastDrawnPaused = false;

// ============================================================
// PSRAM ALLOCATIONS
// ============================================================
static char      (*playlist)[MAX_PATH_LEN] = nullptr;
static int       playlistCount = 0;
static int       songIndex     = 0;
static uint32_t* catalogOffsets = nullptr;
static int       catalogCount   = 0;
static char      (*artistList)[ARTIST_LEN] = nullptr;
static int       artistCount = 0;

// Title search index: every catalog entry's title + its catalog.txt byte
// offset (so playSongByPath can re-read the full path on demand), sorted
// alphabetically by title. Built once at boot, mirrors buildArtistList.
// artistIdx: position in artistList[] (or -1 if the artist overflowed
// MAX_ARTISTS). Costs 2 bytes per catalog entry and is what lets "Find
// Anything" match a song by its artist, and lets song results show the artist,
// without re-reading the path from SD for all 2000 entries.
struct TitleEntry { char title[80]; uint32_t catalogOffset; int16_t artistIdx; };
static TitleEntry* titleIndex   = nullptr;
static int         titleCount   = 0;
static uint8_t*  artworkBuffer = nullptr;
// 2026-08-10: nullptr when the file is missing, malformed, or the wrong size.
// Every draw path checks for null and falls back to the old plain-black screen,
// so a bad or absent BMP degrades instead of failing.
static uint8_t*  sleepBgBuffer     = nullptr;
static uint8_t*  deepSleepBgBuffer = nullptr;

// ============================================================
// DISPLAY
// ============================================================
SPIClass hspi(HSPI);
GxEPD2_BW<GxEPD2_266_BN, GxEPD2_266_BN::HEIGHT/2> display(
    GxEPD2_266_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// ============================================================
// AUDIO
// ============================================================
BluetoothA2DPSource a2dp_source;
File             currentFile;
volatile bool    songFinished  = false;
volatile int16_t lastSample    = 0;
volatile int     fadeFrames    = 0;
volatile bool    reqNext       = false;
volatile bool    reqPrev       = false;
volatile bool    isPaused      = false;
volatile bool    sdError       = false;
static int       consecutiveOpenFails = 0;

SemaphoreHandle_t sdMutex = nullptr;
struct SdLock {
    SdLock()  { if (sdMutex) xSemaphoreTakeRecursive(sdMutex, portMAX_DELAY); }
    ~SdLock() { if (sdMutex) xSemaphoreGiveRecursive(sdMutex); }
};

static const uint32_t RING_SIZE    = 1u << 19;   // 512 KB
static const uint32_t RING_MASK    = RING_SIZE - 1;
static const uint32_t PREFILL_BYTES = 32768;
static uint8_t*       ring = nullptr;
static std::atomic<uint32_t> ringHead{0};
static std::atomic<uint32_t> ringTail{0};
static std::atomic<bool>     audioMuted{false};
static std::atomic<uint32_t> playedBytes{0};
static uint32_t       producedBytes    = 0;
// 2026-08-10 (session resume): a one-shot byte offset into the PCM data of the
// next song opened. restoreSession() sets it; openSongCommon() consumes and
// clears it, so exactly one open resumes mid-song and every open after it
// starts at 0. Declared HERE, with the audio globals, because restoreSession()
// runs several hundred lines before openSongCommon() is defined.
//
// Two things this MUST get right, both silent if wrong:
//  - the offset is measured in bytes PLAYED, not bytes produced. audioFillTask
//    runs up to 512 KB (~6 s) ahead of the speaker, so saving producedBytes
//    would skip six seconds of audio on every single resume.
//  - it is rounded DOWN to a 4-byte boundary. A frame is 16-bit stereo = 4
//    bytes; land mid-frame and the channels stay swapped for the rest of the
//    song, which sounds like a vague stereo smear rather than an obvious fault.
static uint32_t       pendingResumeBytes = 0;
volatile bool         producerEof      = false;
volatile bool         producerStarted  = false;
static TaskHandle_t   producerTaskHandle = nullptr;
static uint8_t        prodChunk[4096];
static const uint32_t PROD_CHUNK = sizeof(prodChunk);

// ============================================================
// BUTTON STATE
// ============================================================
static uint32_t btnLastPress[6] = {0};
static uint32_t btnReadyAt      = 0;
static bool     btnLastState[6];

// v1.6: Home hold-to-jump-home. Tracked separately from the normal
// press-edge logic above because a hold needs to fire mid-press (not on
// release) and must NOT also fire the regular short-press action once it
// does. homeHoldTriggered is a one-shot flag: readButton() sets it true for
// exactly one loop() pass when the threshold is crossed; loop() consumes
// (clears) it immediately after acting on it.
static const uint32_t HOME_HOLD_MS   = 700;
static uint32_t        homeHeldSince = 0;      // 0 = not currently held
static bool            homeLongFired = false;  // true once this hold has fired
static bool            homeHoldTriggered = false;

// ============================================================
// MENU STATE
// ============================================================
enum MenuState {
    MS_NOW_PLAYING=0, MS_MENU, MS_LIB_BROWSE, MS_LIB_SELECT,
    MS_ART_BROWSE, MS_ART_SELECT, MS_ALBUM_BROWSE, MS_ALBUM_SELECT,
    MS_ARTSONG_BROWSE, MS_ARTSONG_SELECT, MS_SETTINGS, MS_BLUETOOTH,
    MS_BT_SELECT, MS_BT_SCAN, MS_BT_CONNECTING, MS_SETTINGS2, MS_SCREEN_SLEEP,
    MS_SEARCH_TYPE, MS_SEARCH_BISECT, MS_SEARCH_RESULTS, MS_SEARCH_NO_MATCH
};
static MenuState menuState       = MS_NOW_PLAYING;
static int  libPage              = 0;
static bool libCatalog           = false;
static int  artPage              = 0;
static int  albumPage            = 0;
static int  artSongPage          = 0;
static char selArtist[ARTIST_LEN]= "";
static char selAlbum[ARTIST_LEN] = "";
static bool skippedAlbums        = false;
// True while browsing an artist's albums/songs that was entered via search
// rather than the normal artist browser -- routes H back to the search
// results screen (where the user actually came from) instead of MS_ART_BROWSE.
static bool artistFromSearch     = false;
static int  settingsEditIdx      = -1;
static int  settings2EditIdx     = -1;

// Search state. The bisection mechanic itself (splitRange over [searchLo,
// searchHi], with searchStack letting H pop back one level at a time) is
// shared by THREE different pools, selected by searchKind:
//   SP_ARTISTS / SP_TITLES            -- the v1.4 alphabetical browse, as-is
//   SP_SYMBOLS                        -- bisecting the on-screen "keyboard"
//                                         alphabet while composing a search
//                                         word (v1.5)
//   SP_FILTERED_ARTISTS/SP_FILTERED_TITLES -- bisecting the (possibly
//                                         non-contiguous) set of substring
//                                         matches once a word search runs
enum SearchPoolKind { SP_ARTISTS, SP_TITLES, SP_FILTERED_ARTISTS, SP_FILTERED_TITLES, SP_SYMBOLS };
static SearchPoolKind searchKind = SP_ARTISTS;
static int  searchLo = 0, searchHi = 0;
static const int SEARCH_STACK_MAX = 8;   // ceil(log5(2000)) ~= 5, plenty of headroom
static int  searchStackLo[SEARCH_STACK_MAX], searchStackHi[SEARCH_STACK_MAX];
static int  searchStackDepth = 0;

// v1.5 word entry: the fragment being composed via symbol bisection, and
// which pool (artists or titles) it will filter once OK is pressed. This is
// distinct from searchKind because searchKind==SP_SYMBOLS doesn't itself
// say which pool we're building a query FOR.
static const int SEARCH_WORD_MAX = 24;
static char searchWordBuf[SEARCH_WORD_MAX + 1] = "";
static int  searchWordLen = 0;

// v1.7: what the typed word is matched against. WS_ANY matches a song when the
// word appears in EITHER its title or its artist, which is the "I only
// remember one word of it" case that WS_TITLE alone misses.
enum WordScope { WS_TITLE, WS_ARTIST, WS_ANY };
static WordScope wordScope = WS_TITLE;

// Where H should land once the search is backed all the way out. Search can now
// be opened from the library/artist/album/song browsers as well as the search
// menu, and returning the user to the menu they didn't come from is disorienting.
static MenuState searchReturn = MS_SEARCH_TYPE;

// 39-symbol "keyboard": space, 0-9, A-Z, Backspace, OK -- bisected with the
// exact same splitRange() used for browsing, so composing a word reuses a
// control scheme the user already knows instead of introducing a new one.
static const int SYMBOL_COUNT = 39;
// v1.7: spelled-out tokens. "_" read as a letter rather than a space, and
// "<-"/"OK" were ambiguous next to real song titles in the same list.
static const char* const SYMBOL_GLYPH[SYMBOL_COUNT] = {
    "SPACE","0","1","2","3","4","5","6","7","8","9",
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    "DELETE","SEARCH"
};
static const char SYMBOL_CHAR[SYMBOL_COUNT] = {
    ' ','0','1','2','3','4','5','6','7','8','9',
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    0,0   // Backspace/OK are actions, not literal characters
};
static const int SYMBOL_BACKSPACE_IDX = 37;
static const int SYMBOL_OK_IDX        = 38;

// Match indices from the most recent word search, sized once at boot to
// max(artistCount, titleCount) and reused for every search thereafter.
static int* searchFilterIdx  = nullptr;
static int  searchFilterCap  = 0;
static int  searchFilterCount = 0;

// ============================================================
// SETTINGS
// ============================================================
// 2026-08-10: `shuffle` and `epdRefresh` removed from the struct along with
// their Settings rows. Neither had a single consumer outside load/save/draw:
// loadPlaylist() shuffled unconditionally, and "EPD refresh: N songs" could
// not mean anything because every song change already forces a full
// drawNowPlaying(). Both keys are simply ignored if still present in an older
// /settings.txt, and are dropped from the file on the next save.
// 2026-08-10: deepSleepTimeout (minutes of button inactivity before deep
// sleep, 0 = off) and deepSleepMode (0 = only while the BT link is DOWN,
// 1 = always). Split into two fields rather than one "0/90/-90" encoding
// because the two questions are independent: how long, and under what
// condition. Default is 90 minutes in BT-DOWN-only mode, which is the
// battery case -- link dropped, firmware happily decoding into nothing.
struct Settings { uint8_t playlistSize, sleepTimeout, deepSleepTimeout, deepSleepMode,
                  volStep, progStep; };
static Settings cfg = {100, 0, 90, 0, 5, 10};
static const uint8_t DSM_BT_DOWN = 0;
static const uint8_t DSM_ALWAYS  = 1;

// 2026-08-10: two new page-1 settings, both filling rows that were left blank
// when Shuffle and EPD refresh were deleted, and both with real consumers.
//
// volStep -- the Vol+/Vol- increment. 5% meant 20 presses to cross the range on
// a device whose only input is five buttons; 5..20 in steps of 5.
//
// progStep -- the progress-bar bucket, i.e. how often Now Playing spends a
// PARTIAL refresh. This is the panel-wear control the deleted "EPD refresh" row
// only pretended to be: at 5% a song costs 20 partial refreshes, at 25% it
// costs 4. Restricted to divisors of 100, because a step like 15 would make the
// last bucket 90 and the bar would never reach the end under partial refresh.
static const uint8_t PROG_STEPS[]  = {5, 10, 20, 25};
static const int     PROG_STEP_N   = sizeof(PROG_STEPS)/sizeof(PROG_STEPS[0]);
static const uint8_t VOL_STEP_MIN  = 5;
static const uint8_t VOL_STEP_MAX  = 20;
// progStepIndex() is deliberately NOT defined here -- see the note at the top
// of the file. It lives with the settings row helpers instead.
static uint32_t lastActivityMs = 0;
static int      deviceVolume   = 80;
static char     connectedDevice[32] = "";

// ============================================================
// BT SCAN STATE
// ============================================================
static const int SCAN_MAX      = 5;
static const int SCAN_NAME_LEN = 32;
static bool scanOwnsGap = false;   // v1.9.1: true while we hold the GAP cb
static char      scanNames[SCAN_MAX][SCAN_NAME_LEN];
volatile int     scanCount  = 0;
volatile bool    scanActive = false;
volatile bool    scanDone   = false;
static uint32_t  scanStartMs   = 0;
// v1.9.1: back to matching OUR inquiry length. The duration arg to
// esp_bt_gap_start_discovery() is 8 units of 1.28s = ~10.2s, so this just needs
// to outlast that and catch the DISCOVERY_STOPPED event.
static const uint32_t SCAN_TIMEOUT_MS = 13000;

static const int SAVED_MAX = 8;
static char      savedDevices[SAVED_MAX][SCAN_NAME_LEN];
static int       savedCount = 0;
static int       savedDevPage = 0;   // FIX K (wired): current page into savedDevices[]
// 2026-08-10: was `int scanLastDrawn` carrying a tri-state (-1 = nothing
// drawn, -2 = the done-screen is up, any other value unused). Only the -2
// latch was ever read, so it is now what it always was: a one-shot bool that
// stops the finished-scan screen being redrawn on every loop pass. It is NOT
// dead code -- dropping it would queue a ~4 s full refresh every 10 ms.
// Deliberately still no mid-scan repaint: results appear when the inquiry
// ends, because a redraw per discovery would cost up to 5 full refreshes
// inside a 10 s inquiry and panel refreshes are the scarce resource.
static bool      scanDoneDrawn   = false;
static bool      settingsDirty   = false;
static uint32_t  settingsDirtyAt = 0;
static const uint32_t SAVE_DELAY_MS = 2000;

// ============================================================
// METADATA / PROGRESS
// ============================================================
struct Metadata { char title[80], artist[48], album[48], length[12]; };
static Metadata currentMeta;
static char     nowPlayingPath[MAX_PATH_LEN] = "";
static uint32_t songDataStart  = 44;
static uint32_t songDataBytes  = 0;
static int      lastProgressPct = 0;

// ============================================================
// HEAP SNAPSHOT
// ============================================================
static void printHeap(const char* label) {
    Serial.printf("[MEM] %-28s free=%u largest=%u psram=%u\n", label,
        ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        ESP.getFreePsram());
}

// ============================================================
// RING BUFFER  (lock-free SPSC)
// ============================================================
static inline uint32_t ringAvailable() {
    return ringHead.load(std::memory_order_acquire)
         - ringTail.load(std::memory_order_relaxed);
}
static inline uint32_t ringFree() { return RING_SIZE - ringAvailable(); }

static void ringPush(const uint8_t* src, uint32_t len) {
    uint32_t head = ringHead.load(std::memory_order_relaxed);
    uint32_t idx  = head & RING_MASK;
    uint32_t first = RING_SIZE - idx;
    if (first > len) first = len;
    memcpy(&ring[idx], src, first);
    if (len > first) memcpy(&ring[0], src + first, len - first);
    ringHead.store(head + len, std::memory_order_release);
}

static uint32_t ringPop(uint8_t* dst, uint32_t len) {
    uint32_t tail  = ringTail.load(std::memory_order_relaxed);
    uint32_t avail = ringHead.load(std::memory_order_acquire) - tail;
    uint32_t n = (len < avail) ? len : avail;
    uint32_t idx   = tail & RING_MASK;
    uint32_t first = RING_SIZE - idx;
    if (first > n) first = n;
    memcpy(dst, &ring[idx], first);
    if (n > first) memcpy(dst + first, &ring[0], n - first);
    ringTail.store(tail + n, std::memory_order_release);
    return n;
}

static void waitForBufferFill() {
    if (!producerStarted) return;
    uint32_t t0 = millis();
    while (ringAvailable() < PREFILL_BYTES && !producerEof && !sdError
           && (millis()-t0) < 1500)
        vTaskDelay(pdMS_TO_TICKS(5));
}

// ============================================================
// AUDIO PRODUCER TASK
// ============================================================
static void audioFillTask(void*) {
    static uint32_t pdbg = 0;
    for (;;) {
        if (isPaused || sdError || producerEof || !ring
            || audioMuted.load(std::memory_order_acquire)
            || ringFree() < PROD_CHUNK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        bool didRead = false;
        {
            SdLock lock;
            if (currentFile) {
                int n = currentFile.read(prodChunk, PROD_CHUNK);
                if (n > 0) {
                    ringPush(prodChunk, (uint32_t)n);
                    producedBytes += (uint32_t)n;
                    didRead = true;
                } else {
                    if (songDataBytes > 0 && producedBytes >= songDataBytes)
                        producerEof = true;
                    else
                        sdError = true;
                }
            }
        }
        if (didRead && (pdbg++ % 64)==0)
            Serial.printf("[BUF] played=%u produced=%u fill=%u/%u\n",
                (unsigned)playedBytes.load(), (unsigned)producedBytes,
                (unsigned)ringAvailable(), (unsigned)RING_SIZE);
        if (!didRead) vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ============================================================
// BUTTONS
// ============================================================
void initButtons() {
    for (int i=0; i<6; i++) {
        uint8_t p = BTN_PINS[i];
        // GPIO 34-39 are input-only on the ESP32 and have no internal pull
        // resistors at all (not just 35) -- they need the board's external
        // pull-up. INPUT_PULLUP is a real internal pull only on 32/33/22/25/21.
        bool inputOnly = (p>=34 && p<=39);
        pinMode(p, inputOnly ? INPUT : INPUT_PULLUP);
        btnLastState[i] = true;
    }
}
// v1.8: emergency reset -- hold button 1 + Home together for RESET_HOLD_MS.
// Deliberately a separate function rather than a case inside readButton()'s
// edge logic, so any context can poll it: v1.9 calls it from guardTask (core
// 0) and from fatalHalt(), neither of which goes anywhere near readButton().
// The hold requirement is what keeps it from firing on an accidental brush of
// two buttons; the combo is 1+H specifically because neither is adjacent to
// the other on the board.
// 2026-08-10: the old "so the blocking BT teardown can poll it too (see
// busyDelay())" note is gone with busyDelay itself -- there is no blocking
// teardown any more, so nothing on the loop task needs to poll this.
//
// LIMIT, worth being clear about: this only works while code is still
// RUNNING. It cannot rescue a true hard hang or a crash inside the BT stack,
// because nothing is left to poll the pins. For that you need either the
// physical EN/reset button or the hardware task watchdog.
static volatile uint32_t resetComboSince = 0;
static bool checkResetCombo() {
    bool homeDown = !(bool)digitalRead(BTN_PINS[5]);
    bool oneDown  = !(bool)digitalRead(BTN_PINS[0]);
    if (!(homeDown && oneDown)) { resetComboSince = 0; return false; }
    uint32_t now = millis();
    if (resetComboSince == 0) { resetComboSince = now; return false; }
    if (now - resetComboSince < RESET_HOLD_MS) return false;
    Serial.println("[RESET] 1+H held -- restarting");
    Serial.flush();
    // Try to unmount cleanly, but never block on it: if the audio task is
    // wedged holding sdMutex, that's likely WHY the user is resetting, and
    // SdLock's portMAX_DELAY would hang the rescue path itself.
    if (sdMutex && xSemaphoreTakeRecursive(sdMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        if (currentFile) currentFile.close();
        SD.end();
        xSemaphoreGiveRecursive(sdMutex);
    } else {
        Serial.println("[RESET] SD busy -- restarting without unmount");
    }
    delay(40);
    ESP.restart();
    return true;                       // not reached
}

// v1.9: the reset combo now lives on its OWN task.
//
// v1.8 polled it from readButton() and from busyDelay(), both of which run on
// the Arduino loop task. That is useless in exactly the case it was built for:
// when a2dp_source.end() deadlocked, the loop task stopped running and the
// combo stopped being polled with it. The serial log ends at "[BT] step: end"
// and no amount of button holding did anything.
//
// This task is tiny, has no dependencies on anything the loop touches, and is
// pinned to core 0, so a blocked loop task cannot take it down with it. It
// also watches a heartbeat: if loop() stops ticking for LOOP_STALL_MS the
// firmware is wedged somewhere it cannot recover from, so reboot rather than
// sit there dead.
volatile uint32_t loopHeartbeat = 0;
static const uint32_t LOOP_STALL_MS = 45000;
static void guardTask(void*){
    for(;;){
        checkResetCombo();
        uint32_t hb=loopHeartbeat;
        if(hb && (millis()-hb)>LOOP_STALL_MS){
            Serial.printf("[GUARD] loop() stalled %us -- restarting\n",
                          (unsigned)((millis()-hb)/1000));
            Serial.flush(); delay(40); ESP.restart();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 2026-08-10: busyDelay() removed. It existed to keep the 1+H combo polled
// during the blocking BT teardown, but v1.9 deleted that teardown and moved
// combo polling to guardTask, after which the body was a plain delay() with
// no call sites left. Kept in the header history below; gone from the code.

// Returns 1-5 for buttons 1-5 on press, 0 for a SHORT Home press (released
// before HOME_HOLD_MS), or -1 if nothing actionable happened this call. A
// LONG Home hold is reported separately via homeHoldTriggered (checked by
// loop() right after calling this) rather than through the return value, so
// "jump to Now Playing" can fire from ANY menu state without threading a
// new case through every branch of handleButton()'s switch.
int readButton() {
    uint32_t now = millis();
    if (now < btnReadyAt) return -1;

    // While 1+H are both down we swallow all normal button meaning, otherwise
    // the gesture would also fire Home's short press and button 1's action on
    // the way to the reboot. guardTask() does the actual detection.
    if (resetComboSince != 0) {
        homeHeldSince = 0; homeLongFired = false;
        btnLastState[5] = false; btnLastState[0] = false;
        return -1;
    }

    // ---- Home (BTN_PINS[5]): hold-tracked ----
    bool homeDown = !(bool)digitalRead(BTN_PINS[5]);   // INPUT_PULLUP: low = pressed
    if (homeDown) {
        if (homeHeldSince == 0) {
            // Fresh press -- same debounce gate the other buttons use below.
            if (btnLastState[5] && (now - btnLastPress[5]) > DEBOUNCE_MS) {
                btnLastPress[5]  = now;
                homeHeldSince    = now;
                homeLongFired    = false;
            }
        } else if (!homeLongFired && (now - homeHeldSince) >= HOME_HOLD_MS) {
            homeLongFired     = true;     // fire exactly once per hold
            homeHoldTriggered = true;
        }
        btnLastState[5] = false;
    } else {
        bool firesShortPress = (homeHeldSince != 0) && !homeLongFired;
        btnLastState[5] = true;
        homeHeldSince   = 0;
        if (firesShortPress) return 0;    // released early -> normal H press
    }

    // ---- Buttons 1-5 (BTN_PINS[0..4]): unchanged simple press-edge logic ----
    // 2026-08-10, worth knowing: this returns the FIRST button found down, in
    // pin order, so pressing 1 and 2 together always reports 1. On Now Playing
    // that is Vol+, which is what made "volume down doesn't work" look like a
    // firmware fault for two versions -- it was a simultaneous press, and up
    // wins by scan order every time. Left as-is deliberately: swallowing the
    // press when both are down would also swallow fast alternating presses,
    // and the real behaviour here is correct.
    for (int i = 0; i < 5; i++) {
        bool s  = (bool)digitalRead(BTN_PINS[i]);
        bool pr = (!s && btnLastState[i] && (now - btnLastPress[i]) > DEBOUNCE_MS);
        btnLastState[i] = s;
        if (pr) { btnLastPress[i] = now; return i + 1; }
    }
    return -1;
}

// ============================================================
// SETTINGS PERSISTENCE
// ============================================================
static void deviceKey(char* out, int outLen, const char* devName) {
    snprintf(out, outLen, "vol_%s", devName);
    for (char* p=out; *p; p++) if (*p==' ') *p='_';
}

void loadSettings() {
    SdLock lock;
    File f = SD.open(SETTINGS_FILE);
    if (!f) { Serial.println("[SETTINGS] Not found, using defaults"); return; }
    while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        if (!line.length() || line[0]=='#') continue;
        int eq = line.indexOf('='); if (eq<0) continue;
        String key=line.substring(0,eq), val=line.substring(eq+1);
        // 2026-08-10: shuffle= and epd_refresh= are no longer read. An older
        // settings.txt containing them still loads fine -- they just fall
        // through as unrecognised keys.
        if (key=="playlist_size") {
            int v = val.toInt();
            if (v < 10) v = 10;
            if (v > MAX_PLAYLIST) v = MAX_PLAYLIST;
            cfg.playlistSize = (uint8_t)v;
        }
        if (key=="sleep_timeout") cfg.sleepTimeout = val.toInt();
        if (key=="deep_sleep_timeout") {
            int v = val.toInt();
            if (v < 0)   v = 0;
            if (v > 240) v = 240;
            cfg.deepSleepTimeout = (uint8_t)v;
        }
        if (key=="deep_sleep_mode")
            cfg.deepSleepMode = (val.toInt()==DSM_ALWAYS) ? DSM_ALWAYS : DSM_BT_DOWN;
        if (key=="vol_step") {
            int v = (val.toInt()/5)*5;                 // snap to the 5% grid
            if (v < VOL_STEP_MIN) v = VOL_STEP_MIN;
            if (v > VOL_STEP_MAX) v = VOL_STEP_MAX;
            cfg.volStep = (uint8_t)v;
        }
        if (key=="prog_step") {
            // Must be one of PROG_STEPS; anything else (hand-edited file, older
            // firmware) falls back to 10 rather than producing a bar that never
            // reaches the end.
            int v = val.toInt(); cfg.progStep = 10;
            for (int i=0;i<PROG_STEP_N;i++) if (PROG_STEPS[i]==v) cfg.progStep=(uint8_t)v;
        }
    }
    f.close(); Serial.println("[SETTINGS] Loaded");
}

int loadDeviceVolume(const char* devName) {
    SdLock lock;
    char key[48]; deviceKey(key, sizeof(key), devName);
    File f = SD.open(SETTINGS_FILE); if (!f) return 80;
    while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        int eq = line.indexOf('='); if (eq<0) continue;
        if (line.substring(0,eq)==key) { f.close(); return line.substring(eq+1).toInt(); }
    }
    f.close(); return 80;
}

// ============================================================
// SAVED DEVICE LIST
// ============================================================
void loadSavedDevices() {
    SdLock lock; savedCount=0;
    File f = SD.open(BTDEVICES_FILE);
    if (!f) {
        for (int i=0; i<BT_CANDIDATE_COUNT && savedCount<SAVED_MAX; i++)
            strncpy(savedDevices[savedCount++], BT_CANDIDATES[i], SCAN_NAME_LEN-1);
        return;
    }
    while (f.available() && savedCount<SAVED_MAX) {
        String line = f.readStringUntil('\n'); line.trim();
        if (line.length() && line.length()<SCAN_NAME_LEN) {
            strncpy(savedDevices[savedCount], line.c_str(), SCAN_NAME_LEN-1);
            savedDevices[savedCount][SCAN_NAME_LEN-1]='\0'; savedCount++;
        }
    }
    f.close(); Serial.printf("[BT] Loaded %d saved devices\n", savedCount);
}

void saveBTDevices() {
    SdLock lock;
    File f = SD.open(BTDEVICES_FILE, FILE_WRITE);
    if (!f) { Serial.println("[BT] Cannot write btdevices.txt"); return; }
    for (int i=0; i<savedCount; i++) f.printf("%s\n", savedDevices[i]);
    f.close(); Serial.printf("[BT] Saved %d devices\n", savedCount);
}

void addSavedDevice(const char* name) {
    char rebuilt[SAVED_MAX][SCAN_NAME_LEN]; int n=0;
    strncpy(rebuilt[n], name, SCAN_NAME_LEN-1); rebuilt[n][SCAN_NAME_LEN-1]='\0'; n++;
    for (int i=0; i<savedCount && n<SAVED_MAX; i++) {
        if (strncmp(savedDevices[i], name, SCAN_NAME_LEN-1)==0) continue;
        memcpy(rebuilt[n++], savedDevices[i], SCAN_NAME_LEN);
    }
    memcpy(savedDevices, rebuilt, (size_t)n*SCAN_NAME_LEN); savedCount=n;
    saveBTDevices();
}

// ============================================================
// SETTINGS SAVE
// ============================================================
void saveSettings() {
    SdLock lock;
    static const int MAX_VOL=16; char volLines[MAX_VOL][64]; int vlc=0;
    char curKey[48]=""; if (connectedDevice[0]) deviceKey(curKey,sizeof(curKey),connectedDevice);
    File rf = SD.open(SETTINGS_FILE);
    if (rf) {
        while (rf.available() && vlc<MAX_VOL) {
            String line=rf.readStringUntil('\n'); line.trim();
            if (line.startsWith("vol_")) {
                int eq=line.indexOf('=');
                String k=(eq>0)?line.substring(0,eq):line;
                if (k!=String(curKey)) { strncpy(volLines[vlc],line.c_str(),63); volLines[vlc++][63]='\0'; }
            }
        }
        rf.close();
    }
    File f=SD.open(SETTINGS_FILE,FILE_WRITE); if(!f){Serial.println("[SETTINGS] Cannot write");return;}
    f.printf("playlist_size=%d\nsleep_timeout=%d\ndeep_sleep_timeout=%d\ndeep_sleep_mode=%d\n"
             "vol_step=%d\nprog_step=%d\n",
        cfg.playlistSize,cfg.sleepTimeout,cfg.deepSleepTimeout,cfg.deepSleepMode,
        cfg.volStep,cfg.progStep);
    for (int i=0;i<vlc;i++) f.printf("%s\n",volLines[i]);
    if (curKey[0]) f.printf("%s=%d\n",curKey,deviceVolume);
    f.close();
    Serial.printf("[SETTINGS] Saved (dev=%s vol=%d)\n", connectedDevice[0]?connectedDevice:"none", deviceVolume);
}

// ============================================================
// GAP CALLBACK
// ============================================================
// The library declares this C wrapper (BluetoothA2DPCommon.cpp) and registers
// it as its own GAP handler. We borrow GAP during a scan and MUST hand it back
// -- see endScan(). Redeclaring it here is harmless if the library header
// already does.
extern "C" void ccall_app_gap_callback(esp_bt_gap_cb_event_t event,
                                       esp_bt_gap_cb_param_t* param);

// v1.9.1: our own inquiry is BACK, because the library only runs discovery
// while it is DISCONNECTED and hunting for a target. Once a link is up, every
// 0xff00 heartbeat goes to bt_app_av_state_connected_hdlr ("media ready
// checking") and no DISC_RES event ever arrives again -- so a scan that merely
// listened to the library found nothing while connected, which is precisely
// what v1.9 shipped. The v1.8 bug was never RESTORING the library's callback,
// not owning the inquiry, so that is the only part that changes.
// 2026-08-10: MASK FIX. This tested the wrong bit for two versions.
//
// A CoD is: bits 2-7 minor device class, 8-12 major device class, 13-23 major
// SERVICE class. Shifting right by 13 puts service bit 13 at position 0, so
// Audio (bit 21) lands at position 8 = 0x100. The old test masked 0x08, i.e.
// position 3 = bit 16 = POSITIONING, which is not a thing headphones set.
//
// It went unnoticed because the second clause caught everything in practice.
// The JBL from the v1.9.2 log, CoD=0x240404: service field = 0x120, so
// 0x120 & 0x08 == 0 (old test FAILED) while 0x120 & 0x100 != 0 (Audio, correct
// test passes) -- it was only ever found by major class 4 (Audio/Video). A
// headset that advertises the Audio service under some other major class was
// silently filtered out of every scan.
static bool isAudioDevice(uint32_t cod) {
    const uint32_t service = (cod >> 13) & 0x7FF;   // major service class bits
    const uint32_t major   = (cod >> 8)  & 0x1F;    // major device class
    return (service & 0x100) || major == 4;         // Audio service, or A/V device
}
static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
    if (!scanActive) return;
    if (event==ESP_BT_GAP_DISC_RES_EVT) {
        char name[SCAN_NAME_LEN]=""; uint32_t cod=0;
        for (int i=0;i<param->disc_res.num_prop;i++) {
            esp_bt_gap_dev_prop_t* p=&param->disc_res.prop[i];
            if (p->type==ESP_BT_GAP_DEV_PROP_EIR) {
                uint8_t* eir=(uint8_t*)p->val; uint8_t nl=0;
                uint8_t* nm=esp_bt_gap_resolve_eir_data(eir,ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,&nl);
                if (!nm) nm=esp_bt_gap_resolve_eir_data(eir,ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME,&nl);
                if (nm&&nl>0&&nl<SCAN_NAME_LEN){memcpy(name,nm,nl);name[nl]='\0';}
            }
            if (p->type==ESP_BT_GAP_DEV_PROP_COD) cod=*(uint32_t*)p->val;
            if (p->type==ESP_BT_GAP_DEV_PROP_BDNAME&&name[0]=='\0') {
                int l=min((int)p->len,SCAN_NAME_LEN-1); memcpy(name,p->val,l); name[l]='\0';
            }
        }
        if (!name[0]) return;
        if (!isAudioDevice(cod)&&cod!=0) return;
        for (int i=0;i<scanCount;i++) if (strncmp(scanNames[i],name,SCAN_NAME_LEN-1)==0) return;
        if (scanCount<SCAN_MAX) {
            strncpy(scanNames[scanCount],name,SCAN_NAME_LEN-1);
            scanNames[scanCount][SCAN_NAME_LEN-1]='\0'; scanCount++;
            Serial.printf("[SCAN] Found: %s (CoD=0x%06x)\n",name,cod);
        }
        if (scanCount>=SCAN_MAX) scanDone=true;   // endScan() stops+restores
    }
    if (event==ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
        if (param->disc_st_chg.state==ESP_BT_GAP_DISCOVERY_STOPPED) {
            scanActive=false; scanDone=true; Serial.println("[SCAN] Discovery stopped");
        }
    }
}

// ============================================================
// VOLUME + AVRC + BT CALLBACKS
// ============================================================
void applyVolume() { a2dp_source.set_volume((uint8_t)((int32_t)deviceVolume*127/100)); }

void requestNextSong(); void requestPrevSong();

static void onAvrcPassthrough(uint8_t key, bool isReleased) {
    if (isReleased) return;
    switch(key) {
        case ESP_AVRC_PT_CMD_PLAY:     isPaused=false; break;
        case ESP_AVRC_PT_CMD_PAUSE:    isPaused=true;  break;
        case ESP_AVRC_PT_CMD_FORWARD:  requestNextSong(); break;
        case ESP_AVRC_PT_CMD_BACKWARD: requestPrevSong(); break;
        default: Serial.printf("[AVRC] key 0x%02x\n",key); break;
    }
}
volatile bool btJustConnected=false;
volatile bool btIsConnected=false;                 // v1.9: real link state
static char   btTargetName[SCAN_NAME_LEN]="";      // v1.9: explicit user pick
static volatile bool btConnectBusy=false;
static char   btPrevDevice[SCAN_NAME_LEN]="";      // v1.9.2: restore on failure
static int    btConnectTry=0;                      // v1.9.2: kick counter
static uint32_t btLastKickAt=0;
static const uint32_t BT_KICK_INTERVAL_MS=12000;   // one inquiry per attempt
static const int      BT_CONNECT_MAX_TRIES=4;      // ~48s total
static char pendingConnectName[32]="";
static void onConnectionStateChanged(esp_a2d_connection_state_t state, void*) {
    if (state==ESP_A2D_CONNECTION_STATE_CONNECTED){
        btJustConnected=true;btIsConnected=true;Serial.println("[BT] Connected");
    } else if (state==ESP_A2D_CONNECTION_STATE_DISCONNECTED){
        btIsConnected=false;Serial.println("[BT] Disconnected");
    }
}
// v1.9: this is now the ONLY place discovery results are handled. It does two
// jobs: it fills the scan list for the UI, and it tells the library which
// device to connect to.
//
// Previously startScan() called esp_bt_gap_register_callback(gapCallback),
// which REPLACED the library's own GAP handler and never put it back. After
// one scan the library was permanently blind -- it kept logging "Discovery
// started" forever but never saw a single result, so it could never connect
// to anything again. The serial log shows it plainly: zero ccall_app_gap_
// callback lines after the first [SCAN]. Using the library's own hook means
// we never touch its callback registration at all.
bool onSsidFound(const char* ssid, esp_bd_addr_t, int) {
    if (!ssid||!ssid[0]) return false;

    // Feed the scan screen (deduped) whenever the user is looking at it.
    if (scanActive) {
        bool known=false;
        for (int i=0;i<scanCount;i++)
            if (strncmp(scanNames[i],ssid,SCAN_NAME_LEN-1)==0){known=true;break;}
        if (!known && scanCount<SCAN_MAX) {
            strncpy(scanNames[scanCount],ssid,SCAN_NAME_LEN-1);
            scanNames[scanCount][SCAN_NAME_LEN-1]='\0'; scanCount++;
            Serial.printf("[SCAN] Found: %s\n",ssid);
        }
    }

    // An explicit pick beats the saved list: connect to that and nothing else.
    if (btTargetName[0]) {
        if (strncmp(btTargetName,ssid,SCAN_NAME_LEN-1)!=0) return false;
        strncpy(pendingConnectName,ssid,sizeof(pendingConnectName)-1);
        pendingConnectName[sizeof(pendingConnectName)-1]='\0';
        Serial.printf("[BT] Accepting target: %s\n",ssid); return true;
    }

    // v1.9.2: with no explicit target, accept a saved device ONLY while we are
    // not already connected. Without this guard the library would happily
    // abandon a working link for whichever other saved device its next inquiry
    // happened to see first -- exactly what dropped C17A for the JBL at 107s.
    if (btIsConnected) return false;

    for (int i=0;i<savedCount;i++)
        if (strncmp(savedDevices[i],ssid,SCAN_NAME_LEN-1)==0) {
            strncpy(pendingConnectName,ssid,sizeof(pendingConnectName)-1);
            pendingConnectName[sizeof(pendingConnectName)-1]='\0';
            Serial.printf("[BT] Accepting: %s\n",ssid); return true;
        }
    return false;
}

// ============================================================
// A2DP CALLBACK  (pure consumer - never touches SD)
// ============================================================
int32_t get_audio_data(Frame* data, int32_t frameCount) {
    if (audioMuted.load(std::memory_order_acquire)||isPaused||sdError) {
        for (int i=0;i<frameCount;i++) {
            int16_t s=(fadeFrames>0)?(int16_t)((int32_t)lastSample*fadeFrames/FADE_FRAMES):0;
            if (fadeFrames>0) fadeFrames--;
            data[i].channel1=s; data[i].channel2=s;
        }
        return frameCount;
    }
    uint8_t tmp[512]; int done=0;
    while (done<frameCount) {
        int want=frameCount-done; if(want>256)want=256;
        uint32_t got=ringPop(tmp,(uint32_t)want*2);
        int fg=(int)(got/2);
        for (int i=0;i<fg;i++){int16_t s;memcpy(&s,&tmp[i*2],2);lastSample=s;data[done+i].channel1=s;data[done+i].channel2=s;}
        if (fg>0) playedBytes.fetch_add((uint32_t)fg*2,std::memory_order_relaxed);
        done+=fg; if(fg<want)break;
    }
    if (done<frameCount) {
        for (int i=done;i<frameCount;i++){
            int16_t s=(fadeFrames>0)?(int16_t)((int32_t)lastSample*fadeFrames/FADE_FRAMES):0;
            if(fadeFrames>0)fadeFrames--;
            data[i].channel1=s;data[i].channel2=s;
        }
    } else fadeFrames=FADE_FRAMES;
    return frameCount;
}

// ============================================================
// PLAYLIST  (reservoir sampling)
// ============================================================
static void normalisePath(char* out,int outLen,const String& raw){
    if(raw[0]!='/'){out[0]='/';strncpy(out+1,raw.c_str(),outLen-2);out[outLen-1]='\0';}
    else{strncpy(out,raw.c_str(),outLen-1);out[outLen-1]='\0';}
}
// 2026-08-10: cfg.playlistSize is now WIRED. It was previously read from
// settings.txt, shown on the Settings page, edited, and saved -- and used by
// nothing at all; the reservoir was always MAX_PLAYLIST. playlistCap is the
// resolved queue length for this boot, cached here so playSongByPath() uses
// the same number and the queue can't drift above what the user asked for.
//
// The buffer is still allocated at MAX_PLAYLIST regardless of the setting, so
// the setting can only take effect at boot -- see drawSettings(), which says
// so on the edit footer rather than leaving the user to wonder.
static int playlistCap = MAX_PLAYLIST;
// fatalHalt() is defined next to setup(), far below this point, but the boot
// failure paths that need it start here. Forward-declared rather than moved,
// because it depends on checkResetCombo() and belongs with the other boot code.
static void fatalHalt(const char* what);

// 2026-08-10: allocation split out so restoreSession() can fill the same
// buffer without duplicating the ps_malloc (and without the two drifting on
// size). Idempotent: whichever of the two paths runs first allocates.
static bool allocPlaylist(){
    if (playlist) return true;
    playlist=(char(*)[MAX_PATH_LEN])ps_malloc((size_t)MAX_PLAYLIST*MAX_PATH_LEN);
    return playlist != nullptr;
}
// Resolve cfg.playlistSize into the live cap. Shared by loadPlaylist() and
// restoreSession(), which must not disagree about it.
static void resolvePlaylistCap(){
    playlistCap = cfg.playlistSize;
    if (playlistCap < 1)            playlistCap = 1;
    if (playlistCap > MAX_PLAYLIST) playlistCap = MAX_PLAYLIST;
}
void loadPlaylist() {
    SdLock lock;
    // 2026-08-10: was `while(1)delay(1000);` -- the last survivor of the
    // pattern v1.8 replaced everywhere else. A silent forever-hang with a blank
    // panel is indistinguishable from a dead board; fatalHalt() says what
    // happened on serial, keeps saying it, and honours the 1+H reset combo.
    if (!allocPlaylist()) fatalHalt("Playlist alloc failed");
    resolvePlaylistCap();
    playlistCount=0; randomSeed(micros());
    File f=SD.open(CATALOG_FILE);
    if (!f){Serial.println("[ERROR] catalog.txt missing");return;}
    uint32_t seen=0;
    while (f.available()&&seen<MAX_CATALOG){
        String line=f.readStringUntil('\n');line.trim();
        if(!line.length()||line.length()>=MAX_PATH_LEN) continue;
        char norm[MAX_PATH_LEN]; normalisePath(norm,sizeof(norm),line); seen++;
        if (playlistCount<playlistCap) memcpy(playlist[playlistCount++],norm,MAX_PATH_LEN);
        else{uint32_t r=random(0,seen);if(r<(uint32_t)playlistCap)memcpy(playlist[r],norm,MAX_PATH_LEN);}
    }
    f.close();
    for (int i=playlistCount-1;i>0;i--){
        int j=random(0,i+1); char tmp[MAX_PATH_LEN];
        memcpy(tmp,playlist[i],MAX_PATH_LEN);
        memcpy(playlist[i],playlist[j],MAX_PATH_LEN);
        memcpy(playlist[j],tmp,MAX_PATH_LEN);
    }
    Serial.printf("[PLAYLIST] %d songs from %u\n",playlistCount,seen);
    printHeap("after loadPlaylist");
}

// ============================================================
// SESSION SAVE / RESTORE  (2026-08-10)
// ============================================================
// Deep sleep resets the SoC, so the shuffled queue and the position in the
// current song are gone unless they are written to SD first. Format is plain
// text, one field per line, guarded by a magic line -- a truncated or
// half-written file fails the field count and is discarded rather than
// half-applied.
//
//   SESSION 1
//   <songIndex>
//   <resume bytes into PCM data, 4-byte aligned>
//   <playlistCount>
//   <nowPlayingPath>
//   <playlist path 0> ... <playlist path n-1>
//
// nowPlayingPath is stored separately from playlist[songIndex] on purpose:
// playSongByPath() can insert a track, and nowPlayingPath is the authoritative
// record of what was actually open.
static bool saveSession(){
    if(!playlist||playlistCount<=0) return false;
    // Bounded mutex take, NOT SdLock. Same rule as checkResetCombo(): a wedged
    // audioFillTask holding sdMutex must not hang the sleep path. Losing the
    // session is a far better outcome than never reaching esp_deep_sleep_start().
    if(!sdMutex||xSemaphoreTakeRecursive(sdMutex,pdMS_TO_TICKS(500))!=pdTRUE){
        Serial.println("[SESSION] SD busy -- not saved");
        return false;
    }
    bool ok=false;
    SD.remove(SESSION_FILE);
    File f=SD.open(SESSION_FILE,FILE_WRITE);
    if(f){
        uint32_t resume=playedBytes.load(std::memory_order_relaxed)&~3u;
        if(resume>songDataBytes) resume=0;
        f.printf("%s\n%d\n%u\n%d\n%s\n",SESSION_MAGIC,songIndex,
                 (unsigned)resume,playlistCount,nowPlayingPath);
        for(int i=0;i<playlistCount;i++) f.printf("%s\n",playlist[i]);
        f.close();
        Serial.printf("[SESSION] Saved %d songs, index %d, resume %u\n",
                      playlistCount,songIndex,(unsigned)resume);
        ok=true;
    } else Serial.println("[SESSION] Cannot write session.txt");
    xSemaphoreGiveRecursive(sdMutex);
    return ok;
}

// Returns true only if a complete session was applied. Every failure path
// leaves playlistCount at 0 so the caller falls back to a fresh loadPlaylist().
static bool restoreSession(){
    SdLock lock;
    File f=SD.open(SESSION_FILE);
    if(!f){Serial.println("[SESSION] No session file");return false;}
    auto rd=[&](String& s)->bool{
        if(!f.available())return false;
        s=f.readStringUntil('\n');s.trim();return true;
    };
    String magic,sIdx,sResume,sCount,sPath;
    bool hdr = rd(magic)&&rd(sIdx)&&rd(sResume)&&rd(sCount)&&rd(sPath);
    if(!hdr||magic!=String(SESSION_MAGIC)){
        f.close();SD.remove(SESSION_FILE);
        Serial.println("[SESSION] Bad or truncated header -- discarding");
        return false;
    }
    int count=sCount.toInt();
    if(count<1||count>MAX_PLAYLIST){
        f.close();SD.remove(SESSION_FILE);
        Serial.printf("[SESSION] Implausible count %d -- discarding\n",count);
        return false;
    }
    if(!allocPlaylist()){f.close();Serial.println("[SESSION] Playlist alloc failed");return false;}
    int n=0;
    while(f.available()&&n<count){
        String line=f.readStringUntil('\n');line.trim();
        if(!line.length()||line.length()>=MAX_PATH_LEN)continue;
        char norm[MAX_PATH_LEN];normalisePath(norm,sizeof(norm),line);
        memcpy(playlist[n++],norm,MAX_PATH_LEN);
    }
    f.close();
    if(n!=count){
        SD.remove(SESSION_FILE);
        Serial.printf("[SESSION] Short file (%d of %d paths) -- discarding\n",n,count);
        playlistCount=0;
        return false;
    }
    playlistCount=n;
    songIndex=sIdx.toInt();
    if(songIndex<0||songIndex>=playlistCount)songIndex=0;
    pendingResumeBytes=(uint32_t)sResume.toInt();
    // The restored queue may be longer than the current cfg.playlistSize (the
    // setting changed while asleep, or the session predates the change). Honour
    // the queue we actually have, or playSongByPath() would wrap inside it.
    resolvePlaylistCap();
    if(playlistCount>playlistCap)playlistCap=playlistCount;
    // Consume it: a failed save next time must not silently restore this one.
    SD.remove(SESSION_FILE);
    Serial.printf("[SESSION] Restored %d songs, index %d, resume %u\n",
                  playlistCount,songIndex,(unsigned)pendingResumeBytes);
    printHeap("after restoreSession");
    return true;
}

// ============================================================
// CATALOG INDEX
// ============================================================
void buildCatalogIndex() {
    SdLock lock;
    catalogOffsets=(uint32_t*)ps_malloc(MAX_CATALOG*sizeof(uint32_t));
    if(!catalogOffsets){Serial.println("[WARN] Catalog index alloc failed");return;}
    File f=SD.open(CATALOG_FILE); if(!f)return;
    catalogCount=0;
    while(f.available()&&catalogCount<MAX_CATALOG){
        uint32_t pos=(uint32_t)f.position();
        String line=f.readStringUntil('\n');line.trim();
        if(line.length()&&line.length()<MAX_PATH_LEN) catalogOffsets[catalogCount++]=pos;
    }
    f.close(); Serial.printf("[CATALOG] Indexed %d songs\n",catalogCount);
    printHeap("after buildCatalogIndex");
}

static void parsePathTitleArtist(const char* path,char* title,char* artist){
    String p(path);
    int ls=p.lastIndexOf('/'),dot=p.lastIndexOf('.');
    String t=(ls>=0&&dot>ls)?p.substring(ls+1,dot):p.substring(ls+1);
    String a=""; int fs=p.indexOf('/');
    if(fs>=0){int ss=p.indexOf('/',fs+1);if(ss>fs)a=p.substring(fs+1,ss);}
    strncpy(title,t.c_str(),79);title[79]='\0';
    strncpy(artist,a.c_str(),47);artist[47]='\0';
}

int readListPage(bool useCatalog,int page,char out_titles[][80],char out_artists[][48]){
    int total=useCatalog?catalogCount:playlistCount;
    int start=page*PAGE; if(start>=total)return 0;
    int count=min(PAGE,total-start);
    if(!useCatalog){
        for(int i=0;i<count;i++) parsePathTitleArtist(playlist[start+i],out_titles[i],out_artists[i]);
    } else {
        SdLock lock; File f=SD.open(CATALOG_FILE); if(!f)return 0;
        for(int i=0;i<count;i++){
            f.seek(catalogOffsets[start+i]);
            String line=f.readStringUntil('\n');line.trim();
            if(line[0]!='/') line="/"+line;
            parsePathTitleArtist(line.c_str(),out_titles[i],out_artists[i]);
        }
        f.close();
    }
    return count;
}

String getEntryPath(bool useCatalog,int page,int slot){
    int idx=page*PAGE+slot;
    if(!useCatalog){if(idx<0||idx>=playlistCount)return "";return String(playlist[idx]);}
    if(!catalogOffsets||idx>=catalogCount)return "";
    SdLock lock; File f=SD.open(CATALOG_FILE); if(!f)return "";
    f.seek(catalogOffsets[idx]); String line=f.readStringUntil('\n'); f.close();
    line.trim(); if(line[0]!='/') line="/"+line; return line;
}

// Sibling to getEntryPath, but keyed by a raw catalog.txt byte offset rather
// than a page/slot -- used by title search results (titleIndex stores the
// offset directly since titles aren't naturally page-ordered).
String getPathAtOffset(uint32_t offset){
    SdLock lock; File f=SD.open(CATALOG_FILE); if(!f)return "";
    f.seek(offset); String line=f.readStringUntil('\n'); f.close();
    line.trim(); if(line.length()&&line[0]!='/') line="/"+line; return line;
}

// ============================================================
// ARTIST LIST
// ============================================================
void buildArtistList() {
    SdLock lock;
    artistList=(char(*)[ARTIST_LEN])ps_malloc(MAX_ARTISTS*ARTIST_LEN);
    if(!artistList){Serial.println("[WARN] Artist list alloc failed");return;}
    artistCount=0;
    File f=SD.open(CATALOG_FILE); if(!f)return;
    while(f.available()){
        String line=f.readStringUntil('\n');line.trim();
        if(!line.length())continue; if(line[0]!='/')line="/"+line;
        int s1=line.indexOf('/'),s2=line.indexOf('/',s1+1); if(s2<0)continue;
        String artist=line.substring(s1+1,s2);
        if(!artist.length()||artist.length()>=ARTIST_LEN)continue;
        bool found=false;
        for(int i=0;i<artistCount;i++) if(strncmp(artistList[i],artist.c_str(),ARTIST_LEN-1)==0){found=true;break;}
        if(!found&&artistCount<MAX_ARTISTS){
            strncpy(artistList[artistCount++],artist.c_str(),ARTIST_LEN-1);
            artistList[artistCount-1][ARTIST_LEN-1]='\0';
        }
    }
    f.close();
    // bubble sort
    for(int i=0;i<artistCount-1;i++)
        for(int j=0;j<artistCount-1-i;j++)
            if(strcmp(artistList[j],artistList[j+1])>0){
                char tmp[ARTIST_LEN];
                memcpy(tmp,artistList[j],ARTIST_LEN);
                memcpy(artistList[j],artistList[j+1],ARTIST_LEN);
                memcpy(artistList[j+1],tmp,ARTIST_LEN);
            }
    Serial.printf("[ARTISTS] %d unique\n",artistCount);
    printHeap("after buildArtistList");
}

// Build a title-sorted index over the full catalog for text search.
// Reuses catalogOffsets (already built by buildCatalogIndex) so this is a
// single linear pass over the catalog file -- no re-scan needed.
static int titleEntryCmp(const void* a, const void* b) {
    return strcmp(((const TitleEntry*)a)->title, ((const TitleEntry*)b)->title);
}
// artistList[] is sorted by buildArtistList(), so a binary search resolves each
// catalog entry's artist in ~8 comparisons instead of a 256-entry linear scan.
static int artistIndexOf(const char* name) {
    if (!artistList || !name || !*name) return -1;
    int lo = 0, hi = artistCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strncmp(artistList[mid], name, ARTIST_LEN - 1);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

void buildTitleIndex() {
    SdLock lock;
    if (!catalogOffsets || catalogCount == 0) { Serial.println("[WARN] No catalog index for titles"); return; }
    titleIndex = (TitleEntry*)ps_malloc((size_t)catalogCount * sizeof(TitleEntry));
    if (!titleIndex) { Serial.println("[WARN] Title index alloc failed"); return; }
    titleCount = 0;

    File f = SD.open(CATALOG_FILE);
    if (!f) return;
    for (int i = 0; i < catalogCount; i++) {
        f.seek(catalogOffsets[i]);
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.length()) continue;
        if (line[0] != '/') line = "/" + line;
        int ls = line.lastIndexOf('/'), dot = line.lastIndexOf('.');
        String t = (ls >= 0 && dot > ls) ? line.substring(ls + 1, dot) : line.substring(ls + 1);
        if (!t.length()) continue;
        strncpy(titleIndex[titleCount].title, t.c_str(), 79);
        titleIndex[titleCount].title[79] = '\0';
        titleIndex[titleCount].catalogOffset = catalogOffsets[i];
        // /Artist/Album/Track.wav -> the segment between the 1st and 2nd '/'
        int s1 = line.indexOf('/'), s2 = line.indexOf('/', s1 + 1);
        titleIndex[titleCount].artistIdx =
            (s2 > s1) ? (int16_t)artistIndexOf(line.substring(s1 + 1, s2).c_str()) : (int16_t)-1;
        titleCount++;
    }
    f.close();

    qsort(titleIndex, titleCount, sizeof(TitleEntry), titleEntryCmp);
    Serial.printf("[TITLES] %d indexed\n", titleCount);
    printHeap("after buildTitleIndex");
}

// v1.5: reusable match-index buffer for "Find by Word", sized once to the
// larger of the two pools it might ever need to hold matches from.
void buildSearchFilterBuf() {
    searchFilterCap = max(artistCount, titleCount);
    if (searchFilterCap <= 0) { Serial.println("[WARN] No pools to size search filter for"); return; }
    searchFilterIdx = (int*)ps_malloc((size_t)searchFilterCap * sizeof(int));
    if (!searchFilterIdx) { Serial.println("[WARN] Search filter buffer alloc failed"); searchFilterCap = 0; return; }
    Serial.printf("[SEARCH] Filter buffer sized for %d entries\n", searchFilterCap);
    printHeap("after buildSearchFilterBuf");
}

// ============================================================
// SEARCH  (5-way bisection over a sorted pool -- artists or titles)
// ============================================================

// Split [lo,hi] (inclusive) into up to 5 contiguous, near-equal sub-ranges.
// Returns the number of non-empty groups produced (<=5); fills outLo/outHi.
// Shared by item bisection (artists/titles/filtered matches) AND symbol
// bisection (letter entry) -- same math, different pool underneath.
static int splitRange(int lo, int hi, int outLo[5], int outHi[5]) {
    int n = hi - lo + 1;
    int k = min(5, n);
    int base = n / k, rem = n % k;
    int cur = lo;
    for (int i = 0; i < k; i++) {
        int size = base + (i < rem ? 1 : 0);
        outLo[i] = cur;
        outHi[i] = cur + size - 1;
        cur += size;
    }
    return k;
}

// Draws whichever search screen matches the CURRENT menuState. Several
// entry points (enterSearch, runWordSearch) can land on any of these, so
// callers redraw via this instead of guessing which one applies.
void drawSearchType();      // fwd decl (defined in DISPLAY SCREENS)
void drawSearchBisect();    // fwd decl (defined in DISPLAY SCREENS)
void drawSearchResults();   // fwd decl (defined in DISPLAY SCREENS)
void drawSearchNoMatch();   // fwd decl (defined in DISPLAY SCREENS)
void drawAlbums(bool selectMode);       // fwd decl (defined in DISPLAY SCREENS)
void drawArtistSongs(bool selectMode);  // fwd decl (defined in DISPLAY SCREENS)
void drawForState() {
    switch (menuState) {
        case MS_SEARCH_TYPE:     drawSearchType();     break;
        case MS_SEARCH_BISECT:   drawSearchBisect();   break;
        case MS_SEARCH_RESULTS:  drawSearchResults();  break;
        case MS_SEARCH_NO_MATCH: drawSearchNoMatch();  break;
        default: break;
    }
}

static bool searchIsArtistKind() { return searchKind == SP_ARTISTS || searchKind == SP_FILTERED_ARTISTS; }

static int searchPoolSize() {
    switch (searchKind) {
        case SP_ARTISTS:          return artistCount;
        case SP_TITLES:           return titleCount;
        case SP_FILTERED_ARTISTS:
        case SP_FILTERED_TITLES:  return searchFilterCount;
        case SP_SYMBOLS:          return SYMBOL_COUNT;
    }
    return 0;
}
static const char* searchPoolItem(int idx) {
    switch (searchKind) {
        case SP_ARTISTS:          return artistList[idx];
        case SP_TITLES:           return titleIndex[idx].title;
        case SP_FILTERED_ARTISTS: return artistList[searchFilterIdx[idx]];
        case SP_FILTERED_TITLES:  return titleIndex[searchFilterIdx[idx]].title;
        case SP_SYMBOLS:          return SYMBOL_GLYPH[idx];
    }
    return "";
}
// Catalog offset for a title-pool position, indirecting through the filter
// array when the pool is SP_FILTERED_TITLES. Only valid for title pools.
static uint32_t searchItemCatalogOffset(int idx) {
    int realIdx = (searchKind == SP_FILTERED_TITLES) ? searchFilterIdx[idx] : idx;
    return titleIndex[realIdx].catalogOffset;
}

// Case-insensitive "does hay contain needle anywhere" -- the actual engine
// behind "Find by Word". Plain substring, not a full regex: on a 5-button
// device with no keyboard, a typed fragment matched anywhere in the name
// gets you "I don't remember the exact title" without needing wildcard
// syntax to type out symbol-by-symbol.
static bool strCaseContains(const char* hay, const char* needle) {
    if (!*needle) return true;
    size_t hn = strlen(hay), nn = strlen(needle);
    if (nn > hn) return false;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) break;
        }
        if (j == nn) return true;
    }
    return false;
}

static bool autoOpenSingleResult();   // fwd decl (defined below, needs enterArtist)

void enterSearch(bool byArtist) {
    searchKind = byArtist ? SP_ARTISTS : SP_TITLES;
    searchReturn = MS_SEARCH_TYPE;   // A-Z browse is only reachable from the menu
    searchStackDepth = 0;
    searchLo = 0;
    searchHi = searchPoolSize() - 1;
    if (searchHi < 0) {
        Serial.println("[SEARCH] Empty pool");
        menuState = MS_SEARCH_TYPE;   // explicit: nothing to search, stay put
        return;
    }
    if (searchHi - searchLo + 1 <= PAGE) {
        menuState = MS_SEARCH_RESULTS;
        autoOpenSingleResult();               // v1.8: 1 artist -> open it
    }
    else                                 menuState = MS_SEARCH_BISECT;
}

// Enter word-entry mode: reset the typed buffer and start bisecting the
// 39-symbol alphabet from the top, same shape as enterSearch() above.
void enterWordSearch(WordScope scope, MenuState origin = MS_SEARCH_TYPE) {
    if (!searchFilterIdx) {          // buffer alloc failed at boot
        Serial.println("[SEARCH] Word search unavailable (no filter buffer)");
        menuState = MS_SEARCH_TYPE;
        return;
    }
    wordScope     = scope;
    searchReturn  = origin;
    searchWordBuf[0] = '\0';
    searchWordLen = 0;
    searchKind = SP_SYMBOLS;
    searchStackDepth = 0;
    searchLo = 0;
    searchHi = SYMBOL_COUNT - 1;
    menuState = MS_SEARCH_BISECT;
}

// Does catalog entry i satisfy the current query? Title always; artist too when
// the scope is WS_ANY (resolved through artistIdx, no SD access).
static bool titleMatchesWord(int i) {
    if (strCaseContains(titleIndex[i].title, searchWordBuf)) return true;
    if (wordScope == WS_ANY) {
        int a = titleIndex[i].artistIdx;
        if (a >= 0 && a < artistCount && strCaseContains(artistList[a], searchWordBuf)) return true;
    }
    return false;
}

// Count only -- drives the live "n hits" readout on the typing screen without
// disturbing searchFilterIdx[].
int countWordMatches() {
    if (searchWordLen == 0) return 0;
    int n = 0;
    if (wordScope == WS_ARTIST) {
        for (int i = 0; i < artistCount; i++)
            if (strCaseContains(artistList[i], searchWordBuf)) n++;
    } else {
        for (int i = 0; i < titleCount; i++)
            if (titleMatchesWord(i)) n++;
    }
    return n;
}

// Artist name to show as row metadata beside a song result, so two songs with
// the same title are told apart before one is played. nullptr = draw nothing.
static const char* searchResultMeta(int idx) {
    if (searchKind != SP_TITLES && searchKind != SP_FILTERED_TITLES) return nullptr;
    if (!titleIndex) return nullptr;
    int real = (searchKind == SP_FILTERED_TITLES) ? searchFilterIdx[idx] : idx;
    int a = titleIndex[real].artistIdx;
    return (a >= 0 && a < artistCount) ? artistList[a] : nullptr;
}

// v1.8: a results list holding exactly ONE artist is a menu with a single
// item -- pressing 1 is the only thing you can do with it. Open it directly.
// NOTE: deliberately not applied to a single SONG result. Stepping into a
// browse screen is reversible with H, but starting playback replaces what
// you're listening to and can't be undone, so songs keep their confirming
// press -- the same line enterArtist() already draws for the <=PAGE case.
void enterArtist(const char* artist, bool fromSearch);   // fwd decl
static bool autoOpenSingleResult() {
    if (searchLo != searchHi) return false;
    if (!searchIsArtistKind()) return false;
    Serial.printf("[SEARCH] Single artist result -- opening \"%s\"\n", searchPoolItem(searchLo));
    enterArtist(searchPoolItem(searchLo), true);
    return true;
}

// Run the typed word as a substring filter over whichever pool word-entry
// was launched for. Routes to RESULTS (<=PAGE matches), BISECT (>PAGE, via
// the same generic bisection over searchFilterIdx[]), or NO_MATCH (0). An
// empty buffer is a harmless no-op -- searchKind stays SP_SYMBOLS so the
// caller just redraws the typing screen.
void runWordSearch() {
    if (searchWordLen == 0 || !searchFilterIdx) return;
    int n = 0;
    if (wordScope == WS_ARTIST) {
        for (int i = 0; i < artistCount && n < searchFilterCap; i++)
            if (strCaseContains(artistList[i], searchWordBuf)) searchFilterIdx[n++] = i;
    } else {
        for (int i = 0; i < titleCount && n < searchFilterCap; i++)
            if (titleMatchesWord(i)) searchFilterIdx[n++] = i;
    }
    searchFilterCount = n;
    searchKind = (wordScope == WS_ARTIST) ? SP_FILTERED_ARTISTS : SP_FILTERED_TITLES;
    searchStackDepth = 0;
    searchLo = 0;
    searchHi = searchFilterCount - 1;
    Serial.printf("[SEARCH] \"%s\" -> %d match(es)\n", searchWordBuf, searchFilterCount);
    if (searchFilterCount == 0)      menuState = MS_SEARCH_NO_MATCH;
    else if (searchFilterCount <= PAGE) {
        menuState = MS_SEARCH_RESULTS;
        if (autoOpenSingleResult()) return;    // v1.8: 1 artist -> open it
    }
    else                                 menuState = MS_SEARCH_BISECT;
}

// v1.8: one keystroke of word entry -- append a letter, backspace, or run the
// search. Extracted from the MS_SEARCH_RESULTS handler so the bisect screen
// can apply a single-letter group directly, and so the two call sites can't
// drift apart.
void applySymbolPick(int idx){
    if(idx==SYMBOL_BACKSPACE_IDX){
        if(searchWordLen>0){searchWordLen--;searchWordBuf[searchWordLen]='\0';}
    } else if(idx==SYMBOL_OK_IDX){
        runWordSearch();
    } else if(searchWordLen<SEARCH_WORD_MAX){
        searchWordBuf[searchWordLen++]=SYMBOL_CHAR[idx];
        searchWordBuf[searchWordLen]='\0';
    }
    if(searchKind==SP_SYMBOLS){
        // Still composing (letter/backspace, or OK on an empty buffer) --
        // reset to the top of the alphabet for the next keystroke and stay
        // on the typing screen.
        searchStackDepth=0; searchLo=0; searchHi=SYMBOL_COUNT-1;
        menuState=MS_SEARCH_BISECT; drawSearchBisect();
    } else {
        // runWordSearch() switched us to RESULTS/BISECT/NO_MATCH.
        drawForState();
    }
}

// Leave search entirely, returning to whichever screen opened it.
void drawLibrary(bool selectMode);      // fwd decl (defined in DISPLAY SCREENS)
void drawArtists(bool selectMode);      // fwd decl (defined in DISPLAY SCREENS)
void exitSearchToOrigin() {
    switch (searchReturn) {
        case MS_LIB_BROWSE:     menuState = MS_LIB_BROWSE;     drawLibrary(false);     break;
        case MS_ART_BROWSE:     menuState = MS_ART_BROWSE;     drawArtists(false);     break;
        case MS_ALBUM_BROWSE:   menuState = MS_ALBUM_BROWSE;   drawAlbums(false);      break;
        case MS_ARTSONG_BROWSE: menuState = MS_ARTSONG_BROWSE; drawArtistSongs(false); break;
        default:                menuState = MS_SEARCH_TYPE;    drawSearchType();       break;
    }
}

// Shared H-back handler for MS_SEARCH_BISECT and MS_SEARCH_RESULTS: pop one
// bisection level if there is one, otherwise fall back to wherever makes
// sense for the CURRENT pool kind -- back to word entry (buffer intact) if
// we were showing filtered results, or out to the origin screen otherwise.
void searchPopOrExit() {
    if (searchStackDepth > 0) {
        searchStackDepth--;
        searchLo = searchStackLo[searchStackDepth];
        searchHi = searchStackHi[searchStackDepth];
        menuState = MS_SEARCH_BISECT; drawSearchBisect();
        return;
    }
    if (searchKind == SP_FILTERED_ARTISTS || searchKind == SP_FILTERED_TITLES) {
        searchKind = SP_SYMBOLS;       // back to editing the SAME typed word
        searchStackDepth = 0;
        searchLo = 0; searchHi = SYMBOL_COUNT - 1;
        menuState = MS_SEARCH_BISECT; drawSearchBisect();
        return;
    }
    exitSearchToOrigin();
}

// ============================================================
// ARTIST DRILL-DOWN
// ============================================================
int listAlbumsForArtist(const char* artist,int skip,int maxN,
                        char out_albums[][ARTIST_LEN],int* total){
    SdLock lock; *total=0; int filled=0;
    static char seen[64][ARTIST_LEN]; int seenCount=0;
    File f=SD.open(CATALOG_FILE); if(!f)return 0;
    while(f.available()){
        String line=f.readStringUntil('\n');line.trim();
        if(!line.length())continue; if(line[0]!='/')line="/"+line;
        int s1=line.indexOf('/'),s2=line.indexOf('/',s1+1),s3=line.indexOf('/',s2+1);
        if(s2<0)continue;
        if(line.substring(s1+1,s2)!=String(artist))continue;
        String alb=(s3>s2)?line.substring(s2+1,s3):String("(no album)");
        if(alb.length()>=ARTIST_LEN)alb=alb.substring(0,ARTIST_LEN-1);
        bool dup=false;
        for(int i=0;i<seenCount;i++) if(strncmp(seen[i],alb.c_str(),ARTIST_LEN-1)==0){dup=true;break;}
        if(dup)continue;
        if(seenCount<64){strncpy(seen[seenCount],alb.c_str(),ARTIST_LEN-1);seen[seenCount++][ARTIST_LEN-1]='\0';}
        int idx=(*total)++;
        if(idx>=skip&&filled<maxN){
            strncpy(out_albums[filled],alb.c_str(),ARTIST_LEN-1);
            out_albums[filled++][ARTIST_LEN-1]='\0';
        }
    }
    f.close(); return filled;
}

int listSongsForArtist(const char* artist,const char* album,int skip,int maxN,
                       char out_titles[][80],String* out_paths,int* total){
    SdLock lock; *total=0; int filled=0;
    bool albumFilter=(album&&album[0]);
    File f=SD.open(CATALOG_FILE); if(!f)return 0;
    while(f.available()){
        String line=f.readStringUntil('\n');line.trim();
        if(!line.length())continue; if(line[0]!='/')line="/"+line;
        int s1=line.indexOf('/'),s2=line.indexOf('/',s1+1),s3=line.indexOf('/',s2+1);
        if(s2<0)continue;
        if(line.substring(s1+1,s2)!=String(artist))continue;
        if(albumFilter){
            String alb=(s3>s2)?line.substring(s2+1,s3):String("(no album)");
            if(alb!=String(album))continue;
        }
        int idx=(*total)++;
        if(idx>=skip&&filled<maxN){
            int ls=line.lastIndexOf('/'),dot=line.lastIndexOf('.');
            String t=(ls>=0&&dot>ls)?line.substring(ls+1,dot):line.substring(ls+1);
            strncpy(out_titles[filled],t.c_str(),79);out_titles[filled][79]='\0';
            if(out_paths)out_paths[filled]=line;
            filled++;
        }
    }
    f.close(); return filled;
}

// ============================================================
// PAGE-COUNT HELPERS  (v1.8 quality-of-life)
// ============================================================
// A list that fits on one page has nothing to page through, so the
// BROWSE/SELECT split is pure friction there: 1 and 2 look like they should
// pick rows 1 and 2, but they're paging buttons that silently do nothing, and
// you have to discover "5=SELECT" before any number does what it looks like it
// does. These helpers give ONE definition of "does this list need a browse
// layer", used both when ENTERING a screen and when H BACKS OUT of it, so the
// two can never disagree and drop you onto a dead browse screen with a single
// page and no way to page.
//
// Placed here, right after listAlbumsForArtist/listSongsForArtist, because the
// album/song variants call them and enterArtist() below calls these in turn.
static int libTotalPages(){
    int total=libCatalog?catalogCount:playlistCount;
    return max(1,(total+PAGE-1)/PAGE);
}
static int artTotalPages(){ return max(1,(artistCount+PAGE-1)/PAGE); }
static int btTotalPages(){  return max(1,(savedCount+DEV_PER_PAGE-1)/DEV_PER_PAGE); }
static int albumTotalCount(){
    char tmp[1][ARTIST_LEN];int total=0;
    listAlbumsForArtist(selArtist,0,0,tmp,&total);return total;
}
static int albumTotalPages(){
    return max(1,(albumTotalCount()+ALBUMS_PER_PAGE-1)/ALBUMS_PER_PAGE);
}
static int artSongTotalPages(){
    char tt[1][80];int total=0;
    listSongsForArtist(selArtist,selAlbum,0,0,tt,nullptr,&total);
    return max(1,(total+PAGE-1)/PAGE);
}

// ============================================================
// WAV PLAYBACK
// ============================================================
static bool recoverSD(){
    SdLock lock; Serial.println("[SD] Re-init...");
    SD.end(); delay(100);
    const uint32_t speeds[]={400000UL,10000000UL,20000000UL};
    for(uint32_t sp:speeds){if(SD.begin(SD_CS,SPI,sp)){Serial.printf("[SD] OK @ %lu\n",(unsigned long)sp);return true;}delay(50);}
    Serial.println("[SD] Re-init FAILED"); return false;
}

// The resume offset is consumed here -- see pendingResumeBytes, declared with
// the audio globals because restoreSession() sets it long before this point.
static bool openSongCommon(const char* path){
    if(!path||!path[0])return false;
    audioMuted.store(true,std::memory_order_release);
    vTaskDelay(pdMS_TO_TICKS(3));
    bool ok=false;
    {
        SdLock lock;
        if(currentFile)currentFile.close();
        currentFile=SD.open(path);
        if(!currentFile&&recoverSD())currentFile=SD.open(path);
        if(currentFile){
            songDataStart=44;
            songDataBytes=(currentFile.size()>44)?currentFile.size()-44:0;
            // Resolve the resume offset against THIS file: a session pointing
            // at a song whose file changed size must not seek past the end.
            uint32_t resume = pendingResumeBytes & ~3u;
            pendingResumeBytes = 0;                 // one-shot, always cleared
            if(resume >= songDataBytes) resume = 0; // stale/short file -> restart
            currentFile.seek(songDataStart + resume);
            strncpy(nowPlayingPath,path,MAX_PATH_LEN-1);nowPlayingPath[MAX_PATH_LEN-1]='\0';
            // producedBytes starts AT the resume point so the EOF test
            // (producedBytes >= songDataBytes) and the SD-recovery seek
            // (songDataStart + producedBytes) both stay correct; playedBytes
            // starts there too so the progress bar opens at the right percent.
            producedBytes=resume;
            ringHead.store(0,std::memory_order_relaxed);
            ringTail.store(0,std::memory_order_relaxed);
            playedBytes.store(resume,std::memory_order_relaxed);
            producerEof=false;sdError=false;songFinished=false;
            lastSample=0;fadeFrames=FADE_FRAMES;isPaused=false;
            consecutiveOpenFails=0;ok=true;
            if(resume) Serial.printf("[PLAY] %s (%u bytes, resuming @ %u)\n",
                                     path,(unsigned)songDataBytes,(unsigned)resume);
            else       Serial.printf("[PLAY] %s (%u bytes)\n",path,(unsigned)songDataBytes);
        } else {
            pendingResumeBytes = 0;
            Serial.printf("[ERROR] Cannot open: %s\n",path);
            songDataBytes=0;producerEof=false;sdError=false;songFinished=false;
        }
    }
    audioMuted.store(false,std::memory_order_release);
    if(ok)waitForBufferFill();
    return ok;
}

bool openSong(int index){if(index<0||index>=playlistCount)return false;return openSongCommon(playlist[index]);}
bool openSongPath(const String& path){if(!path.length())return false;return openSongCommon(path.c_str());}
void requestNextSong(){reqNext=true;}
void requestPrevSong(){reqPrev=true;}

void nextSong(){
    for(int a=0;a<playlistCount;a++){
        songIndex=(songIndex+1)%playlistCount;
        Serial.printf("[NEXT] %d/%d\n",songIndex+1,playlistCount);
        if(openSong(songIndex))return;
        if(++consecutiveOpenFails>=10){
            Serial.println("[ERROR] 10 consecutive fails - stopping");
            {SdLock lock;if(currentFile)currentFile.close();}
            songDataBytes=0;isPaused=true;return;
        }
    }
}

void playSongByPath(const String& path){
    if(!path.length())return;
    char norm[MAX_PATH_LEN];
    if(path[0]!='/'){norm[0]='/';strncpy(norm+1,path.c_str(),MAX_PATH_LEN-2);norm[MAX_PATH_LEN-1]='\0';}
    else{strncpy(norm,path.c_str(),MAX_PATH_LEN-1);norm[MAX_PATH_LEN-1]='\0';}
    int insertAt=songIndex+1;if(insertAt>playlistCount)insertAt=playlistCount;
    // 2026-08-10: bounded by playlistCap (the user's setting), not by
    // MAX_PLAYLIST (the allocation). With the two equal at 100 this was the
    // same number; now that MAX_PLAYLIST is 200, using it here would let a
    // queue the user set to 30 creep up to 200 one library pick at a time.
    if(playlistCount<playlistCap){
        for(int i=playlistCount;i>insertAt;i--) memcpy(playlist[i],playlist[i-1],MAX_PATH_LEN);
        memcpy(playlist[insertAt],norm,MAX_PATH_LEN);playlistCount++;
    } else{insertAt=insertAt%playlistCap;memcpy(playlist[insertAt],norm,MAX_PATH_LEN);}
    songIndex=insertAt; openSong(songIndex);
}

// Given an artist name, set up selArtist/selAlbum and jump to whichever
// screen is appropriate (straight to songs if the artist has <=PAGE songs,
// otherwise the album browser). Shared by MS_ART_SELECT and search-by-artist
// results so the two entry points can't drift out of sync.
void enterArtist(const char* artist, bool fromSearch) {
    strncpy(selArtist, artist, ARTIST_LEN-1); selArtist[ARTIST_LEN-1]='\0';
    selAlbum[0]='\0'; albumPage=0; artSongPage=0;
    artistFromSearch = fromSearch;
    char tmpT[PAGE][80]; int songTotal=0;
    listSongsForArtist(selArtist,"",0,PAGE,tmpT,nullptr,&songTotal);
    if (songTotal<=PAGE) {
        // Few enough songs to fit one page -- always safe to jump straight
        // to SELECT mode, no paging would ever be needed here anyway.
        skippedAlbums=true; menuState=MS_ARTSONG_SELECT; drawArtistSongs(true);
        return;
    }
    // v1.4: land in SELECT mode when the albums fit one page (search used to
    // always land in BROWSE, where number presses page instead of picking, so
    // results looked unselectable). More than one page of albums still lands
    // in BROWSE, because MS_ALBUM_SELECT has no paging buttons of its own and
    // going straight there would strand anyone with >ALBUMS_PER_PAGE(4) albums.
    char tmpA[1][ARTIST_LEN]; int albumTotal=0;
    listAlbumsForArtist(selArtist,0,0,tmpA,&albumTotal);

    // v1.8: 0 or 1 albums -> the album screen would offer "[All songs]" and
    // that single album, which resolve to the SAME song list. Two rows, one
    // outcome: skip the screen entirely and go to the songs.
    if (albumTotal<=1) {
        skippedAlbums=true; selAlbum[0]='\0';            // empty = all songs
        if (artSongTotalPages()<=1){menuState=MS_ARTSONG_SELECT;drawArtistSongs(true);}
        else                       {menuState=MS_ARTSONG_BROWSE;drawArtistSongs(false);}
        return;
    }
    skippedAlbums=false;
    if (albumTotal<=ALBUMS_PER_PAGE) { menuState=MS_ALBUM_SELECT; drawAlbums(true); }
    else                             { menuState=MS_ALBUM_BROWSE; drawAlbums(false); }
}

void prevSong(){
    for(int a=0;a<playlistCount;a++){
        songIndex=(songIndex-1+playlistCount)%playlistCount;
        Serial.printf("[PREV] %d/%d\n",songIndex+1,playlistCount);
        if(openSong(songIndex))return;
        if(++consecutiveOpenFails>=10){
            {SdLock lock;if(currentFile)currentFile.close();}
            songDataBytes=0;isPaused=true;return;
        }
    }
}

// ============================================================
// METADATA + ARTWORK
// ============================================================
void loadMetadata(const String& wavPath,Metadata& m){
    SdLock lock; m.title[0]=m.artist[0]=m.album[0]=m.length[0]='\0';
    int dot=wavPath.lastIndexOf('.');
    String txtPath=(dot>0)?wavPath.substring(0,dot)+".txt":wavPath+".txt";
    File f=SD.open(txtPath.c_str());
    if(!f){
        int ls=wavPath.lastIndexOf('/'),ld=wavPath.lastIndexOf('.');
        String t=wavPath.substring(ls+1,ld);strncpy(m.title,t.c_str(),79);m.title[79]='\0';
        int prev=wavPath.lastIndexOf('/',ls-1);
        String a=wavPath.substring(prev+1,ls);strncpy(m.artist,a.c_str(),47);m.artist[47]='\0';
        return;
    }
    auto readLine=[&](char* buf,int len){
        if(!f.available())return;String s=f.readStringUntil('\n');s.trim();
        strncpy(buf,s.c_str(),len-1);buf[len-1]='\0';
    };
    readLine(m.title,80);readLine(m.artist,48);readLine(m.album,48);readLine(m.length,12);
    f.close();
}

// ============================================================
// FULL-SCREEN BACKGROUND BMPs  (2026-08-10)
// ============================================================
// ⚠️ NOTE ON loadArtwork() BELOW: it does NOT parse BMP. It reads ART_BYTES of
// RAW bytes out of a file that merely has a .bmp extension, so the per-song
// artwork files must be raw 1-bit dumps. If they were ever saved as real BMPs,
// the 62-byte header would be rendering as garbage pixels across the top rows
// and the image would be shifted. Worth checking on the device.
//
// These two screens are parsed properly, because a file exported as "296x152
// BMP" from any image editor is a real BMP, and three things in that format
// silently corrupt a naive raw read:
//   1. HEADER. Pixel data starts at the offset stored at byte 10, not byte 0.
//      Usually 62 for 1-bpp (14 file header + 40 DIB + 8 palette), but it is
//      not safe to assume -- some editors write a larger DIB header.
//   2. ROW PADDING. Each row is padded to a 4-byte boundary. 296 px = 37 bytes,
//      which pads to 40. Reading 37-byte rows would skew the image
//      progressively -- a diagonal tear, not an obvious failure.
//   3. ROW ORDER. Positive height means the rows are stored BOTTOM-UP. Read
//      them in file order and the picture is upside down.
// Plus polarity: in a 1-bpp BMP the palette decides what a set bit means, and
// most editors write palette[0]=black, so bit 1 = WHITE -- the opposite of what
// Adafruit_GFX's drawBitmap wants (1 = ink). We read the palette and normalise
// to "1 = black ink" rather than guessing.
//
// A file with no 'BM' magic falls back to a raw read, so a pre-converted raw
// dump of the correct size also works.
//
// Preloaded once at boot, NOT read at sleep time. Deliberate: enterDeepSleep()
// runs at exactly the moment the SD may be wedged, and taking SdLock there is
// the same hazard saveSession() uses a bounded take to avoid. 11 KB of PSRAM
// buys immunity from it.
static bool loadScreenBmp(const char* path, uint8_t** outBuf){
    *outBuf = nullptr;
    SdLock lock;
    File f = SD.open(path);
    if(!f){ Serial.printf("[BG] %s not found -- using plain background\n",path); return false; }

    uint8_t* buf = (uint8_t*)ps_malloc(BG_BYTES);
    if(!buf){ f.close(); Serial.printf("[BG] %s: PSRAM alloc failed\n",path); return false; }
    memset(buf,0,BG_BYTES);                     // 0 = no ink, i.e. white

    uint8_t hdr[2] = {0,0};
    f.read(hdr,2);

    bool ok = false;
    if(hdr[0]=='B' && hdr[1]=='M'){
        // ---- real BMP ----
        auto rd32=[&](uint32_t off)->uint32_t{
            uint8_t b[4]; f.seek(off); f.read(b,4);
            return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
        };
        auto rd16=[&](uint32_t off)->uint16_t{
            uint8_t b[2]; f.seek(off); f.read(b,2);
            return (uint16_t)b[0]|((uint16_t)b[1]<<8);
        };
        uint32_t dataOff = rd32(10);
        uint32_t dibSize = rd32(14);
        int32_t  w       = (int32_t)rd32(18);
        int32_t  h       = (int32_t)rd32(22);
        uint16_t bpp     = rd16(28);
        uint32_t comp    = rd32(30);

        bool topDown = (h < 0);
        int32_t absH  = topDown ? -h : h;

        if(bpp!=1){
            Serial.printf("[BG] %s: %u-bit, need 1-bit monochrome. Re-save as "
                          "monochrome/1-bit BMP.\n",path,(unsigned)bpp);
        } else if(comp!=0){
            Serial.printf("[BG] %s: compressed BMP (comp=%u), need uncompressed\n",
                          path,(unsigned)comp);
        } else if(w!=BG_W || absH!=BG_H){
            Serial.printf("[BG] %s: %dx%d, need %dx%d\n",path,(int)w,(int)absH,
                          (int)BG_W,(int)BG_H);
        } else {
            // Palette entry 0 is the colour of a CLEARED bit. If it is dark,
            // then bit 0 = black and bit 1 = white, so the buffer must be
            // inverted to match Adafruit's "1 = ink".
            bool invert = true;                       // editor default assumption
            if(dibSize>=40){
                uint32_t palOff = 14 + dibSize;
                uint8_t p0[4] = {0,0,0,0};
                f.seek(palOff); f.read(p0,4);         // B,G,R,reserved
                uint32_t lum0 = (uint32_t)p0[0]+p0[1]+p0[2];
                invert = (lum0 < 384);                // palette[0] dark -> invert
            }
            // Rows are padded to 4 bytes: 37 -> 40 for a 296 px wide image.
            const size_t srcRow = (((size_t)w + 31) / 32) * 4;
            uint8_t row[64];
            if(srcRow <= sizeof(row)){
                ok = true;
                for(int32_t y=0; y<absH && ok; y++){
                    // Positive height = bottom-up: file row 0 is the LAST image row.
                    int32_t destY = topDown ? y : (absH - 1 - y);
                    f.seek(dataOff + (uint32_t)y * srcRow);
                    if(f.read(row,srcRow) != (int)srcRow){ ok=false; break; }
                    uint8_t* dst = buf + (size_t)destY * BG_ROW_BYTES;
                    for(size_t b=0; b<BG_ROW_BYTES; b++)
                        dst[b] = invert ? (uint8_t)~row[b] : row[b];
                }
                if(ok) Serial.printf("[BG] %s loaded (%dx%d, 1-bit, %s, %s)\n",
                                     path,(int)w,(int)absH,
                                     topDown?"top-down":"bottom-up",
                                     invert?"inverted":"direct");
            } else Serial.printf("[BG] %s: row stride %u too large\n",path,(unsigned)srcRow);
        }
    } else {
        // ---- raw 1-bit dump, same convention as loadArtwork() ----
        f.seek(0);
        size_t got = f.read(buf,BG_BYTES);
        if(got == BG_BYTES){
            ok = true;
            Serial.printf("[BG] %s loaded as raw 1-bit dump\n",path);
        } else {
            Serial.printf("[BG] %s: raw read got %u of %u bytes\n",
                          path,(unsigned)got,(unsigned)BG_BYTES);
        }
    }
    f.close();
    if(!ok){ free(buf); return false; }
    *outBuf = buf;
    return true;
}

void loadArtwork(const String& wavPath){
    if(!artworkBuffer)return; SdLock lock;
    int dot=wavPath.lastIndexOf('.');
    String bmpPath=(dot>0)?wavPath.substring(0,dot)+".bmp":wavPath+".bmp";
    File f=SD.open(bmpPath.c_str());
    if(!f){memset(artworkBuffer,0xFF,ART_BYTES);return;}
    size_t got=f.read(artworkBuffer,ART_BYTES);f.close();
    if(got<ART_BYTES)memset(artworkBuffer+got,0xFF,ART_BYTES-got);
}

// ============================================================
// DISPLAY HELPERS  (v1.7: measured text engine)
// ============================================================
static void fmtTime(char* buf,uint32_t s){snprintf(buf,8,"%u:%02u",s/60,s%60);}

// ---- Horizontal half of the engine -------------------------------------
// Truncate + ellipsis to maxW *in the currently selected font*. Every caller
// below selects a font via bandFit() immediately before calling this.
static void fitText(char* out,int outLen,const char* text,int16_t maxW){
    strncpy(out,text,outLen-1);out[outLen-1]='\0';
    if(maxW<=0){out[0]='\0';return;}
    int16_t x1,y1;uint16_t tw,th;
    display.getTextBounds(out,0,0,&x1,&y1,&tw,&th);
    if(tw<=(uint16_t)maxW)return;
    int len=strlen(out);
    while(len>1){len--;out[len]='\0';char tmp[160];snprintf(tmp,sizeof(tmp),"%s...",out);
        display.getTextBounds(tmp,0,0,&x1,&y1,&tw,&th);
        if(tw<=(uint16_t)maxW){strncpy(out,tmp,outLen-1);out[outLen-1]='\0';return;}}
    out[0]='\0';   // nothing legible fits: draw nothing rather than a stray glyph
}

static void toUpperCopy(char* out,int outLen,const char* in){
    int i=0;
    for(;in&&in[i]&&i<outLen-1;i++){char c=in[i];out[i]=(c>='a'&&c<='z')?(char)(c-32):c;}
    out[i]='\0';
}

static uint16_t textWidth(const char* s){
    int16_t x1,y1;uint16_t w,h;display.getTextBounds(s,0,0,&x1,&y1,&w,&h);return w;
}

// ---- Vertical half of the engine ---------------------------------------
// Adafruit_GFX places the built-in 5x7 font by its TOP-LEFT corner but places
// GFXfont text by its BASELINE. v1.6 mixed the two with hand-tuned constants
// (ry+11, ry+14, ry+17, ry+20, HDR_H-1, fy+3 ...), which is why rows drifted,
// two-line rows bled 2 px into the divider above them, and header descenders
// were drawn below the black bar in white-on-white (invisible).
//
// getTextBounds() reports, in BOTH modes, the offset y1 from the cursor to the
// top of the ink plus the ink height h, so
//        cursorY = bandTop + (bandH - h)/2 - y1
// vertically centres anything in any band with no magic numbers.
//
// The measurement is taken from a fixed REFERENCE string, not from the text
// being drawn, so every string in a band shares one baseline (measuring the
// real text would make "Now Playing" and "eee" sit at different heights).
// If the preferred font's reference doesn't fit the band, we drop to the 5x7
// font rather than let ink cross into the neighbouring row.
struct BandFit { const GFXfont* font; int16_t cursorY; };

static const char* const REF_FULL = "(Agjy)";  // caps + ascender + descender
static const char* const REF_CAPS = "AZ09";    // caps/digits only (headers)

static const int16_t TEXT_PAD   = 4;
static const int16_t TEXT_GAP   = 6;
static const int16_t META_MAX_W = 96;
static const int16_t ROW_TXT_X  = NUM_W + 5;
static const int16_t CARET_W    = 7;

static BandFit bandFit(int16_t bandY,int16_t bandH,const GFXfont* preferred,
                       const char* ref=REF_FULL){
    BandFit b; int16_t x1,y1; uint16_t w,h;
    if(preferred){
        display.setFont(preferred);
        display.getTextBounds(ref,0,0,&x1,&y1,&w,&h);
        if((int16_t)h<=bandH){
            b.font=preferred; b.cursorY=bandY+(bandH-(int16_t)h)/2-y1; return b;
        }
    }
    display.setFont();
    display.getTextBounds(ref,0,0,&x1,&y1,&w,&h);
    b.font=nullptr; b.cursorY=bandY+(bandH-(int16_t)h)/2-y1; return b;
}

enum TextAlign { TA_LEFT, TA_RIGHT, TA_CENTER };

// Draws text clipped to maxW and returns the width actually used, so callers
// can lay the next item out against it instead of guessing (this is what stops
// left/right pairs in headers, footers and settings rows from colliding).
static uint16_t drawInBand(const char* text,int16_t x,const BandFit& bf,int16_t maxW,
                           uint16_t color,TextAlign align=TA_LEFT){
    if(!text||!*text||maxW<=0) return 0;
    display.setFont(bf.font);
    char buf[128]; fitText(buf,sizeof(buf),text,maxW);
    if(!buf[0]) return 0;
    uint16_t w=textWidth(buf);
    int16_t cx=x;
    if(align==TA_RIGHT)       cx=x-(int16_t)w;
    else if(align==TA_CENTER) cx=x-(int16_t)w/2;
    display.setTextColor(color);
    display.setCursor(cx,bf.cursorY);
    display.print(buf);
    return w;
}

// ---- Chrome -------------------------------------------------------------
// Header text is upper-cased: the bar is HDR_H(15) px and FreeSans9 caps are
// ~13 px, but caps+descenders are ~18 px. Upper-casing guarantees the band can
// hold the whole glyph instead of clipping tails off artist/album names.
static void drawHeader(const char* left,const char* right){
    display.fillRect(0,0,SCREEN_W,HDR_H,GxEPD_BLACK);
    BandFit bf=bandFit(0,HDR_H,&FreeSansBold9pt7b,REF_CAPS);
    char l[96],r[24];
    toUpperCopy(l,sizeof(l),left?left:"");
    toUpperCopy(r,sizeof(r),right?right:"");
    uint16_t rw=0;
    if(r[0]) rw=drawInBand(r,SCREEN_W-TEXT_PAD,bf,SCREEN_W/2,GxEPD_WHITE,TA_RIGHT);
    int16_t leftMax=SCREEN_W-2*TEXT_PAD-(int16_t)rw-(rw?TEXT_GAP:0);
    drawInBand(l,TEXT_PAD,bf,leftMax,GxEPD_WHITE,TA_LEFT);
}

// Header variant for word entry: prints the query and a block caret. v1.6 used
// a literal '_' as the cursor, but an underscore glyph sits BELOW the baseline
// -- i.e. outside the black bar -- so it was drawn white-on-white and never
// visible. A filled rect inside the band always shows.
static void drawHeaderWord(const char* text,const char* right){
    display.fillRect(0,0,SCREEN_W,HDR_H,GxEPD_BLACK);
    BandFit bf=bandFit(0,HDR_H,&FreeSansBold9pt7b,REF_CAPS);
    char r[24]; toUpperCopy(r,sizeof(r),right?right:"");
    uint16_t rw=0;
    if(r[0]) rw=drawInBand(r,SCREEN_W-TEXT_PAD,bf,SCREEN_W/2,GxEPD_WHITE,TA_RIGHT);
    char u[96]; toUpperCopy(u,sizeof(u),text?text:"");
    int16_t maxW=SCREEN_W-2*TEXT_PAD-(int16_t)rw-TEXT_GAP-CARET_W-2;
    uint16_t w=drawInBand(u,TEXT_PAD,bf,maxW,GxEPD_WHITE,TA_LEFT);
    display.fillRect(TEXT_PAD+(int16_t)w+2,HDR_H-4,CARET_W,2,GxEPD_WHITE);
}

static void drawFooter(const char* left,const char* right){
    int16_t fy=SCREEN_H-FTR_H;
    display.fillRect(0,fy,SCREEN_W,FTR_H,GxEPD_WHITE);
    display.drawLine(0,fy,SCREEN_W,fy,GxEPD_BLACK);
    BandFit bf=bandFit(fy+1,FTR_H-1,nullptr);      // always 5x7
    uint16_t rw=0;
    if(right&&*right) rw=drawInBand(right,SCREEN_W-TEXT_PAD,bf,SCREEN_W/2,GxEPD_BLACK,TA_RIGHT);
    drawInBand(left,TEXT_PAD,bf,SCREEN_W-2*TEXT_PAD-(int16_t)rw-(rw?TEXT_GAP:0),
               GxEPD_BLACK,TA_LEFT);
}

// ---- List rows ----------------------------------------------------------
// One renderer for every list in the app (library, artists, albums, songs,
// search, bluetooth, scan) so the vertical rhythm is identical everywhere.
//
// Secondary text ("meta") is now a right-aligned 5x7 column on the SAME line
// rather than a second line inside the row. A 25 px row cannot hold a 13 px
// cap + 4 px descender + 7 px sub-line + separations; v1.6's two-line layout
// solved that by overlapping them. One line each, right-aligned, always fits.
static void drawRowCore(int rowIdx,const char* title,const char* meta,
                        bool isActive,bool invert){
    int16_t ry=HDR_H+rowIdx*ROW_H;
    if(invert) display.fillRect(0,ry,SCREEN_W,ROW_H,GxEPD_BLACK);
    else       display.drawLine(0,ry,SCREEN_W,ry,GxEPD_BLACK);
    uint16_t fg=invert?GxEPD_WHITE:GxEPD_BLACK;
    if(!invert) display.fillRect(0,ry+1,NUM_W,ROW_H-1,isActive?GxEPD_BLACK:GxEPD_WHITE);

    // 1 px inset top and bottom: no glyph can touch this row's divider or the
    // next row's divider, whatever the string.
    const int16_t bandY=ry+1, bandH=ROW_H-2;

    BandFit nf=bandFit(bandY,bandH,&FreeSans9pt7b);
    char nb[3];snprintf(nb,sizeof(nb),"%d",rowIdx+1);
    drawInBand(nb,5,nf,NUM_W-7,(isActive||invert)?GxEPD_WHITE:GxEPD_BLACK,TA_LEFT);
    if(!invert) display.drawLine(NUM_W,ry,NUM_W,ry+ROW_H,GxEPD_BLACK);

    int16_t avail=SCREEN_W-ROW_TXT_X-TEXT_PAD;
    uint16_t mw=0;
    if(meta&&*meta){
        BandFit mf=bandFit(bandY,bandH,nullptr);
        int16_t cap=(int16_t)min((int)META_MAX_W,(int)(avail/2));
        mw=drawInBand(meta,SCREEN_W-TEXT_PAD,mf,cap,fg,TA_RIGHT);
    }
    BandFit tf=bandFit(bandY,bandH,&FreeSans9pt7b);
    drawInBand(title,ROW_TXT_X,tf,avail-(int16_t)mw-(mw?TEXT_GAP:0),fg,TA_LEFT);
}

static void drawListRow(int rowIdx,const char* title,const char* sub,
                        bool isActive,bool invert){
    drawRowCore(rowIdx,title,sub,isActive,invert);
}

static void drawListRowTag(int rowIdx,const char* title,const char* tag,bool invert){
    drawRowCore(rowIdx,title,tag,false,invert);
}

// Settings rows differ from list rows only in that the right-hand value is
// full-size (it is primary information, not metadata). Shared by the full
// redraw and the partial single-row redraw so the two can't drift apart.
static void drawSettingRow(int rowIdx,const char* label,const char* value,bool selected){
    int16_t ry=HDR_H+rowIdx*ROW_H;
    if(selected) display.fillRect(0,ry,SCREEN_W,ROW_H,GxEPD_BLACK);
    else {
        display.fillRect(0,ry,SCREEN_W,ROW_H,GxEPD_WHITE);
        if(rowIdx>0) display.drawLine(0,ry,SCREEN_W,ry,GxEPD_BLACK);
        display.drawLine(NUM_W,ry,NUM_W,ry+ROW_H,GxEPD_BLACK);
    }
    uint16_t fg=selected?GxEPD_WHITE:GxEPD_BLACK;
    const int16_t bandY=ry+1, bandH=ROW_H-2;
    BandFit bf=bandFit(bandY,bandH,&FreeSans9pt7b);
    char nb[3];snprintf(nb,sizeof(nb),"%d",rowIdx+1);
    drawInBand(nb,5,bf,NUM_W-7,fg,TA_LEFT);
    int16_t avail=SCREEN_W-ROW_TXT_X-TEXT_PAD;
    uint16_t vw=0;
    if(value&&*value) vw=drawInBand(value,SCREEN_W-TEXT_PAD,bf,avail/2,fg,TA_RIGHT);
    drawInBand(label,ROW_TXT_X,bf,avail-(int16_t)vw-(vw?TEXT_GAP:0),fg,TA_LEFT);
}

// ---- Now Playing progress region ---------------------------------------
// FIX A: hints live here so every partial refresh redraws them.
// FIX I: track counter below the bar, inside the partial window.
static void drawProgressRegion(int pct,uint32_t elapsed,const char* total){
    display.fillRect(COL_X,PARTIAL_Y,SCREEN_W-COL_X,PARTIAL_H,GxEPD_WHITE);

    display.drawRect(PROG_X,PROG_Y,PROG_W,PROG_H,GxEPD_BLACK);
    int16_t fillW=(int16_t)((int32_t)PROG_W*pct/100);
    if(fillW>0)display.fillRect(PROG_X,PROG_Y,fillW,PROG_H,GxEPD_BLACK);

    // Dual-colour time text inside the bar, drawn per character so it flips
    // white/black at the fill edge. 5x7 font: 6 px advance, 8 px box.
    char eBuf[8];fmtTime(eBuf,elapsed);
    char timeStr[20];
    if(total&&*total)snprintf(timeStr,sizeof(timeStr),"%s / %s",eBuf,total);
    else             snprintf(timeStr,sizeof(timeStr),"%s",eBuf);
    display.setFont();
    int len=strlen(timeStr);
    const int16_t CW=6,CH=8;
    if(len*CW>PROG_W-4){                   // no room for "elapsed / total"
        snprintf(timeStr,sizeof(timeStr),"%s",eBuf);
        len=strlen(timeStr);
    }
    int16_t totalW=len*CW,tx=PROG_X+(PROG_W-totalW)/2,ty=PROG_Y+(PROG_H-CH)/2;
    int16_t fillEdge=PROG_X+fillW;
    for(int i=0;i<len;i++){
        char ch[2]={timeStr[i],0};int16_t cx=tx+i*CW;
        display.setTextColor(((cx+CW/2)<fillEdge)?GxEPD_WHITE:GxEPD_BLACK);
        display.setCursor(cx,ty);display.print(ch);
    }

    // Track counter and volume share the one line that changes. Right item is
    // measured first so the left is capped to what's actually left over, per
    // the same rule drawHeader() follows -- handing both strings the full
    // column width is what let v1.6's header pairs overlap.
    // v1.8: the volume readout is new. There was previously NO on-screen
    // volume anywhere, so a Vol+/Vol- press produced no visible change at all
    // and a dead button was indistinguishable from a sink ignoring the
    // command. It lives here rather than on the hint line so that every
    // changing pixel stays inside the shortened partial window.
    int16_t colW=SCREEN_W-COL_X-4;
    char trackBuf[16];snprintf(trackBuf,sizeof(trackBuf),"%d/%d",songIndex+1,playlistCount);
    char volBuf[12];snprintf(volBuf,sizeof(volBuf),"VOL %d%%",deviceVolume);
    BandFit cb=bandFit(TRACK_CTR_Y-1,10,nullptr);
    uint16_t tw=drawInBand(trackBuf,SCREEN_W-2,cb,colW/2,GxEPD_BLACK,TA_RIGHT);
    drawInBand(volBuf,COL_X+2,cb,colW-(int16_t)tw-(tw?TEXT_GAP:0),GxEPD_BLACK,TA_LEFT);
}

// Static button hints. Deliberately NOT part of drawProgressRegion: they sit
// below the partial window and never change, so painting them once per full
// refresh is enough. v1.7 had them inside the partial region, which meant
// every progress tick erased and re-inked two identical lines of text.
static void drawNowPlayingHints(){
    int16_t colW=SCREEN_W-COL_X-4;
    BandFit h1=bandFit(HINT1_Y-1,10,nullptr);
    BandFit h2=bandFit(HINT2_Y-1,10,nullptr);
    drawInBand("1:Vol+ 2:Vol- 3:Prev",COL_X+2,h1,colW,GxEPD_BLACK,TA_LEFT);
    drawInBand(isPaused?"4:Next 5:Play H:Menu":"4:Next 5:Paus H:Menu",
               COL_X+2,h2,colW,GxEPD_BLACK,TA_LEFT);
}

static void detachWatchdogs(){
    esp_task_wdt_config_t c={.timeout_ms=30000,.idle_core_mask=3,.trigger_panic=false};
    esp_err_t e=esp_task_wdt_reconfigure(&c);if(e!=ESP_OK)esp_task_wdt_init(&c);
    Serial.println("[WDT] 30s");
}

// Word-wrap to at most maxLines. A word wider than the column is hard-cut with
// an ellipsis (v1.6 let a single long token run off the 140 px info panel), and
// text that doesn't fit in maxLines ends in "..." instead of vanishing.
static int16_t drawWrapped(const char* text,int16_t x,int16_t y,int16_t maxW,
                           int16_t lineH,int maxLines=2){
    if(maxLines>3)maxLines=3;
    String lines[3]; int nLines=0; bool overflow=false;
    String t(text); t.trim();
    String line="";
    int i=0,n=t.length();
    while(i<=n){
        int sp=t.indexOf(' ',i); if(sp<0)sp=n;
        String word=t.substring(i,sp);
        if(word.length()){
            String test=line.length()?line+" "+word:word;
            if(textWidth(test.c_str())<=(uint16_t)maxW){
                line=test;
            } else {
                if(line.length()){
                    if(nLines<maxLines) lines[nLines++]=line;
                    else { overflow=true; break; }
                    line="";
                }
                char wbuf[96]; fitText(wbuf,sizeof(wbuf),word.c_str(),maxW);
                line=String(wbuf);
            }
        }
        if(sp>=n) break;
        i=sp+1;
    }
    if(line.length()){
        if(nLines<maxLines) lines[nLines++]=line;
        else overflow=true;
    }
    if(overflow&&nLines>0){
        char tail[96]; snprintf(tail,sizeof(tail),"%s...",lines[nLines-1].c_str());
        char cut[96]; fitText(cut,sizeof(cut),tail,maxW);
        lines[nLines-1]=String(cut);
    }
    for(int k=0;k<nLines;k++){
        display.setCursor(x,y);display.print(lines[k]);y+=lineH;
    }
    return y;
}

// ============================================================
// DISPLAY SCREENS
// ============================================================

// Right-hand info panel: fixed bands, so the artist line, the PAUSED badge and
// the "Next:" line sit at the same y for every song. v1.6 floated them off the
// title's wrapped height, so a 1-line title left a hole and a 2-line title put
// the artist's descenders 1 px from the badge.
static const int16_t NP_TITLE_Y  = 2;    // 2..29 band for line 1, line 2 below
static const int16_t NP_TITLE_H  = 32;   // > the 12pt reference box, so it never falls back
static const int16_t NP_TITLE_LH = 22;
static const int16_t NP_META_Y   = 52;   // artist - album
static const int16_t NP_META_H   = 20;
static const int16_t NP_BADGE_Y  = 73;   // "|| PAUSED"
static const int16_t NP_BADGE_H  = 11;
static const int16_t NP_NEXT_Y   = 86;   // "Next: ..."   (PARTIAL_Y is 98)
static const int16_t NP_NEXT_H   = 11;

void drawNowPlaying(){
    partialRefreshCount=0;
    String wavPath(nowPlayingPath[0]?nowPlayingPath:playlist[songIndex]);
    loadMetadata(wavPath,currentMeta);loadArtwork(wavPath);

    int nextIdx=(songIndex+1)%playlistCount;char nextTitle[80]="";
    {String np(playlist[nextIdx]);int ls=np.lastIndexOf('/'),ld=np.lastIndexOf('.');
     String t=(ls>=0&&ld>ls)?np.substring(ls+1,ld):np.substring(ls+1);
     strncpy(nextTitle,t.c_str(),79);nextTitle[79]='\0';}

    uint32_t pl=playedBytes.load(std::memory_order_relaxed);
    int curPct=(songDataBytes>0)?(int)((uint64_t)pl*100/songDataBytes):0;
    if(curPct>100)curPct=100;
    // Bucket width is cfg.progStep (see loop()); the two MUST agree or the
    // first partial update after a full redraw fires immediately.
    int pstep=(cfg.progStep>0)?(int)cfg.progStep:10;
    int curBucket=(curPct/pstep)*pstep;uint32_t curEl=pl/BYTES_PER_SEC;lastProgressPct=curBucket;

    const int16_t MAXW=SCREEN_W-COL_X-TEXT_PAD;

    display.setFullWindow();display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        if(artworkBuffer)display.drawBitmap(0,0,artworkBuffer,152,152,GxEPD_BLACK);
        // FIX D: separator between artwork (ends x=151) and panel (starts 156).
        display.drawLine(152,0,152,SCREEN_H,GxEPD_BLACK);

        // Title: up to 2 lines, top-aligned in its band.
        BandFit tb=bandFit(NP_TITLE_Y,NP_TITLE_H,&FreeSansBold12pt7b);
        display.setTextColor(GxEPD_BLACK);
        drawWrapped(currentMeta.title,COL_X,tb.cursorY,MAXW,NP_TITLE_LH,2);

        // Artist - album on a fixed line, single line, ellipsised.
        char sub[128];
        if(currentMeta.album[0])snprintf(sub,sizeof(sub),"%s - %s",currentMeta.artist,currentMeta.album);
        else                    snprintf(sub,sizeof(sub),"%s",currentMeta.artist);
        BandFit mb=bandFit(NP_META_Y,NP_META_H,&FreeSans9pt7b);
        drawInBand(sub,COL_X,mb,MAXW,GxEPD_BLACK,TA_LEFT);

        // FIX F: PAUSED badge in the panel, not over the artwork.
        if(isPaused){
            const char* pmsg="|| PAUSED";
            BandFit pb=bandFit(NP_BADGE_Y,NP_BADGE_H,nullptr);
            display.setFont();
            int16_t bw=(int16_t)textWidth(pmsg)+8;
            display.fillRect(COL_X+2,NP_BADGE_Y,bw,NP_BADGE_H,GxEPD_BLACK);
            drawInBand(pmsg,COL_X+6,pb,bw-8,GxEPD_WHITE,TA_LEFT);
        }

        char nextBuf[96];snprintf(nextBuf,sizeof(nextBuf),"Next: %s",nextTitle);
        BandFit nb=bandFit(NP_NEXT_Y,NP_NEXT_H,nullptr);
        drawInBand(nextBuf,COL_X+2,nb,MAXW-2,GxEPD_BLACK,TA_LEFT);

        drawProgressRegion(curBucket,curEl,currentMeta.length);
        drawNowPlayingHints();   // v1.8: hints live outside the partial window
    } while(display.nextPage());
    lastDrawnPaused=isPaused;    // v1.8: badge + hint2 are now on screen
}

void drawMenu(){
    static const char* labels[5]={"Now Playing","Music Library","Search","Bluetooth","Settings"};
    static const char* metas[5] ={"","browse all","find by word","speakers",""};
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);drawHeader("MENU","H=BACK");
       for(int i=0;i<5;i++)drawListRow(i,labels[i],metas[i],false,false);
       drawFooter("1-5 = select","hold H = now playing");}while(display.nextPage());
}

void drawLibrary(bool selectMode){
    char titles[PAGE][80],arts[PAGE][48];
    int count=readListPage(libCatalog,libPage,titles,arts);
    int total=libCatalog?catalogCount:playlistCount;
    int totalPages=max(1,(total+PAGE-1)/PAGE);
    char hdrLeft[40],hdrRight[16];
    snprintf(hdrLeft,sizeof(hdrLeft),"%s  %s",libCatalog?"CATALOG":"PLAYLIST",
             selectMode?"[SELECT]":"[BROWSE]");
    snprintf(hdrRight,sizeof(hdrRight),"%d/%d",libPage+1,totalPages);
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);drawHeader(hdrLeft,hdrRight);
       for(int i=0;i<PAGE;i++){
           if(i<count)drawListRow(i,titles[i],arts[i],selectMode,false);
           else       drawListRow(i,"",nullptr,false,false);}
       if(!selectMode)drawFooter(totalPages>1?"1<pg 2>pg 3=src 4=FIND 5=SELECT"
                                             :"3=src 4=FIND 5=SELECT","H=back");
       else           drawFooter("1-5=play",totalPages>1?"H=browse":"H=back");}while(display.nextPage());
}

void drawArtists(bool selectMode){
    int total=artistCount,totalPages=max(1,(total+PAGE-1)/PAGE),start=artPage*PAGE;
    char hdrRight[16];snprintf(hdrRight,sizeof(hdrRight),"%d/%d",artPage+1,totalPages);
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);
       drawHeader(selectMode?"ARTISTS [SELECT]":"ARTISTS [BROWSE]",hdrRight);
       for(int i=0;i<PAGE;i++){int idx=start+i;
           if(artistList&&idx<total)drawListRow(i,artistList[idx],nullptr,selectMode,false);
           else drawListRow(i,"",nullptr,false,false);}
       if(!selectMode)drawFooter(totalPages>1?"1<pg 2>pg 4=FIND 5=SELECT"
                                             :"4=FIND 5=SELECT","H=back");
       else           drawFooter("1-5=pick",totalPages>1?"H=browse":"H=back");}while(display.nextPage());
}

// ============================================================
// SEARCH SCREENS
// ============================================================

// Query as shown in the header. Long queries slide to show the TAIL (what was
// just typed) since that's what is being edited; matching always uses the full
// buffer. The caret is drawn by drawHeaderWord(), not baked into this string.
static void wordHeaderText(char* out,int outLen){
    const char* what = (wordScope==WS_ARTIST) ? "ARTIST" :
                       (wordScope==WS_ANY)    ? "ANY"    : "SONG";
    if(searchWordLen<=12) snprintf(out,outLen,"%s: %s",what,searchWordBuf);
    else                  snprintf(out,outLen,"%s: ...%s",what,searchWordBuf+searchWordLen-9);
}

// Live match count for the query so far -- shown while typing so you can stop
// as soon as the result set is small instead of guessing.
static void wordHitsText(char* out,int outLen){
    if(searchWordLen==0){ snprintf(out,outLen,"%d left",SYMBOL_COUNT); return; }
    int n=countWordMatches();
    snprintf(out,outLen,"%d hit%s",n,n==1?"":"s");
}

void drawSearchType() {
    char ta[24], aa[24];
    snprintf(ta, sizeof(ta), "all %d", titleCount);
    snprintf(aa, sizeof(aa), "all %d", artistCount);
    display.setFullWindow(); display.firstPage();
    do { display.fillScreen(GxEPD_WHITE);
       drawHeader("SEARCH", "H=BACK");
       drawListRow(0, "Find Song",      ta, false, false);
       drawListRow(1, "Find Artist",    aa, false, false);
       drawListRow(2, "Find Anything",  "song or artist", false, false);
       drawListRow(3, "Browse Titles",  "A-Z", false, false);
       drawListRow(4, "Browse Artists", "A-Z", false, false);
       drawFooter("1-3 = type a word   4-5 = A-Z", "H=back");
    } while (display.nextPage());
}

// Bisection screen: up to 5 rows, each a contiguous slice of what's left, with
// its count. Doubles as the letter-entry screen when searchKind==SP_SYMBOLS --
// same mechanic, the pool is the on-screen alphabet instead of song names.
void drawSearchBisect() {
    int gLo[5], gHi[5];
    int groups = splitRange(searchLo, searchHi, gLo, gHi);
    bool typing = (searchKind == SP_SYMBOLS);

    char hdrL[48], hdrR[20];
    if (typing) { wordHeaderText(hdrL,sizeof(hdrL)); wordHitsText(hdrR,sizeof(hdrR)); }
    else {
        snprintf(hdrL, sizeof(hdrL), "%s", searchIsArtistKind() ? "ARTISTS" : "SONGS");
        snprintf(hdrR, sizeof(hdrR), "%d left", searchHi - searchLo + 1);
    }

    display.setFullWindow(); display.firstPage();
    do { display.fillScreen(GxEPD_WHITE);
       if (typing) drawHeaderWord(hdrL, hdrR); else drawHeader(hdrL, hdrR);
       for (int i = 0; i < PAGE; i++) {
           if (i < groups) {
               const char* first = searchPoolItem(gLo[i]);
               const char* last  = searchPoolItem(gHi[i]);
               char label[64], sub[16];
               if (gLo[i] == gHi[i]) snprintf(label, sizeof(label), "%s", first);
               else                  snprintf(label, sizeof(label), "%.14s - %.14s", first, last);
               snprintf(sub, sizeof(sub), "%d", gHi[i] - gLo[i] + 1);
               drawListRow(i, label, sub, false, false);
           } else {
               drawListRow(i, "", nullptr, false, false);
           }
       }
       drawFooter(typing ? "1-5 = narrow to a letter" : "1-5 = narrow", "H=back");
    } while (display.nextPage());
}

// Leaf picker: <=PAGE items left. Also the letter/action pick during typing.
void drawSearchResults() {
    int count = searchHi - searchLo + 1;
    bool typing = (searchKind == SP_SYMBOLS);
    char hdrL[48], hdrR[20];
    if (typing) { wordHeaderText(hdrL,sizeof(hdrL)); wordHitsText(hdrR,sizeof(hdrR)); }
    else {
        snprintf(hdrL, sizeof(hdrL), "%s", searchIsArtistKind() ? "ARTISTS FOUND" : "SONGS FOUND");
        snprintf(hdrR, sizeof(hdrR), "%d", count);
    }
    display.setFullWindow(); display.firstPage();
    do { display.fillScreen(GxEPD_WHITE);
       if (typing) drawHeaderWord(hdrL, hdrR); else drawHeader(hdrL, hdrR);
       for (int i = 0; i < PAGE; i++) {
           if (i < count) drawListRow(i, searchPoolItem(searchLo + i),
                                      typing ? nullptr : searchResultMeta(searchLo + i),
                                      true, false);
           else           drawListRow(i, "", nullptr, false, false);
       }
       drawFooter(typing ? "1-5 = pick a letter"
                         : (searchIsArtistKind() ? "1-5 = open artist" : "1-5 = play"), "H=back");
    } while (display.nextPage());
}

// 0 matches. H returns to typing with the buffer intact so the natural move is
// to backspace a letter, not retype the word.
void drawSearchNoMatch() {
    char msg[64];
    snprintf(msg, sizeof(msg), "No match for \"%s\"", searchWordBuf);
    display.setFullWindow(); display.firstPage();
    do { display.fillScreen(GxEPD_WHITE);
       drawHeader("NO MATCHES", "H=EDIT");
       BandFit bf=bandFit(HDR_H+34,26,&FreeSans9pt7b);
       display.setTextColor(GxEPD_BLACK);
       display.setFont(bf.font);
       drawWrapped(msg,TEXT_PAD*2,bf.cursorY,SCREEN_W-TEXT_PAD*4,20,2);
       drawFooter("H = edit the word and try again","");
    } while (display.nextPage());
}

// FIX L: uniform pages -- row 0 = [All songs], rows 1-4 = albums.
void drawAlbums(bool selectMode){
    char albums[ALBUMS_PER_PAGE][ARTIST_LEN];int total=0;
    int got=listAlbumsForArtist(selArtist,albumPage*ALBUMS_PER_PAGE,ALBUMS_PER_PAGE,albums,&total);
    int totalPages=max(1,(total+ALBUMS_PER_PAGE-1)/ALBUMS_PER_PAGE);
    char hdrR[16];snprintf(hdrR,sizeof(hdrR),"%d/%d",albumPage+1,totalPages);
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);drawHeader(selArtist,hdrR);
       drawListRow(0,"[All songs]","every track",selectMode,false);
       for(int i=0;i<ALBUMS_PER_PAGE;i++){
           if(i<got)drawListRow(i+1,albums[i],nullptr,selectMode,false);
           else     drawListRow(i+1,"",nullptr,false,false);}
       if(!selectMode)drawFooter(totalPages>1?"1<pg 2>pg 4=FIND 5=SELECT"
                                             :"4=FIND 5=SELECT","H=back");
       else           drawFooter("1=all  2-5=album",totalPages>1?"H=browse":"H=back");}while(display.nextPage());
}

void drawArtistSongs(bool selectMode){
    char titles[PAGE][80];int total=0;
    int got=listSongsForArtist(selArtist,selAlbum,artSongPage*PAGE,PAGE,titles,nullptr,&total);
    int totalPages=max(1,(total+PAGE-1)/PAGE);
    char hdr[80],hdrR[16];
    if(selAlbum[0])snprintf(hdr,sizeof(hdr),"%s",selAlbum);
    else           snprintf(hdr,sizeof(hdr),"%s - ALL",selArtist);
    snprintf(hdrR,sizeof(hdrR),"%d/%d",artSongPage+1,totalPages);
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);drawHeader(hdr,hdrR);
       for(int i=0;i<PAGE;i++){if(i<got)drawListRow(i,titles[i],nullptr,selectMode,false);
                               else     drawListRow(i,"",nullptr,false,false);}
       if(!selectMode)drawFooter(totalPages>1?"1<pg 2>pg 4=FIND 5=SELECT"
                                             :"4=FIND 5=SELECT","H=back");
       else           drawFooter("1-5=play",totalPages>1?"H=browse":"H=back");}while(display.nextPage());
}

// 2026-08-10: page 1 is 3 rows, not 5. "Shuffle" and "EPD refresh" are gone --
// both were decorative (see the Settings struct). Row indices shifted, so
// SET_VOL/SET_PLSIZE/SET_MORE are named rather than open-coded: the edit
// handler, the partial-row redraw and the footer all have to agree on them,
// and three separate literal 3s is how they drift apart.
static const int SET_VOL     = 0;
static const int SET_PLSIZE  = 1;
static const int SET_VOLSTEP = 2;   // 2026-08-10
static const int SET_PROGSTEP= 3;   // 2026-08-10
static const int SET_MORE    = 4;
static const int SET_ROWS    = 5;   // page is full again
static const char* const SETTINGS_LABELS[SET_ROWS] =
    {"Volume","Playlist size","Volume step","Progress updates","More settings >"};

// A blank row past the end of a short list. NOT drawListRow(i,"",...), which
// still prints the row number -- fine on a library page where empty slots are
// just the tail of a list, wrong here, where a numbered row invites a press
// that does nothing. This is the same "no dead rows" rule as v1.8.
// 2026-08-10: SET_ROWS == PAGE again, so the loop calling this runs zero times.
// Kept regardless -- it is what stops a future row removal from reintroducing
// exactly that dead numbered row.
static void drawBlankSettingRow(int rowIdx){
    int16_t ry=HDR_H+rowIdx*ROW_H;
    display.fillRect(0,ry,SCREEN_W,ROW_H,GxEPD_WHITE);
    if(rowIdx>0) display.drawLine(0,ry,SCREEN_W,ry,GxEPD_BLACK);
}

// One accessor, shared by the full redraw and the partial single-row redraw,
// so the two cannot disagree about what a row's value string is.
static void settingsValue(int row,char* out,int len){
    switch(row){
        case SET_VOL:      snprintf(out,len,"%d%%",deviceVolume);         break;
        case SET_PLSIZE:   snprintf(out,len,"%d songs",cfg.playlistSize); break;
        case SET_VOLSTEP:  snprintf(out,len,"%d%%",cfg.volStep);          break;
        case SET_PROGSTEP: snprintf(out,len,"every %d%%",cfg.progStep);   break;
        default:           out[0]='\0';                                   break;
    }
}
static bool settingsEditable(int row){ return row>=SET_VOL && row<=SET_PROGSTEP; }

// Position of the current progress step within PROG_STEPS, so +/- can walk the
// table instead of doing arithmetic that could land on a non-divisor of 100.
// MOVED HERE 2026-08-10: defined in the settings section, it was the first
// function definition in the whole sketch and sat 44 lines above
// `struct Metadata` -- which broke the build. See the top-of-file note.
static int progStepIndex(){
    for(int i=0;i<PROG_STEP_N;i++) if(PROG_STEPS[i]==cfg.progStep) return i;
    return 1;   // 10% -- the value every version before this one hardcoded
}

void drawSettings(int editIdx){
    char vals[SET_ROWS][20];
    for(int i=0;i<SET_ROWS;i++) settingsValue(i,vals[i],sizeof(vals[i]));
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);
       if(editIdx>=0)drawHeader("SETTINGS  [EDIT]","H=DONE");
       else          drawHeader("SETTINGS","H=BACK");
       for(int i=0;i<SET_ROWS;i++) drawSettingRow(i,SETTINGS_LABELS[i],vals[i],i==editIdx);
       for(int i=SET_ROWS;i<PAGE;i++) drawBlankSettingRow(i);
       // Queue length is sampled once, at boot, so say so on the row that
       // changes it rather than letting the user press + and watch nothing
       // happen to what's playing.
       if(editIdx==SET_PLSIZE) drawFooter("1:+  2:-  3:save now (next boot)","H:Done");
       else if(editIdx>=0)     drawFooter("1:+  2:-  3:save now","H:Done");
       else                    drawFooter("1-4:edit  5:more","H:back");}while(display.nextPage());
}

void drawSettingsRowPartial(int row,bool selected){
    if(!settingsEditable(row))return;   // SET_MORE and any blank row
    char val[20]; settingsValue(row,val,sizeof(val));
    int16_t ry=HDR_H+row*ROW_H;
    display.setPartialWindow(0,ry,SCREEN_W,ROW_H);display.firstPage();
    do{ drawSettingRow(row,SETTINGS_LABELS[row],val,selected); }while(display.nextPage());
}

// FIX J: footer labels all four buttons including "4:abt".
// 2026-08-10: page 2 rebuilt. "About" and "< Back to page 1" are gone -- About
// was a dead press whose only content (the version) now sits in the header
// where it is always visible, and Back was redundant once H was made to return
// to page 1 rather than jumping past it to the main menu, which is what every
// other back-step in the app does. That freed the two slots the deep-sleep
// settings needed, so page 2 is still one screen with no page 3.
// The "1+H = reset" hint moved from Back's value column into the footer.
static const int SET2_SCRSLEEP     = 0;   // action
static const int SET2_DEEPNOW      = 1;   // action
static const int SET2_SCRTIMEOUT   = 2;   // editable
static const int SET2_DEEPTIMEOUT  = 3;   // editable
static const int SET2_DEEPMODE     = 4;   // editable
static const int SET2_ROWS         = 5;
static const char* const SETTINGS2_LABELS[SET2_ROWS] =
    {"Sleep screen now","Deep sleep now","Screen sleep after",
     "Deep sleep after","Deep sleep when"};
// Renamed from "Auto-sleep timeout": with two independent timeouts on one
// screen, "Auto-sleep" no longer says which one it means.

static const int DEEP_SLEEP_STEP_MIN = 15;   // 0 = off, else 15..240
static const int DEEP_SLEEP_MAX_MIN  = 240;

static void settings2Value(int row,char* out,int len){
    switch(row){
        case SET2_SCRTIMEOUT:
            if(cfg.sleepTimeout==0) snprintf(out,len,"OFF");
            else                    snprintf(out,len,"%d min",cfg.sleepTimeout);
            break;
        case SET2_DEEPTIMEOUT:
            if(cfg.deepSleepTimeout==0) snprintf(out,len,"OFF");
            else                        snprintf(out,len,"%d min",cfg.deepSleepTimeout);
            break;
        case SET2_DEEPMODE:
            snprintf(out,len,"%s",cfg.deepSleepMode==DSM_ALWAYS?"Always":"BT down");
            break;
        default: out[0]='\0'; break;    // the two action rows carry no value
    }
}
static bool settings2Editable(int row){
    return row==SET2_SCRTIMEOUT||row==SET2_DEEPTIMEOUT||row==SET2_DEEPMODE;
}

void drawSettings2(int editIdx){
    char vals[SET2_ROWS][20];
    for(int i=0;i<SET2_ROWS;i++) settings2Value(i,vals[i],sizeof(vals[i]));
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);
       if(editIdx>=0)drawHeader("POWER  [EDIT]","H=DONE");
       else          drawHeader("POWER / SLEEP",FW_VERSION);
       for(int i=0;i<SET2_ROWS;i++) drawSettingRow(i,SETTINGS2_LABELS[i],vals[i],i==editIdx);
       // The mode row is a two-way pick, not a number, so it gets its own
       // footer instead of a "+ / -" that says nothing about what it does.
       if(editIdx==SET2_DEEPMODE) drawFooter("1:always  2:only if BT down  3:save now","H:Done");
       else if(editIdx>=0)        drawFooter("1:+  2:-  3:save now","H:Done");
       else                       drawFooter("1:slp 2:deep 3-5:edit  1+H=reset","H:back");
    }while(display.nextPage());
}

// 2026-08-10: page 2 gained a partial-row redraw, mirroring page 1's. Every
// edit press here used to call drawSettings2(), i.e. a ~4 s FULL refresh --
// so walking the deep-sleep timeout from 90 to 240 cost ten of them. Panel
// refreshes are the scarce resource; a one-row partial is the whole point of
// drawSettingRow() being shared.
void drawSettings2RowPartial(int row,bool selected){
    if(!settings2Editable(row))return;
    char val[20]; settings2Value(row,val,sizeof(val));
    int16_t ry=HDR_H+row*ROW_H;
    display.setPartialWindow(0,ry,SCREEN_W,ROW_H);display.firstPage();
    do{ drawSettingRow(row,SETTINGS2_LABELS[row],val,selected); }while(display.nextPage());
}

static bool deepSleepScreen=false;
// v1.8: shown before any operation that blocks the loop long enough to look
// like a freeze. Without this, the Bluetooth teardown/restart left whatever
// screen was up frozen and unresponsive, which is indistinguishable from a
// crash. Says what it's doing, on what, and how to bail out.
void drawBusyScreen(const char* what,const char* detail){
    Serial.printf("[BUSY] %s %s\n",what,detail?detail:"");
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);
       BandFit tb=bandFit(SCREEN_H/2-30,26,&FreeSansBold12pt7b);
       drawInBand(what,SCREEN_W/2,tb,SCREEN_W-16,GxEPD_BLACK,TA_CENTER);
       if(detail&&*detail){
           BandFit db=bandFit(SCREEN_H/2-2,20,&FreeSans9pt7b);
           drawInBand(detail,SCREEN_W/2,db,SCREEN_W-16,GxEPD_BLACK,TA_CENTER);
       }
       BandFit hb=bandFit(SCREEN_H-22,12,nullptr);
       drawInBand("working -- hold 1+H to reset",SCREEN_W/2,hb,SCREEN_W-16,GxEPD_BLACK,TA_CENTER);
    }while(display.nextPage());
}

// Band geometry for the sleep screens, named once so the image path and the
// plain-black fallback cannot drift apart (the same class of drift the SET_*
// row constants exist to prevent). Title band 52..84, hint band 132..144 on a
// 152 px panel -- no overlap, nothing past the bottom edge.
// 2026-08-10: title band raised to the midpoint between its old position (52)
// and the top of the panel -> 26. Band is now 26..58. Hint stays at 132..144,
// so the gap between them grows from 48 px to 74 px and nothing crosses an
// edge. Shared by BOTH sleep screens, so SLEEP and DEEP SLEEP stay aligned
// with each other; say the word if only the deep-sleep one should move.
static const int16_t SLEEP_TITLE_Y = (SCREEN_H/2 - 24) / 2;   // 26
static const int16_t SLEEP_TITLE_H = 32;
static const int16_t SLEEP_HINT_Y  = SCREEN_H - 20;
static const int16_t SLEEP_HINT_H  = 12;

// Centred text over artwork, with a white knock-out sized to the text rather
// than to the full screen width. Measure-then-constrain, the same pattern
// drawHeader()/drawFooter() use: fitText() and textWidth() are run exactly as
// drawInBand() will run them, so the box provably matches the glyphs it has to
// cover instead of being eyeballed. A full-width band would work too, but it
// would erase a horizontal stripe of the image the user supplied.
// 2026-08-10: the box now hugs the GLYPH INK, not the band.
//
// It used to be fillRect(x0, bandY-1, bw, bandH+2) -- the full 32 px title band
// plus 2, when FreeSansBold12pt caps are only ~13 px tall. That is a box more
// than twice the height of the letters inside it, which on an image reads as a
// big floating white slab. The band exists to fix the BASELINE (via bandFit's
// reference string, so every string in a band sits at the same height); it was
// never a claim about how much space the ink occupies.
//
// So: measure the fitted string at the exact cursor drawInBand will use, and
// use the returned ink rectangle. getTextBounds(text, x, y, &x1,&y1,&w,&h)
// reports the absolute ink box for a cursor at (x,y) in BOTH font modes, which
// is what makes this work for the 12pt title and the 5x7 hint alike -- no
// per-font fudge factors. bandY/bandH are no longer needed, hence gone from the
// signature.
static const int16_t SLEEP_BOX_PAD_X = 4;
static const int16_t SLEEP_BOX_PAD_Y = 3;
static void drawSleepLabel(const char* text,const BandFit& bf,int16_t maxW){
    if(!text||!*text) return;
    display.setFont(bf.font);
    char buf[128]; fitText(buf,sizeof(buf),text,maxW);
    if(!buf[0]) return;
    // Same centring drawInBand will apply, so the measurement matches the draw.
    int16_t cx = SCREEN_W/2 - (int16_t)textWidth(buf)/2;
    int16_t x1,y1; uint16_t iw,ih;
    display.getTextBounds(buf,cx,bf.cursorY,&x1,&y1,&iw,&ih);
    int16_t x0 = x1 - SLEEP_BOX_PAD_X, y0 = y1 - SLEEP_BOX_PAD_Y;
    int16_t bw = (int16_t)iw + 2*SLEEP_BOX_PAD_X;
    int16_t bh = (int16_t)ih + 2*SLEEP_BOX_PAD_Y;
    if(x0 < 0){ bw += x0; x0 = 0; }                 // clamp, keeping the far edge
    if(y0 < 0){ bh += y0; y0 = 0; }
    if(x0 + bw > SCREEN_W) bw = SCREEN_W - x0;
    if(y0 + bh > SCREEN_H) bh = SCREEN_H - y0;
    if(bw <= 0 || bh <= 0) return;
    display.fillRect(x0,y0,bw,bh,GxEPD_WHITE);
    drawInBand(text,SCREEN_W/2,bf,maxW,GxEPD_BLACK,TA_CENTER);
}

// 2026-08-10: optional full-screen background. With an image loaded the screen
// is drawn white-then-bitmap (1 = ink), so the BMP defines every pixel; without
// one it falls back to the original black screen with white text.
//
// BOTH labels are kept in both paths. The big word is the screen's identity and
// the hint is the only thing telling the user which button gets the device back
// -- on deep sleep, button 5 is genuinely the only one that works. Over an
// image they are inked black on a knock-out box so they stay legible whatever
// the artwork does underneath.
void drawSleepScreen(){
    const char* msg=deepSleepScreen?"DEEP SLEEP":"SLEEP";
    const char* hint=deepSleepScreen?"press PLAY (5) to wake"
                                    :"press any button to wake";
    uint8_t* bg = deepSleepScreen ? deepSleepBgBuffer : sleepBgBuffer;
    display.setFullWindow();display.firstPage();
    do{
        BandFit tb=bandFit(SLEEP_TITLE_Y,SLEEP_TITLE_H,&FreeSansBold12pt7b);
        BandFit hb=bandFit(SLEEP_HINT_Y,SLEEP_HINT_H,nullptr);
        if(bg){
            display.fillScreen(GxEPD_WHITE);
            display.drawBitmap(0,0,bg,BG_W,BG_H,GxEPD_BLACK);
            drawSleepLabel(msg, tb,SCREEN_W-16);
            drawSleepLabel(hint,hb,SCREEN_W-16);
        } else {
            display.fillScreen(GxEPD_BLACK);
            drawInBand(msg, SCREEN_W/2,tb,SCREEN_W-16,GxEPD_WHITE,TA_CENTER);
            drawInBand(hint,SCREEN_W/2,hb,SCREEN_W-16,GxEPD_WHITE,TA_CENTER);
        }
    }while(display.nextPage());
}

static void sortConnectedFirst(){
    if(!connectedDevice[0])return;
    for(int i=1;i<savedCount;i++)
        if(strncmp(savedDevices[i],connectedDevice,SCAN_NAME_LEN-1)==0){
            char tmp[SCAN_NAME_LEN];memcpy(tmp,savedDevices[i],SCAN_NAME_LEN);
            for(int j=i;j>0;j--)memcpy(savedDevices[j],savedDevices[j-1],SCAN_NAME_LEN);
            memcpy(savedDevices[0],tmp,SCAN_NAME_LEN);break;}
}

// FIX K (wired): real pagination via savedDevPage; DEV_PER_PAGE=4 device rows
// per page, the row after the last device is always "+ Scan for new".
void drawBluetooth(bool selectMode){
    sortConnectedFirst();
    int totalPages=max(1,(savedCount+DEV_PER_PAGE-1)/DEV_PER_PAGE);
    if (savedDevPage>=totalPages) savedDevPage=totalPages-1;
    if (savedDevPage<0) savedDevPage=0;
    int start=savedDevPage*DEV_PER_PAGE;
    int shown=max(0,min(DEV_PER_PAGE,savedCount-start));

    char hdrR[16];snprintf(hdrR,sizeof(hdrR),"%d/%d",savedDevPage+1,totalPages);
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);
       drawHeader(selectMode?"BLUETOOTH [SELECT]":"BLUETOOTH",hdrR);
       for(int i=0;i<shown;i++){
           bool conn=(strncmp(savedDevices[start+i],connectedDevice,SCAN_NAME_LEN-1)==0);
           drawListRowTag(i,savedDevices[start+i],conn?"CONNECTED":"",false);}
       drawListRowTag(shown,"+ Scan for new","",false);
       for(int i=shown+1;i<PAGE;i++)drawListRow(i,"",nullptr,false,false);
       if(!selectMode)drawFooter(totalPages>1?"1<pg 2>pg  5=SELECT":"5 = SELECT","H=back");
       else           drawFooter("1-5 = connect/scan",totalPages>1?"H=browse":"H=back");
    }while(display.nextPage());
}

void drawScanScreen(){
    int count=scanCount;bool done=scanDone;
    display.setFullWindow();display.firstPage();
    do{display.fillScreen(GxEPD_WHITE);
       if(!done){
           drawHeader("BLUETOOTH SCAN","H=CANCEL");
           BandFit tb=bandFit(HDR_H+30,30,&FreeSansBold12pt7b);
           drawInBand("SEARCHING...",SCREEN_W/2,tb,SCREEN_W-16,GxEPD_BLACK,TA_CENTER);
           BandFit sb=bandFit(HDR_H+66,12,nullptr);
           drawInBand("looking for audio devices (~10s)",SCREEN_W/2,sb,SCREEN_W-16,
                      GxEPD_BLACK,TA_CENTER);
           drawFooter("please wait...","H=cancel");
       } else if(count==0){
           drawHeader("SCAN DONE","H=BACK");
           BandFit tb=bandFit(HDR_H+40,24,&FreeSans9pt7b);
           drawInBand("No audio devices found.",SCREEN_W/2,tb,SCREEN_W-16,GxEPD_BLACK,TA_CENTER);
           drawFooter("H = back to retry","");
       } else{
           char hdr[24];snprintf(hdr,sizeof(hdr),"FOUND %d",count);
           drawHeader(hdr,"H=BACK");
           for(int i=0;i<PAGE;i++){
               if(i<count) drawListRowTag(i,scanNames[i],"",false);
               else        drawListRow(i,"",nullptr,false,false);}
           drawFooter("1-5 = connect & save","H=back");}
    }while(display.nextPage());
}

void updateProgressBar(int pct,uint32_t elapsedSec){
    display.setPartialWindow(COL_X,PARTIAL_Y,SCREEN_W-COL_X,PARTIAL_H);
    display.firstPage();
    do{drawProgressRegion(pct,elapsedSec,currentMeta.length);}while(display.nextPage());
    if(++partialRefreshCount>=GHOST_CLEAR_AFTER){partialRefreshCount=0;drawNowPlaying();}
}

// v1.8: repaint the Now Playing partial window after a volume change so the
// readout updates. Reuses the progress-bar partial window (the volume lives on
// the hint line inside it) and its ghost-clear counter, so rapid volume presses
// get the same periodic full refresh every other partial update does.
static void showVolumeChange(){
    if(menuState!=MS_NOW_PLAYING)return;
    uint32_t el=playedBytes.load(std::memory_order_relaxed)/BYTES_PER_SEC;
    updateProgressBar(lastProgressPct,el);
}

// ============================================================
// SCAN CONTROL
// ============================================================
void startScan(){
    Serial.println("[SCAN] Starting BT discovery");
    scanCount=0;scanActive=true;scanDone=false;scanDoneDrawn=false;scanStartMs=millis();
    scanOwnsGap=true;
    esp_bt_gap_register_callback(gapCallback);      // BORROWED -- see endScan()
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,8,0);
    menuState=MS_BT_SCAN;drawScanScreen();
}

// v1.9.1: every exit from the scan screen goes through here. Handing GAP back
// is the whole point: v1.8 registered gapCallback and never restored it, so
// after one scan the library could not see a single discovery result and could
// never connect again -- the log showed zero ccall_app_gap_callback lines from
// that moment on. Pairing start/end makes it impossible to leak.
void endScan(){
    if(!scanOwnsGap){scanActive=false;return;}
    // NB: scanNames[]/scanCount are deliberately left intact -- the user may
    // still be looking at the results; we are only giving the radio back.
    scanOwnsGap=false; scanActive=false;
    // v1.9.2: ONLY cancel if our inquiry is still running. When it had already
    // ended by itself (gapCallback logged "Discovery stopped"), the extra
    // cancel produced a second GAP event that the just-restored library
    // callback read as "Device discovery failed, continue to discover..." --
    // so it started discovering WHILE CONNECTED, found another saved device,
    // and switched the link to it unasked. That is the 107s C17A -> JBL jump
    // in the log, which nobody requested.
    if(!scanDone) esp_bt_gap_cancel_discovery();
    esp_bt_gap_register_callback(ccall_app_gap_callback);   // give it BACK
    Serial.println("[SCAN] GAP returned to library");
}

// Stable storage for the name this connect attempt is for.
//
// 2026-08-10 comment correction: the old note here said btTargetName is "what
// gets handed to a2dp_source.start(), because the library keeps the POINTERS
// from the vector". That was true of v1.8, which restarted the stack from this
// function. v1.9 deleted the restart -- start() is now called exactly once, in
// setup(), so THIS function hands the library nothing. What still matters, and
// is the actual reason for the copy below, is local: callers pass a pointer
// INTO savedDevices[] or scanNames[], and addSavedDevice() memcpy's a rebuilt
// list straight over savedDevices, so every later read of `name` in this
// function would return a different device's string.
void connectToDevice(const char* name){
    if(!name||!name[0])return;
    if(btConnectBusy){Serial.println("[BT] Connect already in progress");return;}
    btConnectBusy=true;

    // Copy the name BEFORE anything touches the arrays: callers pass a pointer
    // INTO savedDevices[] or scanNames[], and addSavedDevice() memcpy's a
    // rebuilt list straight over savedDevices, so later reads of `name` would
    // return a different device's string.
    strncpy(btTargetName,name,SCAN_NAME_LEN-1);
    btTargetName[SCAN_NAME_LEN-1]='\0';

    // GAP goes back to the library FIRST: the moment we disconnect below it
    // returns to DISCOVERING, and it must be able to see results by then.
    endScan();

    // Remember what we were on BEFORE overwriting connectedDevice, so the
    // "am I already there?" test below is meaningful and so a failed attempt
    // can put the old name back on screen.
    strncpy(btPrevDevice,connectedDevice,SCAN_NAME_LEN-1);
    btPrevDevice[SCAN_NAME_LEN-1]='\0';
    bool sameTarget = (strncmp(btPrevDevice,btTargetName,SCAN_NAME_LEN-1)==0);

    Serial.printf("[BT] Target set: %s\n",btTargetName);
    addSavedDevice(btTargetName);
    strncpy(connectedDevice,btTargetName,sizeof(connectedDevice)-1);
    connectedDevice[sizeof(connectedDevice)-1]='\0';
    deviceVolume=loadDeviceVolume(btTargetName);
    Serial.printf("[VOL] Loaded %d%% for %s\n",deviceVolume,btTargetName);
    settingsDirty=true;settingsDirtyAt=millis();

    // v1.9: the whole disconnect/end/start teardown is GONE.
    //
    // a2dp_source.end(false) DEADLOCKED. The serial log ends at "[BT] step:
    // end" -- no "step: start", no "step: done" -- while the BT app task kept
    // dispatching evt 0xff00 every 10s. end() waits for that task to shut down
    // and it never does, so the Arduino loop task blocked forever. That froze
    // the UI *and* killed the 1+H reset, because nothing was left running to
    // poll the pins.
    //
    // None of it was necessary. The library is already sitting in a permanent
    // discovery loop looking for a name to connect to; setting btTargetName is
    // enough, and onSsidFound() will accept that device on the next round. Only
    // disconnect() is used, and only when something is actually connected --
    // that call completed cleanly in the log ("[BT] Disconnected").
    // v1.9.2: if we are already on the requested device, do nothing but reload
    // its volume. The old code compared connectedDevice AFTER overwriting it
    // with the target, so the test was always false and it tore down the link
    // every time -- including, at 123s in the log, killing an in-flight connect
    // to the very device the user had just asked for.
    if(sameTarget && btIsConnected){
        Serial.println("[BT] already connected to target");
        applyVolume(); savedDevPage=0; btConnectBusy=false;
        btTargetName[0]='\0';
        menuState=MS_NOW_PLAYING;drawNowPlaying();
        return;
    }
    if(btIsConnected){
        Serial.printf("[BT] disconnecting %s\n",btPrevDevice);
        a2dp_source.disconnect();
    }
    applyVolume();
    savedDevPage=0;
    btConnectBusy=false;
    btLastKickAt=0;              // kick immediately on the next loop pass
    btConnectTry=0;
    menuState=MS_BT_CONNECTING;drawBusyScreen("CONNECTING",btTargetName);
}

// ============================================================
// SLEEP / POWER
// ============================================================
void enterScreenSleep(){
    Serial.println("[SLEEP] Screen sleep");deepSleepScreen=false;
    drawSleepScreen();display.hibernate();menuState=MS_SCREEN_SLEEP;
}
void wakeFromSleep(){
    Serial.println("[SLEEP] Waking");lastActivityMs=millis();
    menuState=MS_NOW_PLAYING;drawNowPlaying();
}
void enterDeepSleep(){
    Serial.println("[SLEEP] DEEP SLEEP");deepSleepScreen=true;
    drawSleepScreen();display.hibernate();
    isPaused=true;
    // Session first, while the SD is still mounted and currentFile is open.
    saveSession();
    {SdLock lock;if(currentFile)currentFile.close();}
    // 2026-08-10: was a2dp_source.end(true) -- the last surviving call against
    // Rule 1 (end() waits on a BT task shutdown that never completes; the v1.9
    // log ends dead at "[BT] step: end"). It mattered less as a button press,
    // but auto deep sleep makes it a scheduled event: wedge here and guardTask
    // reboots at 45 s, session restore brings the player back, and it can do
    // that all night. disconnect() is the call the log proved clean, and end()
    // was never needed anyway -- esp_deep_sleep_start() resets the SoC and
    // powers the BT controller down regardless.
    if(btIsConnected){
        Serial.println("[SLEEP] disconnecting BT");
        a2dp_source.disconnect();
        delay(300);            // let the sink see the teardown before power-off
    }
    // GPIO34 = button 5 (BTN_PINS[4]) -- the pin the "press PLAY (5) to wake"
    // hint actually means. It's input-only with no internal pull hardware,
    // so unlike a 32/33/22/25/21-style pin there's nothing for
    // rtc_gpio_pullup_en() to enable here; the board's own external pull-up
    // is what holds it high so a button press (pulling it low) triggers ext0.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)34,0);
    delay(100);esp_deep_sleep_start();
}

// ============================================================
// MENU / BUTTON HANDLER
// ============================================================
void handleButton(int btn){
    switch(menuState){

    case MS_NOW_PLAYING:
        // v1.8: both directions now redraw the partial region so the new
        // volume is visible, and log it -- if the number moves here but the
        // speaker doesn't, the fault is downstream in the sink/AVRC, not here.
        if(btn==1||btn==2){
            deviceVolume=(btn==1)?min(100,deviceVolume+(int)cfg.volStep)
                                :max(0,  deviceVolume-(int)cfg.volStep);
            applyVolume();settingsDirty=true;settingsDirtyAt=millis();
            Serial.printf("[VOL] %s -> %d%%\n",btn==1?"UP":"DOWN",deviceVolume);
            showVolumeChange();
        }
        if(btn==3){prevSong();drawNowPlaying();}
        if(btn==4){nextSong();drawNowPlaying();}
        if(btn==5){isPaused=!isPaused;drawNowPlaying();}
        if(btn==0){menuState=MS_MENU;drawMenu();}
        break;

    case MS_MENU:
        if(btn==1){menuState=MS_NOW_PLAYING;drawNowPlaying();}
        if(btn==2){
            libPage=0;
            if(libTotalPages()<=1){menuState=MS_LIB_SELECT;drawLibrary(true);}   // v1.8
            else                  {menuState=MS_LIB_BROWSE;drawLibrary(false);}
        }
        if(btn==3){menuState=MS_SEARCH_TYPE;drawSearchType();}
        if(btn==4){
            savedDevPage=0;
            // v1.8: with <=DEV_PER_PAGE saved devices there is nothing to page,
            // so the browse layer only makes 1 and 2 look like device pickers
            // that do nothing. Go straight to SELECT, where numbers connect.
            if(btTotalPages()<=1){menuState=MS_BT_SELECT;drawBluetooth(true);}
            else                 {menuState=MS_BLUETOOTH;drawBluetooth(false);}
        }
        if(btn==5){menuState=MS_SETTINGS;settingsEditIdx=-1;drawSettings(-1);}
        if(btn==0){menuState=MS_NOW_PLAYING;drawNowPlaying();}
        break;

    case MS_LIB_BROWSE:{
        int total=libCatalog?catalogCount:playlistCount;
        int maxPage=max(0,(total-1)/PAGE);
        if(btn==1){libPage=max(0,libPage-1);drawLibrary(false);}
        if(btn==2){libPage=min(maxPage,libPage+1);drawLibrary(false);}
        if(btn==3){
            libCatalog=!libCatalog;libPage=0;
            // v1.8: toggling the source changes the page count, so the browse
            // layer may no longer be warranted (catalog 2000 -> playlist 3).
            if(libTotalPages()<=1){menuState=MS_LIB_SELECT;drawLibrary(true);}
            else                   drawLibrary(false);
        }
        // v1.7: word search over the whole catalog without backing out to the
        // menu first; H from the search returns here rather than to the menu.
        if(btn==4){enterWordSearch(WS_ANY,MS_LIB_BROWSE);drawForState();}
        if(btn==5){menuState=MS_LIB_SELECT;drawLibrary(true);}
        if(btn==0){menuState=MS_MENU;drawMenu();}
        break;}

    case MS_LIB_SELECT:
        if(btn>=1&&btn<=5){
            String path=getEntryPath(libCatalog,libPage,btn-1);
            if(path.length()){playSongByPath(path);menuState=MS_NOW_PLAYING;drawNowPlaying();}
        }
        if(btn==0){
            if(libTotalPages()<=1){menuState=MS_MENU;drawMenu();}                // v1.8
            else                  {menuState=MS_LIB_BROWSE;drawLibrary(false);}
        }
        break;

    case MS_SEARCH_TYPE:
        // 1-3 are word searches over the WHOLE catalog; 4-5 are the A-Z browse.
        if(btn==1){enterWordSearch(WS_TITLE);  drawForState();}  // Find Song
        if(btn==2){enterWordSearch(WS_ARTIST); drawForState();}  // Find Artist
        if(btn==3){enterWordSearch(WS_ANY);    drawForState();}  // Find Anything
        if(btn==4){enterSearch(false); drawForState();}          // Browse Titles A-Z
        if(btn==5){enterSearch(true);  drawForState();}          // Browse Artists A-Z
        if(btn==0){menuState=MS_MENU;drawMenu();}
        break;

    case MS_SEARCH_BISECT:{
        if(btn>=1&&btn<=5){
            int gLo[5],gHi[5];
            int groups=splitRange(searchLo,searchHi,gLo,gHi);
            int g=btn-1;
            if(g<groups){
                // v1.8: a group holding exactly ONE letter is a keystroke, not
                // a menu. The bisect row already shows that letter, so the old
                // path spent a full panel refresh drawing a one-row picker
                // whose only possible action was "press 1", then another
                // refresh returning to the alphabet. Type it now instead.
                // Panel refreshes are the scarce resource here -- don't spend
                // one asking a question with a single answer.
                if(searchKind==SP_SYMBOLS && gLo[g]==gHi[g]){
                    applySymbolPick(gLo[g]);
                    break;
                }
                // push current range so H can pop back to it
                if(searchStackDepth<SEARCH_STACK_MAX){
                    searchStackLo[searchStackDepth]=searchLo;
                    searchStackHi[searchStackDepth]=searchHi;
                    searchStackDepth++;
                }
                searchLo=gLo[g]; searchHi=gHi[g];
                if(searchHi-searchLo+1<=PAGE){
                    menuState=MS_SEARCH_RESULTS;
                    if(!autoOpenSingleResult())drawSearchResults();   // v1.8
                }
                else drawSearchBisect();
            }
        }
        if(btn==0) searchPopOrExit();
        break;}

    // Leaf picker -- final item selection for artist/title browse AND
    // search results, OR (when searchKind==SP_SYMBOLS) the actual letter/
    // action pick during word entry: append a char, backspace, or run OK.
    case MS_SEARCH_RESULTS:{
        int count=searchHi-searchLo+1;
        if(btn>=1&&btn<=5&&btn-1<count){
            int idx=searchLo+btn-1;
            if(searchKind==SP_SYMBOLS){
                applySymbolPick(idx);      // v1.8: shared with the bisect screen
            } else if(searchIsArtistKind()){
                enterArtist(searchPoolItem(idx), true);
            } else {
                String path=getPathAtOffset(searchItemCatalogOffset(idx));
                if(path.length()){playSongByPath(path);menuState=MS_NOW_PLAYING;drawNowPlaying();}
            }
        }
        if(btn==0) searchPopOrExit();
        break;}

    // 0 matches for the typed word. H returns to word entry with the buffer
    // still intact, so backspacing a letter or two is the natural next move.
    case MS_SEARCH_NO_MATCH:
        if(btn==0){
            searchKind=SP_SYMBOLS;
            searchStackDepth=0; searchLo=0; searchHi=SYMBOL_COUNT-1;
            menuState=MS_SEARCH_BISECT; drawSearchBisect();
        }
        break;

    case MS_ART_BROWSE:{
        int maxPage=max(0,(artistCount-1)/PAGE);
        if(btn==1){artPage=max(0,artPage-1);drawArtists(false);}
        if(btn==2){artPage=min(maxPage,artPage+1);drawArtists(false);}
        if(btn==4){enterWordSearch(WS_ANY,MS_ART_BROWSE);drawForState();}   // v1.7
        if(btn==5){menuState=MS_ART_SELECT;drawArtists(true);}
        if(btn==0){menuState=MS_MENU;drawMenu();}
        break;}

    case MS_ART_SELECT:
        if(btn>=1&&btn<=5){
            int idx=artPage*PAGE+btn-1;
            if(artistList&&idx<artistCount) enterArtist(artistList[idx], false);
        }
        if(btn==0){
            if(artTotalPages()<=1){menuState=MS_MENU;drawMenu();}                // v1.8
            else                  {menuState=MS_ART_BROWSE;drawArtists(false);}
        }
        break;

    // FIX L: albumPage now indexes into uniform pages of ALBUMS_PER_PAGE=4.
    case MS_ALBUM_BROWSE:{
        char tmp[1][ARTIST_LEN];int total=0;
        listAlbumsForArtist(selArtist,0,0,tmp,&total);
        int maxPage=max(0,(total-1)/ALBUMS_PER_PAGE);
        if(btn==1){albumPage=max(0,albumPage-1);drawAlbums(false);}
        if(btn==2){albumPage=min(maxPage,albumPage+1);drawAlbums(false);}
        if(btn==4){enterWordSearch(WS_ANY,MS_ALBUM_BROWSE);drawForState();} // v1.7
        if(btn==5){menuState=MS_ALBUM_SELECT;drawAlbums(true);}
        if(btn==0){
            if(artistFromSearch){artistFromSearch=false;menuState=MS_SEARCH_RESULTS;drawSearchResults();}
            else{menuState=MS_ART_BROWSE;drawArtists(false);}
        }
        break;}

    // FIX L: btn1 = [All songs], btn2-5 = album index 0-3. No firstPage logic.
    case MS_ALBUM_SELECT:{
        if(btn==1){
            selAlbum[0]='\0';artSongPage=0;
            if(artSongTotalPages()<=1){menuState=MS_ARTSONG_SELECT;drawArtistSongs(true);}  // v1.8
            else                      {menuState=MS_ARTSONG_BROWSE;drawArtistSongs(false);}
        } else if(btn>=2&&btn<=5){
            char albums[ALBUMS_PER_PAGE][ARTIST_LEN];int total=0;
            int got=listAlbumsForArtist(selArtist,albumPage*ALBUMS_PER_PAGE,ALBUMS_PER_PAGE,albums,&total);
            int idx=btn-2;    // btn2->idx0, btn3->idx1, btn4->idx2, btn5->idx3
            if(idx<got){
                strncpy(selAlbum,albums[idx],ARTIST_LEN-1);selAlbum[ARTIST_LEN-1]='\0';
                artSongPage=0;Serial.printf("[ALBUM] %s\n",selAlbum);
                if(artSongTotalPages()<=1){menuState=MS_ARTSONG_SELECT;drawArtistSongs(true);}  // v1.8
                else                      {menuState=MS_ARTSONG_BROWSE;drawArtistSongs(false);}
            }
        }
        if(btn==0){
            // v1.8: don't drop onto a one-page album BROWSE screen the user
            // never passed through on the way in -- go back up a real level.
            if(albumTotalPages()<=1){
                if(artistFromSearch){artistFromSearch=false;menuState=MS_SEARCH_RESULTS;drawSearchResults();}
                else{menuState=MS_ART_BROWSE;drawArtists(false);}
            }
            else{menuState=MS_ALBUM_BROWSE;drawAlbums(false);}
        }
        break;}

    case MS_ARTSONG_BROWSE:{
        char tt[PAGE][80];int total=0;
        listSongsForArtist(selArtist,selAlbum,0,0,tt,nullptr,&total);
        int maxPage=max(0,(total-1)/PAGE);
        if(btn==1){artSongPage=max(0,artSongPage-1);drawArtistSongs(false);}
        if(btn==2){artSongPage=min(maxPage,artSongPage+1);drawArtistSongs(false);}
        if(btn==4){enterWordSearch(WS_ANY,MS_ARTSONG_BROWSE);drawForState();} // v1.7
        if(btn==5){menuState=MS_ARTSONG_SELECT;drawArtistSongs(true);}
        if(btn==0){
            // v1.8: the album screen may have been skipped on the way in
            // (0/1 albums) -- only ARTSONG_SELECT used to check this, so
            // backing out of BROWSE revealed a screen never passed through.
            if(skippedAlbums){
                skippedAlbums=false;
                if(artistFromSearch){artistFromSearch=false;menuState=MS_SEARCH_RESULTS;drawSearchResults();}
                else{menuState=MS_ART_BROWSE;drawArtists(false);}
            }
            else if(albumTotalPages()<=1){menuState=MS_ALBUM_SELECT;drawAlbums(true);}
            else{menuState=MS_ALBUM_BROWSE;drawAlbums(false);}
        }
        break;}

    case MS_ARTSONG_SELECT:{
        if(btn>=1&&btn<=5){
            char titles[PAGE][80];String paths[PAGE];int total=0;
            int got=listSongsForArtist(selArtist,selAlbum,artSongPage*PAGE,PAGE,titles,paths,&total);
            if(btn-1<got){playSongByPath(paths[btn-1]);menuState=MS_NOW_PLAYING;drawNowPlaying();}
        }
        if(btn==0){
            if(skippedAlbums){
                skippedAlbums=false;
                if(artistFromSearch){artistFromSearch=false;menuState=MS_SEARCH_RESULTS;drawSearchResults();}
                else{menuState=MS_ART_BROWSE;drawArtists(false);}
            }
            // v1.8: songs fit one page, so no song BROWSE layer was shown --
            // go back to the album screen in whichever mode it was shown in.
            else if(artSongTotalPages()<=1){
                if(albumTotalPages()<=1){menuState=MS_ALBUM_SELECT;drawAlbums(true);}
                else{menuState=MS_ALBUM_BROWSE;drawAlbums(false);}
            }
            else{menuState=MS_ARTSONG_BROWSE;drawArtistSongs(false);}
        }
        break;}

    case MS_SETTINGS:
        if(settingsEditIdx<0){
            // 2026-08-10: 1-2 edit, 3 = page 2. Was 1-4 edit / 5 = page 2, but
            // rows 2 and 3 (Shuffle, EPD refresh) are gone.
            // 2026-08-10: 1-4 edit (Volume / Playlist size / Volume step /
            // Progress updates), 5 = page 2.
            if(btn>=1&&btn<=SET_PROGSTEP+1){
                settingsEditIdx=btn-1;
                drawSettings(settingsEditIdx);
            }
            if(btn==SET_MORE+1){menuState=MS_SETTINGS2;settings2EditIdx=-1;drawSettings2(-1);}
            if(btn==0){menuState=MS_MENU;drawMenu();}
        } else {
            if(btn==1){
                if(settingsEditIdx==SET_VOL){deviceVolume=min(100,deviceVolume+(int)cfg.volStep);applyVolume();}
                if(settingsEditIdx==SET_PLSIZE)
                    cfg.playlistSize=(uint8_t)min(MAX_PLAYLIST,(int)cfg.playlistSize+10);
                if(settingsEditIdx==SET_VOLSTEP)
                    cfg.volStep=(uint8_t)min((int)VOL_STEP_MAX,(int)cfg.volStep+5);
                if(settingsEditIdx==SET_PROGSTEP)
                    cfg.progStep=PROG_STEPS[min(PROG_STEP_N-1,progStepIndex()+1)];
                settingsDirty=true;settingsDirtyAt=millis();
                drawSettingsRowPartial(settingsEditIdx,true);
            }
            if(btn==2){
                if(settingsEditIdx==SET_VOL){deviceVolume=max(0,deviceVolume-(int)cfg.volStep);applyVolume();}
                if(settingsEditIdx==SET_PLSIZE)
                    cfg.playlistSize=(uint8_t)max(10,(int)cfg.playlistSize-10);
                if(settingsEditIdx==SET_VOLSTEP)
                    cfg.volStep=(uint8_t)max((int)VOL_STEP_MIN,(int)cfg.volStep-5);
                if(settingsEditIdx==SET_PROGSTEP)
                    cfg.progStep=PROG_STEPS[max(0,progStepIndex()-1)];
                settingsDirty=true;settingsDirtyAt=millis();
                drawSettingsRowPartial(settingsEditIdx,true);
            }
            if(btn==3){saveSettings();settingsDirty=false;settingsEditIdx=-1;drawSettings(-1);}
            // 2026-08-10: H is DONE, not CANCEL. Edits are live the moment
            // they're made (applyVolume() has already gone to the sink), so
            // leaving edit mode keeps them; settingsDirty was set by the +/-
            // press above, so the 2 s deferred save persists them either way.
            // 3 is the same commit, saved immediately instead of in 2 s.
            if(btn==0){settingsEditIdx=-1;drawSettings(-1);}
        }
        break;

    case MS_SETTINGS2:
        if(settings2EditIdx<0){
            if(btn==1)enterScreenSleep();
            if(btn==2)enterDeepSleep();
            // 3/4/5 open the three editable rows. Was 3 = the only timeout,
            // 4 = About (dead press), 5 = back to page 1 (now H's job).
            if(btn==3){settings2EditIdx=SET2_SCRTIMEOUT;   drawSettings2(settings2EditIdx);}
            if(btn==4){settings2EditIdx=SET2_DEEPTIMEOUT;  drawSettings2(settings2EditIdx);}
            if(btn==5){settings2EditIdx=SET2_DEEPMODE;     drawSettings2(settings2EditIdx);}
            // 2026-08-10: H now steps back ONE level, to settings page 1,
            // instead of jumping straight out to the main menu. Every other
            // back-step in the app pops one level; this one skipped a screen,
            // which is also why a separate "< Back to page 1" row had to exist.
            if(btn==0){menuState=MS_SETTINGS;settingsEditIdx=-1;drawSettings(-1);}
        } else {
            // 2026-08-10: these mark settingsDirty. They never did, so with H
            // meaning DONE a timeout change would have been lost unless the
            // user happened to press 3.
            if(btn==1){
                if(settings2EditIdx==SET2_SCRTIMEOUT)
                    cfg.sleepTimeout=min(60,cfg.sleepTimeout+1);
                if(settings2EditIdx==SET2_DEEPTIMEOUT)
                    cfg.deepSleepTimeout=(uint8_t)min(DEEP_SLEEP_MAX_MIN,
                                        (int)cfg.deepSleepTimeout+DEEP_SLEEP_STEP_MIN);
                if(settings2EditIdx==SET2_DEEPMODE) cfg.deepSleepMode=DSM_ALWAYS;
                settingsDirty=true;settingsDirtyAt=millis();
                drawSettings2RowPartial(settings2EditIdx,true);
            }
            if(btn==2){
                if(settings2EditIdx==SET2_SCRTIMEOUT)
                    cfg.sleepTimeout=max(0,cfg.sleepTimeout-1);
                if(settings2EditIdx==SET2_DEEPTIMEOUT)
                    cfg.deepSleepTimeout=(uint8_t)max(0,
                                        (int)cfg.deepSleepTimeout-DEEP_SLEEP_STEP_MIN);
                if(settings2EditIdx==SET2_DEEPMODE) cfg.deepSleepMode=DSM_BT_DOWN;
                settingsDirty=true;settingsDirtyAt=millis();
                drawSettings2RowPartial(settings2EditIdx,true);
            }
            if(btn==3){saveSettings();settingsDirty=false;settings2EditIdx=-1;drawSettings2(-1);}
            if(btn==0){settings2EditIdx=-1;drawSettings2(-1);}   // DONE, not cancel
        }
        break;

    case MS_SCREEN_SLEEP:
        wakeFromSleep();
        break;

    // FIX K (wired): BROWSE mode -- page through savedDevices with 1/2,
    // enter SELECT mode with 5. Mirrors MS_ART_BROWSE/MS_ART_SELECT exactly.
    case MS_BLUETOOTH:{
        int totalPages=max(1,(savedCount+DEV_PER_PAGE-1)/DEV_PER_PAGE);
        if(btn==1){savedDevPage=max(0,savedDevPage-1);drawBluetooth(false);}
        if(btn==2){savedDevPage=min(totalPages-1,savedDevPage+1);drawBluetooth(false);}
        if(btn==5){menuState=MS_BT_SELECT;drawBluetooth(true);}
        if(btn==0){menuState=MS_MENU;drawMenu();}
        break;}

    // FIX K (wired): SELECT mode -- buttons 1-shown connect to a saved device
    // on the CURRENT page; the row after the last device is always Scan.
    case MS_BT_SELECT:{
        int start=savedDevPage*DEV_PER_PAGE;
        int shown=max(0,min(DEV_PER_PAGE,savedCount-start));
        if(btn>=1&&btn<=PAGE){
            int idx=btn-1;
            if(idx==shown)        startScan();
            else if(idx<shown)    connectToDevice(savedDevices[start+idx]);
        }
        if(btn==0){
            // Mirror the entry decision: if browse was skipped on the way in,
            // skip it on the way out too instead of revealing a dead screen.
            if(btTotalPages()<=1){menuState=MS_MENU;drawMenu();}
            else                 {menuState=MS_BLUETOOTH;drawBluetooth(false);}
        }
        break;}

    case MS_BT_CONNECTING:
        // Only H does anything: abandon the attempt.
        if(btn==0){
            btTargetName[0]='\0';
            Serial.println("[BT] Connect cancelled");
            menuState=MS_NOW_PLAYING;drawNowPlaying();
        }
        break;

    case MS_BT_SCAN:
        if(btn>=1&&btn<=SCAN_MAX&&btn-1<scanCount)connectToDevice(scanNames[btn-1]);
        if(btn==0){
            endScan();
            menuState=MS_BT_SELECT;drawBluetooth(true);
        }
        break;
    }
}

// ============================================================
// SETUP
// ============================================================
// v1.8: was `while(1)delay(1000);` -- a silent permanent hang with a blank
// panel and dead buttons, indistinguishable from a bricked board. Now it says
// what happened, keeps saying it, and honours the 1+H reset combo.
// NOTE: it cannot draw to the screen, because display.init() is the LAST thing
// setup() does and these faults happen well before it. Moving EPD init earlier
// would let these be shown on the panel, but that reorders BT/PSRAM/audio
// bring-up, so it's left alone deliberately rather than changed blind.
static void fatalHalt(const char* what){
    uint32_t last=0;
    for(;;){
        if(millis()-last>=3000){last=millis();Serial.printf("[FATAL] %s -- hold 1+H to reset\n",what);}
        checkResetCombo();
        delay(20);
    }
}

// ============================================================
// FIRMWARE UPDATE FROM SD  (2026-08-10)
// ============================================================
// Runs at boot, BEFORE the audio task, Bluetooth and the big PSRAM users are
// brought up. That placement is deliberate and not cosmetic:
//   - Flash writes briefly disable the instruction cache. With audioFillTask
//     and the A2DP task live -- and this project compiled with
//     -mfix-esp32-psram-cache-issue -- that is the worst possible moment to be
//     rewriting flash. At this point in setup() neither task exists yet.
//   - The SD is mounted (line above) but nothing else holds SdLock, so the
//     read cannot contend with the producer.
//
// The one thing this path is allowed to reorder is display.init(). Section 8 of
// the handoff spec says EPD init stays last because moving it drags PSRAM/BT/
// audio bring-up with it -- but an update never continues into that bring-up,
// it reboots. So the display is brought up early ONLY here, and only once the
// user has an image to confirm. epdReady stops setup() initialising it twice on
// the skip path.
//
// Safety, in order of what actually goes wrong:
//   1. A truncated or corrupt image that still flashes cleanly is the way to
//      brick this. Update.setMD5() makes the library verify the written image
//      and refuse to activate on mismatch. /firmware.md5 is optional (per the
//      chosen policy) but a warning is printed when it is absent, because
//      "optional" should not mean "silent".
//   2. The first byte of an ESP32 image is always 0xE9. Checking it rejects a
//      wrong file -- a .zip, a renamed .ino, an SD read error -- before
//      Update.begin() has erased anything.
//   3. Nothing is written to the running slot. Update targets the INACTIVE OTA
//      partition, so a failure at any point up to the final commit leaves the
//      current firmware bootable. This is exactly what huge_app could not do.
//   4. The image is deleted only AFTER a successful end(), and before restart.
//      Leave it and the device prompts on every boot forever.
static bool epdReady = false;

// 2026-08-10 BUGFIX, and it was a bad one. v1.10.0 called display.init() inside
// checkSdFirmwareUpdate() while the EPD's pin setup, hspi.begin() and
// epd2.selectSPI() all still sat at the BOTTOM of setup(). GxEPD2 defaults to
// the global SPI object, which SPI.begin(SD_SCK,SD_MISO,SD_MOSI) had already
// bound to the SD card's VSPI bus -- so the update screen would have driven the
// panel over the SD's bus, with the SD selected on the same wires, in the one
// routine that then streams a firmware image off that card.
//
// And it did not end at the update: epdReady=true made setup() SKIP the real
// init at the bottom, so merely LEAVING firmware.bin on the card and pressing H
// to skip would leave the whole UI running on the wrong bus for that session.
//
// One helper now owns the entire sequence, so the early path and the normal
// path cannot differ. Idempotent via epdReady.
static void initDisplayOnce(){
    if(epdReady) return;
    pinMode(EPD_CS,OUTPUT);pinMode(EPD_DC,OUTPUT);
    pinMode(EPD_RST,OUTPUT);pinMode(EPD_BUSY,INPUT);
    hspi.begin(EPD_SCK,-1,EPD_MOSI,EPD_CS);
    display.epd2.selectSPI(hspi,SPISettings(4000000,MSBFIRST,SPI_MODE0));
    display.init(115200);
    display.setRotation(3);
    epdReady=true;
}

// Reports the flash layout at boot. This exists because the COMPILE OUTPUT
// CANNOT TELL YOU whether partitions.csv took effect: huge_app's single app
// partition and this table's OTA slots are both 0x300000, so the IDE prints
// "Maximum is 3145728 bytes" either way. The only reliable check is asking the
// running device whether a second app slot exists -- so it says so on every
// boot, rather than letting the answer surface as a failed update later.
static void logPartitionInfo(){
    const esp_partition_t* run = esp_ota_get_running_partition();
    const esp_partition_t* nxt = esp_ota_get_next_update_partition(NULL);
    if(run) Serial.printf("[OTA] Running from '%s' at 0x%06X, %u KB\n",
                          run->label,(unsigned)run->address,(unsigned)(run->size/1024));
    if(nxt && run && nxt != run){
        Serial.printf("[OTA] Update slot '%s' available, %u KB -- SD update ENABLED\n",
                      nxt->label,(unsigned)(nxt->size/1024));
    } else {
        Serial.println("[OTA] No second app slot -- SD UPDATE UNAVAILABLE.");
        Serial.println("[OTA] partitions.csv is not in effect; the build is still "
                       "on a single-app scheme such as huge_app.");
    }
}

// 32 lowercase hex chars, or empty if absent/malformed.
static void readFirmwareMd5(char* out,size_t len){
    out[0]='\0';
    SdLock lock;
    File f=SD.open(FW_MD5_FILE);
    if(!f) return;
    String s=f.readStringUntil('\n'); f.close();
    s.trim();
    // md5sum output is "<hash>  <filename>" -- keep only the hash.
    int sp=s.indexOf(' '); if(sp>0) s=s.substring(0,sp);
    if(s.length()!=32){ Serial.println("[FW] firmware.md5 malformed -- ignoring"); return; }
    for(size_t i=0;i<32;i++){
        char c=s[i];
        if(!isxdigit((int)c)){ Serial.println("[FW] firmware.md5 not hex -- ignoring"); return; }
        s.setCharAt(i,(char)tolower((int)c));
    }
    strncpy(out,s.c_str(),len-1); out[len-1]='\0';
}

static void drawUpdateScreen(const char* line1,const char* line2){
    if(!epdReady) return;
    display.setFullWindow();display.firstPage();
    do{
        display.fillScreen(GxEPD_WHITE);
        BandFit tb=bandFit(SLEEP_TITLE_Y,SLEEP_TITLE_H,&FreeSansBold12pt7b);
        drawInBand(line1,SCREEN_W/2,tb,SCREEN_W-16,GxEPD_BLACK,TA_CENTER);
        BandFit hb=bandFit(SLEEP_HINT_Y,SLEEP_HINT_H,nullptr);
        drawInBand(line2,SCREEN_W/2,hb,SCREEN_W-16,GxEPD_BLACK,TA_CENTER);
    }while(display.nextPage());
}

// Returns only if the update was declined or failed; a success ends in restart.
static void checkSdFirmwareUpdate(){
    size_t imgSize=0;
    {
        SdLock lock;
        File f=SD.open(FW_IMAGE_FILE);
        if(!f) return;                       // nothing to do, the normal case
        imgSize=f.size();
        uint8_t magic=0;
        if(f.read(&magic,1)!=1||magic!=0xE9){
            f.close();
            Serial.printf("[FW] %s: first byte 0x%02X, expected 0xE9 -- not an "
                          "ESP32 image, ignoring\n",FW_IMAGE_FILE,magic);
            return;
        }
        f.close();
    }
    if(imgSize<64*1024){
        Serial.printf("[FW] %s only %u bytes -- looks truncated, ignoring\n",
                      FW_IMAGE_FILE,(unsigned)imgSize);
        return;
    }
    Serial.printf("[FW] Update found: %u bytes (%.2f MB)\n",
                  (unsigned)imgSize,imgSize/1048576.0);
    // Fail before the confirm screen, not after the user has agreed to install.
    if(!esp_ota_get_next_update_partition(NULL)){
        Serial.println("[FW] No OTA slot -- cannot install. See partitions.csv.");
        Serial.println("[FW] Image left in place; flash the OTA partition table "
                       "over USB first.");
        return;
    }

    char md5[33]; readFirmwareMd5(md5,sizeof(md5));
    if(md5[0]) Serial.printf("[FW] MD5 supplied: %s\n",md5);
    else       Serial.println("[FW] WARNING: no /firmware.md5 -- installing unverified");

    // Display comes up here, not in setup(). initDisplayOnce() does the FULL
    // sequence including hspi.begin()/selectSPI -- calling display.init() alone
    // here is what put the panel on the SD card's bus in v1.10.0.
    initDisplayOnce();
    char l2[48];
    snprintf(l2,sizeof(l2),"%.2f MB  -  5=install  H=skip",imgSize/1048576.0);
    drawUpdateScreen("FIRMWARE UPDATE",l2);

    // Confirm. btnReadyAt is set later in setup(), so open the gate here or
    // every press is swallowed. 30 s then skip: waiting forever would leave an
    // unattended box stuck at a prompt instead of playing music, and
    // auto-installing was explicitly not wanted, so a timeout that declines is
    // the safe default.
    btnReadyAt=millis()+300;
    Serial.println("[FW] Press 5 to install, Home to skip (30 s)");
    uint32_t deadline=millis()+30000; bool go=false;
    while((int32_t)(millis()-deadline)<0){
        int b=readButton();
        if(b==5){ go=true; break; }
        if(b==0){ Serial.println("[FW] Skipped by user"); break; }
        checkResetCombo();
        delay(20);
    }
    if(!go){
        if((int32_t)(millis()-deadline)>=0) Serial.println("[FW] Timed out -- skipping");
        Serial.println("[FW] Image left in place; it will offer again next boot");
        return;                              // epdReady stays true; setup() skips re-init
    }

    drawUpdateScreen("INSTALLING","do not power off");
    Serial.println("[FW] Writing to the inactive OTA slot...");

    SdLock lock;
    File f=SD.open(FW_IMAGE_FILE);
    if(!f){ Serial.println("[FW] Re-open failed"); drawUpdateScreen("UPDATE FAILED","could not read file"); return; }
    if(!Update.begin(imgSize)){
        Serial.printf("[FW] Update.begin failed: %s\n",Update.errorString());
        Serial.println("[FW] If this says 'partition' the build is still on "
                       "huge_app -- see partitions.csv");
        f.close(); drawUpdateScreen("UPDATE FAILED","no OTA partition"); return;
    }
    if(md5[0]) Update.setMD5(md5);           // verified inside Update.end()

    // 2026-08-10: HEAP, not stack. This was `uint8_t buf[4096]` as a local,
    // which is half of loopTask's 8 KB stack -- and setup() (where this runs)
    // is on that task, with the SD library's own frames underneath. That is a
    // stack overflow waiting to happen, during the one operation in this
    // firmware that can leave the device unbootable. Internal heap here is
    // ~250 KB (BT has not started yet), so a 4 KB malloc is free.
    const size_t BUFSZ=4096;
    uint8_t* buf=(uint8_t*)malloc(BUFSZ);
    if(!buf){
        Serial.println("[FW] Cannot allocate copy buffer");
        Update.abort(); f.close();
        drawUpdateScreen("UPDATE FAILED","out of memory");
        return;
    }
    size_t written=0; int lastPct=-10;
    while(f.available()){
        size_t n=f.read(buf,BUFSZ);
        if(n==0) break;
        if(Update.write(buf,n)!=n){
            Serial.printf("[FW] Write failed at %u: %s\n",(unsigned)written,Update.errorString());
            break;
        }
        written+=n;
        int pct=(int)((uint64_t)written*100/imgSize);
        if(pct>=lastPct+10){ lastPct=pct; Serial.printf("[FW] %d%%\n",pct); }
    }
    f.close();
    free(buf);

    if(written!=imgSize){
        Serial.printf("[FW] Short write: %u of %u\n",(unsigned)written,(unsigned)imgSize);
        Update.abort();
        drawUpdateScreen("UPDATE FAILED","current firmware kept");
        return;
    }
    if(!Update.end(true)){
        // Reached on an MD5 mismatch. The running slot is untouched.
        Serial.printf("[FW] Verify failed: %s\n",Update.errorString());
        drawUpdateScreen("UPDATE FAILED","checksum mismatch");
        return;
    }

    // Only now is the image consumed -- before restart, or this repeats forever.
    SD.remove(FW_IMAGE_FILE);
    SD.remove(FW_MD5_FILE);
    Serial.println("[FW] Success -- image deleted, restarting");
    drawUpdateScreen("UPDATE COMPLETE","restarting...");
    delay(1500);
    esp_restart();
}

void setup(){
    Serial.begin(115200);delay(1000);printHeap("start");
    sdMutex=xSemaphoreCreateRecursiveMutex();
    detachWatchdogs();
    // v1.8: pins configured early so the 1+H reset combo works during the
    // fatal paths below, which run long before the old initButtons() call
    // site. btnReadyAt (the boot debounce gate) stays where it was -- the
    // combo's 1.5s hold is its own guard against spurious boot presses.
    initButtons();
    // v1.9: started before anything that can block, so the reset combo is live
    // even if BT or SD bring-up wedges.
    xTaskCreatePinnedToCore(guardTask,"guard",2560,NULL,1,NULL,0);
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    printHeap("after BLE release");

    artworkBuffer=(uint8_t*)ps_malloc(ART_BYTES);
    if(!artworkBuffer)Serial.println("[WARN] artworkBuffer alloc failed");
    ring=(uint8_t*)ps_malloc(RING_SIZE);
    if(!ring)Serial.println("[FATAL] PCM ring alloc failed - no audio");
    printHeap("after PSRAM alloc");

    SPI.begin(SD_SCK,SD_MISO,SD_MOSI);
    if(!SD.begin(SD_CS,SPI,SD_CLOCK_HZ))fatalHalt("SD mount failed");
    Serial.println("[OK] SD mounted");

    // 2026-08-10: SD firmware update, checked here and nowhere else -- after
    // the SD is up, before the audio task, Bluetooth and the remaining PSRAM
    // allocations. Returns immediately when /firmware.bin is absent, which is
    // every normal boot. On success it does not return; it restarts.
    logPartitionInfo();
    checkSdFirmwareUpdate();

    // 2026-08-10: loadSettings() MOVED UP, from after buildSearchFilterBuf() to
    // here. It used to run after loadPlaylist(), so cfg still held the compiled
    // defaults while the playlist was being built -- which is why wiring
    // cfg.playlistSize needed this reorder as well as the wiring itself. It
    // only takes SdLock and touches nothing but cfg, so it is safe this early;
    // the SD card is mounted on the line above.
    loadSettings();
    // 2026-08-10: sleep backgrounds preloaded here, while boot is the calm
    // part of the run. Not loaded on demand: drawSleepScreen() is called from
    // enterDeepSleep(), which is exactly when the SD may be wedged, and taking
    // SdLock there is the hazard saveSession() uses a bounded take to dodge.
    // Failure is non-fatal in every case -- the buffer stays null and the
    // screen falls back to plain black.
    loadScreenBmp(SLEEP_BMP_FILE,&sleepBgBuffer);
    loadScreenBmp(DEEPSLEEP_BMP_FILE,&deepSleepBgBuffer);
    printHeap("after sleep backgrounds");
    // 2026-08-10: on an ext0 wake (button 5 from deep sleep) try the saved
    // session first -- same shuffled queue, same track, same position. Gated on
    // the wake cause so a cold boot or a 1+H reset still resamples; any failure
    // inside restoreSession() leaves playlistCount at 0 and falls through.
    bool resumed=false;
    if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0){
        Serial.println("[SESSION] ext0 wake -- attempting restore");
        resumed=restoreSession();
    }
    if(!resumed) loadPlaylist();
    if(!playlistCount)fatalHalt("Empty playlist");
    // Resume PLAYS, it doesn't wake paused: openSongCommon() clears isPaused,
    // and openSong(songIndex) below runs after the restore. Nothing is lost if
    // the BT link isn't up yet -- playedBytes only advances when the library
    // pulls frames, so the resume point simply waits for the sink.
    buildCatalogIndex();
    buildArtistList();
    buildTitleIndex();
    buildSearchFilterBuf();
    openSong(songIndex);
    printHeap("after openSong");

    btnReadyAt=millis()+1000;   // v1.8: initButtons() moved to top of setup()
    lastActivityMs=millis();

    loadSavedDevices();
    if(savedCount>0){
        strncpy(connectedDevice,savedDevices[0],sizeof(connectedDevice)-1);
        deviceVolume=loadDeviceVolume(savedDevices[0]);
        Serial.printf("[VOL] Preloaded %d%% for %s\n",deviceVolume,savedDevices[0]);
    }

    printHeap("before BT start");
    a2dp_source.set_avrc_passthru_command_callback(onAvrcPassthrough);
    a2dp_source.set_on_connection_state_changed(onConnectionStateChanged);
    a2dp_source.set_ssid_callback(onSsidFound);
    a2dp_source.set_data_callback_in_frames(get_audio_data);
    std::vector<const char*>btNames;
    for(int i=0;i<savedCount;i++)btNames.push_back(savedDevices[i]);
    if(btNames.empty())btNames.push_back("C17A");
    a2dp_source.start(btNames);
    applyVolume();
    printHeap("after BT start");

    if(ring){
        xTaskCreatePinnedToCore(audioFillTask,"audioFill",8192,NULL,2,&producerTaskHandle,1);
        producerStarted=true;
        waitForBufferFill();
    }

    // EPD on HSPI - last. 2026-08-10: the pin setup, hspi.begin() and
    // selectSPI() that used to be inline here moved into initDisplayOnce(),
    // because the firmware-update path needs the SAME full sequence and having
    // two versions of it is what caused the wrong-bus bug. No-ops if that path
    // already ran.
    initDisplayOnce();
    printHeap("after EPD init");

    drawNowPlaying();
    Serial.println("[READY]");printHeap("setup complete");
}

// ============================================================
// LOOP
// ============================================================
void loop(){
    loopHeartbeat=millis();          // v1.9: guardTask watches this

    // v1.9: connection is asynchronous now -- the library finds the target on
    // its own discovery round. Report success, or give up with a real message
    // instead of leaving CONNECTING on screen forever.
    // v1.9.2: DRIVE the connection instead of waiting for the library to.
    //
    // v1.9.1 set btTargetName and trusted the library to find it on its next
    // discovery round. The log shows why that is not safe: after the disconnect
    // at 123s the library printed "Heartbeat: reconnect retries exhausted,
    // fallback to scanning" at 128s, 138s, 148s and 158s -- four times, forty
    // seconds -- and never actually started an inquiry. It says it will scan
    // and then doesn't, so the CONNECTING screen sat there dead.
    //
    // We start the inquiry ourselves on a fixed interval. The library's GAP
    // callback is registered (endScan gave it back), so its own
    // filter_inquiry_scan_result -> ssid_callback -> esp_a2d_connect path runs
    // exactly as it does on a successful boot -- we are only supplying the
    // trigger it fails to supply. Using its path rather than calling
    // esp_a2d_source_connect() ourselves keeps its internal peer address in
    // sync, which disconnect() later depends on.
    if(menuState==MS_BT_CONNECTING){
        if(btIsConnected){
            Serial.printf("[BT] Connected to %s after %d tr%s\n",
                          btTargetName,btConnectTry,btConnectTry==1?"y":"ies");
            btTargetName[0]='\0';
            menuState=MS_NOW_PLAYING;drawNowPlaying();
        } else if(btLastKickAt==0||millis()-btLastKickAt>=BT_KICK_INTERVAL_MS){
            if(btConnectTry>=BT_CONNECT_MAX_TRIES){
                Serial.printf("[BT] Gave up on %s\n",btTargetName);
                btTargetName[0]='\0';
                // Put the previous device's name back -- connectToDevice
                // overwrote it optimistically and the move never happened.
                strncpy(connectedDevice,btPrevDevice,sizeof(connectedDevice)-1);
                connectedDevice[sizeof(connectedDevice)-1]='\0';
                deviceVolume=loadDeviceVolume(connectedDevice);
                drawBusyScreen("NOT FOUND",btPrevDevice);
                delay(1500);
                menuState=MS_NOW_PLAYING;drawNowPlaying();
            } else {
                btLastKickAt=millis(); btConnectTry++;
                Serial.printf("[BT] Discovery kick %d/%d for %s\n",
                              btConnectTry,BT_CONNECT_MAX_TRIES,btTargetName);
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,8,0);
                // Re-draw so the screen visibly counts up rather than looking
                // frozen for the better part of a minute.
                char det[48];
                snprintf(det,sizeof(det),"%s  (%d/%d)",
                         btTargetName,btConnectTry,BT_CONNECT_MAX_TRIES);
                drawBusyScreen("CONNECTING",det);
            }
        }
    }

    // Remote commands via AVRC
    if(reqNext){reqNext=false;nextSong();if(menuState==MS_NOW_PLAYING)drawNowPlaying();}
    if(reqPrev){reqPrev=false;prevSong();if(menuState==MS_NOW_PLAYING)drawNowPlaying();}

    // SD recovery - audio keeps playing from ring buffer
    if(sdError){
        Serial.println("[SD] I/O error - recovering");
        bool ok=false;
        {SdLock lock;
         for(int t=0;t<3&&!ok;t++)ok=recoverSD();
         if(ok){
             currentFile=SD.open(nowPlayingPath);
             if(currentFile){
                 uint32_t resume=songDataStart+producedBytes;
                 if(resume>currentFile.size())resume=currentFile.size();
                 currentFile.seek(resume);
                 Serial.printf("[SD] Recovered, resume @ %u\n",(unsigned)resume);
             } else ok=false;
         }
         if(!ok){Serial.println("[SD] Recovery failed - pausing");
                 if(currentFile)currentFile.close();isPaused=true;}
         sdError=false;}
    }

    // EOF detection
    if(producerEof&&ringAvailable()==0&&!songFinished)songFinished=true;
    if(songFinished&&fadeFrames==0){
        Serial.println("[SONG] Finished");nextSong();
        if(menuState==MS_NOW_PLAYING)drawNowPlaying();
    }

    // BT scan timeout
    if(menuState==MS_BT_SCAN){
        if(scanActive&&(millis()-scanStartMs)>=SCAN_TIMEOUT_MS){
            scanDone=true;
            Serial.printf("[SCAN] Timeout - %d found\n",scanCount);
        }
        // v1.9.1: release GAP as soon as the inquiry is over, however it ended
        // (timeout here, or DISCOVERY_STOPPED / list-full inside gapCallback).
        // Waiting for the user to press H would leave the library blind for as
        // long as they left the results on screen -- the same class of leak as
        // the v1.8 bug, just shorter.
        if(scanDone&&scanOwnsGap) endScan();
        if(scanDone&&!scanDoneDrawn){scanDoneDrawn=true;drawScanScreen();}
    }

    // v1.8: AVRC play/pause from the phone flips isPaused with no button
    // press. The PAUSED badge and hint line 2 both sit outside the partial
    // window, so repaint once when the painted state actually diverges.
    if(menuState==MS_NOW_PLAYING&&isPaused!=lastDrawnPaused)drawNowPlaying();

    // Progress bar (partial refresh at each 10% bucket)
    if(menuState==MS_NOW_PLAYING&&songDataBytes>0){
        uint32_t played=playedBytes.load(std::memory_order_relaxed);
        int pct=(int)((uint64_t)played*100/songDataBytes);if(pct>100)pct=100;
        // 2026-08-10: bucket width is now cfg.progStep, not a hardcoded 10.
        // This is how often a PARTIAL refresh is spent: 20 per song at 5%,
        // 4 per song at 25%.
        int pstep=(cfg.progStep>0)?(int)cfg.progStep:10;
        int bucket=(pct/pstep)*pstep;
        if(bucket!=lastProgressPct){
            lastProgressPct=bucket;
            uint32_t elapsed=played/BYTES_PER_SEC;
            Serial.printf("[PROG] %d%% (%us)\n",bucket,elapsed);
            updateProgressBar(bucket,elapsed);
        }
    }

    // Auto-sleep
    if(cfg.sleepTimeout>0&&menuState!=MS_SCREEN_SLEEP){
        if((millis()-lastActivityMs)>=(uint32_t)cfg.sleepTimeout*60000UL)enterScreenSleep();
    }

    // 2026-08-10: AUTO DEEP SLEEP. Same inactivity clock as the screen sleep
    // above (lastActivityMs, which only button presses and wake refresh), on a
    // much longer fuse, and gated by cfg.deepSleepMode:
    //   DSM_BT_DOWN -- only while no A2DP link is up. This is the battery case:
    //                  the link dropped, nothing reconnects it (the library's
    //                  auto-reconnect is disabled and our discovery kick only
    //                  runs on the CONNECTING screen), and the firmware decodes
    //                  into nothing for an hour.
    //   DSM_ALWAYS  -- inactivity alone is enough, connected or not.
    // Held off during MS_BT_SCAN and MS_BT_CONNECTING: both own or are driving
    // a GAP inquiry, and powering the radio down mid-inquiry is exactly the
    // kind of half-finished teardown section 4 of the handoff spec is about.
    if(cfg.deepSleepTimeout>0
       && menuState!=MS_BT_SCAN && menuState!=MS_BT_CONNECTING && !btConnectBusy){
        bool eligible = (cfg.deepSleepMode==DSM_ALWAYS) || !btIsConnected;
        if(eligible && (millis()-lastActivityMs)>=(uint32_t)cfg.deepSleepTimeout*60000UL){
            Serial.printf("[SLEEP] Auto deep sleep after %u min idle (mode=%s, link=%s)\n",
                          (unsigned)cfg.deepSleepTimeout,
                          cfg.deepSleepMode==DSM_ALWAYS?"always":"bt-down",
                          btIsConnected?"up":"down");
            enterDeepSleep();
        }
    }

    // Buttons
    int btn=readButton();
    // v1.6: Home held >=HOME_HOLD_MS -> jump straight to Now Playing from
    // ANY menu state/depth, abandoning whatever browse/edit/search state
    // was in progress (same "abandon without saving" semantics the short H
    // press already has within e.g. settings edit mode -- this is just the
    // bigger version of that). Cancels an in-flight BT scan first so it
    // doesn't keep running invisibly in the background.
    if(homeHoldTriggered){
        homeHoldTriggered=false;
        endScan();
        lastActivityMs=millis();
        if(menuState!=MS_NOW_PLAYING){menuState=MS_NOW_PLAYING;drawNowPlaying();}
    } else if(btn>=0){lastActivityMs=millis();handleButton(btn);}

    // BT just connected - load saved volume
    if(btJustConnected){
        btJustConnected=false;
        if(pendingConnectName[0]){
            strncpy(connectedDevice,pendingConnectName,sizeof(connectedDevice)-1);
            connectedDevice[sizeof(connectedDevice)-1]='\0';
            deviceVolume=loadDeviceVolume(connectedDevice);applyVolume();
            Serial.printf("[VOL] %s connected - loaded %d%%\n",connectedDevice,deviceVolume);
            addSavedDevice(connectedDevice);
        }
    }

    // Deferred settings save
    if(settingsDirty&&(millis()-settingsDirtyAt)>=SAVE_DELAY_MS){
        settingsDirty=false;saveSettings();
    }

    delay(10);
}
