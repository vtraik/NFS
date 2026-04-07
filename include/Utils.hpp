#pragma once

#include <string>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h> 
#include <cstdint>

struct Info{
  std::string ip;
  std::string dir;
  std::string entry; // the string
  int port;
};

int read_message(int, std::string&);
int write_message(int, std::string&, size_t);
int write_fd(int, std::string&, size_t);
int read_fd(int, std::string&, size_t, size_t* = nullptr);
std::string get_time();
std::string extract_path(char*);
std::string extract_file(const std::string&);
std::string extract_dir(const std::string&);
bool is_valid_entry(const std::string&);
bool is_dir(const std::string&);
bool is_number(const char*);
void perror_exit(const char*);
void get_info(Info&, const std::string&);
void create_server(int*,const int);
bool connect_to_host(int*, const std::string&, int, std::string&);
