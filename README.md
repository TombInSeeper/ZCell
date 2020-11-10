# Introduction
高速网络和存储对存储软件的设计带来了新的挑战，传统分布式存储的IO路径经过冗长的软件栈，无法充分释放高速硬件的性能。
- 使用Run-to-completion模型
- ZStore 是一个使用高速SSD和PM的存储后端，SSD放数据，PM放元数据，提供基于对象ID的随机读写，与 SPDK BlobStore 相比提供更强的一致性。
- 网络部分 //TODO
