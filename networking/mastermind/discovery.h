#ifndef MASTERMIND_DISCOVERY_H
#define MASTERMIND_DISCOVERY_H

#include "ui.h"
#include <stdint.h>

#define MM_MAX_PLAYERS 32
#define MM_BROADCAST_INTERVAL_SEC 2
#define MM_PEER_TIMEOUT_SEC 5

typedef struct {
    int udp_fd;
    uint64_t local_instance_id;
    uint16_t local_port;
    char local_name[MM_MAX_NAME_LEN];
    int cost_cutting;
    time_t last_broadcast_time;
    DiscoveredPlayer players[MM_MAX_PLAYERS];
    int player_count;
    int next_id;
} DiscoveryManager;

/**
 * Initializes UDP discovery system, binds broadcast socket to MM_DISCOVERY_PORT.
 * Returns 0 on success, -1 on failure.
 */
int discovery_init(DiscoveryManager *dm, const char *name, uint16_t listen_port, int cost_cutting);

/**
 * Sends a UDP broadcast announcement if 2 seconds have elapsed since last broadcast.
 */
void discovery_broadcast_tick(DiscoveryManager *dm);

/**
 * Processes incoming discovery packets, filtering non-matching magic packets and self-broadcasts.
 * Returns 1 if player list changed, 0 otherwise.
 */
int discovery_recv_tick(DiscoveryManager *dm);

/**
 * Removes peers whose last broadcast was >= 5 seconds ago.
 * Returns 1 if any peer was pruned, 0 otherwise.
 */
int discovery_prune_expired(DiscoveryManager *dm);

/**
 * Finds an online player by their displayed ID.
 * Returns pointer to DiscoveredPlayer or NULL if not found.
 */
const DiscoveredPlayer *discovery_find_by_id(const DiscoveryManager *dm, int id);

/**
 * Closes discovery socket and cleans up resources.
 */
void discovery_close(DiscoveryManager *dm);

#endif /* MASTERMIND_DISCOVERY_H */
