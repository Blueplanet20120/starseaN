// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0
#ifndef STARSEAD_DNS_H
#define STARSEAD_DNS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct starsead_config;

/* AEN1 control frame, see enhance/src/main/module/docs/control-protocol.md. */
#define STARSEAD_DNS_FRAME_MAGIC 0x314e4541
#define STARSEAD_DNS_FRAME_VERSION 1
#define STARSEAD_DNS_HEADER_SIZE 40
#define STARSEAD_DNS_COMMAND_TIMEOUT_MILLIS 2000
enum starsead_dns_opcode {
    STARSEAD_DNS_OP_HELLO = 1,
    STARSEAD_DNS_OP_PREPARE = 2,
    STARSEAD_DNS_OP_ACTIVATE = 3,
    STARSEAD_DNS_OP_CHECK = 4,
    STARSEAD_DNS_OP_DEACTIVATE = 5,
    STARSEAD_DNS_OP_RELEASE = 6,
    STARSEAD_DNS_OP_ACK = 7,
    STARSEAD_DNS_OP_ERROR = 8,
    STARSEAD_DNS_OP_CAPABILITIES = 9,
    STARSEAD_DNS_OP_INFO = 10,
    STARSEAD_DNS_OP_INFO_RESPONSE = 11,
    STARSEAD_DNS_OP_NETWORK_CHANGED = 12,
};
enum starsead_dns_result {
    STARSEAD_DNS_RESULT_OK = 0,
    STARSEAD_DNS_RESULT_INVALID = 1,
    STARSEAD_DNS_RESULT_UNSUPPORTED = 2,
    STARSEAD_DNS_RESULT_STALE = 3,
    STARSEAD_DNS_RESULT_BUSY = 4,
    STARSEAD_DNS_RESULT_CACHE_ERROR = 5,
    STARSEAD_DNS_RESULT_DRAINING = 6,
    STARSEAD_DNS_RESULT_EXHAUSTED = 7,
    STARSEAD_DNS_RESULT_IO_ERROR = 8,
};
/* Module-side policy phase reported in ACK/ERROR bodies. */
enum starsead_dns_module_phase {
    STARSEAD_DNS_MODULE_IDLE = 0,
    STARSEAD_DNS_MODULE_PREPARED = 1,
    STARSEAD_DNS_MODULE_ACTIVE = 2,
    STARSEAD_DNS_MODULE_DRAINING = 3,
};

enum starsead_dns_phase {
    STARSEAD_DNS_PHASE_DISABLED,
    STARSEAD_DNS_PHASE_PREPARING,
    STARSEAD_DNS_PHASE_PREPARED,
    STARSEAD_DNS_PHASE_ACTIVE,
    STARSEAD_DNS_PHASE_INACTIVE,
    STARSEAD_DNS_PHASE_DRAINING,
    STARSEAD_DNS_PHASE_UNSUPPORTED,
    STARSEAD_DNS_PHASE_FAILED,
    STARSEAD_DNS_PHASE_WAITING_NETWORK
};
static inline bool starsead_dns_serving(enum starsead_dns_phase phase) {
    return phase == STARSEAD_DNS_PHASE_ACTIVE || phase == STARSEAD_DNS_PHASE_WAITING_NETWORK;
}
struct starsead_dns_snapshot {
    bool required;
    enum starsead_dns_phase phase;
    uint64_t instance_id, cycle_id, generation;
    char reason[128];
};
#define STARSEAD_DNS_MAX_ROUTES 64U
struct starsead_dns_route {
    uint32_t netid, mark, family, scope;
    unsigned char address[16];
};
struct starsead_dns_client {
    struct starsead_dns_snapshot snapshot;
    int fd;
    uint32_t request_id, result, retained;
    uint16_t opcode;
    uint64_t netd, deadline;
    bool pending, complete, connected, suspended, busy, topology_changed, network_changed;
    bool prepare_retry;
    // PREPARE answered BUSY while the previous session drains; transient.
    bool prepare_draining;
    // Module reports release progress with NETWORK_CHANGED; no drain timer.
    bool drain_events;
    unsigned char *tx;
    size_t tx_size, tx_offset, rx_size;
    unsigned char rx[68 + 32 * STARSEAD_DNS_MAX_ROUTES];
    size_t route_count;
    struct starsead_dns_route routes[STARSEAD_DNS_MAX_ROUTES];
};
void starsead_dns_init(struct starsead_dns_client *);
void starsead_dns_close(struct starsead_dns_client *);
int starsead_dns_begin(struct starsead_dns_client *, uint64_t, uint64_t, uint64_t, uint64_t);
int starsead_dns_command(
    struct starsead_dns_client *, uint16_t, const struct starsead_config *, uint64_t);
int starsead_dns_step(struct starsead_dns_client *, uint64_t);
int starsead_dns_accept(struct starsead_dns_client *, const unsigned char *, size_t, uint64_t);
void starsead_dns_fail(struct starsead_dns_client *, const char *);
uint64_t starsead_dns_monotime(void);
int starsead_dns_info(void);
#endif
