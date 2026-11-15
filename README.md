# ohh-ls

A standard-LSP-only server for OHH (Open Hand History, `.ohh`).

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
