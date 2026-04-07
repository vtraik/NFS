#include <iostream>
#include <cstring>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include "Utils.hpp"

using namespace std;

// message: (length: int) + message.data
int read_message(int sock, string& buffer){
  size_t total = 0;
  ssize_t nread = 0;
  uint32_t mes_length,n_length;
  // read prefix int: message length
  while(total < sizeof(n_length)){
    nread = read(sock,((char*)& n_length) + total,sizeof(n_length) - total);
    if(nread == -1){
      if(errno == EINTR) continue; // interrupted by signal
      return false;
    }else if(nread == 0){ // EOF
      return 0;
    }
    total += nread;
  }
  mes_length = ntohl(n_length); // conv to host order int read
  // read message
  total = 0;
  nread = 0;
  buffer.resize(mes_length); // resize to fit message
  while(total < mes_length){
    nread = read(sock,&buffer[0] + total,mes_length - total);
    if(nread == -1 && errno == EINTR){ // interrupted by signal and didn't read anything
      continue;
    }else if(nread == -1){  // other fail
      buffer.resize(256);
      char* ptr;
      ptr = strerror_r(errno,&buffer[0],256);
      buffer = ptr;
      buffer.resize(strlen(buffer.c_str()));
      return 0;
    }else if(nread == 0){ // EOF
      break;
    }
    total += nread;
  }
  return 1;
}


// message: (length: int) + message.data
int write_message(int sock, string& buffer, size_t size){
  size_t t_bwritten=0;
  ssize_t bwritten=0;
  uint32_t n_length = htonl(static_cast<uint32_t>(size)); // convert to net order int send
  
  if(size > UINT32_MAX) return false; // too big 
  
  // write prefix int: message length
  while(t_bwritten < sizeof(uint32_t)){
    bwritten = write(sock,((char*)& n_length) + t_bwritten,sizeof(uint32_t) - t_bwritten);
    if(bwritten == -1){
      if(errno == EINTR) continue; // interrupted by signal
      return 0;
    }
    t_bwritten += bwritten;
  }
  
  // write message
  t_bwritten = 0;
  bwritten = 0;
  const char* base = buffer.c_str();
  while(t_bwritten < size){
    bwritten = write(sock,base + t_bwritten,size - t_bwritten);
    if(bwritten == -1 && errno == EINTR){ // interrupted by signal and didn't write anything
      continue;
    }else if(bwritten == -1){  // other fail
      buffer.resize(256);
      char* ptr;
      ptr = strerror_r(errno,&buffer[0],256);
      buffer = ptr;
      buffer.resize(strlen(buffer.c_str()));
      return 0;
    }
    t_bwritten += bwritten;
  }
  return 1;
}

// read from fd size bytes and store it in buffer, return total read
// buffer should be big enough (>= size), else UB
int read_fd(int fd, string& buffer, size_t size, size_t* b_read){
  size_t total = 0;
  ssize_t nread = 0;
  int ret_val = 1; 
  while(total < size){
    nread = read(fd,&buffer[0] + total,size - total);
    if(nread == -1 && errno == EINTR){ // interrupted by signal and didn't read anything
      continue;
    }else if(nread == -1){  // other fail
      ret_val = 0;
      buffer.resize(256);
      char* ptr;
      ptr = strerror_r(errno,&buffer[0],256);
      buffer = ptr;
      buffer.resize(strlen(buffer.c_str()));
      break;
    }else if(nread == 0){ // EOF
      ret_val = 2;
      break;
    }
    total += nread;
  }
  // return total read if needed
  if(b_read != nullptr) 
    *b_read = total;
  return ret_val;
}

// write to fd the buffer
int write_fd(int fd, string& buffer, size_t size){
  size_t t_bwritten=0;
  ssize_t bwritten=0;
  const char* base = buffer.c_str();
  while(t_bwritten < size){
    bwritten = write(fd,base + t_bwritten,size - t_bwritten);
    if(bwritten == -1 && errno == EINTR){ // interrupted by signal and didn't write anything
      continue;
    }else if(bwritten == -1){  // other fail
      buffer.resize(256);
      char* ptr;
      ptr = strerror_r(errno,&buffer[0],256);
      buffer = ptr;
      buffer.resize(strlen(buffer.c_str()));
      return 0;
    }
    t_bwritten += bwritten;
  }
  return 1;
}

