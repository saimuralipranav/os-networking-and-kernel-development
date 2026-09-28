#ifndef MASTERMIND_UDP_TRANSPORT_H
#define MASTERMIND_UDP_TRANSPORT_H

#include "protocol.h"
#include <netinet/in.h>
#include <stdint.h>
#include <time.h>

#define MM_RETRANSMIT_TIMEOUT_MS 100
#define MM_MAX_IN_FLIGHT_CHUNKS  16
#define MM_MAX_CHUNKS_PER_MSG    16

typedef struct {
    uint16_t seq_num;
    uint16_t total_chunks;
    uint32_t msg_id;
    uint16_t payload_len;
    char payload[MM_CHUNK_SIZE];
    uint64_t last_sent_ms;
    int acked;
} InFlightChunk;

typedef struct {
    int active;
    uint32_t msg_id;
    MessageType type;
    int total_chunks;
    int chunks_received;
    char chunks[MM_MAX_CHUNKS_PER_MSG][MM_CHUNK_SIZE];
    uint16_t chunk_lens[MM_MAX_CHUNKS_PER_MSG];
    int chunk_present[MM_MAX_CHUNKS_PER_MSG];
    time_t start_time;
} ReassemblyBuffer;

typedef struct {
    int udp_fd;
    uint16_t local_port;
    struct sockaddr_in peer_addr;
    int has_peer;

    uint32_t next_msg_id;

    /* Outgoing in-flight chunks awaiting ACK */
    InFlightChunk out_chunks[MM_MAX_IN_FLIGHT_CHUNKS];
    int out_chunk_count;

    /* Incoming message reassembly buffer */
    ReassemblyBuffer in_buf;

    time_t last_peer_contact;
    time_t last_heartbeat_sent;
} UdpTransportManager;

/**
 * Initializes UDP transport socket on the specified port.
 */
int udp_transport_init(UdpTransportManager *ut, uint16_t port);

/**
 * Sets the remote peer's address for state transfer.
 */
void udp_transport_set_peer(UdpTransportManager *ut, const char *ip, uint16_t port);

/**
 * Sends a game message using reliable chunked UDP transport with pipelining.
 * Returns 0 on success, -1 on error.
 */
int udp_transport_send_msg(UdpTransportManager *ut, MessageType type, const char *text);

/**
 * Sweeps unacknowledged in-flight chunks and retransmits any that exceeded 100 ms.
 */
void udp_transport_retransmit_tick(UdpTransportManager *ut);

/**
 * Returns the milliseconds until the next 100ms retransmission timer expires (for poll timeout).
 */
int udp_transport_get_poll_timeout_ms(const UdpTransportManager *ut);

/**
 * Processes incoming UDP packets (chunks and ACKs).
 * Returns 1 if a full message was completed and assembled, 0 if still waiting, -1 on error.
 */
int udp_transport_recv_tick(UdpTransportManager *ut, GameMessage *out_msg);

/**
 * Checks if the remote peer has timed out (disconnection detection for UDP).
 * Returns 1 if peer disconnected (no traffic for > 5 seconds), 0 otherwise.
 */
int udp_transport_check_peer_timeout(UdpTransportManager *ut);

/**
 * Closes the UDP transport socket.
 */
void udp_transport_close(UdpTransportManager *ut);

#endif /* MASTERMIND_UDP_TRANSPORT_H */
