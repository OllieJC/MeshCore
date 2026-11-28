#include "SensorWatch.h"
#include "../simple_repeater/KeyedChannel.h"
#include <helpers/TxtDataHelpers.h>
#include <strings.h>

namespace {
constexpr char command_name[] = "sensor.watch";
constexpr char default_rule[] = "#m7ojc wx,sensor,sensors";

char lowerAscii(char c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

bool blank(char c) { return c == ' ' || c == '\t'; }

void ruleKey(int slot, char (&key)[8]) {
  snprintf(key, sizeof(key), "rule%d", slot + 1);
}

// Match the whole message. A star consumes zero or more bytes.
bool matchPattern(const char* p, const char* end, const char* text, const char* text_end) {
  const char* after_star = nullptr;
  const char* retry = nullptr;
  while (text < text_end) {
    if (p < end && *p == '*') {
      after_star = ++p;
      retry = text;
    } else if (p < end && *p == lowerAscii(*text)) {
      ++p;
      ++text;
    } else if (after_star) {
      p = after_star;
      text = ++retry;
    } else {
      return false;
    }
  }
  while (p < end && *p == '*') ++p;
  return p == end;
}
}

const char* SensorWatch::Rule::parse(char* args) {
  char* name = nextToken(&args);
  if (!name || name[0] != '#' || !name[1] || strlen(name) > 31 ||
      !validChannelName(name) || strchr(name, ' ')) {
    return "Err - expected #channel (2 to 31 bytes)";
  }
  for (char* p = name; *p; ++p) *p = lowerAscii(*p);
  const size_t offset = strlen(name) + 1;
  memcpy(spec, name, offset - 1);
  spec[offset - 1] = ' ';
  char* out = spec + offset;
  const char* part = args;
  while (part) {
    const char* comma = strchr(part, ',');
    const char* end = comma ? comma : part + strlen(part);
    while (part < end && blank(*part)) ++part;
    while (end > part && blank(end[-1])) --end;
    if (end == part) return "Err - expected a pattern between commas";
    const bool separator = out != spec + offset;
    if (end - part + separator >= spec + sizeof(spec) - out) return "Err - rule exceeds 127 bytes";
    if (separator) *out++ = ',';
    while (part < end) {
      if (static_cast<unsigned char>(*part) < ' ') return "Err - use single-line patterns";
      *out++ = lowerAscii(*part++);
    }
    part = comma ? comma + 1 : nullptr;
  }
  *out = 0;
  deriveChannelKey(name, channel.secret, channel.hash);
  patterns_at = offset;
  return nullptr;
}

void SensorWatch::begin(KeyValueStore& store) {
  settings = &store;
  char state[4];
  enabled = !store.getByKey("enabled", state, sizeof(state)) || !strcmp(state, "on");
  for (int i = 0; i < MaxRules; ++i) {
    char key[8], spec[sizeof(Rule::spec)];
    ruleKey(i, key);
    rules[i] = {};
    if (!store.getByKey(key, spec, sizeof(spec))) {
      if (i != 0) continue;
      strcpy(spec, default_rule);
    }
    if (spec[0] == '#') rules[i].parse(spec);
  }
}

int SensorWatch::collectChannels(const uint8_t* hash, mesh::GroupChannel out[], int limit) const {
  int count = 0;
  for (const Rule& rule : rules) {
    if (rule.patterns_at) {
      count += appendKeyedChannel(hash, rule.channel.secret, rule.channel.hash[0], out, limit, count);
    }
  }
  return count;
}

int SensorWatch::searchChannels(const uint8_t* hash, mesh::GroupChannel out[], int limit) const {
  return enabled ? collectChannels(hash, out, limit) : 0;
}

bool SensorWatch::matches(uint8_t type, const mesh::GroupChannel& channel,
                          const uint8_t* data, size_t len) const {
  if (!enabled || type != PAYLOAD_TYPE_GRP_TXT || len <= 5 || (data[4] >> 2) != TXT_TYPE_PLAIN) return false;
  // Weather reports are multiline. Even a catch-all request must not start a reply loop.
  const char* body = reinterpret_cast<const char*>(data + 5);
  const size_t visible = strnlen(body, len - 5);
  if (memchr(body, '\r', visible) || memchr(body, '\n', visible)) return false;
  char sender[1], text[MAX_PACKET_PAYLOAD];
  bool has_sender, has_text;
  parseGroupText(data, len, sender, sizeof(sender), text, sizeof(text), &has_text, &has_sender);
  if (!has_sender || !has_text || !*text) return false;
  const char* end = text + strlen(text);
  while (end > text && blank(end[-1])) --end;
  for (const Rule& rule : rules) {
    if (!rule.patterns_at || memcmp(channel.secret, rule.channel.secret, sizeof(channel.secret))) continue;
    const char* part = rule.spec + rule.patterns_at;
    while (part) {
      const char* comma = strchr(part, ',');
      if (matchPattern(part, comma ? comma : part + strlen(part), text, end)) return true;
      part = comma ? comma + 1 : nullptr;
    }
  }
  return false;
}

void SensorWatch::save(int slot, const Rule& rule, char* reply) {
  char key[8];
  ruleKey(slot, key);
  // A stored deletion distinguishes an empty first slot from an upgrade with no settings.
  if (!settings->setByKey(key, rule.patterns_at ? rule.spec : "-")) {
    strcpy(reply, "Err - could not save watch rule");
    return;
  }
  rules[slot] = rule;
  snprintf(reply, CLI_REPLY_MAX, "OK - %s rule %d", rule.patterns_at ? "added" : "deleted", slot + 1);
}

void SensorWatch::add(char* args, char* reply) {
  Rule candidate;
  if (const char* error = candidate.parse(args)) {
    snprintf(reply, CLI_REPLY_MAX, "%s", error);
    return;
  }
  int free_slot = -1;
  for (int i = 0; i < MaxRules; ++i) {
    if (!rules[i].patterns_at) {
      if (free_slot < 0) free_slot = i;
    } else if (!strcmp(rules[i].spec, candidate.spec)) {
      snprintf(reply, CLI_REPLY_MAX, "OK - rule %d already exists", i + 1);
      return;
    }
  }
  if (free_slot < 0) {
    snprintf(reply, CLI_REPLY_MAX, "Err - watch holds %d rules", MaxRules);
    return;
  }
  // Mesh::onRecvPacket tries at most four distinct keys with the same one-byte hash.
  mesh::GroupChannel keys[4];
  if (collectChannels(candidate.channel.hash, keys, 4) == 4) {
    bool known = false;
    for (const auto& key : keys) {
      if (!memcmp(key.secret, candidate.channel.secret, sizeof(key.secret))) known = true;
    }
    if (!known) {
      strcpy(reply, "Err - four other channels already share this hash");
      return;
    }
  }
  save(free_slot, candidate, reply);
}

void SensorWatch::list(int start, char* reply) const {
  CliBuf out(reply, CLI_REPLY_MAX);
  for (int i = start; i < MaxRules; ++i) {
    if (!rules[i].patterns_at) continue;
    if (out.remain() < static_cast<int>(strlen(rules[i].spec) + sizeof("\n8 \nnext=8"))) {
      out.add("\nnext=%d", i + 1);
      break;
    }
    out.add("%s%d %s", out.len() ? "\n" : "", i + 1, rules[i].spec);
  }
  if (!out.len()) out.addStr("No watch rules");
}

bool SensorWatch::handleCommand(const char* command, char* reply) {
  constexpr size_t n = sizeof(command_name) - 1;
  if (strncasecmp(command, command_name, n) || (command[n] && command[n] != ' ')) return false;
  if (!settings) {
    strcpy(reply, "Err - watch settings unavailable");
    return true;
  }
  char scratch[CLI_REPLY_BUFFER_MAX];
  if (!cliPrepare(command + n, scratch, sizeof(scratch), reply)) return true;
  char* args = scratch;
  const char* verb = nextToken(&args);
  if (!verb) {
    int count = 0;
    for (const Rule& rule : rules) if (rule.patterns_at) ++count;
    snprintf(reply, CLI_REPLY_MAX, "sensor.watch %s, %d rule%s", enabled ? "on" : "off",
             count, count == 1 ? "" : "s");
  } else if (!strcasecmp(verb, "add")) {
    add(args, reply);
  } else if (!strcasecmp(verb, "on") || !strcasecmp(verb, "off")) {
    if (!cliNoExtra(args, reply, "Err - use sensor.watch on or off")) return true;
    const bool on = !strcasecmp(verb, "on");
    if (on != enabled && !settings->setByKey("enabled", on ? "on" : "off")) {
      strcpy(reply, "Err - could not save watch state");
    } else {
      enabled = on;
      snprintf(reply, CLI_REPLY_MAX, "OK - sensor.watch %s", on ? "on" : "off");
    }
  } else if (!strcasecmp(verb, "list") || !strcasecmp(verb, "del")) {
    const bool listing = !strcasecmp(verb, "list");
    const char* id = nextToken(&args);
    long number = 1;
    if ((!id && !listing) || (id && (!allDigits(id) || !parseIntRange(id, 1, MaxRules, &number))) ||
        nextToken(&args)) {
      snprintf(reply, CLI_REPLY_MAX, "Err - use sensor.watch %s (IDs 1 to %d)",
               listing ? "list [id]" : "del <id>", MaxRules);
    } else if (listing) {
      list(number - 1, reply);
    } else if (rules[number - 1].patterns_at) {
      save(number - 1, Rule{}, reply);
    } else {
      strcpy(reply, "Err - no such watch rule");
    }
  } else {
    strcpy(reply, "Err - use sensor.watch [add #channel patterns | list [id] | del id | on | off]");
  }
  return true;
}
