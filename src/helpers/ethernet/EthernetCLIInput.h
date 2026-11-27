#pragma once

#include <stddef.h>
#include <string.h>

// Line and login state belongs to one TCP connection.
class EthernetCLIInput {
  size_t used = 0;
  bool authenticated = false;
  bool discard = false;

public:
  enum Result { Pending, LoggedIn, Denied, InvalidLine, Command };

  static bool passwordConfigured(const char* password) {
    return password[0] && strcmp(password, "password") != 0;
  }

  Result consume(char c, char* line, size_t capacity, const char* password) {
    if (!passwordConfigured(password)) return Denied;
    if (c != '\r' && c != '\n') {
      if (c == 0 || used + 1 >= capacity) discard = true;
      if (!discard) line[used++] = c;
      return Pending;
    }
    if (discard) {
      discard = false;
      used = 0;
      memset(line, 0, capacity);
      return InvalidLine;
    }
    if (used == 0) return Pending;

    line[used] = 0;
    used = 0;
    if (authenticated) return Command;
    authenticated = strcmp(line, password) == 0;
    memset(line, 0, capacity);
    return authenticated ? LoggedIn : Denied;
  }
};
