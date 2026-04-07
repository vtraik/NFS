#pragma once

#include <string>
#include <fstream>
#include "Utils.hpp"

typedef struct Nfs_console* NfsConsole;

struct Nfs_console{
  int manager_sock;
  std::ofstream log_stream;
};

NfsConsole NfsConsole_init(const std::string&, const std::string&, int);
void NfsConsole_destroy(NfsConsole);
bool parse_console(char**);