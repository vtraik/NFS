#include <iostream>
#include <sstream>
#include <unordered_map>
#include <memory>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <signal.h>
#include "Utils.hpp"
using namespace std;

struct DirId{
  dev_t dev;
  ino_t ino;
  bool operator==(const DirId& dir2) const{
    return dev == dir2.dev && ino == dir2.ino;
  }

};

// boost combine hash
struct DirId_Hash{
size_t operator()(const DirId& d) const {
  size_t seed = 0;
  seed ^= hash<dev_t>{}(d.dev) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  seed ^= hash<ino_t>{}(d.ino) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  return seed;
}
};

// sync method: either reading on dir (list/pull) or writing (push)
// we hypothesize not to concurrent writing to the same file (reading is ok)
struct dir_lock{
  pthread_mutex_t lock;
  pthread_cond_t can_read;
  pthread_cond_t can_write;
  int num_of_readers; // either file or same dir open on other thread
  int num_of_writers; // of dir
  dir_lock();
  ~dir_lock();
};

dir_lock::dir_lock()
: num_of_readers(0), num_of_writers(0){
  pthread_mutex_init(&lock,NULL);
  pthread_cond_init(&can_read,NULL);
  pthread_cond_init(&can_write,NULL);
}

dir_lock::~dir_lock(){
  pthread_mutex_destroy(&lock);
  pthread_cond_destroy(&can_read);
  pthread_cond_destroy(&can_write);
}

unordered_map<DirId,shared_ptr<dir_lock>,DirId_Hash> dirs; // stores locks for each dir
pthread_mutex_t MAP_LOCK;
pthread_mutex_t cleanup_mut;
pthread_cond_t should_cleanup;
pthread_cond_t can_exit;
pthread_mutex_t worker_mut;

shared_ptr<dir_lock> get_dir_lock(DirId& id){
  pthread_mutex_lock(&MAP_LOCK);
  auto it = dirs.find(id);
  if(it == dirs.end()){
    dirs[id] = make_shared<dir_lock>();
    if(dirs.size() >= 1000) // 1k records reached, should do come cleanup 
      pthread_cond_signal(&should_cleanup);
    pthread_mutex_unlock(&MAP_LOCK);
    return dirs[id];
  }
  pthread_mutex_unlock(&MAP_LOCK);
  return it->second;
}

volatile sig_atomic_t exit_client = 0;
int cur_workers = 0;
int list_socket = -1;

void* shutdown_client(void* args){
  sigset_t sg = *(sigset_t*) args;
  delete static_cast<sigset_t*>(args);
  int sig;
  if(sigwait(&sg, &sig) == 0){
    if(sig == SIGINT){
      exit_client = 1;
      shutdown(list_socket, SHUT_RDWR);
      close(list_socket); // wake up main thread
    }
  }
  pthread_exit(NULL); // SIGINT detected, exit
}

void init_handler(){
  // block SIGINT on all threads
  sigset_t* sg = new sigset_t;
  sigemptyset(sg);
  sigaddset(sg, SIGINT);
  pthread_sigmask(SIG_BLOCK, sg, NULL);
  pthread_t pid;
  // create signal handler thread
  if(pthread_create(&pid,NULL,shutdown_client,sg) != 0) perror_exit("pthread_create");
  if(pthread_detach(pid) != 0) perror_exit("pthread_detach");
  
}

// cleans up map entries to prevent heap exhaustion
void* cleanup(void*){
  pthread_mutex_lock(&cleanup_mut);
  while(!exit_client){
    pthread_cond_wait(&should_cleanup,&cleanup_mut);
      
    pthread_mutex_lock(&MAP_LOCK);
    if(dirs.size() != 0){
      for(auto it = dirs.begin(); it != dirs.end();){
        if(it->second.use_count() == 1){
          it = dirs.erase(it);
        }else{
          it++;
        }
      }
    }

    pthread_mutex_unlock(&MAP_LOCK);
  }
  pthread_mutex_unlock(&cleanup_mut);
  pthread_exit(NULL);
}

void start_reading(shared_ptr<dir_lock>& dlock_ptr){
  pthread_mutex_lock(&dlock_ptr->lock);
  while(dlock_ptr->num_of_writers > 0){
    pthread_cond_wait(&dlock_ptr->can_read,&dlock_ptr->lock);
  }
  dlock_ptr->num_of_readers++;
  pthread_mutex_unlock(&dlock_ptr->lock);
}

