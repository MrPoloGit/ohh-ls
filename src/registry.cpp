#include "registry.h"

#include <yyjson.h>

namespace ohh {

namespace {

yyjson_doc *read_json_file(const std::string &path, std::string &error) {
  yyjson_read_err err;
  yyjson_doc *doc = yyjson_read_file(path.c_str(), YYJSON_READ_NOFLAG, nullptr, &err);
  if (!doc) {
    error = "failed to parse " + path + ": " + (err.msg ? err.msg : "unknown error");
    return nullptr;
  }
  return doc;
}

std::string str_or_empty(yyjson_val *v) {
  const char *s = (v && yyjson_is_str(v)) ? yyjson_get_str(v) : nullptr;
  return s ? std::string(s) : std::string();
}

void load_field_entry(const std::string &name, yyjson_val *val, Registry &reg) {
  if (reg.fields.count(name)) return; // fields.json's top-level entry wins on collision
  FieldInfo info;
  info.type = str_or_empty(yyjson_obj_get(val, "type"));
  info.required = yyjson_get_bool(yyjson_obj_get(val, "required"));
  info.doc = str_or_empty(yyjson_obj_get(val, "doc"));
  info.enum_group = str_or_empty(yyjson_obj_get(val, "enum"));
  reg.fields[name] = std::move(info);
}

bool load_fields(yyjson_val *root, Registry &reg, std::string &error) {
  yyjson_val *fields = yyjson_obj_get(root, "fields");
  if (!yyjson_is_obj(fields)) {
    error = "fields.json: missing top-level \"fields\" object";
    return false;
  }
  size_t idx, max;
  yyjson_val *key, *val;
  yyjson_obj_foreach(fields, idx, max, key, val) {
    std::string name = str_or_empty(key);
    load_field_entry(name, val, reg);
    if (yyjson_get_bool(yyjson_obj_get(val, "required"))) {
      reg.required_top_level_fields.push_back(name);
    }
  }
  return true;
}

bool load_objects(yyjson_val *root, Registry &reg, std::string &error) {
  yyjson_val *objects = yyjson_obj_get(root, "objects");
  if (!yyjson_is_obj(objects)) {
    error = "objects.json: missing top-level \"objects\" object";
    return false;
  }
  size_t oidx, omax;
  yyjson_val *okey, *oval;
  yyjson_obj_foreach(objects, oidx, omax, okey, oval) {
    yyjson_val *obj_fields = yyjson_obj_get(oval, "fields");
    if (!yyjson_is_obj(obj_fields)) continue;
    size_t idx, max;
    yyjson_val *key, *val;
    yyjson_obj_foreach(obj_fields, idx, max, key, val) {
      load_field_entry(str_or_empty(key), val, reg);
    }
  }
  return true;
}

bool load_enums(yyjson_val *root, Registry &reg, std::string & /*error*/) {
  size_t idx, max;
  yyjson_val *key, *val;
  yyjson_obj_foreach(root, idx, max, key, val) {
    std::string group = str_or_empty(key);
    if (group == "_comment") continue;
    yyjson_val *values = yyjson_obj_get(val, "values");
    if (!yyjson_is_obj(values)) continue; // e.g. "currency" has no fixed "values" list, skipped by design
    EnumInfo info;
    size_t vidx, vmax;
    yyjson_val *vkey, *vval;
    yyjson_obj_foreach(values, vidx, vmax, vkey, vval) {
      info.values[str_or_empty(vkey)] = str_or_empty(vval);
    }
    reg.enums[group] = std::move(info);
  }
  return true;
}

} // namespace

bool Registry::load(const std::string &directory, std::string &error) {
  struct FilePart {
    const char *name;
    bool (*loader)(yyjson_val *, Registry &, std::string &);
  };
  const FilePart parts[] = {
      {"fields.json", load_fields},
      {"objects.json", load_objects},
      {"enums.json", load_enums},
  };

  for (const auto &part : parts) {
    std::string path = directory + "/" + part.name;
    yyjson_doc *doc = read_json_file(path, error);
    if (!doc) return false;
    yyjson_val *root = yyjson_doc_get_root(doc);
    bool ok = part.loader(root, *this, error);
    yyjson_doc_free(doc);
    if (!ok) return false;
  }
  return true;
}

bool Registry::enum_contains(const std::string &group, const std::string &value) const {
  auto it = enums.find(group);
  if (it == enums.end()) return true; // unknown enum group: nothing to validate against, don't false-flag
  return it->second.values.count(value) > 0;
}

} // namespace ohh
