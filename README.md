# SPDK OSS
A spdk-based object storage server

## Introduction
高速网络和存储对存储软件的设计带来了新的挑战，传统分布式存储的IO路径经过冗长的软件栈，无法充分释放高速硬件的性能。
SPDK OSS 使用 Run-to-completion 模型，并使用 SPDK bdev layer + nvme driver。
ZStore 是一个使用高速SSD和PM的存储后端，SSD放数据，PM放元数据，提供基于对象ID的对象读写访问，与 SPDK BlobStore 相比的提供更强的一致性。

网络部分
//TODO