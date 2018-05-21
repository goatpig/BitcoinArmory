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

#include <btc/block.h>
#include <btc/blockchain.h>
#include <btc/checkpoints.h>
#include <btc/headersdb.h>
#include <btc/headersdb_file.h>
#include <btc/net.h>
#include <btc/netspv.h>
#include <btc/protocol.h>
#include <btc/serialize.h>
#include <btc/tx.h>
#include <btc/utils.h>

#ifdef _WIN32
#include <getopt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

static const unsigned int HEADERS_MAX_RESPONSE_TIME = 60;
static const unsigned int MIN_TIME_DELTA_FOR_STATE_CHECK = 5;
static const unsigned int BLOCK_GAP_TO_DEDUCT_TO_START_SCAN_FROM = 5;
static const unsigned int BLOCKS_DELTA_IN_S = 600;
static const unsigned int COMPLETED_WHEN_NUM_NODES_AT_SAME_HEIGHT = 2;

static btc_bool btc_net_spv_node_timer_callback(btc_node *node, uint64_t *now);
void btc_net_spv_post_cmd(btc_node *node, btc_p2p_msg_hdr *hdr, struct const_buffer *buf);
void btc_net_spv_node_handshake_done(btc_node *node);

void btc_net_set_spv(btc_node_group *nodegroup)
{
    nodegroup->postcmd_cb = btc_net_spv_post_cmd;
    nodegroup->handshake_done_cb = btc_net_spv_node_handshake_done;
    nodegroup->node_connection_state_changed_cb = NULL;
    nodegroup->periodic_timer_cb = btc_net_spv_node_timer_callback;
}

btc_spv_client* btc_spv_client_new(const btc_chainparams *params, btc_bool debug, btc_bool headers_memonly)
{
    btc_spv_client* client;
    client = calloc(1, sizeof(*client));

    client->last_headersrequest_time = 0; //!< time when we requested the last header package
    client->last_statecheck_time = 0;
    client->oldest_item_of_interest = time(NULL)-5*60;
    client->stateflags = SPV_HEADER_SYNC_FLAG;

    client->chainparams = params;

    client->nodegroup = btc_node_group_new(params);
    client->nodegroup->ctx = client;
    client->nodegroup->desired_amount_connected_nodes = 3; /* TODO */

    btc_net_set_spv(client->nodegroup);

    if (debug) {
        client->nodegroup->log_write_cb = net_write_log_printf;
    }

    if (params == &btc_chainparams_main) {
        client->use_checkpoints = true;
    }
    client->headers_db = &btc_headers_db_interface_file;
    client->headers_db_ctx = client->headers_db->init(params, headers_memonly);

    // set callbacks
    client->header_connected = NULL;
    client->called_sync_completed = false;
    client->sync_completed = NULL;
    client->header_message_processed = NULL;
    client->sync_transaction = NULL;

    return client;
}

void btc_spv_client_discover_peers(btc_spv_client* client, const char *ips)
{
    btc_node_group_add_peers_by_ip_or_seed(client->nodegroup, ips);
}

void btc_spv_client_runloop(btc_spv_client* client)
{
    btc_node_group_connect_next_nodes(client->nodegroup);
    btc_node_group_event_loop(client->nodegroup);
}

void btc_spv_client_free(btc_spv_client *client)
{
    if (!client)
        return;

    if (client->headers_db)
    {
        client->headers_db->free(client->headers_db_ctx);
        client->headers_db_ctx = NULL;
        client->headers_db = NULL;
    }

    if (client->nodegroup) {
        btc_node_group_free(client->nodegroup);
        client->nodegroup = NULL;
    }

    free(client);
}

btc_bool btc_spv_client_load(btc_spv_client *client, const char *file_path)
{
    if (!client)
        return false;

    if (!client->headers_db)
        return false;

    return client->headers_db->load(client->headers_db_ctx, file_path);

}

