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

- CMake finds `Fcitx5Core`, runs `cargo build --release --locked -p core` as a custom target, and links
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

All lookups go through fcitx5's standard path API. The documented install is system-wide; fcitx5 only
loads addon libraries from its own library directory unless `FCITX_ADDON_DIRS` is set. When `data/lm`
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
- Page Up and Page Down map to kinds 14 and 15 through a separate function. The engine unit sends them
  only while the last applied output had `candidate_vertical` 1 and no modifier is held.
- Modifiers: Shift bit 0, Control bit 1, Alt bit 2, Super bit 3, Caps Lock bit 4. "Forget this word" is
  therefore Super+Backspace. A compositor that grabs Super combinations will hide it; that is a known
  limitation of the first version.
- Key releases are never translated.

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
composition; every other option is sent to the live engine.

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
- Before an Enter with no candidate window open, the engine unit waits a bounded time for a running job
  and applies it first, so the committed text matches what the rerank would have shown
  (`docs/spec/semantic-rerank.md`).
- On shutdown the worker is joined before the engine is freed. Jobs own their data, so the order is for
  tidiness, not safety.

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
- **Modifier keys** pressed alone are ignored: they neither reach the core nor end the composition.
- **Key path**: ignore releases; before each key sample the learning gate and call `set_learning`
  (on when the setting is on and the context is not a password field); at the start of a composition
  call `set_left_context`; translate; a key outside the table commits an open composition with
  `reset(0)` and passes through; otherwise `shanjie_engine_key` and apply.
- **Left context**: when the context supports surrounding text and it is valid, the text before the
  cursor, at most 64 characters; otherwise none.

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
panel, and pass the key through. Non-zero codes from the rerank functions never mean pass-through.
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

Defaults follow the macOS app's defaults. A settings change first commits an open composition, so the
snapshots the setting calls return are not applied.

## 7. Testing

- **Unit tests** (CTest, no fcitx5, no models): every ANSI key maps to its unshifted character; special
  keys; modifier bits; keys outside the table; Page Up/Down gating; UTF-16 to byte conversion including
  surrogate pairs and out-of-range offsets.
- **Core wrapper test** (real lexicon and models): both layouts type 你好 through `core_engine`; the
  settings sequence is sent after every build; a missing model directory still yields a typing engine.
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
- Super+Backspace for forgetting may be taken by the compositor.
- Without surrounding-text support in the application there is no left context, so the first word of a
  composition is decoded as a sentence start.
- Changing layout or packs blocks the fcitx5 main loop for about a second while the engine rebuilds.
