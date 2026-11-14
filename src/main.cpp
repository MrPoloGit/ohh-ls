// ohh-ls: a standard-LSP-only server for OHH (.ohh), over stdio. Mirrors
// phh-ls/src/main.cpp's shape exactly.

#include "document.h"
#include "jsonrpc.h"
#include "registry.h"
#include "validate.h"

#include <yyjson.h>

#include <cstdio>
#include <iostream>
#include <map>
#include <string>

using namespace ohh;

namespace {

int severity_to_lsp(Severity s) {
  switch (s) {
  case Severity::Error: return 1;
  case Severity::Warning: return 2;
  case Severity::Information: return 3;
  case Severity::Hint: return 4;
  }
  return 1;
}

yyjson_mut_val *position_to_json(yyjson_mut_doc *doc, const Position &p) {
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_uint(doc, obj, "line", p.line);
  yyjson_mut_obj_add_uint(doc, obj, "character", p.character);
  return obj;
}

yyjson_mut_val *range_to_json(yyjson_mut_doc *doc, const Range &r) {
  yyjson_mut_val *obj = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_val(doc, obj, "start", position_to_json(doc, r.start));
  yyjson_mut_obj_add_val(doc, obj, "end", position_to_json(doc, r.end));
  return obj;
}

struct OpenDocument {
  std::string text;
};

bool position_in_range(const Range &r, const Position &pos) {
  auto before_or_eq = [](const Position &a, const Position &b) {
    return a.line < b.line || (a.line == b.line && a.character <= b.character);
  };
  return before_or_eq(r.start, pos) && before_or_eq(pos, r.end);
}

// Finds the deepest JsonField (by key or value range) containing `pos`,
// searching the whole hand tree recursively, OHH's fields nest
// arbitrarily (rounds -> actions -> action), unlike phh-ls's flat
// field_at, which only had to search one hand's direct fields.
const JsonField *find_field_at(const JsonValue &node, const Position &pos) {
  if (node.kind == JsonKind::Object) {
    for (const auto &f : node.fields) {
      bool in_key = position_in_range(f.key_range, pos);
      bool in_value = f.value && position_in_range(f.value->range, pos);
      if (!in_key && !in_value) continue;
      if (f.value) {
        if (const JsonField *deeper = find_field_at(*f.value, pos)) return deeper;
      }
      return &f;
    }
  } else if (node.kind == JsonKind::Array) {
    for (const auto &el : node.elements) {
      if (position_in_range(el.range, pos)) {
        if (const JsonField *deeper = find_field_at(el, pos)) return deeper;
      }
    }
  }
  return nullptr;
}

class Server {
public:
  Server(Registry registry) : registry_(std::move(registry)) {}

  void run() {
    while (true) {
      auto body = read_message(stdin);
      if (!body) break;

      yyjson_doc *doc = yyjson_read(body->data(), body->size(), 0);
      if (!doc) continue;
      yyjson_val *root = yyjson_doc_get_root(doc);
      yyjson_val *method_val = yyjson_obj_get(root, "method");
      yyjson_val *id_val = yyjson_obj_get(root, "id");
      yyjson_val *params = yyjson_obj_get(root, "params");
      std::string method = method_val ? yyjson_get_str(method_val) : "";

      dispatch(method, id_val, params);

      yyjson_doc_free(doc);
      if (should_exit_) break;
    }
  }

private:
  Registry registry_;
  DocumentParser parser_;
  std::map<std::string, OpenDocument> open_documents_;
  bool should_exit_ = false;

  void dispatch(const std::string &method, yyjson_val *id, yyjson_val *params) {
    if (method == "initialize") {
      respond_initialize(id);
    } else if (method == "initialized") {
      // nothing to do
    } else if (method == "textDocument/didOpen") {
      on_did_open(params);
    } else if (method == "textDocument/didChange") {
      on_did_change(params);
    } else if (method == "textDocument/didClose") {
      on_did_close(params);
    } else if (method == "textDocument/hover") {
      on_hover(id, params);
    } else if (method == "textDocument/completion") {
      on_completion(id, params);
    } else if (method == "shutdown") {
      respond_null(id);
    } else if (method == "exit") {
      should_exit_ = true;
    } else if (id) {
      respond_method_not_found(id, method);
    }
  }

  void send(yyjson_mut_doc *doc) {
    char *json = yyjson_mut_write(doc, 0, nullptr);
    if (json) {
      write_message(stdout, json);
      free(json);
    }
    yyjson_mut_doc_free(doc);
  }

