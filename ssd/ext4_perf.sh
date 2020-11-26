fio --ioengine=libaio --direct=1 --thread \
--norandommap --nrfiles=5000 --size=4M --name=ext4 \
--output=ext4.log --rw=randwrite --bs=4k --numjobs=1 \
--iodepth=128 --ramp_time=10 --runtime=30 --time_based --group_reporting
