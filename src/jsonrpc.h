#pragma once

#include <cstdio>
#include <optional>
#include <string>

// Minimal JSON-RPC-over-stdio framing for LSP (Content-Length headers,
// \r\n\r\n separator), identical in shape to phh-ls/src/jsonrpc.h, kept as
// a separate copy per package rather than a shared library, per
//  principle 1 ("one language server per
// language... they can share a code library, but they install and run
// separately"). If a third server needs this, promote it to a tiny shared
// header-only lib then, not before.
namespace ohh {

// Puts stdin/stdout into binary mode on Windows, a no-op everywhere else.
// Without this, the C runtime's default text mode rewrites every "\n" this
// protocol writes (including inside the literal "\r\n\r\n" header
// terminator) into "\r\n", corrupting the framing for every client. Call
// once, before any other stdio I/O.
void ensure_binary_stdio();

std::optional<std::string> read_message(FILE *in);
void write_message(FILE *out, const std::string &body);

} // namespace ohh
