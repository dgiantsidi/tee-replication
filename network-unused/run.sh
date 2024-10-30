#!/bin/bash

#### config ####
while getopts p:r:a: flag
do
    case "${flag}" in
        p) _process_id=${OPTARG};;
        r) _reqs_num=${OPTARG};;
        a) _authenticator_mode=${OPTARG};;
    esac
done


##### set directories ####
cur_dir=$(pwd)
cd ../
project_dir=$(pwd)
cd ${cur_dir}
echo "${project_dir}"
export sgx_server_dir=${project_dir}/sha256-example/digital_signatures


${sgx_server_dir}/server &
server_pid=$!

echo "sudo -E ${cur_dir}/build/bench_martha --process_id=${_process_id} --reqs_num=${_reqs_num} --authenticator_mode=${_authenticator_mode}"
sudo -E ${cur_dir}/build/bench --process_id=${_process_id} --reqs_num=${_reqs_num} --authenticator_mode=${_authenticator_mode}


sleep 2
kill -9 ${server_pid}
