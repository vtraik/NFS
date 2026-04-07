#include <iostream>
#include <sstream>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include "NfsConsole.hpp"

using namespace std;

bool handle_input(NfsConsole console, string& line){
  string command,instr;
  istringstream li(line);
  li >> command;
  if(command == "add"){
    string source,target;
    li >> source >> target;
    if(!is_valid_entry(source) || !is_valid_entry(target)) return false; // skip non valid entries
    instr = get_time() + " Command add " + source + " -> " + target + "\n\n";
  }else if(command == "cancel"){
    string dir;
    li >> dir; 
    if(!is_valid_entry(dir)) return false; 
    instr = get_time() + " Command cancel " + dir + "\n\n";
  }else if(command == "shutdown"){
    instr = get_time() + " Command shutdown\n\n";
  }else{
    return false;
  }
  // log in console file, send message to manager
  console->log_stream << instr;
  write_message(console->manager_sock,line,line.size());
  return true;  
}

bool handle_response(NfsConsole console){
  string mes;
  read_message(console->manager_sock,mes);
  if(mes == ".") return true; // empty folder
  cout << mes; // print message to terminal
  console->log_stream << mes; // log

  // search for add or shutdown response
  size_t pos = mes.find("Added");
  size_t pos2 = mes.find("Shutting");
  if(pos != string::npos){
    int abort = -1;
    while(abort != 0){
      abort = read_message(console->manager_sock,mes);
      if(mes == ".") break; // read all added files
      cout << mes;
      console->log_stream << mes;
    }
  }
  if(pos2 != string::npos){
    for(int i=0; i < 2; i++){ // read the rest shutdown messages
      read_message(console->manager_sock,mes);
      cout << mes; 
      console->log_stream << mes; 
    }
    return false;
  }
  return true;
}


int main(int argc, char** argv){
  NfsConsole console;
  if(argc == 7 && parse_console(argv)){
    console = NfsConsole_init(argv[2],argv[4],atoi(argv[6]));
  }else{
    printf("Usage:\n");
    printf("%s -l <logfile> ",argv[0]);
    exit(EXIT_FAILURE);
  }

  handle_response(console); // config mes response
  string command;
  while(true){
    cout << "> " << flush;
    getline(cin,command);
    if(!handle_input(console,command)) continue;
    if(!handle_response(console)) break;
  }

  NfsConsole_destroy(console);
  return 0;
}
