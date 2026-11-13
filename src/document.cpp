#include "document.h"

extern "C" const TSLanguage *tree_sitter_ohh(void);

#include <cstring>

namespace ohh {

namespace {

Position point_to_position(TSPoint p) {
  Position pos;
  pos.line = p.row;
  pos.character = p.column;
  return pos;
}

Range node_range(TSNode node) {
  Range r;
  r.start = point_to_position(ts_node_start_point(node));
  r.end = point_to_position(ts_node_end_point(node));
  return r;
}

std::string node_text(TSNode node, const std::string &source) {
  uint32_t start = ts_node_start_byte(node);
  uint32_t end = ts_node_end_byte(node);
  if (end <= start || end > source.size()) return {};
  return source.substr(start, end - start);
}

// Strips the leading/trailing '"', a `string` node's raw text is always
// "<quoted content>" by construction of tree-sitter-ohh/grammar.js.
std::string strip_quotes(const std::string &raw) {
  if (raw.size() >= 2) return raw.substr(1, raw.size() - 2);
  return raw;
}

JsonKind classify(TSNode node) {
  const char *type = ts_node_type(node);
  if (!std::strcmp(type, "object")) return JsonKind::Object;
  if (!std::strcmp(type, "array")) return JsonKind::Array;
  if (!std::strcmp(type, "string")) return JsonKind::String;
  if (!std::strcmp(type, "number")) return JsonKind::Number;
  if (!std::strcmp(type, "true")) return JsonKind::True;
  if (!std::strcmp(type, "false")) return JsonKind::False;
  if (!std::strcmp(type, "null")) return JsonKind::Null;
  return JsonKind::Unknown;
}

std::unique_ptr<JsonValue> build_value(TSNode node, const std::string &source) {
  auto value = std::make_unique<JsonValue>();
  value->kind = classify(node);
  value->range = node_range(node);

  switch (value->kind) {
  case JsonKind::String:
    value->text = strip_quotes(node_text(node, source));
    break;
  case JsonKind::Number:
    value->text = node_text(node, source);
    break;
  case JsonKind::Object: {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_named_child(node, i);
      if (std::strcmp(ts_node_type(child), "pair") != 0) continue;
      TSNode key_node = ts_node_child_by_field_name(child, "key", 3);
      TSNode value_node = ts_node_child_by_field_name(child, "value", 5);
      JsonField field;
      field.key = strip_quotes(node_text(key_node, source));
      field.key_range = node_range(key_node);
      field.value = build_value(value_node, source);
      value->fields.push_back(std::move(field));
    }
    break;
  }
  case JsonKind::Array: {
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
      TSNode child = ts_node_named_child(node, i);
      value->elements.push_back(std::move(*build_value(child, source)));
    }
    break;
  }
  default:
    break;
  }
  return value;
}

void collect_error_ranges(TSNode node, std::vector<Range> &out) {
  if (ts_node_is_error(node) || ts_node_is_missing(node)) {
    out.push_back(node_range(node));
    return;
  }
  uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_error_ranges(ts_node_child(node, i), out);
  }
}

} // namespace

DocumentParser::DocumentParser() {
  parser_ = ts_parser_new();
  ts_parser_set_language(parser_, tree_sitter_ohh());
}

DocumentParser::~DocumentParser() { ts_parser_delete(parser_); }

ParsedDocument DocumentParser::parse(const std::string &source) const {
  ParsedDocument result;

  TSTree *tree = ts_parser_parse_string(parser_, nullptr, source.c_str(),
                                         static_cast<uint32_t>(source.size()));
  TSNode root = ts_tree_root_node(tree);

  if (ts_node_has_error(root)) {
    result.has_syntax_error = true;
    collect_error_ranges(root, result.syntax_error_ranges);
  }

  // Each top-level value in `document` (tree-sitter-ohh/grammar.js's root
  // rule: repeat of _value) is expected to be a `{"ohh": <hand>}` wrapper
  // object; its "ohh" pair's value is the hand itself. A top-level value
  // that is not such a wrapper (e.g. not an object, or an object with no
  // "ohh" key) produces no hand entry, best-effort. This skip is not
  // itself reported as an error here, since it may well be a different,
  // deliberately-permissive top-level value a future OHH revision adds.
  uint32_t count = ts_node_named_child_count(root);
  for (uint32_t i = 0; i < count; ++i) {
    TSNode child = ts_node_named_child(root, i);
    if (std::strcmp(ts_node_type(child), "object") != 0) continue;
    auto wrapper = build_value(child, source);
    for (auto &f : wrapper->fields) {
      if (f.key == "ohh" && f.value && f.value->kind == JsonKind::Object) {
        result.hands.push_back(std::move(*f.value));
        break;
      }
    }
  }

  ts_tree_delete(tree);
  return result;
}

} // namespace ohh
