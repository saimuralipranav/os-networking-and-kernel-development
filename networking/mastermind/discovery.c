#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include "discovery.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>

int discovery_init(DiscoveryManager *dm, const char *name, uint16_t listen_port, int cost_cutting) {
    if (!dm || !name) return -1;
    memset(dm, 0, sizeof(*dm));

    dm->local_port = listen_port;
    dm->cost_cutting = cost_cutting;
    strncpy(dm->local_name, name, MM_MAX_NAME_LEN - 1);
    dm->local_name[MM_MAX_NAME_LEN - 1] = '\0';
    dm->next_id = 1;

    /* Generate globally unique instance ID using /dev/urandom fallback to time/pid */
    uint64_t inst_id = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(&inst_id, sizeof(inst_id), 1, f) != 1) {
            inst_id = 0;
        }
        fclose(f);
    }
    if (inst_id == 0) {
        inst_id = ((uint64_t)time(NULL) << 32) ^ (uint64_t)getpid() ^ ((uint64_t)listen_port << 16);
    }
    dm->local_instance_id = inst_id;

    dm->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (dm->udp_fd < 0) {
        return -1;
    }

    int opt_broadcast = 1;
    setsockopt(dm->udp_fd, SOL_SOCKET, SO_BROADCAST, &opt_broadcast, sizeof(opt_broadcast));

    int opt_reuse = 1;
    setsockopt(dm->udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse, sizeof(opt_reuse));
#ifdef SO_REUSEPORT
    setsockopt(dm->udp_fd, SOL_SOCKET, SO_REUSEPORT, &opt_reuse, sizeof(opt_reuse));
#endif

    /* Non-blocking socket */
    int flags = fcntl(dm->udp_fd, F_GETFL, 0);
    fcntl(dm->udp_fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(MM_DISCOVERY_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(dm->udp_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        /* If port cannot bind, close and return */
        close(dm->udp_fd);
        dm->udp_fd = -1;
        return -1;
    }

    logger_log("UDP discovery initialized on port %d", MM_DISCOVERY_PORT);
    return 0;
}

void discovery_broadcast_tick(DiscoveryManager *dm) {
    if (!dm || dm->udp_fd < 0) return;

    time_t now = time(NULL);
    if (now - dm->last_broadcast_time < MM_BROADCAST_INTERVAL_SEC) {
        return;
    }
    dm->last_broadcast_time = now;

    DiscoveryPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.magic = htonl(MM_MAGIC_DISCOVERY);
    pkt.instance_id = dm->local_instance_id;
    pkt.listen_port = htons(dm->local_port);
    pkt.cost_cutting = (uint8_t)dm->cost_cutting;
    strncpy(pkt.name, dm->local_name, sizeof(pkt.name) - 1);

    /* Broadcast to general INADDR_BROADCAST (255.255.255.255) */
    struct sockaddr_in bcast_addr;
    memset(&bcast_addr, 0, sizeof(bcast_addr));
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(MM_DISCOVERY_PORT);
    bcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(dm->udp_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&bcast_addr, sizeof(bcast_addr));

    /* Also broadcast to specific subnet broadcast address of every active interface (crucial for macOS & mirrored WSL) */
    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!(ifa->ifa_flags & IFF_UP)) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            if (ifa->ifa_flags & IFF_BROADCAST && ifa->ifa_broadaddr) {
                struct sockaddr_in *dst = (struct sockaddr_in *)ifa->ifa_broadaddr;
                dst->sin_port = htons(MM_DISCOVERY_PORT);
                sendto(dm->udp_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)dst, sizeof(*dst));
            }
        }
        freeifaddrs(ifaddr);
    }

    /* Also send to 127.0.0.1 for seamless multi-instance testing on localhost */
    struct sockaddr_in loop_addr;
    memset(&loop_addr, 0, sizeof(loop_addr));
    loop_addr.sin_family = AF_INET;
    loop_addr.sin_port = htons(MM_DISCOVERY_PORT);
    inet_pton(AF_INET, "127.0.0.1", &loop_addr.sin_addr);
    sendto(dm->udp_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&loop_addr, sizeof(loop_addr));

    logger_log("Sent discovery broadcast (name=%s, port=%u, instance_id=%llu)",
               dm->local_name, dm->local_port, (unsigned long long)dm->local_instance_id);
}

