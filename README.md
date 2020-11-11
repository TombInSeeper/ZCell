# ZStore Introduction
高速网络和存储对存储软件的设计带来了新的挑战，传统分布式存储的IO路径经过冗长的软件栈，无法充分释放高速硬件的性能。

学术界Reflex和开源的SPDK(iSCSI,nvme-of) target 实现了高性能的远端存储，但只能提供块设备接口。
Ceph crimson-osd 保留了ceph 的对象存储，但尚不可用。

- 使用 Run-to-completion 模型。SPDK app framework 通过注册 poller 实现不同功能的组合。
- 使用基于 RDMA 的网络库。//未实现
- ZStore 使用高速SSD存数据，PM存元数据，提供基于64位对象ID的增删查改接口。ZStore 使用类似 PMDK Transaction 的方法对 PM 上的多个位置的元数据进行更新，并参考 ext4 的先写数据再原子更新元数据的方式，保证写操作的原子性。
