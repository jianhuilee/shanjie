# Shanjie on Linux: fcitx5 addon design

Date: 2026-11-12. Status: design, not yet implemented.

## 1. Goal and scope

A Linux input method for fcitx5 that is usable as a daily driver: typing, preedit, candidate selection,
the prediction row, the language model with semantic rerank, the learning file, and settings through
fcitx5's own config UI.

The Rust core (`core/`) is not changed. It already builds and passes its tests on Linux and exposes
everything through the C ABI in `core/include/shanjie.h` (`docs/spec/c-abi.md`). The port is a new shell,
in the same role as `macos/` and `windows/`.

Developed on a branch of a fork. The `docs/spec/` page and the CI job that `CONTRIBUTING.md` asks for are
added when the work is sent upstream, not before.

### 1.1 Not in the first version

- A custom-drawn candidate window, and the 9-column expanded grid (§5.2).
- Single-Shift tap to toggle English; fcitx5's own input-method switching covers it.
- Automatic chat/formal choice per application; the profile is a manual setting.
- A "clear learning" button, a custom punctuation table, automatic CJK/Latin spacing.
- Diagnostics export, an updater, distribution packages.
- The upstream spec page and CI job.

## 2. Layout and build

```
linux/fcitx5/
  CMakeLists.txt
  src/keymap.{h,cpp}          no fcitx5 dependency
  src/utf.{h,cpp}             no fcitx5 dependency
  src/core_engine.{h,cpp}     no fcitx5 dependency
  src/rerank_runner.{h,cpp}   no fcitx5 dependency
  src/session.{h,cpp}         no fcitx5 dependency
  src/settings.h
  src/candidates.{h,cpp}
  src/engine.{h,cpp}
  src/shanjie-addon.conf
  src/shanjie-im.conf         input method entry
  test/
```

- CMake finds `Fcitx5Core`, runs `cargo build --release --locked -p core --target-dir <repo>/target` as
  a custom target (the directory is explicit because cargo otherwise follows `CARGO_TARGET_DIR` and
  `build.target-dir`, and the link would then miss the archive or pick up a stale one), and links
  `target/release/libcore.a` into one shared addon, `shanjie.so`, with the native libraries that
  `cargo rustc -- --print native-static-libs` reports.
- The header is included from `core/include/` in place. There is no copy, so the header and the static
  library always come from the same commit (`docs/spec/c-abi.md` §2.1).
- The five units without an fcitx5 dependency build as a static library that the addon and the unit
  tests both link.
- Build requirements: CMake, a C++20 compiler, the fcitx5 development package, and the Rust toolchain
  the repository pins.
- Models are not in git. They are public release assets; the documented way to get them locally is the
  `curl` commands in `CONTRIBUTING.md`, followed by the checksum check. `scripts/fetch-models.sh` is the
  CI copy of that step and needs an authenticated `gh`, so the port does not depend on it.

## 3. Installed files and lookup

