#!/bin/bash
#set -x

fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=4 --size=1G --name=ext4_init_seq \
--output=/tmp/perf/ext4/init_seq.log --rw=seqwrite --bs=128k \
--numjobs=1 \
--iodepth=64  ---loops=2 --group_reporting

fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=4 --size=1G --name=ext4_init_rand \
--output=/tmp/perf/ext4/init_rand.log --rw=randwrite --bs=4k \
--numjobs=1 \
--iodepth=128 --ramp_time=0 --runtime=10 --time_based --group_reporting

for i in 4 8 16 32 64 128
do
    for j in 1 8 32 128
    do
        echo "Start $i K randwrite benchmark  in qd $j"
        fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=4 --size=1G --name=ext4_init_rand \
--output=/tmp/perf/ext4/rw_${i}K_${j}qd.log --rw=randwrite --bs=4k \
--numjobs=1 \
--write_iops_log=/tmp/perf/ext4/rw_${i}K_${j}qd \
--write_bw_log=/tmp/perf/ext4/rw_${i}K_${j}qd \
--write_lat_log=/tmp/perf/ext4/rw_${i}K_${j}qd \
--iodepth=128 --ramp_time=0 --runtime=10 --time_based --group_reporting
    echo "Start $i K randwrite benchmark  in qd $j done"
    done
done



