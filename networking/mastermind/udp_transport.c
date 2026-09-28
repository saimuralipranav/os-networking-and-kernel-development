#include "udp_transport.h"
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

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int udp_transport_init(UdpTransportManager *ut, uint16_t port) {
    if (!ut) return -1;
    memset(ut, 0, sizeof(*ut));
    ut->next_msg_id = 1;

    ut->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ut->udp_fd < 0) {
        return -1;
    }

    int opt_reuse = 1;
    setsockopt(ut->udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse, sizeof(opt_reuse));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(ut->udp_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        close(ut->udp_fd);
        ut->udp_fd = -1;
        return -1;
    }

    /* Retrieve dynamically assigned port if 0 was passed */
    struct sockaddr_in actual_addr;
    socklen_t actual_len = sizeof(actual_addr);
    if (getsockname(ut->udp_fd, (struct sockaddr *)&actual_addr, &actual_len) == 0) {
        ut->local_port = ntohs(actual_addr.sin_port);
    } else {
        ut->local_port = port;
    }

    /* Non-blocking socket */
    int flags = fcntl(ut->udp_fd, F_GETFL, 0);
    fcntl(ut->udp_fd, F_SETFL, flags | O_NONBLOCK);

    logger_log("Reliable UDP transport bound to port %u", ut->local_port);
    return 0;
}

void udp_transport_set_peer(UdpTransportManager *ut, const char *ip, uint16_t port) {
    if (!ut) return;
    memset(&ut->peer_addr, 0, sizeof(ut->peer_addr));
    ut->peer_addr.sin_family = AF_INET;
    ut->peer_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &ut->peer_addr.sin_addr);
    ut->has_peer = 1;
    ut->last_peer_contact = time(NULL);
    ut->last_heartbeat_sent = time(NULL);
    logger_log("UDP peer configured: %s:%u", ip, port);
}

int udp_transport_send_msg(UdpTransportManager *ut, MessageType type, const char *text) {
    if (!ut || !ut->has_peer || ut->udp_fd < 0) return -1;

    size_t text_len = text ? strlen(text) : 0;
    /* Format message content as: [1-byte opcode][text] */
    char full_payload[1024];
    full_payload[0] = (char)type;
    if (text_len > 0) {
        if (text_len > sizeof(full_payload) - 2) {
            text_len = sizeof(full_payload) - 2;
        }
        memcpy(full_payload + 1, text, text_len);
    }
    size_t total_payload_len = 1 + text_len;

    /* Calculate chunks */
    size_t chunk_data_capacity = 64; /* fixed chunk size per requirements */
    int total_chunks = (int)((total_payload_len + chunk_data_capacity - 1) / chunk_data_capacity);
    if (total_chunks <= 0) total_chunks = 1;
    if (total_chunks > MM_MAX_IN_FLIGHT_CHUNKS) total_chunks = MM_MAX_IN_FLIGHT_CHUNKS;

    uint32_t msg_id = ut->next_msg_id++;
    uint64_t now_ms = get_time_ms();

    ut->out_chunk_count = total_chunks;

    /* Populate and pipeline all chunks immediately without waiting for ACK */
    for (int i = 0; i < total_chunks; i++) {
        size_t offset = (size_t)i * chunk_data_capacity;
        size_t len = total_payload_len - offset;
        if (len > chunk_data_capacity) {
            len = chunk_data_capacity;
        }

        InFlightChunk *c = &ut->out_chunks[i];
        c->msg_id = msg_id;
        c->seq_num = (uint16_t)i;
        c->total_chunks = (uint16_t)total_chunks;
        c->payload_len = (uint16_t)len;
        memcpy(c->payload, full_payload + offset, len);
        c->last_sent_ms = now_ms;
        c->acked = 0;

        UdpChunkPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.magic = htonl(MM_MAGIC_CHUNK);
        pkt.msg_id = htonl(c->msg_id);
        pkt.seq_num = htons(c->seq_num);
        pkt.total_chunks = htons(c->total_chunks);
        pkt.payload_len = htons(c->payload_len);
        memcpy(pkt.payload, c->payload, len);

        sendto(ut->udp_fd, &pkt, sizeof(pkt), 0,
               (struct sockaddr *)&ut->peer_addr, sizeof(ut->peer_addr));
        logger_log("UDP sent chunk seq=%u/%u msg_id=%u len=%u",
                   c->seq_num, c->total_chunks, c->msg_id, c->payload_len);
    }

    return 0;
}

