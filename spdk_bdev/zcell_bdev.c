
#include "spdk/stdinc.h"

#include "spdk/barrier.h"
#include "spdk/bdev.h"
#include "spdk/bdev_module.h"
#include "spdk/env.h"
#include "spdk/likely.h"
#include "spdk/thread.h"
#include "spdk/json.h"
#include "spdk/util.h"
#include "spdk/string.h"
#include "spdk/log.h"

#include "./zcell_bdev.h"
#include "../liboss.h"

#define ZCELL_MAX (1024)
#define MAX_EVENTS_PER_POLL (32)

#define BLOCK_SIZE (4ul << 10)
#define BLOCK_SIZE_SHIFT (12)
#define STRIPE_UNIT (4ul << 20)
#define STRIPE_UNIT_SHIFT (22)

static TAILQ_HEAD( , zcell_disk) g_zcell_disk_head;


static uint64_t oid_max = 0;

//Vdisk ==> Object map policy
static uint64_t GetOidStart(size_t GiB , uint64_t stripes) {
    uint64_t objs = GiB  << (30-STRIPE_UNIT_SHIFT);
    objs /= stripes;
    uint64_t oid = oid_max;
    oid_max += objs;
    return oid; 
}

/**
 * 
 * vdisk 按照 RAID0 方式以 4M 
 * 为粒度条带化到所有 zcell 上。
 * 
 * liboss_poller
 * |Polling
 * zcell0_session
 * zcell1_session
 * zcell2_session
 * zcell3_session
 * 
 */
// struct zcell_iochannel {
//     struct io_channel * ch;
//     TAILQ_ENTRY(zcell_iochannel) link;
// };


struct zcell_channel_group {
    struct spdk_poller	*completion_poller;
    //已经连接的 local zcell 的 io_channels
    uint32_t zcell_nr;
    struct io_channel *zioch_list[128];
};

struct zcell_ioctx {
    int zcell_op_id; //  
};

struct zcell_disk {
    struct spdk_bdev disk;
    uint64_t stripe_oid_start;
    TAILQ_ENTRY(zcell_disk) link;
};

struct zcell_disk_io_channel {
    // uint64_t io_inflight;
    struct zcell_channel_group *zgrp;
};


static struct spdk_io_channel *
bdev_zcell_get_io_channel(void *ctx)
{
	struct zcell_disk *zdisk = ctx;
	return spdk_get_io_channel(zdisk);
}

static bool
bdev_zcell_io_type_supported(void *ctx, enum spdk_bdev_io_type io_type)
{
	switch (io_type) {
	case SPDK_BDEV_IO_TYPE_READ:
	case SPDK_BDEV_IO_TYPE_WRITE:
	case SPDK_BDEV_IO_TYPE_FLUSH:
	case SPDK_BDEV_IO_TYPE_RESET:
		return true;
	default:
		return false;
	}
}

static int
bdev_zcell_destruct(void *ctx)
{
	struct zcell_disk *disk = ctx;
	TAILQ_REMOVE(&g_zcell_disk_head, disk, link);
	spdk_io_device_unregister(disk, NULL);
    free(disk);
	return 0;
}


static int
bdev_zcell_dump_info_json(void *ctx, struct spdk_json_write_ctx *w)
{
	struct zcell_disk *fdisk = ctx;
	spdk_json_write_named_object_begin(w, "zcell");
	spdk_json_write_named_string(w, "diskname", fdisk->disk.name);
	spdk_json_write_object_end(w);
	return 0;
}

static void
bdev_zcell_write_json_config(struct spdk_bdev *bdev, struct spdk_json_write_ctx *w)
{
	spdk_json_write_object_begin(w);

	spdk_json_write_object_end(w);
}

