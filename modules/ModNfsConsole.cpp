#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include "NfsConsole.hpp"

using namespace std;

NfsConsole NfsConsole_init(const string& log, const string& ip, int port){
  
  NfsConsole console = new Nfs_console; 
  string err;
  if(!connect_to_host(&console->manager_sock,ip,port,err)){
    cerr << "connecting to host failed: " << err << endl;
    exit(EXIT_FAILURE);
  }

  ofstream log_stream(log); // create and open log file
  console->log_stream = move(log_stream); // create and open log file

  return console; 
}

void NfsConsole_destroy(NfsConsole console){
  console->log_stream.close();
  close(console->manager_sock);
  delete console;
}

bool parse_console(char** argv){
  struct in_addr dummy;
  if(!strcmp(argv[1]," -l")) return false; 
  if(!strcmp(argv[3]," -h")) return false; 
  if(inet_pton(AF_INET,argv[4],&dummy) != 1) return false;
  if(!strcmp(argv[5]," -p")) return false; 
  if(!is_number(argv[6])) return false;
  string path = extract_path(argv[2]);
  if(!is_dir(path)) return false;
  return true;
}
