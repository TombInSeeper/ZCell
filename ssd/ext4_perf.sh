fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=81920 --size=320G --name=ext4_init_seq \
--output=/tmp/perf/ext4_init_seq.log --rw=seqwrite --bs=128k \
--numjobs=1 \
--iodepth=32 --ramp_time=10 --runtime=30 --time_based --group_reporting

fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=81920 --size=320G --name=ext4_init_rand \
--output=/tmp/perf/ext4_init_rand.log --rw=randwrite --bs=4k \
--numjobs=1 \
--iodepth=128 --ramp_time=30 --runtime=3600 --time_based --group_reporting
