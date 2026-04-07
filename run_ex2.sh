#!/bin/bash

if [ ! -d "bin" ]; then
  make all
fi

# clients run on linux13 (source) and linux 15 (target) on proj root
# should also be included test1_nfs dir

# commands that will run (input: cons_input.txt)
# cancel /test1_nfs/config_source_dir@195.134.65.84:5556
# add /test1_nfs/added_source_dir@195.134.65.84:5556 /test1_nfs/added_target_dir@195.134.65.86:5557
# shutdown

# remove target if it exists
rm -rf ./test1_nfs/config_target_dir/*
rm -rf ./test1_nfs/added_target_dir/*

./bin/nfs_manager -l ./logs/np_nfs_manager.log -c ./test1_nfs/config_nfs.cfg -n 5 -p 5555 -b 7  > man_output.txt 2>&1 &

MANAGER_PID=$!

sleep 1 # needs it for console to find manager's socket open 

./bin/nfs_console -l ./logs/np_nfs_console.log -h 127.0.0.1 -p 5555 < cons_input.txt > cons_output.txt 2>&1 &

CONSOLE_PID=$! 

wait $MANAGER_PID 
wait $CONSOLE_PID

echo "=================================="
echo "Diff results"

# config_dir
diff -r ./test1_nfs/config_source_dir ./test1_nfs/config_target_dir
if [ $? -eq 0 ]; then
  echo "success diff for config_source_dir"
else
  echo "failed diff for config_source_dir"
fi

# added_source_dir
diff -r ./test1_nfs/added_source_dir ./test1_nfs/added_target_dir
if [ $? -eq 0 ]; then
  echo "success diff for added_source_dir"
else
  echo "failed diff for added_source_dir"
fi
