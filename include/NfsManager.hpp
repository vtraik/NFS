#pragma once

#include <string>
#include <unordered_map>
#include <list>
#include <memory>
#include <fstream>
#include <sys/signalfd.h>
#include "Utils.hpp"

struct pthread_mut{
pthread_mutex_t queue;
pthread_mutex_t log_file;
};

struct pthread_cond{
  pthread_cond_t start_job; 
  pthread_cond_t queue_not_maxsize;
};

struct Job{
  std::string source_file;
  std::string target_file;
  std::string entry_s;
  std::string entry_t;
  std::string record;
  Job();
};

struct manager_info{
  int log_fd;
  bool exit_nfs;
  std::unordered_map <std::string,int> Jobs_record; // holds entries like those in config   
  std::list<Job> Jobs; // shared job queue, size of bufsize
};


typedef struct Nfs_manager* NfsManager;

struct Nfs_manager {
  int console_sock;
  int port_num;
  int worker_lim;
  size_t buf_size;
  pthread_t* workers;
  std::string configfile;
};

NfsManager NfsManager_init(const std::string&, const std::string&, int, int, int=5);
void NfsManager_destroy(NfsManager);
void config_files(NfsManager);
void list_command(NfsManager, const Info&, const Info&, int);
bool parse_manager(char**,int);
void* worker(void*);