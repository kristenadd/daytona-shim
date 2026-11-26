#pragma once
#include "edition.hpp"

namespace shim {
// The cabinet shell: a TCP server on 127.0.0.1:18888 that answers the game's
// DYT link (greeting, security challenge, credits). Runs on its own thread.
void start_shell(Edition edition);
// A coin: SHELL_MSG_SETCREDIT with the game's current count plus one.
void shell_insert_coin();
}  // namespace shim
