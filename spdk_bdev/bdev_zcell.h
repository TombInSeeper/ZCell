#ifndef ZCELL_BDEV_H
#define ZCELL_BDEV_H

#include "spdk/stdinc.h"
#include "spdk/bdev.h"
#include "spdk/bdev_module.h"
#include "spdk/log.h"
#include "spdk/rpc.h"
#include "spdk/string.h"
#include "spdk/util.h"


typedef void (*delete_zcell_bdev_complete)(void *cb_arg, int bdeverrno);


/**
 * Create new zcell bdev.
 *
 * \param core 要连接的 zcell 的 CPU id
 * \param zcell_object_id_start 占有的起始 Object Id 
 * \param zcell_object_num  占有的 Object 数量
 * \param zcell_object_size 一个 Object有多少字节
 * \return 0 on success, other on failure.
 */
int create_zcell_disk(const char *name , uint32_t size_GiB);

/**
 * Delete zcell bdev.
 *
 * \param bdev Pointer to pass through bdev.
 * \param cb_fn Function to call after deletion.
 * \param cb_arg Argument to pass to cb_fn.
 */
void delete_zcell_disk(struct spdk_bdev *bdev, spdk_bdev_unregister_cb cb_fn,
			  void *cb_arg);

#endif