void btc_net_spv_periodic_statecheck(btc_node *node, uint64_t *now)
{
    /* statecheck logic */
    /* ================ */

    btc_spv_client *client = (btc_spv_client*)node->nodegroup->ctx;

    client->nodegroup->log_write_cb("Statecheck: amount of connected nodes: %d\n", btc_node_group_amount_of_connected_nodes(client->nodegroup, NODE_CONNECTED));

    /* check if the node chosen for NODE_HEADERSYNC during SPV_HEADER_SYNC has stalled */
    if (client->last_headersrequest_time > 0 && *now > client->last_headersrequest_time)
    {
        int64_t timedetla = *now - client->last_headersrequest_time;
        if (timedetla > HEADERS_MAX_RESPONSE_TIME)
        {
            client->nodegroup->log_write_cb("No header response in time (used %d) for node %d\n", timedetla, node->nodeid);
            /* disconnect the node if we haven't got a header after requesting some with a getheaders message */
            node->state &= ~NODE_HEADERSYNC;
            btc_node_disconnect(node);
            client->last_headersrequest_time = 0;
            btc_net_spv_request_headers(client);
        }
    }
    if (node->time_last_request > 0 && *now > node->time_last_request)
    {
        // we are downloading blocks from this peer
        int64_t timedetla = *now - node->time_last_request;

        client->nodegroup->log_write_cb("No block response in time (used %d) for node %d\n", timedetla, node->nodeid);
        if (timedetla > HEADERS_MAX_RESPONSE_TIME)
        {
            /* disconnect the node if a blockdownload has stalled */
            btc_node_disconnect(node);
            node->time_last_request = 0;
            btc_net_spv_request_headers(client);
        }
    }

    /* check if we need to sync headers from a different peer */
    if ((client->stateflags & SPV_HEADER_SYNC_FLAG) == SPV_HEADER_SYNC_FLAG)
    {
        btc_net_spv_request_headers(client);
    }
    else
    {
        /* headers sync should be done at this point */

    }

    client->last_statecheck_time = *now;
}

static btc_bool btc_net_spv_node_timer_callback(btc_node *node, uint64_t *now)
{
    btc_spv_client *client = (btc_spv_client*)node->nodegroup->ctx;

    if (client->last_statecheck_time + MIN_TIME_DELTA_FOR_STATE_CHECK < *now)
    {
        /* do a state check only every <n> seconds */
        btc_net_spv_periodic_statecheck(node, now);
    }

    /* return true = run internal timer logic (ping, disconnect-timeout, etc.) */
    return true;
}

void btc_net_spv_fill_block_locator(btc_spv_client *client, vector *blocklocators)
{
    if (client->headers_db->getchaintip(client->headers_db_ctx)->height == 0)
    {
        if (client->use_checkpoints && client->oldest_item_of_interest > BLOCK_GAP_TO_DEDUCT_TO_START_SCAN_FROM * BLOCKS_DELTA_IN_S) {
            /* jump to checkpoint */
            /* check oldest item of interest and set genesis/checkpoint */

            int64_t min_timestamp = client->oldest_item_of_interest - BLOCK_GAP_TO_DEDUCT_TO_START_SCAN_FROM * BLOCKS_DELTA_IN_S; /* ensure we going back ~144 blocks */
            for (int i = (sizeof(btc_mainnet_checkpoint_array) / sizeof(btc_mainnet_checkpoint_array[0]))-1; i >= 0 ; i--)
            {
                const btc_checkpoint *cp = &btc_mainnet_checkpoint_array[i];
                if ( btc_mainnet_checkpoint_array[i].timestamp < min_timestamp)
                {
                    uint256 *hash = btc_calloc(1, sizeof(uint256));
                    utils_uint256_sethex((char *)btc_mainnet_checkpoint_array[i].hash, (uint8_t *)hash);
                    vector_add(blocklocators, (void *)hash);

                    if (!client->headers_db->has_checkpoint_start(client->headers_db_ctx)) {
                        client->headers_db->set_checkpoint_start(client->headers_db_ctx, *hash, btc_mainnet_checkpoint_array[i].height);
                    }
                }
            }
            if (blocklocators->len > 0) {
                // return if we could fill up the blocklocator with checkpoints
                return;
            }
        }
        uint256 *hash = btc_calloc(1, sizeof(uint256));
        memcpy(hash, &client->chainparams->genesisblockhash, sizeof(uint256));
        vector_add(blocklocators, (void *)hash);
        client->nodegroup->log_write_cb("Setting blocklocator with genesis block\n");
    }
    else
    {
        client->headers_db->fill_blocklocator_tip(client->headers_db_ctx, blocklocators);
    }
}

