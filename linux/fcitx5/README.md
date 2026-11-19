# Shanjie for fcitx5 (Linux)

An fcitx5 input method built on the Shanjie core. Design: `docs/superpowers/specs/2026-11-12-fcitx5-port-design.md`.

## Requirements

- fcitx5 5.1.13 or later with its development package (Debian and Ubuntu: `libfcitx5core-dev`; the 5.1.7
  of Ubuntu 24.04 is too old)
- CMake 3.21 or later, a C++20 compiler
- The Rust toolchain named in `CONTRIBUTING.md`
- The model files in `data/lm/` (download commands in `CONTRIBUTING.md`; the `curl` form needs no account)

## Build and test

From the repository root:

    cmake -S linux/fcitx5 -B build/fcitx5 -DCMAKE_BUILD_TYPE=Release -DFCITX_INSTALL_USE_FCITX_SYS_PATHS=ON
    cmake --build build/fcitx5 -j
    ctest --test-dir build/fcitx5 --output-on-failure

The tests use the real lexicon and models and fail, rather than skip, when the models are missing.

## Install

fcitx5 loads addon libraries only from its own library directory, so the addon must be installed
there, not under `/usr/local`. `FCITX_INSTALL_USE_FCITX_SYS_PATHS=ON` (the default in this project,
and spelled out in the configure command above) does that. The configure step prints the directory
("Shanjie addon library goes to ..."); it must be the one that holds fcitx5's other addons.

    sudo cmake --install build/fcitx5
    fcitx5 -rd

Then add "Shanjie Zhuyin" in the fcitx5 configuration tool. Settings are under the input method's
configure button. The learning file is kept in `shanjie/learn` under fcitx5's user data directory.

The licenses and attribution of the shipped data are installed in `shanjie/LICENSES` next to it
(`data.md` lists every source).

To update the models later, replace the files by rename, not by writing in place: the trigram tables
are memory-mapped while fcitx5 runs.

## Selection keys

Candidates are picked with 1–9 by default. "Candidate selection keys" in the settings switches to the
home row `a s d f g h j k l ;`; the list then shows the letters, and the digits still work. The letters
pick only while a list is open for choosing (after Space or Down, or after Tab in the prediction row);
otherwise they type Zhuyin. A page holds nine items, so `;` is spare.

## Known limitations

- The candidate window is always vertical; there is no expanded grid.
- "Forget this word" is Super+Backspace, which some desktops reserve.
- Applications that do not report surrounding text give the first word of a composition no left context.
- Page Up and Page Down page the open candidate window; in any other state, or with a modifier, they
  go to the application and the composition stays open (on macOS they commit it first).
- The semantic rerank uses the core's portable path, not Accelerate. On x86-64 with AVX2 or AVX-512 it
  takes about 30 ms for a short sentence (well inside Enter's 100 ms wait for it); other CPUs, such as
  64-bit ARM, run the scalar kernel and are slower, so there an Enter pressed right after the last key
  may send the uncorrected text.
- Changing the keyboard layout or a word pack blocks fcitx5 for about a second while the engine rebuilds.
- No single-Shift English toggle, no automatic chat/formal choice per application.
