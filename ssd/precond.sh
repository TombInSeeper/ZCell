#对Flash based SSD预处理以使进入稳态
nvme format /dev/nvme0n1 -b 4096 -s 1

fio --ioengine=libaio --direct=1 --thread --norandommap --filename=/dev/nvme0n1 --name=init_seq --output=init_seq.log --rw=write --bs=128k --numjobs=1 --iodepth=64 --loops=2

fio --ioengine=libaio --direct=1 --thread --norandommap --filename=/dev/nvme0n1 --name=init_rand --output=init_rand.log --rw=randwrite --bs=4k --numjobs=4 --iodepth=128 --ramp_time=60 --runtime=14400 --time_based --group_reporting