void end_reading(shared_ptr<dir_lock>& dlock_ptr){
  pthread_mutex_lock(&dlock_ptr->lock);
  if(--dlock_ptr->num_of_readers == 0)
    pthread_cond_broadcast(&dlock_ptr->can_write);
  pthread_mutex_unlock(&dlock_ptr->lock);
}

void start_writing(shared_ptr<dir_lock>& dlock_ptr){
  pthread_mutex_lock(&dlock_ptr->lock);
  while(dlock_ptr->num_of_readers > 0){
    pthread_cond_wait(&dlock_ptr->can_write,&dlock_ptr->lock);
  }
  dlock_ptr->num_of_writers++;
  pthread_mutex_unlock(&dlock_ptr->lock);
}

void end_writing(shared_ptr<dir_lock>& dlock_ptr){
  pthread_mutex_lock(&dlock_ptr->lock);
  if(--dlock_ptr->num_of_writers == 0)
    pthread_cond_broadcast(&dlock_ptr->can_read);
  pthread_cond_signal(&dlock_ptr->can_write);
  pthread_mutex_unlock(&dlock_ptr->lock);
}

void list(const string& dir, int sock){ // eg. LIST dir1/dir2 <=> LIST ./dir1/dir2
  DIR* dir_p;
  struct dirent* dir_entp;
  struct stat st;
  DirId id;
  shared_ptr<dir_lock> dlock_ptr;
  
  if(stat(dir.c_str(),&st) == -1){
    shutdown(sock,SHUT_WR); // no more writing 
    return;
  }
  id.dev = st.st_dev;
  id.ino = st.st_ino; 

  // wait until there is no writer
  dlock_ptr = get_dir_lock(id);
  start_reading(dlock_ptr);
  
  if((dir_p = opendir(dir.c_str())) == NULL){
    shutdown(sock,SHUT_WR); // no more writing 
    end_reading(dlock_ptr);
    return;
  }

  // read dir entries and write them to socket
  string file_name;
  while((dir_entp = readdir(dir_p)) != NULL){ //
    if(dir_entp->d_type == DT_DIR) continue;
    file_name = dir_entp->d_name;
    file_name += "\n";
    write_message(sock,file_name,file_name.size());
  }

  // signal end
  file_name = ".";
  write_message(sock,file_name,1); // signal end of listing
  shutdown(sock,SHUT_WR); // no more writing
  closedir(dir_p);

  end_reading(dlock_ptr);
}

void pull(const string& file, int sock){
  int fdr;
  off_t filesize = 0;
  string file_name =  extract_file(file), buf,error;
  error.resize(256);
  bool check_next = true;
  DirId id;
  struct stat st;
  shared_ptr<dir_lock> dlock_ptr;
  string dir = extract_dir(file); // ./dir

  if(stat(dir.c_str(),&st) == -1){
    filesize = -1;
    char* ptr;
    ptr = strerror_r(errno, &error[0], error.size());
    error = ptr;
    error.resize(strlen(error.c_str()));
    check_next = false; // no need to check open, operation failed
  }
  if(check_next && (stat(file.c_str(),&st)) == -1){
    filesize = -1;
    char* ptr;
    ptr = strerror_r(errno, &error[0], error.size());
    error = ptr;
    error.resize(strlen(error.c_str()));
    check_next = false; // no need to check open, operation failed
  }

  id.dev = st.st_dev;
  id.ino = st.st_ino;
  dlock_ptr = get_dir_lock(id); 
  start_reading(dlock_ptr);

  if(check_next && (fdr = open(file.c_str(),O_RDONLY)) == -1){
    filesize = -1;
    check_next = false;
    char* ptr = strerror_r(errno,&error[0],256);
    error = ptr;
    error.resize(strlen(error.c_str()));
  }

  filesize = filesize == 0 ? st.st_size : -1;

  // write filesize (as string) to socket (-1 if failure)
  string mes = to_string(filesize) + " "; // filesize<space>

  write_message(sock,mes,mes.size()); 

  // error in opening/fstat -> write cause
  if(filesize == -1){
    write_message(sock,error,error.size());
    shutdown(sock,SHUT_WR);
    end_reading(dlock_ptr);
    return;
  }

  // write file to socket while reading
  buf.resize(1024); // buf size
  int should_read = -1;
  size_t b_read;
  while(should_read != 2 && should_read != 0){
    should_read = read_fd(fdr,buf,buf.size(),&b_read);
    write_fd(sock,buf,b_read);
  }

  shutdown(sock,SHUT_WR); // no more writing
  close(fdr);

  end_reading(dlock_ptr);

}