static int _bdev_zcell_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{

	struct zcell_disk_io_channel *zch = spdk_io_channel_get_ctx(ch);
    struct zcell_channel_group *zgrp = zch->zgrp;
    int rc;
	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
	case SPDK_BDEV_IO_TYPE_WRITE: {
		// spdk_bdev_io_get_buf(bdev_io, bdev_aio_get_buf_cb,
		// bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen);
        struct zcell_disk *disk = (struct zcell_disk *)(bdev_io->bdev->ctxt);
        (void)disk;
        struct zcell_ioctx *zio = (struct zcell_ioctx *)(bdev_io->driver_ctx);
        struct iovec *iov = bdev_io->u.bdev.iovs;
        uint32_t iovcnt = bdev_io->u.bdev.iovcnt;
        uint32_t len = bdev_io->u.bdev.num_blocks * bdev_io->bdev->blocklen;
        uint64_t offset = bdev_io->u.bdev.offset_blocks * bdev_io->bdev->blocklen;

        uint64_t stripe_id = (offset >> STRIPE_UNIT_SHIFT) % 1 ;
        
        uint64_t obj_id =  (offset / STRIPE_UNIT);
        uint64_t obj_offset = (offset % STRIPE_UNIT);
        uint64_t obj_len = len;

        if(iovcnt > 1) {
            assert(iovcnt == 1);
		    // return -1;
        }

        if(bdev_io->type == SPDK_BDEV_IO_TYPE_READ) {
            zio->zcell_op_id = io_read2(zgrp->zioch_list[stripe_id] , iov->iov_base ,
                obj_id , obj_offset , obj_len );
            assert(zio->zcell_op_id >= 0);
        } else {
            zio->zcell_op_id = io_write(zgrp->zioch_list[stripe_id] ,
                obj_id , iov->iov_base , obj_offset , obj_len );
            assert(zio->zcell_op_id >= 0);
        }
        op_set_userdata(zgrp->zioch_list[stripe_id] , 
           zio->zcell_op_id, (uint64_t)(bdev_io));
        int op_ids[] = { zio->zcell_op_id };

        rc = io_submit_to_channel(zgrp->zioch_list[stripe_id] , op_ids , 1);
        
        if(rc) {
            abort();
        }  
        return 0;
    }

	case SPDK_BDEV_IO_TYPE_FLUSH:
	case SPDK_BDEV_IO_TYPE_RESET: {
        spdk_bdev_io_complete(bdev_io , SPDK_BDEV_IO_STATUS_SUCCESS);
		return 0;
    }

	default:
		return -1;
	}
}

static void bdev_zcell_submit_request(struct spdk_io_channel *ch, struct spdk_bdev_io *bdev_io)
{
	if (_bdev_zcell_submit_request(ch, bdev_io) < 0) {
		spdk_bdev_io_complete(bdev_io, SPDK_BDEV_IO_STATUS_FAILED);
	}
}




static const struct spdk_bdev_fn_table zcell_disk_fn_table = {
	.destruct		= bdev_zcell_destruct,
	.submit_request		= bdev_zcell_submit_request,
	.io_type_supported	= bdev_zcell_io_type_supported,
	.get_io_channel		= bdev_zcell_get_io_channel,
	.dump_info_json		= bdev_zcell_dump_info_json,
	.write_config_json	= bdev_zcell_write_json_config,
};


/**
 * 
 * 
 * 
 */


static int bdev_zcell_initialize(void);
static void bdev_zcell_fini(void);
static int bdev_zcell_get_ctx_size(void);

static struct spdk_bdev_module zcell_if = {
	.name		= "zcell",
	.module_init	= bdev_zcell_initialize,
	.module_fini	= bdev_zcell_fini,
	.get_ctx_size	= bdev_zcell_get_ctx_size,
};

SPDK_BDEV_MODULE_REGISTER(zcell, &zcell_if)



static int
bdev_zcell_create_cb(void *io_device, void *ctx_buf)
{
    struct zcell_disk_io_channel *ch = ctx_buf;
    ch->zgrp = spdk_io_channel_get_ctx(spdk_get_io_channel(&zcell_if));
	return 0;
}

static void
bdev_zcell_destroy_cb(void *io_device, void *ctx_buf)
{
    struct zcell_disk_io_channel *ch = ctx_buf;
    spdk_put_io_channel(spdk_io_channel_from_ctx(ch->zgrp));
}

int create_zcell_disk(const char *name , uint32_t size_GiB)
{
    struct zcell_disk *zdisk;
    zdisk = calloc(1, sizeof(*zdisk));
	if (!zdisk) {
		SPDK_ERRLOG("Unable to allocate enough memory for zcell backend\n");
		return -ENOMEM;
	}
    zdisk->stripe_oid_start = GetOidStart(size_GiB , 1);

    zdisk->disk.module = &zcell_if;
    zdisk->disk.blocklen = 0x1000;
    zdisk->disk.blockcnt = ((uint64_t)size_GiB) << 18;
    zdisk->disk.ctxt = zdisk;
    zdisk->disk.write_unit_size = 1;
    zdisk->disk.product_name = "Zcell Disk";
    zdisk->disk.fn_table = &zcell_disk_fn_table;
    zdisk->disk.split_on_optimal_io_boundary = true;
    zdisk->disk.name = strdup(name);
    zdisk->disk.optimal_io_boundary = STRIPE_UNIT >> 12 ;



	spdk_io_device_register(zdisk, bdev_zcell_create_cb, bdev_zcell_destroy_cb,
				sizeof(struct zcell_disk_io_channel),
				zdisk->disk.name);

    int rc = spdk_bdev_register(&zdisk->disk);
    (void)rc;

    TAILQ_INSERT_TAIL(&g_zcell_disk_head , zdisk , link);

    return 0;
}

