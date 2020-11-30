#!/bin/bash
set -x

nrfiles=$1
totalsize=${2}G
flag=$3
dir=/mnt/ext4

for i in 4 8 16 32 64 128
do
    for j in 1 8 32 128
    do
        echo "Start $i K randwrite benchmark  in qd $j\n"
        fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=${nrfiles} --size=${totalsize}  --directory=${dir} \
--filename_format=test.'$'filenum \
--name=ext4_perf_rand \
--output=/run/perf/ext4/perf_${i}K_${j}qd.log \
--file_service_type=random \
--numjobs=1  \
--log_avg_msec=1000 \
--write_iops_log=/run/perf/ext4/perf_${i}K_${j}qd \
--write_bw_log=/run/perf/ext4/perf_${i}K_${j}qd \
--write_lat_log=/run/perf/ext4/perf_${i}K_${j}qd \
--rw=randwrite --bs=4k \
--iodepth=$j --ramp_time=10 --runtime=60 --time_based --group_reporting
        echo "Start $i K randwrite benchmark  in qd $j done"
    done
done
