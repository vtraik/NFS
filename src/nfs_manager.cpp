#include <iostream>
#include <algorithm>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "NfsManager.hpp"

using namespace std;

pthread_mut MUTEX;
pthread_cond COND_VAR;
manager_info SHARED_DATA;

void add(NfsManager manager,const string& source_entry, const string target_entry){
  string rec = source_entry + " " + target_entry;
  pthread_mutex_lock(&MUTEX.queue);
  auto end_it = SHARED_DATA.Jobs_record.end();
  if(SHARED_DATA.Jobs_record.find(rec) != end_it){
    pthread_mutex_unlock(&MUTEX.queue);
    string mes = get_time() + " Already in queue: " + source_entry + "\n\n";
    cout << mes;
    write_message(manager->console_sock,mes,mes.size());
    return;
  }
  pthread_mutex_unlock(&MUTEX.queue);

  // get folder from source host
  int client_sock;
  string err;
  Info info_s,info_t;
  get_info(info_s,source_entry);
  get_info(info_t,target_entry);

  if(!connect_to_host(&client_sock,info_s.ip,info_s.port,err)){
    cout << err << endl;
    return;
  }
  list_command(manager, info_s, info_t, client_sock);
  // signal to console end of listing
  string mes = ".";
  write_message(manager->console_sock,mes,1);
  close(client_sock);
}

void cancel(NfsManager manager, const string& source_entry){
  int found = 0;
  // erase entrties with: /dir@host:port = source_entry
  pthread_mutex_lock(&MUTEX.queue);
  for(auto it = SHARED_DATA.Jobs.begin();  it != SHARED_DATA.Jobs.end();){ // might fix to be O(1)
    if(it->entry_s == source_entry){
      found++;
      SHARED_DATA.Jobs_record[it->record]--; // dec num of occurrences of record
      if(SHARED_DATA.Jobs_record[it->record] == 0)
        SHARED_DATA.Jobs_record.erase(it->record);
      it = SHARED_DATA.Jobs.erase(it);      
    }else{
      it++;
    }
  }
  if(found > 1)
    pthread_cond_broadcast(&COND_VAR.queue_not_maxsize);
  else if(found == 1)
    pthread_cond_signal(&COND_VAR.queue_not_maxsize);
  pthread_mutex_unlock(&MUTEX.queue);
   
  string mes;
  if(found){
    mes = get_time() + " Synchronization stopped for " + source_entry + "\n\n";
    pthread_mutex_lock(&MUTEX.log_file);
    write_fd(SHARED_DATA.log_fd,mes,mes.size());
    pthread_mutex_lock(&MUTEX.log_file);
  }else{
    mes = get_time() + " Directory not being synchronized:\n" + source_entry + "\n\n";
  }

  // print in terminal and send console
  cout << mes;
  write_message(manager->console_sock,mes,mes.size());
}

void shutdown(NfsManager manager){
  SHARED_DATA.exit_nfs = true;
  string timestamp = get_time();
  string mes1 = timestamp + " Shutting down manager...\n" + 
  timestamp + " Waiting for all active workers to finish.\n";
  cout << mes1;
  write_message(manager->console_sock,mes1,mes1.size());

  
  timestamp = get_time();
  mes1 = timestamp + " Processing remaining queued tasks.\n";
  cout << mes1;
  write_message(manager->console_sock,mes1,mes1.size());
  
  // wake up sleeping threads
  pthread_cond_broadcast(&COND_VAR.start_job);

  // join threads
  for(int i = 0; i < manager->worker_lim; i++){
    if(pthread_join(manager->workers[i],NULL) != 0) perror_exit("shutdown join");
  }

  
  timestamp = get_time();
  mes1 = timestamp + " Manager shutdown complete.\n";
  cout << mes1;
  // signal end of writing and close socket
  write_message(manager->console_sock,mes1,mes1.size());
  shutdown(manager->console_sock,SHUT_WR);
  close(manager->console_sock);

  if(pthread_mutex_destroy(&MUTEX.log_file) != 0) perror_exit("mutex destroy");
  if(pthread_mutex_destroy(&MUTEX.queue) != 0) perror_exit("mutex destroy");
  if(pthread_cond_destroy(&COND_VAR.queue_not_maxsize) != 0) perror_exit("cond destroy");
  if(pthread_cond_destroy(&COND_VAR.start_job) != 0) perror_exit("cond destroy");

}