int discovery_recv_tick(DiscoveryManager *dm) {
    if (!dm || dm->udp_fd < 0) return 0;

    int changed = 0;
    while (1) {
        DiscoveryPacket pkt;
        struct sockaddr_in src_addr;
        socklen_t addr_len = sizeof(src_addr);

        ssize_t n = recvfrom(dm->udp_fd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&src_addr, &addr_len);
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                break;
            }
            break;
        }

        if ((size_t)n < sizeof(DiscoveryPacket)) {
            continue;
        }

        if (ntohl(pkt.magic) != MM_MAGIC_DISCOVERY) {
            /* Ignore non-matching magic packets */
            continue;
        }

        /* Filter self-broadcast: ignore if matching local instance ID or port */
        if (pkt.instance_id == dm->local_instance_id || ntohs(pkt.listen_port) == dm->local_port) {
            continue;
        }

        uint16_t peer_port = ntohs(pkt.listen_port);
        char src_ip[64];
        inet_ntop(AF_INET, &src_addr.sin_addr, src_ip, sizeof(src_ip));

        pkt.name[sizeof(pkt.name) - 1] = '\0';
        time_t now = time(NULL);

        /* Match existing peer strictly by unique instance_id */
        int found_idx = -1;
        for (int i = 0; i < dm->player_count; i++) {
            if (dm->players[i].instance_id == pkt.instance_id) {
                found_idx = i;
                break;
            }
        }

        if (found_idx >= 0) {
            dm->players[found_idx].last_seen = now;
            /* Prioritize real LAN IP over loopback for cross-machine accessibility */
            if (strcmp(src_ip, "127.0.0.1") != 0 || strcmp(dm->players[found_idx].ip, "127.0.0.1") == 0) {
                strncpy(dm->players[found_idx].ip, src_ip, sizeof(dm->players[found_idx].ip) - 1);
            }
            dm->players[found_idx].port = peer_port;
            strncpy(dm->players[found_idx].name, pkt.name, sizeof(dm->players[found_idx].name) - 1);
            dm->players[found_idx].cost_cutting = pkt.cost_cutting;
        } else if (dm->player_count < MM_MAX_PLAYERS) {
            int idx = dm->player_count++;
            dm->players[idx].id = dm->next_id++;
            dm->players[idx].instance_id = pkt.instance_id;
            strncpy(dm->players[idx].name, pkt.name, sizeof(dm->players[idx].name) - 1);
            dm->players[idx].name[sizeof(dm->players[idx].name) - 1] = '\0';
            strncpy(dm->players[idx].ip, src_ip, sizeof(dm->players[idx].ip) - 1);
            dm->players[idx].port = peer_port;
            dm->players[idx].cost_cutting = pkt.cost_cutting;
            dm->players[idx].last_seen = now;
            changed = 1;
            logger_log("Discovered new peer ID=%d, Name=%s, IP=%s:%u",
                       dm->players[idx].id, dm->players[idx].name, src_ip, peer_port);
        }
    }

    return changed;
}

int discovery_prune_expired(DiscoveryManager *dm) {
    if (!dm) return 0;

    time_t now = time(NULL);
    int changed = 0;
    int i = 0;

    while (i < dm->player_count) {
        if (now - dm->players[i].last_seen >= MM_PEER_TIMEOUT_SEC) {
            logger_log("Pruned inactive peer ID=%d, Name=%s (%s:%u)",
                       dm->players[i].id, dm->players[i].name, dm->players[i].ip, dm->players[i].port);
            /* Remove by shifting remaining entries left */
            for (int j = i; j < dm->player_count - 1; j++) {
                dm->players[j] = dm->players[j + 1];
            }
            dm->player_count--;
            changed = 1;
        } else {
            i++;
        }
    }

    /* Re-index active players to 1..count so IDs remain contiguous */
    for (int k = 0; k < dm->player_count; k++) {
        dm->players[k].id = k + 1;
    }
    dm->next_id = dm->player_count + 1;

    return changed;
}

const DiscoveredPlayer *discovery_find_by_id(const DiscoveryManager *dm, int id) {
    if (!dm) return NULL;
    for (int i = 0; i < dm->player_count; i++) {
        if (dm->players[i].id == id) {
            return &dm->players[i];
        }
    }
    return NULL;
}

void discovery_close(DiscoveryManager *dm) {
    if (!dm) return;
    if (dm->udp_fd >= 0) {
        close(dm->udp_fd);
        dm->udp_fd = -1;
    }
}
