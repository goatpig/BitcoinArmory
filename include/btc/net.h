/*

 The MIT License (MIT)

 Copyright (c) 2016 Jonas Schnelli

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.

 */

#ifndef __LIBBTC_NET_H__
#define __LIBBTC_NET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event.h>

#include "btc.h"
#include "buffer.h"
#include "chainparams.h"
#include "cstr.h"
#include "protocol.h"
#include "vector.h"

static const unsigned int BTC_P2P_MESSAGE_CHUNK_SIZE = 4096;

enum NODE_STATE {
    NODE_CONNECTING = (1 << 0),
    NODE_CONNECTED = (1 << 1),
    NODE_ERRORED = (1 << 2),
    NODE_TIMEOUT = (1 << 3),
    NODE_HEADERSYNC = (1 << 4),
    NODE_BLOCKSYNC	= (1 << 5),
    NODE_MISSBEHAVED = (1 << 6),
    NODE_DISCONNECTED = (1 << 7),
    NODE_DISCONNECTED_FROM_REMOTE_PEER = (1 << 8),
};

/* basic group-of-nodes structure */
struct btc_node_;
typedef struct btc_node_group_ {
    void* ctx; /* flexible context usefull in conjunction with the callbacks */
    struct event_base* event_base;
    vector* nodes; /* the groups nodes */
    char clientstr[1024];
    int desired_amount_connected_nodes;
    const btc_chainparams* chainparams;

    /* callbacks */
    int (*log_write_cb)(const char* format, ...); /* log callback, default=printf */
    btc_bool (*parse_cmd_cb)(struct btc_node_* node, btc_p2p_msg_hdr* hdr, struct const_buffer* buf);
    void (*postcmd_cb)(struct btc_node_* node, btc_p2p_msg_hdr* hdr, struct const_buffer* buf);
    void (*node_connection_state_changed_cb)(struct btc_node_* node);
    btc_bool (*should_connect_to_more_nodes_cb)(struct btc_node_* node);
    void (*handshake_done_cb)(struct btc_node_* node);
    btc_bool (*periodic_timer_cb)(struct btc_node_* node, uint64_t* time); // return false will cancle the internal logic
} btc_node_group;

enum {
    NODE_CONNECTIONSTATE_DISCONNECTED = 0,
    NODE_CONNECTIONSTATE_CONNECTING = 5,
    NODE_CONNECTIONSTATE_CONNECTED = 50,
    NODE_CONNECTIONSTATE_ERRORED = 100,
    NODE_CONNECTIONSTATE_ERRORED_TIMEOUT = 101,
};

/* basic node structure */
typedef struct btc_node_ {
    struct sockaddr addr;
    struct bufferevent* event_bev;
    struct event* timer_event;
    btc_node_group* nodegroup;
    int nodeid;
    uint64_t lastping;
    uint64_t time_started_con;
    uint64_t time_last_request;
    uint256 last_requested_inv;

    cstring* recvBuffer;
    uint64_t nonce;
    uint64_t services;
    uint32_t state;
    int missbehavescore;
    btc_bool version_handshake;

    unsigned int bestknownheight;

    uint32_t hints; /* can be use for user defined state */
} btc_node;

LIBBTC_API int net_write_log_printf(const char* format, ...);

/* =================================== */
/* NODES */
/* =================================== */

/* create new node object */
LIBBTC_API btc_node* btc_node_new();
LIBBTC_API void btc_node_free(btc_node* group);

/* set the nodes ip address and port (ipv4 or ipv6)*/
LIBBTC_API btc_bool btc_node_set_ipport(btc_node* node, const char* ipport);

/* disconnect a node */
LIBBTC_API void btc_node_disconnect(btc_node* node);

/* mark a node missbehave and disconnect */
LIBBTC_API btc_bool btc_node_missbehave(btc_node* node);

/* =================================== */
/* NODE GROUPS */
/* =================================== */

/* create a new node group */
LIBBTC_API btc_node_group* btc_node_group_new(const btc_chainparams* chainparams);
LIBBTC_API void btc_node_group_free(btc_node_group* group);

/* disconnect all peers */
LIBBTC_API void btc_node_group_shutdown(btc_node_group* group);

/* add a node to a node group */
LIBBTC_API void btc_node_group_add_node(btc_node_group* group, btc_node* node);

/* start node groups event loop */
LIBBTC_API void btc_node_group_event_loop(btc_node_group* group);

/* connect to more nodex */
LIBBTC_API btc_bool btc_node_group_connect_next_nodes(btc_node_group* group);

/* get the amount of connected nodes */
LIBBTC_API int btc_node_group_amount_of_connected_nodes(btc_node_group* group, enum NODE_STATE state);

/* sends version command to node */
LIBBTC_API void btc_node_send_version(btc_node* node);

/* send arbitrary data to node */
LIBBTC_API void btc_node_send(btc_node* node, cstring* data);

LIBBTC_API int btc_node_parse_message(btc_node* node, btc_p2p_msg_hdr* hdr, struct const_buffer* buf);
LIBBTC_API void btc_node_connection_state_changed(btc_node* node);

/* =================================== */
/* DNS */
/* =================================== */

LIBBTC_API btc_bool btc_node_group_add_peers_by_ip_or_seed(btc_node_group *group, const char *ips);
LIBBTC_API int btc_get_peers_from_dns(const char* seed, vector* ips_out, int port, int family);

#ifdef __cplusplus
}
#endif

#endif //__LIBBTC_NET_H__