// handles console events written to fss_in
void* handle_console(void* args){
  NfsManager* managerp = (NfsManager*) args;
  string mes,command;
  while(!SHARED_DATA.exit_nfs){
    // read command
    read_message((*managerp)->console_sock,mes);
    istringstream str(mes);
    str >> command;
    if(command == "add"){
      string source,target;
      str >> source >> target;
      add(*managerp,source,target);
    }else if(command == "cancel"){
      string source;
      str >> source;
      cancel(*managerp,source);
    }else if(command == "shutdown"){
      shutdown(*managerp);
    }
  }
  pthread_exit(NULL);
}

// choice: false -> pull | true -> push
void log_rec_in_manager(const string& source, const string& target,
pthread_t id, bool success, bool choice, const string& details, bool should_lock){
  string oper, res;
  if(choice) oper = "[PUSH] ";
  else oper = "[PULL] ";
  if(success) res = "[SUCCESS] ";
  else res = "[ERROR] ";
  string rec = get_time() + " [" + source + "]" +
  " [" + target + "]\n" + "[" + to_string(id) + "] " +
  oper + res + details + "\n\n";
 
  // log in manager 
  if(should_lock) pthread_mutex_lock(&MUTEX.log_file);
  cout << rec << endl;
  write_fd(SHARED_DATA.log_fd,rec,rec.size());
  if(should_lock) pthread_mutex_unlock(&MUTEX.log_file);

}

void terminate_connection(const string& source, const string& target,
pthread_t id, bool success, bool choice, const string& details, int s_sock, int t_sock){
  shutdown(t_sock,SHUT_WR);
  close(s_sock);
  close(t_sock);
  log_rec_in_manager(source, target, id, success, choice, details,true);
}