void btc_net_spv_node_request_headers_or_blocks(btc_node *node, btc_bool blocks)
{
    // request next headers
    vector *blocklocators = vector_new(1, free);

    btc_net_spv_fill_block_locator((btc_spv_client *)node->nodegroup->ctx, blocklocators);

    cstring *getheader_msg = cstr_new_sz(256);
    btc_p2p_msg_getheaders(blocklocators, NULL, getheader_msg);

    /* create p2p message */
    cstring *p2p_msg = btc_p2p_message_new(node->nodegroup->chainparams->netmagic, (blocks ? "getblocks" : "getheaders"), getheader_msg->str, getheader_msg->len);
    cstr_free(getheader_msg, true);

    /* send message */
    btc_node_send(node, p2p_msg);
    node->state |= ( blocks ? NODE_BLOCKSYNC : NODE_HEADERSYNC);

    /* remember last headers request time */
    if (blocks) {
        node->time_last_request = time(NULL);
    }
    else {
        ((btc_spv_client*)node->nodegroup->ctx)->last_headersrequest_time = time(NULL);
    }

    /* cleanup */
    vector_free(blocklocators, true);
    cstr_free(p2p_msg, true);
}

btc_bool btc_net_spv_request_headers(btc_spv_client *client)
{
    /* make sure only one node is used for header sync */
    for(size_t i =0;i< client->nodegroup->nodes->len; i++)
    {
        btc_node *check_node = vector_idx(client->nodegroup->nodes, i);
        if (  ( (check_node->state & NODE_HEADERSYNC) == NODE_HEADERSYNC
                 ||
                (check_node->state & NODE_BLOCKSYNC) == NODE_BLOCKSYNC
               )
            &&
            (check_node->state & NODE_CONNECTED) == NODE_CONNECTED)
            return true;
    }

    /* We are not downloading headers at this point */
    /* try to request headers from a peer where the version handshake has been done */
    btc_bool new_headers_available = false;
    unsigned int nodes_at_same_height = 0;
    if (client->headers_db->getchaintip(client->headers_db_ctx)->header.timestamp > client->oldest_item_of_interest - (BLOCK_GAP_TO_DEDUCT_TO_START_SCAN_FROM * BLOCKS_DELTA_IN_S) )
    {
        // no need to fetch headers;
    }
    else {
        for(size_t i =0;i< client->nodegroup->nodes->len; i++)
        {
            btc_node *check_node = vector_idx(client->nodegroup->nodes, i);
            if ( ((check_node->state & NODE_CONNECTED) == NODE_CONNECTED) && check_node->version_handshake)
            {
                if (check_node->bestknownheight > client->headers_db->getchaintip(client->headers_db_ctx)->height) {
                    btc_net_spv_node_request_headers_or_blocks(check_node, false);
                    new_headers_available = true;
                    return true;
                } else if (check_node->bestknownheight == client->headers_db->getchaintip(client->headers_db_ctx)->height) {
                    nodes_at_same_height++;
                }
            }
        }
    }

    if (!new_headers_available && btc_node_group_amount_of_connected_nodes(client->nodegroup, NODE_CONNECTED) > 0) {
        // try to fetch blocks if no new headers are available but connected nodes are reachable
        for(size_t i =0;i< client->nodegroup->nodes->len; i++)
        {
            btc_node *check_node = vector_idx(client->nodegroup->nodes, i);
            if ( ((check_node->state & NODE_CONNECTED) == NODE_CONNECTED) && check_node->version_handshake)
            {
                if (check_node->bestknownheight > client->headers_db->getchaintip(client->headers_db_ctx)->height) {
                    btc_net_spv_node_request_headers_or_blocks(check_node, true);
                    return true;
                } else if (check_node->bestknownheight == client->headers_db->getchaintip(client->headers_db_ctx)->height) {
                    nodes_at_same_height++;
                }
            }
        }
    }

    if ( nodes_at_same_height >= COMPLETED_WHEN_NUM_NODES_AT_SAME_HEIGHT &&
         !client->called_sync_completed &&
         client->sync_completed )
    {
        client->sync_completed(client);
        client->called_sync_completed = true;
    }

    /* we could not request more headers, need more peers to connect to */
    return false;
}
void btc_net_spv_node_handshake_done(btc_node *node)
{
    btc_net_spv_request_headers((btc_spv_client*)node->nodegroup->ctx);
}

