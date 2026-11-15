# ohh-ls

A standard-LSP-only server for OHH (Open Hand History, `.ohh`), over
stdio, the exact same architectural role `phh-ls` plays for PHH.
Every editor package
(`vscode-ohh`, and later Neovim/Vim/Zed) talks to this exact binary the
same way: start it, speak LSP over stdio.

## Build

```sh
cmake -S . -B build
cmake --build build -j4
```

Works the same way on macOS, Linux and Windows: the only prerequisites
are a C/C++ compiler and CMake 3.16+, same as `phh-ls`'s own README
explains in full, tree-sitter (the runtime library) and yyjson are
fetched and built from source via CMake's `FetchContent`, not found
through a system package manager.

Produces `ohh-ls` (the LSP server) and `ohh-check` (a standalone CLI for
fast iteration against the corpus, independent of the LSP/stdio layer, mirrors `phh-check`). Both build against `../tree-sitter-ohh`'s generated
parser directly, same as `phh-ls` does for its two grammars.

## What's implemented

- **Parsing**: `src/document.{h,cpp}` walks `tree-sitter-ohh`'s parse tree
  into a generic JSON value tree (`JsonValue`/`JsonField`), unlike
  `phh-ls`'s flat `Hand`/`Field` model (TOML tables are one level deep),
  OHH's fields nest arbitrarily (`rounds` → `actions` → `action`), so the
  validator needs the real recursive shape.
- **Diagnostics** (`src/validate.{h,cpp}`): missing required top-level
  fields (`ohh-data/registry/fields.json`), enum-value membership for
  `game_type`, `bet_type` (inside `bet_limit`), `street` and `action`
  (inside every round and action), and the spec's own stated exception
  rule ("Bet" is not valid on the "Preflop" street).
- **Hover and completion** (`src/main.cpp`): hover walks the parsed tree
  to the deepest field under the cursor and looks it up in the registry
  (enum member description if enum-valued, else type/required/doc text);
  completion lists every known field name plus every known enum value.
- Standard LSP only (`initialize`, `didOpen`/`didChange`/`didClose`,
  `hover`, `completion`, `shutdown`/`exit`), full-document sync, same
  capability set as `phh-ls`.

## Known scope limits (first pass, not hidden)

- **Only top-level hand-object field presence is checked** for
  "missing required field." Nested-object required-field completeness
  (e.g. a `player_obj` missing `starting_stack`) is not checked yet.
- **`currency` is not validated** against ISO 4217, no list is vendored
  (`ohh-data/registry/README.md`'s "known gaps"); `Registry::enum_contains`
  returns `true` (no-op) for any enum group with no loaded values, by
  design, so an unvalidated field never produces a false diagnostic.
- **Hover precision for `flags`** is documented as approximate: the
  registry merges nested-object field docs by plain key name
  (`registry.h`'s `fields` map), and `flags` means different things
  (and uses a different enum group) at the hand level vs. inside
  `tournament_info`, the hand-level entry wins on this name collision.
  Fixing this precisely would need hover to carry path context, not done
  in this first pass.
- **`start_date_utc`'s exact format is not validated**, the spec gives
  only an example, no explicit grammar.
- **Card-string format** (`ohh-data/registry/cards.json`) is not validated at
  all yet, no check corresponds to PHH's short-deck-rank-legality check.

## How this was verified

- **`ohh-check` against the full corpus** (`ohh-data/corpus/`) reproduces
  exactly zero diagnostics on every `valid/` fixture and exactly one
  diagnostic, the intended violation, on each `invalid/` fixture.
  **This caught a real registry bug, not a code bug**: `fields.json`
  originally marked `tournament`, `tournament_info`, `table_handle` and
  `table_skin` as `required: true` (following the spec's literal global
  default), and running the validator against
  `holdem-cash-pokerstars.ohh`, the one fixture sourced directly from
  the spec site's own published example, immediately flagged all four
  as missing. Fixed by marking them optional, with the empirical
  counter-example recorded in `ohh-data/registry/fields.json` and
  `ohh-data/registry/README.md`, rather than trusting the spec's prose over
  its own worked example.
- **`ohh-ls/tests/lsp_smoke_test.py` drives the actual server binary**
  over real Content-Length-framed JSON-RPC (not internal function calls):
  initialize → open every corpus file and check `publishDiagnostics` →
  hover on the real spec example's `game_type` field → completion →
  a live `didChange` that introduces a `bad-enum-value` diagnostic →
  `didClose` → `shutdown`/`exit`. **25/25 checks pass.**
- A real compile-time bug was also caught and fixed before any of the
  above: an early draft of `document.cpp`'s hand-extraction code tried to
  copy a `JsonValue` (which holds `unique_ptr` members and is therefore
  move-only) via a convoluted cast; CMake's real build would have failed
  outright. Fixed to move the value out instead, and the resulting build
  succeeds cleanly.
