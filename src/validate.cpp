#include "validate.h"

namespace ohh {

namespace {

void check_missing_required(const JsonValue &hand, const Registry &registry,
                             std::vector<Diagnostic> &out) {
  for (const auto &name : registry.required_top_level_fields) {
    if (!hand.find(name)) {
      Diagnostic d;
      d.range = hand.range;
      d.severity = Severity::Error;
      d.code = "missing-required-field";
      d.message = "Missing required field \"" + name + "\".";
      out.push_back(std::move(d));
    }
  }
}

void check_enum(const JsonValue *value, const std::string &field_name, const std::string &group,
                 const Registry &registry, std::vector<Diagnostic> &out) {
  if (!value || value->kind != JsonKind::String) return;
  if (registry.enum_contains(group, value->text)) return;
  Diagnostic d;
  d.range = value->range;
  d.severity = Severity::Error;
  d.code = "bad-enum-value";
  d.message = "\"" + value->text + "\" is not a known value for \"" + field_name + "\".";
  out.push_back(std::move(d));
}

void check_rounds(const JsonValue &hand, const Registry &registry, std::vector<Diagnostic> &out) {
  const JsonValue *rounds = hand.find("rounds");
  if (!rounds || rounds->kind != JsonKind::Array) return;

  for (const auto &round : rounds->elements) {
    if (round.kind != JsonKind::Object) continue;
    const JsonValue *street = round.find("street");
    check_enum(street, "street", "street", registry, out);

    const JsonValue *actions = round.find("actions");
    if (!actions || actions->kind != JsonKind::Array) continue;

    for (const auto &action : actions->elements) {
      if (action.kind != JsonKind::Object) continue;
      const JsonValue *action_val = action.find("action");
      check_enum(action_val, "action", "action", registry, out);

      // The spec's own stated exception : "Bet" is not valid preflop.
      if (action_val && action_val->kind == JsonKind::String && action_val->text == "Bet" &&
          street && street->kind == JsonKind::String && street->text == "Preflop") {
        Diagnostic d;
        d.range = action_val->range;
        d.severity = Severity::Error;
        d.code = "bet-not-valid-preflop";
        d.message = "\"Bet\" is not a valid action on the Preflop street, "
                     "there is already a live bet from the blinds "
                     "(players Check or Raise instead).";
        out.push_back(std::move(d));
      }
    }
  }
}

} // namespace

std::vector<Diagnostic> validate(const ParsedDocument &doc, const Registry &registry) {
  std::vector<Diagnostic> out;
  for (const auto &hand : doc.hands) {
    check_missing_required(hand, registry, out);
    check_enum(hand.find("game_type"), "game_type", "game_type", registry, out);

    if (const JsonValue *bet_limit = hand.find("bet_limit")) {
      check_enum(bet_limit->find("bet_type"), "bet_type", "bet_type", registry, out);
    }

    check_rounds(hand, registry, out);
  }
  return out;
}

} // namespace ohh
