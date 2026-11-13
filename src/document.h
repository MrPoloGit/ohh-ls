#pragma once

#include <tree_sitter/api.h>

#include <memory>
#include <string>
#include <vector>

// Parses OHH source text with tree-sitter-ohh and reduces the resulting
// tree to a generic JSON value tree. Unlike phh-ls/src/document.h's flat
// Hand/Field model (which fits PHH's one-level TOML tables), OHH's
// fields nest arbitrarily (rounds -> actions -> action), so the
// validator needs the real tree shape, not a flattening of it.
namespace ohh {

struct Position {
  uint32_t line = 0;
  uint32_t character = 0;
};

struct Range {
  Position start;
  Position end;
};

enum class JsonKind { Object, Array, String, Number, True, False, Null, Unknown };

struct JsonValue;

struct JsonField {
  std::string key; // unquoted key text
  Range key_range;
  std::unique_ptr<JsonValue> value;
};

struct JsonValue {
  JsonKind kind = JsonKind::Unknown;
  Range range;

  // Populated for String (content with surrounding quotes stripped, escapes
  // NOT unescaped, every check this validator does only ever compares
  // against known plain-ASCII enum tokens, same reasoning as
  // phh-ls/src/document.h's string_value) and for Number (the raw text).
  std::string text;

  std::vector<JsonField> fields;   // populated for Object, in source order
  std::vector<JsonValue> elements; // populated for Array, in source order

  const JsonValue *find(const std::string &key) const {
    for (const auto &f : fields) {
      if (f.key == key) return f.value.get();
    }
    return nullptr;
  }
};

struct ParsedDocument {
  // One entry per hand: the value of each top-level `{"ohh": <this>}`
  // wrapper object found in the document. A multi-hand .ohh file naturally produces more than one entry here, no separate handling
  // needed, same principle as tree-sitter-ohh's own document root rule.
  std::vector<JsonValue> hands;

  bool has_syntax_error = false;
  std::vector<Range> syntax_error_ranges;
};

class DocumentParser {
public:
  DocumentParser();
  ~DocumentParser();
  DocumentParser(const DocumentParser &) = delete;
  DocumentParser &operator=(const DocumentParser &) = delete;

  ParsedDocument parse(const std::string &source) const;

private:
  TSParser *parser_;
};

} // namespace ohh
