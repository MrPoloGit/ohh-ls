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

std::optional<std::string> read_message(FILE *in);
void write_message(FILE *out, const std::string &body);

} // namespace ohh
