#pragma once

#include "document.h"
#include "registry.h"

#include <string>
#include <vector>

namespace ohh {

enum class Severity { Error, Warning, Information, Hint };

struct Diagnostic {
  Range range;
  Severity severity = Severity::Error;
  std::string code;
  std::string message;
};

// Runs the L1 checks against an
// already-parsed document: missing required top-level fields, enum-value
// membership for game_type/bet_type/street/action, and the spec's own
// stated "Bet is not a valid preflop action" rule. Does NOT re-check JSON
// syntax; ParsedDocument::has_syntax_error/syntax_error_ranges already
// cover that, from the grammar directly.
//
// Known scope limits (see ohh-ls/README.md for the full list): only
// top-level hand-object field presence is checked for "missing required
// field", nested-object (player_obj, round_obj, etc.) required-field
// completeness is not checked in this first pass; currency is not
// validated against ISO 4217 (no list vendored yet, per
// ohh/registry/README.md's "known gaps").
std::vector<Diagnostic> validate(const ParsedDocument &doc, const Registry &registry);

} // namespace ohh