struct delete_zcell_bdev_ctx {
	delete_zcell_bdev_complete cb_fn;
	void *cb_arg;
};

static void
zcell_bdev_unregister_cb(void *arg, int bdeverrno)
{
	struct delete_zcell_bdev_ctx *ctx = arg;
	ctx->cb_fn(ctx->cb_arg, bdeverrno);
	free(ctx);
}

void delete_zcell_disk(struct spdk_bdev *bdev, spdk_bdev_unregister_cb cb_fn,
			  void *cb_arg)
{
	struct delete_zcell_bdev_ctx *ctx;

	if (!bdev || bdev->module != &zcell_if) {
		cb_fn(cb_arg, -ENODEV);
		return;
	}

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		cb_fn(cb_arg, -ENOMEM);
		return;
	}

	ctx->cb_fn = cb_fn;
	ctx->cb_arg = cb_arg;
	spdk_bdev_unregister(bdev, zcell_bdev_unregister_cb, ctx);

}




static int 
bdev_zcell_group_poll( void * grp_)
{
    struct zcell_channel_group *grp = grp_;
    uint32_t i;
    uint32_t count = 0;

    for ( i = 0 ; i < grp->zcell_nr ; ++i) {
        struct io_channel *ch = grp->zioch_list[i];
        int cpls[MAX_EVENTS_PER_POLL];
        int n = io_poll_channel(ch , cpls , 0 , MAX_EVENTS_PER_POLL);
        assert(n >= 0);
        if( n > 0 ) {
            int j;
            for ( j = 0 ; j < n ; ++j) {
                uint64_t bio_ = op_get_userdata(ch, cpls[j]);
                int errcode ;
                void *databuf;
                uint32_t datalen;
                int rc = op_claim_result(ch , cpls[j] , &errcode , NULL, &databuf, &datalen);
                if(rc) {
                    abort();
                }
                op_destory(ch , cpls[j]);
                if(errcode) {
                    errcode = SPDK_BDEV_IO_STATUS_FAILED;
                } else {
                    errcode = SPDK_BDEV_IO_STATUS_SUCCESS;
                }
                struct spdk_bdev_io *bio = (void *)(uintptr_t)(bio_);
                spdk_bdev_io_complete(bio , errcode);
            }
            count += n;
        }

    }


    return count;
}

static int
bdev_zcell_group_create_cb(void *io_device, void *ctx_buf)
{
	struct zcell_channel_group *grp = ctx_buf;

    int rc = tls_io_ctx_init(0); // 初始化当前线程的 msgr 
    if(rc) {
        SPDK_ERRLOG("liboss ctx tls init failed\n");
        return rc;
    }

    grp->zcell_nr = 1;
    grp->zioch_list[0] = get_io_channel_with_local(0 , 512);
    if(!grp->zioch_list[0]) {
        SPDK_ERRLOG("liboss get channel with 0\n");
        return -1;
    }

    grp->completion_poller = spdk_poller_register(bdev_zcell_group_poll , grp , 0 );
    if(!grp->completion_poller) {
        return -1;
    }
    return 0;
}

static void
bdev_zcell_group_destroy_cb(void *io_device, void *ctx_buf)
{
	struct zcell_channel_group *grp = ctx_buf;
    spdk_poller_unregister(&grp->completion_poller);
    uint64_t i;
    for (i = 0 ; i<grp->zcell_nr ; ++i) {
       put_io_channel(grp->zioch_list[i]);
    }   
    tls_io_ctx_fini(); //销毁当前线程的 msgr    

}


static int bdev_zcell_get_ctx_size(void)
{
    return sizeof(struct zcell_ioctx);
}

static int
bdev_zcell_initialize(void)
{
    TAILQ_INIT(&g_zcell_disk_head);
	spdk_io_device_register(&zcell_if, bdev_zcell_group_create_cb, bdev_zcell_group_destroy_cb,
				sizeof(struct zcell_channel_group),
				"zcell_module");
	return 0;
}

static void
bdev_zcell_fini(void)
{
    spdk_io_device_unregister(&zcell_if , NULL);
}









// SPDK_LOG_REGISTER_COMPONENT("zcell", SPDK_LOG_ZCELL)