  yyjson_mut_doc *new_response(yyjson_val *id, yyjson_mut_val **out_obj) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, obj);
    yyjson_mut_obj_add_strcpy(doc, obj, "jsonrpc", "2.0");
    if (id) {
      yyjson_mut_val *id_copy = yyjson_val_mut_copy(doc, id);
      yyjson_mut_obj_add_val(doc, obj, "id", id_copy);
    } else {
      yyjson_mut_obj_add_null(doc, obj, "id");
    }
    *out_obj = obj;
    return doc;
  }

  void respond_null(yyjson_val *id) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_obj_add_null(doc, obj, "result");
    send(doc);
  }

  void respond_method_not_found(yyjson_val *id, const std::string &method) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *err = yyjson_mut_obj_add_obj(doc, obj, "error");
    yyjson_mut_obj_add_int(doc, err, "code", -32601);
    yyjson_mut_obj_add_strcpy(doc, err, "message", ("method not found: " + method).c_str());
    send(doc);
  }

  void respond_initialize(yyjson_val *id) {
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_obj_add_obj(doc, obj, "result");
    yyjson_mut_val *caps = yyjson_mut_obj_add_obj(doc, result, "capabilities");
    yyjson_mut_obj_add_int(doc, caps, "textDocumentSync", 1);
    yyjson_mut_obj_add_true(doc, caps, "hoverProvider");
    yyjson_mut_val *completion = yyjson_mut_obj_add_obj(doc, caps, "completionProvider");
    yyjson_mut_val *trigger = yyjson_mut_arr(doc);
    yyjson_mut_arr_add_strcpy(doc, trigger, "\"");
    yyjson_mut_obj_add_val(doc, completion, "triggerCharacters", trigger);

    yyjson_mut_val *info = yyjson_mut_obj_add_obj(doc, result, "serverInfo");
    yyjson_mut_obj_add_strcpy(doc, info, "name", "ohh-ls");
    yyjson_mut_obj_add_strcpy(doc, info, "version", "0.1.0");
    send(doc);
  }

  std::string extract_uri(yyjson_val *params) {
    yyjson_val *td = yyjson_obj_get(params, "textDocument");
    yyjson_val *uri = yyjson_obj_get(td, "uri");
    return uri ? yyjson_get_str(uri) : std::string();
  }

  void on_did_open(yyjson_val *params) {
    yyjson_val *td = yyjson_obj_get(params, "textDocument");
    std::string uri = extract_uri(params);
    yyjson_val *text = yyjson_obj_get(td, "text");
    if (uri.empty() || !text) return;
    open_documents_[uri] = OpenDocument{yyjson_get_str(text)};
    publish_diagnostics(uri);
  }

  void on_did_change(yyjson_val *params) {
    std::string uri = extract_uri(params);
    if (uri.empty()) return;
    yyjson_val *changes = yyjson_obj_get(params, "contentChanges");
    if (!yyjson_is_arr(changes) || yyjson_arr_size(changes) == 0) return;
    yyjson_val *last = yyjson_arr_get_last(changes);
    yyjson_val *text = yyjson_obj_get(last, "text");
    if (!text) return;
    open_documents_[uri] = OpenDocument{yyjson_get_str(text)};
    publish_diagnostics(uri);
  }

  void on_did_close(yyjson_val *params) {
    std::string uri = extract_uri(params);
    open_documents_.erase(uri);
    send_publish_diagnostics(uri, {}, nullptr);
  }

  void publish_diagnostics(const std::string &uri) {
    auto it = open_documents_.find(uri);
    if (it == open_documents_.end()) return;
    ParsedDocument parsed = parser_.parse(it->second.text);
    auto diagnostics = validate(parsed, registry_);
    send_publish_diagnostics(uri, diagnostics, &parsed);
  }

  void send_publish_diagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics,
                                 const ParsedDocument *parsed) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val *obj = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, obj);
    yyjson_mut_obj_add_strcpy(doc, obj, "jsonrpc", "2.0");
    yyjson_mut_obj_add_strcpy(doc, obj, "method", "textDocument/publishDiagnostics");
    yyjson_mut_val *params = yyjson_mut_obj_add_obj(doc, obj, "params");
    yyjson_mut_obj_add_strcpy(doc, params, "uri", uri.c_str());
    yyjson_mut_val *arr = yyjson_mut_obj_add_arr(doc, params, "diagnostics");

    if (parsed) {
      for (const auto &r : parsed->syntax_error_ranges) {
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, arr);
        yyjson_mut_obj_add_val(doc, item, "range", range_to_json(doc, r));
        yyjson_mut_obj_add_int(doc, item, "severity", 1);
        yyjson_mut_obj_add_strcpy(doc, item, "source", "ohh-ls");
        yyjson_mut_obj_add_strcpy(doc, item, "code", "syntax-error");
        yyjson_mut_obj_add_strcpy(doc, item, "message", "Invalid JSON/OHH syntax.");
      }
    }
    for (const auto &d : diagnostics) {
      yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, arr);
      yyjson_mut_obj_add_val(doc, item, "range", range_to_json(doc, d.range));
      yyjson_mut_obj_add_int(doc, item, "severity", severity_to_lsp(d.severity));
      yyjson_mut_obj_add_strcpy(doc, item, "source", "ohh-ls");
      yyjson_mut_obj_add_strcpy(doc, item, "code", d.code.c_str());
      yyjson_mut_obj_add_strcpy(doc, item, "message", d.message.c_str());
    }
    send(doc);
  }

  void on_hover(yyjson_val *id, yyjson_val *params) {
    std::string uri = extract_uri(params);
    yyjson_val *position = yyjson_obj_get(params, "position");
    auto it = open_documents_.find(uri);
    if (it == open_documents_.end() || !position) { respond_null(id); return; }

    Position pos;
    pos.line = static_cast<uint32_t>(yyjson_get_uint(yyjson_obj_get(position, "line")));
    pos.character = static_cast<uint32_t>(yyjson_get_uint(yyjson_obj_get(position, "character")));

    ParsedDocument parsed = parser_.parse(it->second.text);
    const JsonField *field = nullptr;
    for (const auto &hand : parsed.hands) {
      field = find_field_at(hand, pos);
      if (field) break;
    }
    if (!field) { respond_null(id); return; }

    std::string text;
    auto field_it = registry_.fields.find(field->key);
    bool has_value = field->value && field->value->kind == JsonKind::String;

    if (field_it != registry_.fields.end() && !field_it->second.enum_group.empty() && has_value) {
      auto enum_it = registry_.enums.find(field_it->second.enum_group);
      std::string value = field->value->text;
      if (enum_it != registry_.enums.end() && enum_it->second.values.count(value)) {
        text = "**" + value + "** (" + field->key + ")\n\n" + enum_it->second.values.at(value);
      } else {
        text = "**" + field->key + "**\n\n" + field_it->second.doc;
      }
    } else if (field_it != registry_.fields.end()) {
      text = "**" + field->key + "** (" + field_it->second.type +
             (field_it->second.required ? ", required" : ", optional") + ")\n\n" +
             field_it->second.doc;
    } else {
      respond_null(id);
      return;
    }

    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_obj_add_obj(doc, obj, "result");
    yyjson_mut_val *contents = yyjson_mut_obj_add_obj(doc, result, "contents");
    yyjson_mut_obj_add_strcpy(doc, contents, "kind", "markdown");
    yyjson_mut_obj_add_strcpy(doc, contents, "value", text.c_str());
    send(doc);
  }

  void on_completion(yyjson_val *id, yyjson_val * /*params*/) {
    // Same deliberately simple first-release shape as phh-ls::on_completion
    // : every known field name, letting
    // the editor's own fuzzy filtering narrow it by context.
    yyjson_mut_val *obj;
    yyjson_mut_doc *doc = new_response(id, &obj);
    yyjson_mut_val *result = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, obj, "result", result);

    auto add_item = [&](const std::string &label, const std::string &detail, int kind) {
      yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, result);
      yyjson_mut_obj_add_strcpy(doc, item, "label", label.c_str());
      yyjson_mut_obj_add_strcpy(doc, item, "detail", detail.c_str());
      yyjson_mut_obj_add_int(doc, item, "kind", kind);
    };

    for (const auto &[name, info] : registry_.fields) {
      add_item(name, info.type, 5 /* Field */);
    }
    for (const auto &[group, info] : registry_.enums) {
      for (const auto &[value, desc] : info.values) {
        add_item(value, group + ": " + desc, 21 /* EnumMember */);
      }
    }

    send(doc);
  }
};

} // namespace

int main(int argc, char **argv) {
  ensure_binary_stdio();

  if (argc < 2) {
    std::cerr << "usage: ohh-ls <registry-dir>\n";
    return 2;
  }

  Registry registry;
  std::string error;
  if (!registry.load(argv[1], error)) {
    std::cerr << "ohh-ls: failed to load registry: " << error << "\n";
    return 1;
  }

  Server server(std::move(registry));
  server.run();
  return 0;
}