| What | Where |
|---|---|
| Addon | `${libdir}/fcitx5/shanjie.so` |
| Addon and input method entries | `${datadir}/fcitx5/addon/shanjie.conf`, `${datadir}/fcitx5/inputmethod/shanjie.conf` |
| Lexicon (`data/lexicon`) | `${datadir}/fcitx5/shanjie/lexicon/` |
| Word packs (`data/packs/*-add.tsv`) | `${datadir}/fcitx5/shanjie/packs/` |
| Models (`data/lm`) | `fcitx5/shanjie/lm/` in the user data directory, else the system one |
| Learning file | `fcitx5/shanjie/learn/` in the user data directory, created 0700 by the core |
| Settings | `fcitx5/conf/shanjie.conf` in the user config directory |
| Licenses and attribution | `${datadir}/fcitx5/shanjie/LICENSES/`: `LICENSE`, `data.md`, `McBopomofo-MIT.txt`, `name-suggestion-index-BSD-3-Clause.txt`, `CC-BY-SA-4.0-attribution.txt` (the app's text, taken from `scripts/build-app.sh` at configure time) and, with the models, `colloquial-attribution.tsv` |

fcitx5 5.1.13 or later is required: it is the first release with `fcitx-utils/standardpaths.h`; every
other API the addon uses is older. CMake, the addon entry (`core:5.1.13`) and the READMEs say so.

All lookups go through fcitx5's standard path API. The documented install is system-wide; fcitx5 only
loads addon libraries from its own library directory unless `FCITX_ADDON_DIRS` is set. The build
therefore sets `FCITX_INSTALL_USE_FCITX_SYS_PATHS=ON` by default (CMake's default prefix, `/usr/local`,
would install a library fcitx5 never loads) and prints the addon directory at configure time. When `data/lm`
exists at configure time the model files are installed to the system data directory as well. The model directory is looked up as a whole: the first directory that holds `bigram.sjlm` is
used for every model file, because the files are paired by hash.

The trigram tables are memory-mapped while the engine is alive. Replacing a model file must be done by
rename, never by rewriting in place (`shanjie.h`, language model notes).

## 4. Units

### 4.1 `keymap`

Turns a physical keycode and modifier state into a `ShanjieKey`, or reports "not in the table".

- Character keys use the evdev keycode and a fixed ANSI table, giving the unshifted ASCII character.
  The XKB layout and dead keys therefore cannot change what the core receives, as on macOS
  (`docs/spec/macos-shell.md` §3).
- Special keys: Return and keypad Enter, Space, Backspace, Delete, Escape, the four arrows, Home, End, Tab.
- Page Up and Page Down map to kinds 14 and 15 through a separate function. The session sends them
  only while the last applied output had `candidate_vertical` 1 and no modifier is held. In every other
  state (no window, a prediction row, a composition, or a modifier held) they are passed to the
  application and the composition is left exactly as it is, which is what the core does with these keys
  when it is not showing a vertical window. They do not take the "outside the table" path, so they do
  not commit. The macOS shell commits here; this shell deliberately does not.
- Modifiers: Shift bit 0, Control bit 1, Alt bit 2, Super bit 3, Caps Lock bit 4. AltGr (fcitx5's
  `Mod5`, how layouts report their level-3 characters) is sent as Alt, the core's Option: such a key is
  the layout's character, not the physical key's Zhuyin symbol, and the core passes Option keys through
  without touching the composition (rule 1), as macOS does with its level-3 key. "Forget this word" is
  therefore Super+Backspace. A compositor that grabs Super combinations will hide it; that is a known
  limitation of the first version.
- Key releases are never translated.
- **Keycode normalisation happens at the adapter boundary** (`fcitx_key.h`, `toKeyInput`), before
  `keymap` is called. fcitx5 reports the X keycode, which is the evdev code plus 8 (its Wayland and X11
  frontends both do), so the adapter subtracts 8. A key event with no keycode (0, or any value below 8)
  falls back to the keysym read as a US layout; a keysym that maps to no key gives evdev 0, which
  `keymap` treats as outside its tables. A modifier keysym (fcitx5's `Key::isModifier`, plus Caps Lock,
  Shift Lock, the level-3 and level-5 shifts and Mode_switch) is marked as a modifier whatever its code,
  so a frontend without keycodes cannot turn a Shift press into an "outside the table" commit. `keymap`
  itself only ever sees evdev codes. This is tested with
  real `fcitx::Key` values (`test_fcitx_key`), not only with the evdev table.

### 4.2 `utf`

Converts a UTF-16 code-unit offset in a UTF-8 string to a byte offset, clamped to the string and to a
character boundary. Used for the cursor and the English run, which the core reports in UTF-16 units
while fcitx5 wants byte offsets.

### 4.3 `core_engine`

Owns the `ShanjieEngine*`. Every method copies the `ShanjieOutput` into a plain C++ value and frees it in
the same call; no output pointer escapes.

- `build(config)`: frees the old engine, creates one with `shanjie_engine_new_packs`, then `load_lm`;
  on success `load_tinylm`; on success `load_colloquial`; then sends every setting (§6), then
  `learning_open`. A rebuilt engine starts from core defaults, so nothing is assumed to carry over.
- Pack fall-back as on macOS: if creation fails with packs, try without packs; if that works, drop one
  enabled pack at a time in order and keep the first combination that builds. Settings are not changed.
- No model: the engine still types, only the return code is logged. No engine at all: every key passes
  through.
- The copied output type has no stream operator and is never passed to a logger.

### 4.4 `settings`

An fcitx5 `Configuration` (§6). Changing layout or packs rebuilds the engine after committing the open
composition; every other option is sent to the live engine without ending the composition (§6).

### 4.5 `candidates`

An fcitx5 candidate list built from one output.

- One entry per string in `candidates`, labelled 1–9. Cursor index is `candidate_selected`; −1 means no
  highlight (the prediction row before it is entered), and then the first label is the Tab glyph.
- Selecting an entry (mouse click) calls `shanjie_engine_pick(index)` and applies the output.
- The list has no paging buttons; paging is by keyboard, so the core stays the only owner of the window
  position.
- Layout hint is vertical (§5.2).

### 4.6 `rerank_runner`

One worker thread and a slot for the newest job.

- It has no fcitx5 dependency: it is given a `post` function that runs a callback on the main loop
  (fcitx5's event dispatcher in the addon, a hand-pumped queue in tests).
- After every applied output the session calls `rerank_take`. A job replaces any queued job that has
  not started; the replaced job is freed.
- The worker calls `shanjie_rerank_run` and posts the finished job to the main loop through fcitx5's
  event dispatcher. The main loop calls `rerank_apply`; code 0 applies the snapshot to the owner context
  if it still exists, code 5 does nothing. The job is then freed. A job is never touched by two threads
  at once.
- **Enter's wait** (no selection open): one deadline of 100 ms covers every job in flight, the running
  one and the newer one queued behind it. The wait ends when nothing is running or queued, and hands
  back the newest finished job, which the session applies on the main thread at once. An older result
  that a newer one superseded is freed, never applied (the core would answer "no change"). After the
  deadline Enter commits what is shown.
- **No double delivery**: a finished job sits in one slot. Whoever empties it first, the wait or the
  posted completion, owns the job; the other finds the slot empty and does nothing.
- **Cancellation**: ending a composition by reset (an application reset, focus-out, a destroyed context,
  any failure) and rebuilding the engine cancel all jobs: queued and finished ones are freed, a running
  one is freed when it ends and is never delivered.
- **Callback lifetime**: joining the worker stops computation but can leave a completion callback
  queued on the main loop. Each posted callback holds a shared "alive" flag that the runner clears in
  its destructor, so a late callback touches nothing. Job ownership does not depend on the callback
  running: the destructor frees whatever is queued or finished. In the addon the event dispatcher
  outlives the session, and is detached with the engine.
- On shutdown the worker is joined before the engine is freed.

### 4.7 `session` and `engine`

`session` holds the shell logic and has no fcitx5 dependency: it talks to the application through a small
`Client` interface (commit, set and clear preedit, show and hide candidates, password flag, left context),
as `ShanjieKit` does on macOS, so it is tested against a fake client with the real core. `engine` is the
fcitx5 input method engine: a thin adapter that turns fcitx5 events into session calls and implements
`Client` per input context. All C ABI calls except `rerank_run` and `rerank_free` happen on the fcitx5
main thread.

- **One engine per process** (about 240 MB). Per input context the shell keeps only the last preedit it
  sent and whether a composition is open.
- **Owner**: the input context that last received a key. A key in another context first commits the open
  composition to its owner. Switching away from the input method calls `reset(0)` and commits. On
  focus-out fcitx5 (or the client) commits the client preedit itself, so the session only discards with
  `reset(1)`; a context without client preedit gets `reset(0)`. A password or sensitive context is never
  composed in. A context destroyed while it owns a composition gets `reset(1)`.
- **Application reset** (`InputMethodEngine::reset`, which fcitx5 leaves empty by default): an
  application may ask for a reset while it keeps the focus, having dropped its own preedit. The adapter
  overrides it. If the context owns the composition the session discards it with `reset(1)`, clears the
  preedit and candidates it showed and its last-sent cache, and cancels pending rerank jobs, so neither
  the next key nor a late rerank result brings the old text back. A reset from a context that is not
  the owner does nothing.
- **Modifier keys** pressed alone are ignored: they neither reach the core nor end the composition.
- **Key path**: ignore releases; before each key sample the learning gate and call `set_learning`
  (on when the setting is on and the context is not a password field); at the start of a composition
  call `set_left_context`; translate; a key outside the table commits an open composition with
  `reset(0)` and passes through; otherwise `shanjie_engine_key` and apply.
- **Left context**: when the context supports surrounding text and it is valid, the text before the
  insertion point (the start of the selection, whichever way it was made, since the selection is about
  to be replaced) on the same line (after the last line break or NUL, the macOS shell's rule), at most 64
  characters; otherwise none. In a terminal (fcitx5's `Terminal` capability) the
  surrounding text is commands and their output, so only the run of Han characters right before the
  cursor is passed on, as the C ABI requires (`shanjie.h`, `set_left_context`); the Han ranges are the
  core's (`core/src/learn.rs`, `is_han`).
- **Sensitive fields**: the password-or-sensitive check gates every path that could send text to a
  context, not only keys. A candidate click and a finished rerank job in such a context discard the
  composition instead of updating it; a commit that would land there (owner change, settings change,
  input method switch) is turned into a discard. fcitx5 deactivates the input method on the way into
  a password field while the context's flags are still the old ones: it arrives as a capability event
  carrying the new flags, or as an input method switch whose reason is the capability change. The
  adapter's deactivate policy (`fcitx_policy.h`, `commitOnDeactivate`) therefore reads the event, not
  only the current flags: new flags that are password or sensitive, or a switch with that reason,
  always discard; only a switch the person asked for commits. It is a pure function tested with real
  fcitx5 event objects (`test_fcitx_policy`). The adapter also watches fcitx5's capability-changed event,
  so a field that becomes sensitive while it owns a composition loses it at once and its pending rerank
  jobs are cancelled.
- **Inline preedit turned on or off** (fcitx5's embedded-preedit toggle, or the application changing
  its Preedit flag) while a composition is open: the preedit must change place, or the old place keeps
  a copy and focus-out commits it twice (off) or not at all (on). On the capability-about-to-change event,
  while the old flags still apply, the session takes the owner's preedit out of its old place
  (`InputContext::updatePreedit` sends nothing to the application once the Preedit flag is gone); on the
  capability-changed event it puts the same preedit into the new place. `fcitx_policy.h`
  (`preeditMoves`) decides from the event whether the Preedit flag changed.

## 5. Applying an output

### 5.1 Order

1. `commit` not empty: commit the string to the context.
2. `preedit`: one underlined segment, with the English run in a distinct style, and the cursor at the
   converted byte offset. Client preedit when the context supports it, otherwise the panel preedit.
   If preedit, cursor and English run equal what this context was last sent and there is no commit,
   nothing is resent. Clearing the preedit forgets the last-sent value.
3. `handled` 0 with an empty commit is a pure pass-through: the context is not touched.
   `handled` 0 with a commit: apply, then let the key through.
4. Candidates: empty hides the panel; otherwise set the list from §4.5.
5. Take the rerank job (§4.6).

### 5.2 Vertical candidate window

fcitx5's stock panel cannot draw the core's expanded grid. The shell therefore always sends
`set_candidate_vertical(1)`, as the Windows port does: nine rows per page, numbered 1–9, no grid, and the
prediction row is vertical as well. A horizontal option is left for a later version.

### 5.3 Errors

A non-zero code from `key` or `pick`: log the code only, call `reset(1)`, clear the preedit and the
panel, and pass the key through.

The rerank functions have no key, so their codes never mean pass-through, but they differ in what they
leave behind (`shanjie.h`, the rerank section):

- `rerank_take` or `rerank_apply` returning 4: the core has already discarded the composition. The
  shell must follow: clear the preedit, the candidates and its composition state, cancel pending jobs,
  and log the code. Other non-zero codes from these two are handled the same way.
- `rerank_apply` returning 5: nothing changed; nothing to do.
- `rerank_run` failing: no engine is touched and no result is kept; the composition stays as shown and
  a later apply answers 5.

Input text, candidates, model paths and job pointers are never logged (`docs/spec/privacy.md`, R2).

## 6. Settings

| Option | Core call | Rebuild |
|---|---|---|
| Keyboard layout: standard, ETen | `new_packs` layout | yes |
| Word packs: ACG, quotes, IT and AI, meme, beauty | `new_packs` mask | yes |
| Writing style: chat, formal | `set_profile` | no |
| Avoid sensitive words | `set_demote` | no |
| Prediction row | `set_prediction` | no |
| Abbreviated finals | `set_abbreviation` | no |
| English mixing, and its three options | `set_english`, `set_english_overwrite`, `set_english_key_order`, `set_english_reversal_guard` | no |
| Fuzzy input, five flags | `set_fuzzy` | no |
| Semantic rerank, and strength | `set_rerank`, `set_rerank_level` | no |
| Learning | gate for `set_learning` | no |
| Key habit: Shanjie, Microsoft Zhuyin | `set_key_scheme` | no |
| Candidate selection keys: digits, `asdfghjkl;` | none (shell only, §6.1) | no |

Defaults follow the macOS app's defaults. A change that keeps the engine leaves an open composition
open: only the setters whose value changed are called (`CoreEngine::applyChanges`), as the macOS shell
calls one setter per change, because every setter recomputes the composition even with an unchanged
value (`set_profile` and turning prediction off close an open candidate window). The last snapshot is
applied, with the commits of all of them in order; code 4 from any setter ends the composition as any
failure does. A change only the shell sees (the selection keys) shows the list on screen again with the
new labels. A composition in a password or sensitive field is dropped first, as everywhere.

### 6.1 Letter selection keys

The core picks with the digits 1–9. Letter selection keys are a shell feature, with no change to the
core: while a selection is open (the candidate window, or the prediction row after Tab; `candidate_selected`
0 or more) an unmodified selection key calls `shanjie_engine_pick` with its position, the path a mouse
click takes, so learning is the same as for a digit. A key whose position is beyond the items shown is
eaten. The digits keep working. A prediction row that is only showing is not a selection, so the keys
type Zhuyin there, as they do whenever nothing is open. The list's labels show the letters. The row
has ten keys and a page nine items, so `;` never has an item and is eaten.

## 7. Testing

- **Unit tests** (CTest, no fcitx5, no models): every ANSI key maps to its unshifted character; special
  keys; modifier bits; keys outside the table; Page Up/Down gating; UTF-16 to byte conversion including
  surrogate pairs and out-of-range offsets.
- **Core wrapper test** (real lexicon and models): both layouts type 你好 through `core_engine`; the
  settings sequence is sent after every build; a missing model directory still yields a typing engine.
- **Rerank reaches the client**: a fixed key sequence whose n-gram decoding differs from its reranked
  one, with the exact preedit and committed text asserted for asynchronous completion, for Enter's wait
  and for a zero wait. A session that drops rerank results fails these.
- **Session tests** (fake client, real core and models): `s u 3 c l 3` then Enter commits 你好; Space
  opens the vertical window and a pick works; a key outside the table commits; a lone modifier does not;
  a key in another client commits to the owner; a password client passes through; the ETen layout after
  a settings change; a rerank job runs and applies through a hand-pumped queue.
- The fcitx5 adapter has no automated test (the test frontend's header is not in the development
  package); it is checked by hand.
- **Manual check**: a GTK application, a Qt application and a terminal, under Wayland and X11 where
  available.
- Tests that need models fail with a message saying how to get them; they do not skip.

## 8. Known limitations

- Vertical candidate window only.
- The rerank runs on the core's plain (non-Accelerate) path. With its AVX2 and AVX-512 kernels a
  short sentence takes about 30 ms on the development machine (median 28 ms, slowest 37 ms over 245
  sentences), well inside Enter's 100 ms wait. CPUs without them (the scalar kernel, e.g. 64-bit ARM)
  are slower and unmeasured; there an Enter right after the last key can send the uncorrected text.
- Super+Backspace for forgetting may be taken by the compositor.
- Without surrounding-text support in the application there is no left context, so the first word of a
  composition is decoded as a sentence start.
- Changing layout or packs blocks the fcitx5 main loop for about a second while the engine rebuilds.
