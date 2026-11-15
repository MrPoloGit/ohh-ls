#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// Loads ohh-data/registry/*.json into
// lookup structures ohh-ls's validator and hover reads at request time.
// Mirrors phh-ls/src/registry.h's role for this project; see
// ohh-data/registry/README.md for the confidence caveat this registry carries
// that phh-data/registry does not (no cross-check against a second
// implementation yet).
namespace ohh {

struct FieldInfo {
  std::string type;
  bool required = false;
  std::string doc;
  std::string enum_group; // key into Registry::enums, or empty if not enum-valued
};

struct EnumInfo {
  // value string -> description. Order doesn't matter for lookups; a
  // std::map is fine here, there's no completion-ordering requirement yet.
  std::map<std::string, std::string> values;
};

class Registry {
public:
  // Loads fields.json, objects.json, enums.json from `directory`. Returns
  // false (and leaves `error` set) if any file is missing or malformed,
  // same startup discipline as phh-ls.
  bool load(const std::string &directory, std::string &error);

  // Keyed by plain field name (e.g. "game_type", "street", "action").
  // Populated from both fields.json (the top-level hand object) and
  // objects.json (every nested object), merged by name. A name that
  // means different things at different nesting depths (currently only
  // "flags": hand-level vs. tournament-level, with different enum groups)
  // collides here; fields.json's top-level entry wins. This is a known,
  // documented hover-precision limitation (ohh-ls/README.md), not a
  // silent bug, nested per-path field resolution would need the
  // validator to carry path context into hover, not done in this first
  // pass.
  std::map<std::string, FieldInfo> fields;

  std::map<std::string, EnumInfo> enums; // "game_type", "bet_type", "street", "action", "hand_flags", "tournament_flags"

  // The fields.json-defined top-level hand-object fields that are
  // required=true, this is the only set "missing required field"
  // actually checks against (nested-object required-field completeness is
  // not checked in this first pass; see ohh-ls/README.md).
  std::vector<std::string> required_top_level_fields;

  bool enum_contains(const std::string &group, const std::string &value) const;
};

} // namespace ohh
