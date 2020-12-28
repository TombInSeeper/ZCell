/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "zcell_bdev.h"



/* Structure to hold the parameters for this RPC method. */
struct rpc_construct_zcell_bdev {
    char *name;
    uint32_t size_GiB;
};

/* Free the allocated memory resource after the RPC handling. */
static void
free_rpc_construct_zcell_bdev(struct rpc_construct_zcell_bdev *r)
{
    free(r->name);
}

/* Structure to decode the input parameters for this RPC method. */
static const struct spdk_json_object_decoder rpc_construct_zcell_bdev_decoders[] = {
	{"name", offsetof(struct rpc_construct_zcell_bdev, name), spdk_json_decode_string},
	{"size", offsetof(struct rpc_construct_zcell_bdev, size_GiB), spdk_json_decode_uint32},
	// {"zcell_object_id_start", offsetof(struct rpc_construct_zcell_bdev, zcell_object_id_start), spdk_json_decode_uint64},
	// {"zcell_object_size", offsetof(struct rpc_construct_zcell_bdev, zcell_object_size), spdk_json_decode_uint32},
	// {"zcell_object_num", offsetof(struct rpc_construct_zcell_bdev, zcell_object_num), spdk_json_decode_uint32},
};




/* Decode the parameters for this RPC method and properly construct the zcell
 * device. Error status returned in the failed cases.
 */
static void
spdk_rpc_construct_zcell_bdev(struct spdk_jsonrpc_request *request,
				 const struct spdk_json_val *params)
{
	struct rpc_construct_zcell_bdev req = { 0 };
	struct spdk_json_write_ctx *w;
	int rc;

	if (spdk_json_decode_object(params, rpc_construct_zcell_bdev_decoders,
				    SPDK_COUNTOF(rpc_construct_zcell_bdev_decoders),
				    &req)) {
		SPDK_ERRLOG("spdk_json_decode_object failed\n");
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto cleanup;
	}

	rc = create_zcell_disk(req.name ,req.size_GiB);

    
	if (rc != 0) {
		spdk_jsonrpc_send_error_response(request, rc, spdk_strerror(-rc));
		goto cleanup;
	}

	w = spdk_jsonrpc_begin_result(request);
	spdk_json_write_string(w, req.name);
	spdk_jsonrpc_end_result(request, w);

cleanup:
	free_rpc_construct_zcell_bdev(&req);
}


SPDK_RPC_REGISTER("construct_zcell_bdev", spdk_rpc_construct_zcell_bdev, SPDK_RPC_RUNTIME)


//-------------
struct rpc_delete_zcell_bdev {
	char *name;
};

static void
free_rpc_delete_zcell_bdev(struct rpc_delete_zcell_bdev *req)
{
	free(req->name);
}


static const struct spdk_json_object_decoder rpc_delete_zcell_bdev_decoders[] = {
	{"name", offsetof(struct rpc_delete_zcell_bdev, name), spdk_json_decode_string},
};

static void
_spdk_rpc_delete_zcell_bdev_cb(void *cb_arg, int bdeverrno)
{
	struct spdk_jsonrpc_request *request = cb_arg;
	struct spdk_json_write_ctx *w;

	w = spdk_jsonrpc_begin_result(request);
	spdk_json_write_bool(w, bdeverrno == 0);
	spdk_jsonrpc_end_result(request, w);
}

static void
spdk_rpc_delete_zcell_bdev(struct spdk_jsonrpc_request *request,
			      const struct spdk_json_val *params)
{
	struct rpc_delete_zcell_bdev req = {NULL};
	struct spdk_bdev *bdev;

	if (spdk_json_decode_object(params, rpc_delete_zcell_bdev_decoders,
				    SPDK_COUNTOF(rpc_delete_zcell_bdev_decoders),
				    &req)) {
		spdk_jsonrpc_send_error_response(request, SPDK_JSONRPC_ERROR_INTERNAL_ERROR,
						 "spdk_json_decode_object failed");
		goto cleanup;
	}

	bdev = spdk_bdev_get_by_name(req.name);
	if (bdev == NULL) {
		spdk_jsonrpc_send_error_response(request, -ENODEV, spdk_strerror(ENODEV));
		goto cleanup;
	}

	delete_zcell_disk(bdev, _spdk_rpc_delete_zcell_bdev_cb, request);

cleanup:
	free_rpc_delete_zcell_bdev(&req);
}

SPDK_RPC_REGISTER("delete_zcell_bdev", spdk_rpc_delete_zcell_bdev, SPDK_RPC_RUNTIME)