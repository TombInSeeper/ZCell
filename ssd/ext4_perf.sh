#!/bin/bash
set -x

nrfiles=81920
totalsize=320G


fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=${nrfiles} --size=${totalsize} --name=ext4_init_seq \
--output=/run/perf/ext4/init_seq.log --rw=write --bs=128k \
--numjobs=1 \
--log_avg_msec=500\
--write_bw_log=/run/perf/ext4/ext4_init_seq \
--iodepth=64  --loops=2 --group_reporting

sleep 1

fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=${nrfiles} --size=${totalsize} --name=ext4_init_rand \
--output=/run/perf/ext4/init_rand.log --rw=randwrite --bs=4k \
--numjobs=1 --log_avg_msec=500\
--write_iops_log=/run/perf/ext4/ext4_init_rand \
--write_bw_log=/run/perf/ext4/ext4_init_rand \
--write_lat_log=/run/perf/ext4/ext4_init_rand \
--iodepth=128 --ramp_time=0 --runtime=10 --time_based --group_reporting

sleep 1


for i in 4 8 16 32 64 128
do
    for j in 1 8 32 128
    do
        echo "Start $i K randwrite benchmark  in qd $j\n"
        fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=${nrfiles} --size=${totalsize} --name=ext4_init_rand \
--output=/run/perf/ext4/rw_${i}K_${j}qd.log --rw=randwrite --bs=4k \
--numjobs=1 --log_avg_msec=500\
--write_iops_log=/run/perf/ext4/rw_${i}K_${j}qd \
--write_bw_log=/run/perf/ext4/rw_${i}K_${j}qd \
--write_lat_log=/run/perf/ext4/rw_${i}K_${j}qd \
--iodepth=128 --ramp_time=10 --runtime=60 --time_based --group_reporting
        echo "Start $i K randwrite benchmark  in qd $j done"
    done
done