void push(const string& file, int sock){
  int fdw;
  DirId id;
  struct stat st;
  shared_ptr<dir_lock> dlock_ptr;
  string dir = extract_dir(file); // ./dir

  if(stat(dir.c_str(),&st) == -1){
    return;  
  }
  id.dev = st.st_dev;
  id.ino = st.st_ino; 

  dlock_ptr = get_dir_lock(id);
  start_writing(dlock_ptr);

  if((fdw = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)) == -1){
    end_writing(dlock_ptr);
    return;
  }
  // write to file
  string command;
  string data;
  int end = -1;
  bool write_complete = false;

  // do the copy 
  while(end != 2 && end != 0){ // might be the case where an error occurs in manager and terminates early, should stop
    end = read_message(sock,command); // command = PUSH /dir1/file1.txt<space>
    size_t pos = command.rfind(' ');
    if(pos == string::npos){
      break;
    }
   
    size_t chunk_size = stoul(command.substr(pos+1)); // after last space is chunck_size only
    if(chunk_size == 0){
      write_complete = true;
      break;
    }
    data.resize(chunk_size); // might fix to get called only when it changes
    end = read_fd(sock,data,chunk_size); // data
    write_fd(fdw,data,chunk_size);
  }
  close(fdw);
  if(!write_complete) unlink(file.c_str()); // not complete write
  
  end_writing(dlock_ptr);

}

void* handle_client(void* args){
  string data,command;
  int cl_sock = *static_cast<int*>(args);
  delete static_cast<int*>(args);
  
  pthread_mutex_lock(&worker_mut);
    cur_workers++;
  pthread_mutex_unlock(&worker_mut);

  read_message(cl_sock,data);

  istringstream str(data);
  str >> command;
 
  if(command == "LIST"){
    string dir;
    str >> dir;
    list(dir,cl_sock);
  }else if(command == "PULL"){
    string file;
    str >> file;
    file.insert(0,"."); // insert relative notation in given filepath: /dir1/file.txt -> ./dir1/file.txt
    pull(file,cl_sock);
  }else if(command == "PUSH"){
    string file,chunk;
    str >> file;
    file.insert(0,".");
    str >> chunk; // 1st chunk size = -1
    push(file,cl_sock);
  }
  close(cl_sock); // close client connection
  pthread_mutex_lock(&worker_mut);
  cur_workers--;
  if(cur_workers == 0 && exit_client){
    pthread_cond_signal(&should_cleanup); // about to exit, call cleanup 
    pthread_cond_signal(&can_exit);
  }
  pthread_mutex_unlock(&worker_mut);
  pthread_exit(NULL);
} 

int main(int argc, char** argv){
  init_handler();
  if(argc != 3 || !strcmp(argv[1]," -p") || !is_number(argv[2])){
    printf("Usage:\n");
    printf("%s -p <port number>\n",argv[0]);
    exit(EXIT_FAILURE);
  }
  int port = atoi(argv[2]); 
  create_server(&list_socket,port);
  int* new_cl_fd,temp_fd;
  pthread_t client_pid,cleanup_pid;
  if(pthread_create(&cleanup_pid,NULL,cleanup,NULL) != 0) perror_exit("create cleanup");
  while(!exit_client){
    temp_fd = accept(list_socket,NULL,NULL);
    if(temp_fd == -1 && exit_client){ 
      break; // woke up, should exit
    }else if(temp_fd == -1){ // other error
      perror_exit("accept");
    }
    new_cl_fd = new int;
    *new_cl_fd = temp_fd;
    if(pthread_create(&client_pid,NULL,handle_client,new_cl_fd) != 0)
      perror_exit("pthread_create");
    if(pthread_detach(client_pid) != 0)
      perror_exit("pthread_detach");
  }
  
  // wait for requests to be served and exit gracefully
  pthread_mutex_lock(&worker_mut);
  while(cur_workers > 0){
    pthread_cond_wait(&can_exit,&worker_mut);
  }
  pthread_cond_signal(&should_cleanup); // there might be no workers -> while will fail -> i should signal cleanup
  pthread_mutex_unlock(&worker_mut);
  if(pthread_join(cleanup_pid,NULL) != 0) perror_exit("pthread_join");

}