void btc_net_spv_post_cmd(btc_node *node, btc_p2p_msg_hdr *hdr, struct const_buffer *buf)
{
    btc_spv_client *client = (btc_spv_client *)node->nodegroup->ctx;

    if (strcmp(hdr->command, BTC_MSG_INV) == 0 && (node->state & NODE_BLOCKSYNC) == NODE_BLOCKSYNC)
    {
        struct const_buffer original_inv = { buf->p, buf->len };
        uint32_t varlen;
        deser_varlen(&varlen, buf);
        btc_bool contains_block = false;

        client->nodegroup->log_write_cb("Get inv request with %d items\n", varlen);

        for (unsigned int i=0;i<varlen;i++)
        {
            uint32_t type;
            deser_u32(&type, buf);
            if (type == BTC_INV_TYPE_BLOCK)
                contains_block = true;

            /* skip the hash, we are going to directly use the inv-buffer for the getdata */
            /* this means we don't support invs contanining blocks and txns as a getblock answer */
            if (type == BTC_INV_TYPE_BLOCK) {
                deser_u256(node->last_requested_inv, buf);
            }
            else {
                deser_skip(buf, 32);
            }
        }

        if (contains_block)
        {
            node->time_last_request = time(NULL);

            /* request the blocks */
            client->nodegroup->log_write_cb("Requesting %d blocks\n", varlen);
            cstring *p2p_msg = btc_p2p_message_new(node->nodegroup->chainparams->netmagic, BTC_MSG_GETDATA, original_inv.p, original_inv.len);
            btc_node_send(node, p2p_msg);
            cstr_free(p2p_msg, true);

            if (varlen >= 500) {
                /* directly request more blocks */
                /* not sure if this is clever if we want to download, as example, the complete chain */
                btc_net_spv_node_request_headers_or_blocks(node, true);
            }
        }
    }
    if (strcmp(hdr->command, BTC_MSG_BLOCK) == 0)
    {
        btc_bool connected;
        btc_blockindex *pindex = client->headers_db->connect_hdr(client->headers_db_ctx, buf, false, &connected);
        /* deserialize the p2p header */
        if (!pindex) {
            /* deserialization failed */
            return;
        }

        uint32_t amount_of_txs;
        if (!deser_varlen(&amount_of_txs, buf)) {
            /* deserialization transaction varint failed */
            return;
        }

        // flag off the block request stall check
        node->time_last_request = time(NULL);

        // for now, turn of stall checks if we are near the tip
        if (pindex->header.timestamp > node->time_last_request - 30*60) {
            node->time_last_request = 0;
        }

        /* for now, only scan if the block could be connected on top */
        if (connected) {
            if (client->header_connected) { client->header_connected(client); }
            time_t lasttime = pindex->header.timestamp;
            printf("Downloaded new block with size %d at height %d (%s)\n", hdr->data_len, pindex->height, ctime(&lasttime));
            uint64_t start = time(NULL);
            printf("Start parsing %d transactions...", amount_of_txs);

            size_t consumedlength = 0;
            for (unsigned int i=0;i<amount_of_txs;i++)
            {
                btc_tx* tx = btc_tx_new();
                if (!btc_tx_deserialize(buf->p, buf->len, tx, &consumedlength, true)) {
                    printf("Error deserializing transaction\n");
                }
                deser_skip(buf, consumedlength);

                /* send info to possible callback */
                if (client->sync_transaction) { client->sync_transaction(client->sync_transaction_ctx, tx, i, pindex); }

                btc_tx_free(tx);
            }
            printf("done (took %llu secs)\n", time(NULL) - start);
        }
        else {
            fprintf(stderr, "Could not connect block on top of the chain\n");
        }

        if (btc_hash_equal(node->last_requested_inv, pindex->hash)) {
            // last requested block reached, consider stop syncing
            if (!client->called_sync_completed && client->sync_completed) { client->sync_completed(client); client->called_sync_completed = true; }
        }
    }
    if (strcmp(hdr->command, BTC_MSG_HEADERS) == 0)
    {
        uint32_t amount_of_headers;
        if (!deser_varlen(&amount_of_headers, buf)) return;
        uint64_t now = time(NULL);
        client->nodegroup->log_write_cb("Got %d headers (took %d s) from node %d\n", amount_of_headers, now - client->last_headersrequest_time, node->nodeid);

        // flag off the request stall check
        client->last_headersrequest_time = 0;

        unsigned int connected_headers = 0;
        for (unsigned int i=0;i<amount_of_headers;i++)
        {
            btc_bool connected;
            btc_blockindex *pindex = client->headers_db->connect_hdr(client->headers_db_ctx, buf, false, &connected);
            /* deserialize the p2p header */
            if (!pindex)
            {
                client->nodegroup->log_write_cb("Header deserialization failed (node %d)\n", node->nodeid);
                return;
            }

            /* skip tx count */
            if (!deser_skip(buf, 1)) {
                client->nodegroup->log_write_cb("Header deserialization (tx count skip) failed (node %d)\n", node->nodeid);
                return;
            }

            if (!connected)
            {
                /* error, header sequence missmatch
                   mark node as missbehaving */
                client->nodegroup->log_write_cb("Got invalid headers (not in sequence) from node %d\n", node->nodeid);
                node->state &= ~NODE_HEADERSYNC;
                btc_node_missbehave(node);

                /* see if we can fetch headers from a different peer */
                btc_net_spv_request_headers(client);
            }
            else {
                connected_headers++;
                if (pindex->header.timestamp > client->oldest_item_of_interest - (BLOCK_GAP_TO_DEDUCT_TO_START_SCAN_FROM * BLOCKS_DELTA_IN_S) ) {

                    /* we should start loading block from this point */
                    client->stateflags &= ~SPV_HEADER_SYNC_FLAG;
                    client->stateflags |= SPV_FULLBLOCK_SYNC_FLAG;
                    node->state &= ~NODE_HEADERSYNC;
                    node->state |= NODE_BLOCKSYNC;

                    client->nodegroup->log_write_cb("start loading block from node %d at height %d at time: %ld\n", node->nodeid, client->headers_db->getchaintip(client->headers_db_ctx)->height, client->headers_db->getchaintip(client->headers_db_ctx)->header.timestamp);
                    btc_net_spv_node_request_headers_or_blocks(node, true);

                    /* ignore the rest of the headers */
                    /* we are going to request blocks now */
                    break;
                }
            }
        }
        btc_blockindex *chaintip = client->headers_db->getchaintip(client->headers_db_ctx);

        client->nodegroup->log_write_cb("Connected %d headers\n", connected_headers);
        client->nodegroup->log_write_cb("Chaintip at height %d\n", chaintip->height);

        /* call the header message processed callback and allow canceling the further logic commands */
        if (client->header_message_processed && client->header_message_processed(client, node, chaintip) == false)
            return;

        if (amount_of_headers == MAX_HEADERS_RESULTS && ((node->state & NODE_BLOCKSYNC) != NODE_BLOCKSYNC))
        {
            /* peer sent maximal amount of headers, very likely, there will be more */
            time_t lasttime = chaintip->header.timestamp;
            client->nodegroup->log_write_cb("chain size: %d, last time %s", chaintip->height, ctime(&lasttime));
            btc_net_spv_node_request_headers_or_blocks(node, false);
        }
        else
        {
            /* headers download seems to be completed */
            /* we should have switched to block request if the oldest_item_of_interest was set correctly */
        }
    }
}