// one thread will be for console input and doing the requested commands (add,cancel,shutdown)
void* worker(void*){
  pthread_t pid = pthread_self();
  while(true){
    // wait for Jobs to be non-empty
    pthread_mutex_lock(&MUTEX.queue);
    while(SHARED_DATA.Jobs.size() <= 0 && !SHARED_DATA.exit_nfs){
      pthread_cond_wait(&COND_VAR.start_job,&MUTEX.queue);
    }
    // job done, can exit
    if(SHARED_DATA.Jobs.size() == 0 && SHARED_DATA.exit_nfs){
      pthread_mutex_unlock(&MUTEX.queue);
      pthread_exit(NULL);
    }
    Job job = SHARED_DATA.Jobs.front();
    SHARED_DATA.Jobs.pop_front();
    SHARED_DATA.Jobs_record[job.record]--; // dec num of occurrences
    if(SHARED_DATA.Jobs_record[job.record] == 0)
      SHARED_DATA.Jobs_record.erase(job.record);
    pthread_cond_signal(&COND_VAR.queue_not_maxsize); // removed a job, might want to add
    pthread_mutex_unlock(&MUTEX.queue);
    Info info_s,info_t;
    get_info(info_s,job.entry_s); // get con info from entry_s
    get_info(info_t,job.entry_t); // get con info from entry_t 
    string err_pull, err_push; //error buffers
    string buf;
    string file = extract_file(job.source_file); // file from filepath
    buf.resize(1024); // chunk size
    size_t b_read;
    int source_sock, targ_sock;

    // connect to hosts: source, target    
    if(!connect_to_host(&source_sock,info_s.ip,info_s.port,buf)){
      err_pull += "[File: " + file + " - " + buf + "]";
      log_rec_in_manager(job.source_file,job.target_file,pid,
      false,false,err_pull,true);
      continue; 
    }
    if(!connect_to_host(&targ_sock,info_t.ip,info_t.port,buf)){
      err_push += "[File: " + file + " - " + buf + "]";
      log_rec_in_manager(job.source_file,job.target_file,pid,
        false,true,err_push,true);
      close(source_sock);
      continue; 
    }
    
    // send pull to source
    string mes = "PULL " + job.source_file;
    if(!write_message(source_sock,mes,mes.size())){
      err_pull += "[File: " + file + " - " + mes + "]";
      terminate_connection(job.source_file,job.target_file,pid,
        false,false,err_pull,source_sock,targ_sock);
      continue;    
    }
    // send -1 (signal trunc)
    mes = "PUSH " + job.target_file + " -1";
    if(!write_message(targ_sock,mes,mes.size())){ // first_part
      err_push += "[File: " + file + " - " + mes + "]";
      terminate_connection(job.source_file,job.target_file,pid,
        false,true,err_push,source_sock,targ_sock);
      continue;
    }
    
    // copy
    size_t total=0; // total bytes read

    // read (filesize + space)
    if(!read_message(source_sock,mes)){ // if errors occur they'll fit in 1024 bytes (just open,fstat)
      err_pull += "[File: " + file + " - " + mes + "]";
      terminate_connection(job.source_file,job.target_file,pid,
        false,false,err_pull,source_sock,targ_sock);
      continue;
    } 
    string filesize_s = mes.substr(0,mes.size()-1); // remove space 
    if(filesize_s == "-1"){ // error occured in client
      read_message(source_sock,mes); // read error mes
      err_pull += "[File: " + file + " - " + mes + "]";
      terminate_connection(job.source_file,job.target_file,pid,
        false,false,err_pull,source_sock,targ_sock);
      continue;
    }
    size_t filesize = stoull(filesize_s);
    
    // copy file
    bool should_cont = false;
    int end = -1;
    while(end!=2){ // end != 2 is needed if file is <= 1024 bytes
      if((end = read_fd(source_sock,buf,buf.size(),&b_read)) == 0){
        should_cont = true;
        err_pull += "[File: " + file + " - " + buf + "]";
        terminate_connection(job.source_file,job.target_file,pid,
          false,false,err_pull,source_sock,targ_sock);
        break;
      }

      total+=b_read;
      mes = "PUSH " + job.target_file + " " + to_string(b_read);
      
      if(!write_message(targ_sock,mes,mes.size())){ // first_part
        should_cont = true;
        err_push += "[File: " + file + " - " + mes + "]";
        terminate_connection(job.source_file,job.target_file,pid,
          false,true,err_push,source_sock,targ_sock);
        break;
      }
      if(!write_fd(targ_sock,buf,b_read)){ // write data
        should_cont = true;
        err_push += "[File: " + file + " - " + buf + "]";
        terminate_connection(job.source_file,job.target_file,pid,
          false,true,err_push,source_sock,targ_sock);
        break;
      }
    }

    if(b_read != 0 && !should_cont){ // have not send 0 yet, send it 
      mes = "PUSH " + job.target_file + " 0";
      if(!write_message(targ_sock,mes,mes.size())){
        err_push += "[File: " + file + " - " + mes + "]";
        terminate_connection(job.source_file,job.target_file,pid,
          false,true,err_push,source_sock,targ_sock);
        continue;
      }
    }else if(should_cont){
      continue;
    }

    // partial read
    if(total != filesize){
      err_pull += "[File: " + file + " - Partial read]";
      terminate_connection(job.source_file,job.target_file,pid,
        false,false,err_pull,source_sock,targ_sock);
      continue;
    }

    // close connection
    shutdown(targ_sock,SHUT_WR);
    close(source_sock);
    close(targ_sock);

    // log successful copy in manager (atomically)
    pthread_mutex_lock(&MUTEX.log_file);
    buf = "[" + filesize_s + " bytes pulled]";
    log_rec_in_manager(job.entry_s,job.entry_t,pid,
      true,false,buf,false);
    buf = "[" + filesize_s + " bytes pushed]";
    log_rec_in_manager(job.entry_s,job.entry_t,pid,
      true,true,buf,false);
    pthread_mutex_unlock(&MUTEX.log_file);
    
  }
}

int main(int argc, char** argv){
  NfsManager manager;
  if(argc == 9 && parse_manager(argv,9)){
    string log(argv[2]),conf(argv[4]);
    manager = NfsManager_init(log,conf,atoi(argv[6]),atoi(argv[8])); // port,buf_size 
  }else if(argc == 11 && parse_manager(argv,11)){
    string log(argv[2]),conf(argv[4]);
    manager = NfsManager_init(log,conf,atoi(argv[8]),atoi(argv[10]),atoi(argv[6])); // port,buf_size,wl
  }else{
    printf("Usage:\n");
    printf("%s -l <logfile> -c <configfile> -n <worker limit> (default=5) -p <port number> -b <bufsize>\n",argv[0]);
    exit(EXIT_FAILURE);
  }

  // read from config file and enqueue jobs
  config_files(manager);
  
 // init console thread
 pthread_t cons_pid;
 pthread_create(&cons_pid,NULL,handle_console,&manager);

 // wait for console to terminate
 if(pthread_join(cons_pid,NULL) != 0) perror_exit("console join");
 
 // destroy manager
  NfsManager_destroy(manager);
  exit(EXIT_SUCCESS);
}