void udp_transport_retransmit_tick(UdpTransportManager *ut) {
    if (!ut || !ut->has_peer || ut->udp_fd < 0 || ut->out_chunk_count == 0) return;

    uint64_t now_ms = get_time_ms();
    for (int i = 0; i < ut->out_chunk_count; i++) {
        InFlightChunk *c = &ut->out_chunks[i];
        if (!c->acked) {
            if (now_ms - c->last_sent_ms >= MM_RETRANSMIT_TIMEOUT_MS) {
                /* 0.1s expired without ACK: Retransmit chunk */
                c->last_sent_ms = now_ms;

                UdpChunkPacket pkt;
                memset(&pkt, 0, sizeof(pkt));
                pkt.magic = htonl(MM_MAGIC_CHUNK);
                pkt.msg_id = htonl(c->msg_id);
                pkt.seq_num = htons(c->seq_num);
                pkt.total_chunks = htons(c->total_chunks);
                pkt.payload_len = htons(c->payload_len);
                memcpy(pkt.payload, c->payload, c->payload_len);

                sendto(ut->udp_fd, &pkt, sizeof(pkt), 0,
                       (struct sockaddr *)&ut->peer_addr, sizeof(ut->peer_addr));
                logger_log("UDP retransmitted chunk seq=%u/%u msg_id=%u",
                           c->seq_num, c->total_chunks, c->msg_id);
            }
        }
    }
}

int udp_transport_get_poll_timeout_ms(const UdpTransportManager *ut) {
    if (!ut || ut->out_chunk_count == 0) {
        return 500; /* Default idle poll timeout */
    }

    uint64_t now_ms = get_time_ms();
    uint64_t min_wait_ms = MM_RETRANSMIT_TIMEOUT_MS;

    for (int i = 0; i < ut->out_chunk_count; i++) {
        const InFlightChunk *c = &ut->out_chunks[i];
        if (!c->acked) {
            uint64_t elapsed = now_ms - c->last_sent_ms;
            if (elapsed >= MM_RETRANSMIT_TIMEOUT_MS) {
                return 0; /* Retransmission immediately due */
            }
            uint64_t rem = MM_RETRANSMIT_TIMEOUT_MS - elapsed;
            if (rem < min_wait_ms) {
                min_wait_ms = rem;
            }
        }
    }

    return (int)min_wait_ms;
}

