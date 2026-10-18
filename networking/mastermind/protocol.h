#ifndef MASTERMIND_PROTOCOL_H
#define MASTERMIND_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define MM_MAGIC_DISCOVERY 0x4D4D4E44  /* "MMND" */
#define MM_MAGIC_CHUNK     0x4D4D434B  /* "MMCK" */
#define MM_MAGIC_ACK       0x4D4D414B  /* "MMAK" */

#define MM_DISCOVERY_PORT  34567
#define MM_MAX_NAME_LEN    32
#define MM_SEQ_LEN         5
#define MM_MAX_ATTEMPTS    12
#define MM_CHUNK_SIZE      256

/* Message Opcode Verbs */
typedef enum {
    MSG_NONE = 0,
    MSG_CHALLENGE = 1,
    MSG_ACCEPT = 2,
    MSG_REJECT = 3,
    MSG_ATTEMPT = 4,
    MSG_FEEDBACK = 5,
    MSG_END = 6,
    MSG_DISCONNECT = 7,
    MSG_HEARTBEAT = 8,
    MSG_START = 9
} MessageType;

/* UDP Discovery Broadcast Packet */
typedef struct __attribute__((packed)) {
    uint32_t magic;                       /* MM_MAGIC_DISCOVERY */
    uint64_t instance_id;                 /* Globally unique process instance ID */
    uint16_t listen_port;                 /* Port peer is listening on (network byte order) */
    uint8_t cost_cutting;                 /* 1 if peer is in cost-cutting mode, 0 if TCP */
    char name[MM_MAX_NAME_LEN];           /* Null-terminated player name */
} DiscoveryPacket;

/* Reliable UDP Data Chunk Packet (--cost-cutting) */
typedef struct __attribute__((packed)) {
    uint32_t magic;                       /* MM_MAGIC_CHUNK */
    uint32_t msg_id;                      /* Unique message identifier */
    uint16_t seq_num;                     /* Chunk index (0 .. total_chunks - 1) */
    uint16_t total_chunks;                /* Total number of chunks in this message */
    uint16_t payload_len;                 /* Number of valid data bytes in payload */
    char payload[MM_CHUNK_SIZE];          /* Chunk payload data */
} UdpChunkPacket;

/* Reliable UDP ACK Packet (--cost-cutting) */
typedef struct __attribute__((packed)) {
    uint32_t magic;                       /* MM_MAGIC_ACK */
    uint32_t msg_id;                      /* Message identifier being acknowledged */
    uint16_t seq_num;                     /* Chunk index being acknowledged */
} UdpAckPacket;

/* Structured Game Message (exchanged via TCP or reassembled UDP) */
typedef struct {
    MessageType type;
    char text[512];
} GameMessage;

#endif /* MASTERMIND_PROTOCOL_H */
