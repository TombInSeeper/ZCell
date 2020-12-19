#!/bin/bash
set -x

nrfiles=$1
totalsize=${2}G
flag=$3


dir=/mnt/ext4
if 
fio --ioengine=libaio --direct=1 --thread --norandommap \
--nrfiles=${nrfiles}  --directory=${dir}  \
--filename_format=test.'$'filenum \
--size=${totalsize} \
--name=ext4_init_seq \
--file_service_type=sequential \
--output=/run/perf/ext4/init_seq.log --rw=write --bs=128k \
--numjobs=1 \
--log_avg_msec=1000 \
--write_bw_log=/run/perf/ext4/init_seq \
--write_iops_log=/run/perf/ext4/init_seq \
--write_lat_log=/run/perf/ext4/init_seq \
--iodepth=128 --loops=2 --group_reporting


sleep 1

fio --ioengine=libaio --direct=1 --thread --norandommap \
--nrfiles=${nrfiles}  --directory=${dir} \
--filename_format=test.'$'filenum  \
--size=${totalsize} --name=ext4_init_rand \
--file_service_type=random \
--output=/run/perf/ext4/init_rand.log --rw=randwrite --bs=4k \
--numjobs=1 \
--log_avg_msec=1000 \
--write_iops_log=/run/perf/ext4/init_rand \
--write_bw_log=/run/perf/ext4/init_rand \
--write_lat_log=/run/perf/ext4/init_rand \
--iodepth=128 --ramp_time=0 --runtime=3600 --time_based --group_reporting

sleep 1





