#include <iostream>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include "NfsManager.hpp"

extern pthread_mut MUTEX;
extern pthread_cond COND_VAR;
extern manager_info SHARED_DATA;

using namespace std;

NfsManager NfsManager_init(const string& log, const string& config, int port, int buf_size, int lim){

  NfsManager manager = new Nfs_manager;
  if(port >= 1024 && port <= 65535)
    manager->port_num = port;
  else
    manager->port_num = 49152; // some safe port
  
  // get a socket for communication with console  
  int console_listen_sock;
  create_server(&console_listen_sock,manager->port_num);
  manager->console_sock = accept(console_listen_sock,NULL,NULL); // wait for console
  close(console_listen_sock); // close listening sock

  // init configfile, port, workerlim
  manager->configfile = config;
  if((SHARED_DATA.log_fd = open(log.c_str(),O_WRONLY | O_CREAT | O_TRUNC,0600)) == -1){
    perror_exit("logfile");
  } 
  if(lim > 5)
    manager->worker_lim = lim;
  else
    manager->worker_lim = 5;
  
  SHARED_DATA.exit_nfs = false;
  if(buf_size <= 0){
    cerr << "buff size should be > 0" << endl;
    exit(EXIT_FAILURE);
  }
  manager->buf_size = (size_t)buf_size;
  // init_thread pool
  if(pthread_mutex_init(&MUTEX.log_file,NULL) != 0) perror_exit("mutex_init");
  if(pthread_mutex_init(&MUTEX.queue,NULL) != 0) perror_exit("mutex_init");

  if(pthread_cond_init(&COND_VAR.queue_not_maxsize,NULL) != 0) perror_exit("cond_init");
  if(pthread_cond_init(&COND_VAR.start_job,NULL) != 0) perror_exit("cond_init");

  manager->workers = new pthread_t[manager->worker_lim]();
  for(int i=0; i < manager->worker_lim; i++){
    pthread_create(&manager->workers[i],NULL,worker,NULL);
  }
 
  return manager; 
}

// implements list command from /source_dir@host1:port -> /t_dir@host2:port
// and prints/logs messages. Record is a record in config file (and add command input)
void list_command(NfsManager manager, const Info& info_s, const Info& info_t, int cl_sock){  
  // write to client LIST command
  string mes = "LIST " + info_s.dir.substr(1); // without /
  write_message(cl_sock,mes,mes.size());
  string file;
  Job job;
  job.record = info_s.entry + " " + info_t.entry; 
  job.entry_s = info_s.entry; // first part: source
  job.entry_t = info_t.entry; // second part: target
  int abort = -1;
  while(abort != 0){ // emergency exit
    abort = read_message(cl_sock,file);

    if(file == ".") break; // list ended
    file.pop_back(); // remove \n
    job.source_file = info_s.dir + "/" + file;
    job.target_file = info_t.dir + "/" + file;

    // needs locks (push only if < bufsize)
    pthread_mutex_lock(&MUTEX.queue);
    while(SHARED_DATA.Jobs.size() >= manager->buf_size){
      pthread_cond_wait(&COND_VAR.queue_not_maxsize,&MUTEX.queue);
    }
    SHARED_DATA.Jobs.push_back(job);
    SHARED_DATA.Jobs_record[job.record]++; // incr count of record
    pthread_cond_signal(&COND_VAR.start_job); // added a job, wake up a susp worker
    pthread_mutex_unlock(&MUTEX.queue);

    // print, send to console and log
    string timestamp = get_time();
    string mes1 = timestamp + " Added file: " + job.entry_s + " ->\n" + job.entry_t + "\n\n";
    
    // log in manager
    write_fd(SHARED_DATA.log_fd,mes1,mes1.size()); 

    cout << mes1; // print in terminal
    write_message(manager->console_sock,mes1,mes1.size());
  }

}

void config_files(NfsManager manager){

  ifstream conf(manager->configfile);
  string mes,entry_s,entry_t;
  if(!conf.is_open()){
    cout << "Error in reading config file" << endl;
    exit(EXIT_FAILURE);
  }
  string err;
  while(conf >> entry_s >> entry_t){
    // connect to source host
    Info info_s,info_t;
    get_info(info_s,entry_s);
    get_info(info_t,entry_t);
    int sock;
    if(!connect_to_host(&sock,info_s.ip,info_s.port,err)){
      continue;
    }
    list_command(manager,info_s,info_t,sock);
    close(sock); // close source host connection
  }
  // signal to console end of listing
  mes = ".";
  write_message(manager->console_sock,mes,1);
  conf.close();
}


void NfsManager_destroy(NfsManager manager){
  close(SHARED_DATA.log_fd);
  delete[] manager->workers;
  delete manager;
}


Job::Job()
:source_file(""), target_file(""), entry_s(""), entry_t(""),
record(""){}


bool parse_manager(char** argv, int size){
  for(int i = 1; i < size; i++){
    if(i%2 == 0 && i <= 4){ // even pos -> should be valid dir
      string path = extract_path(argv[i]);
      if(!is_dir(path)) return false;
    }else{                 // odd pos -> should be valid option
      switch(i){
        case 1:
          if(!strcmp(argv[1]," -l")) return false; 
          break;
        case 3:
          if(!strcmp(argv[3]," -c")) return false; 
          break;
        case 5:
          if(!strcmp(argv[5]," -n")) return false; 
          break;
        case 7:
          if(!strcmp(argv[7]," -p")) return false; 
          break;
        case 9:
          if(!strcmp(argv[9]," -b")) return false; 
          break;
        default: // for worker lim, port num
          if(!is_number(argv[i])) return false;
      }
    }
  }
  return true;
}