string get_time(){
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char timestamp[30];
  strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S]", t);
  string time(timestamp);
  return time;
}

// extracts path from filepath
string extract_path(char* filepath){
  string dir_path(filepath);
  size_t pos = dir_path.rfind('/');
  if(pos == string::npos) return "";
  return dir_path.substr(0,pos);
}

// extract file from path
string extract_file(const string& path){
  size_t pos = path.rfind('/');
  if(pos == string::npos) return "";
  return path.substr(pos+1);
}

// extract dir from filepath
string extract_dir(const string& filepath){
  size_t pos = filepath.rfind('/');
  if(pos == string::npos) return "";
  return filepath.substr(0,pos);
}

// checks if entry is of the form /dir@host_ip:port
bool is_valid_entry(const string& entry){
  if(entry[0] != '/') return false; // should start with /
  size_t pos = entry.rfind(':');
  if(pos == string::npos) return false;
  string str = entry.substr(pos+1); // check port
  if(!is_number(str.c_str())) return false;
  size_t pos2 = entry.rfind('@',pos); // check ip
  if(pos2 == string::npos) return false;
  str = entry.substr(pos2+1,pos-pos2-1); // ip part
  struct sockaddr_in dummy;
  if(inet_pton(AF_INET, str.c_str(), &dummy.sin_addr) <= 0) 
    return false;
  return true;
}

bool is_dir(const string& path){
  struct stat st;
  if(path == "") return false;
  if(stat(path.c_str(),&st) == -1)
    return false;
  return S_ISDIR(st.st_mode);  
}

bool is_number(const char* str){
  while(*str != '\0'){
    if(*str < '0' || *str > '9') return false;
    str++;
  }
  return true;
}

void perror_exit(const char* mes){
  perror(mes);
  exit(EXIT_FAILURE);
}

// gets info from string like: /source1@123.10.10.20:8000 and returns a Info struct
void get_info(Info& info, const string& str){
  size_t pos = str.find('@');
  size_t pos2 = str.find(':');
  info.dir = str.substr(0,pos);
  info.ip = str.substr(pos+1,pos2-pos-1);
  info.port = stoi(str.substr(pos2+1));
  info.entry = str;
}

// creates a listening socket and binds on port
void create_server(int* sock, const int port_num){
  struct sockaddr_in server;
  if((*sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    perror_exit("socket");
 
  int option = 1;
  if(setsockopt(*sock, SOL_SOCKET, SO_REUSEADDR,&option,sizeof(option)) < 0){
    perror("setsockopt");
    close(*sock);
  }
  
  server.sin_family = AF_INET ;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(port_num);
  if(bind(*sock, (struct sockaddr *)&server, sizeof(server)) < 0)
    perror_exit("bind");
  if(listen(*sock,32) < 0)
    perror_exit("listen");
}

// connects to host: ip,port and returns a socket for communication
bool connect_to_host(int* sock, const string& ip, int port, string& err_buf){
  struct sockaddr_in server ;
  char* ptr;
  if((*sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
    err_buf.resize(256);
    ptr = strerror_r(errno,&err_buf[0],256);
    err_buf = ptr;
    err_buf.resize(strlen(err_buf.c_str()));
    return false;
  }
  server.sin_family = AF_INET;
  if(inet_pton(AF_INET, ip.c_str(), &server.sin_addr) <= 0){
    err_buf.resize(256);
    ptr = strerror_r(errno,&err_buf[0],256);
    err_buf = ptr;
    err_buf.resize(strlen(err_buf.c_str()));
    return false;
  }
  server.sin_port = htons(port);
  if(connect(*sock,(struct sockaddr*)& server,sizeof(server)) < 0){
    err_buf.resize(256);
    ptr = strerror_r(errno,&err_buf[0],256);
    err_buf = ptr;
    err_buf.resize(strlen(err_buf.c_str()));
    return false;
  }
  return true;
}