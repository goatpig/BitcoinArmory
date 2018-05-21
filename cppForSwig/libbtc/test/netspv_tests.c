#include "utest.h"
#include <btc/block.h>
#include <btc/net.h>
#include <btc/netspv.h>
#include <btc/utils.h>
#include <btc/serialize.h>
#include <btc/tx.h>

#include <unistd.h>

void test_spv_sync_completed(btc_spv_client* client) {
    printf("Sync completed, at height %d\n", client->headers_db->getchaintip(client->headers_db_ctx)->height);
    btc_node_group_shutdown(client->nodegroup);
}

btc_bool test_spv_header_message_processed(struct btc_spv_client_ *client, btc_node *node, btc_blockindex *newtip) {
    UNUSED(client);
    UNUSED(node);
    if (newtip) {
        printf("New headers tip height %d\n", newtip->height);
    }
    return true;
}

void test_netspv()
{
    unlink("headers.db");
    btc_spv_client* client = btc_spv_client_new(&btc_chainparams_main, true, false);
    client->header_message_processed = test_spv_header_message_processed;
    client->sync_completed = test_spv_sync_completed;

    btc_spv_client_load(client, "headers.db");

    printf("Discover peers...");
    btc_spv_client_discover_peers(client, NULL);
    printf("done\n");
    printf("Start interacting with the p2p network...\n");
    btc_spv_client_runloop(client);
    btc_spv_client_free(client);
}