int udp_transport_recv_tick(UdpTransportManager *ut, GameMessage *out_msg) {
    if (!ut || ut->udp_fd < 0) return 0;

    int message_completed = 0;

    while (1) {
        char buf[1024];
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);

        ssize_t n = recvfrom(ut->udp_fd, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
                break;
            }
            break;
        }

        if ((size_t)n < 4) {
            continue;
        }

        uint32_t magic = ntohl(*(uint32_t *)buf);

        if (magic == MM_MAGIC_ACK && (size_t)n >= sizeof(UdpAckPacket)) {
            /* Received ACK for a chunk */
            UdpAckPacket *ack = (UdpAckPacket *)buf;
            uint32_t ack_msg_id = ntohl(ack->msg_id);
            uint16_t ack_seq = ntohs(ack->seq_num);

            ut->last_peer_contact = time(NULL);

            for (int i = 0; i < ut->out_chunk_count; i++) {
                if (ut->out_chunks[i].msg_id == ack_msg_id && ut->out_chunks[i].seq_num == ack_seq) {
                    ut->out_chunks[i].acked = 1;
                    logger_log("UDP received ACK for seq=%u msg_id=%u", ack_seq, ack_msg_id);
                    break;
                }
            }

            /* Check if all chunks in current transmission are acknowledged */
            int all_acked = 1;
            for (int i = 0; i < ut->out_chunk_count; i++) {
                if (!ut->out_chunks[i].acked) {
                    all_acked = 0;
                    break;
                }
            }
            if (all_acked) {
                ut->out_chunk_count = 0;
            }
        } else if (magic == MM_MAGIC_CHUNK && (size_t)n >= sizeof(UdpChunkPacket)) {
            /* Received Data Chunk */
            UdpChunkPacket *chk = (UdpChunkPacket *)buf;
            uint32_t msg_id = ntohl(chk->msg_id);
            uint16_t seq = ntohs(chk->seq_num);
            uint16_t total = ntohs(chk->total_chunks);
            uint16_t len = ntohs(chk->payload_len);

            ut->last_peer_contact = time(NULL);

            /* Send immediate ACK for this chunk referencing seq_num */
            UdpAckPacket ack;
            memset(&ack, 0, sizeof(ack));
            ack.magic = htonl(MM_MAGIC_ACK);
            ack.msg_id = htonl(msg_id);
            ack.seq_num = htons(seq);

            sendto(ut->udp_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&src, src_len);
            logger_log("UDP received chunk seq=%u/%u msg_id=%u, sent ACK", seq, total, msg_id);

            /* Aggregate into reassembly buffer */
            if (!ut->in_buf.active || ut->in_buf.msg_id != msg_id) {
                /* Start fresh message reassembly */
                memset(&ut->in_buf, 0, sizeof(ut->in_buf));
                ut->in_buf.active = 1;
                ut->in_buf.msg_id = msg_id;
                ut->in_buf.total_chunks = total;
                ut->in_buf.start_time = time(NULL);
            }

            if (seq < MM_MAX_CHUNKS_PER_MSG && !ut->in_buf.chunk_present[seq]) {
                if (len > MM_CHUNK_SIZE) len = MM_CHUNK_SIZE;
                memcpy(ut->in_buf.chunks[seq], chk->payload, len);
                ut->in_buf.chunk_lens[seq] = len;
                ut->in_buf.chunk_present[seq] = 1;
                ut->in_buf.chunks_received++;
            }

            /* Check if all chunks have arrived */
            if (ut->in_buf.chunks_received >= ut->in_buf.total_chunks && !message_completed) {
                /* Reorder and assemble full message */
                char assembled[1024];
                size_t offset = 0;
                for (int s = 0; s < ut->in_buf.total_chunks; s++) {
                    size_t clen = ut->in_buf.chunk_lens[s];
                    if (offset + clen < sizeof(assembled)) {
                        memcpy(assembled + offset, ut->in_buf.chunks[s], clen);
                        offset += clen;
                    }
                }

                if (offset > 0 && out_msg) {
                    out_msg->type = (MessageType)(unsigned char)assembled[0];
                    size_t text_bytes = offset - 1;
                    if (text_bytes > sizeof(out_msg->text) - 1) {
                        text_bytes = sizeof(out_msg->text) - 1;
                    }
                    memcpy(out_msg->text, assembled + 1, text_bytes);
                    out_msg->text[text_bytes] = '\0';
                    message_completed = 1;
                    logger_log("UDP reassembled complete message type=%d, len=%zu",
                               (int)out_msg->type, text_bytes);
                }

                ut->in_buf.active = 0; /* Reset reassembly buffer */
            }
        }
    }

    return message_completed;
}

int udp_transport_check_peer_timeout(UdpTransportManager *ut) {
    if (!ut || !ut->has_peer) return 0;

    time_t now = time(NULL);
    /* Send periodic keepalive heartbeat every 1 second */
    if (now - ut->last_heartbeat_sent >= 1) {
        ut->last_heartbeat_sent = now;
        UdpAckPacket hb;
        memset(&hb, 0, sizeof(hb));
        hb.magic = htonl(MM_MAGIC_ACK);
        hb.msg_id = 0;
        hb.seq_num = 0xFFFF;
        sendto(ut->udp_fd, &hb, sizeof(hb), 0, (struct sockaddr *)&ut->peer_addr, sizeof(ut->peer_addr));
    }

    /* If no packet received from peer for 5 seconds, declare disconnected */
    if (now - ut->last_peer_contact >= 5) {
        return 1;
    }
    return 0;
}

void udp_transport_close(UdpTransportManager *ut) {
    if (!ut) return;
    if (ut->udp_fd >= 0) {
        close(ut->udp_fd);
        ut->udp_fd = -1;
    }
    ut->has_peer = 0;
}
