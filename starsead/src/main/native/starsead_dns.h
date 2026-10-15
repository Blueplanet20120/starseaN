// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0
#ifndef STARSEAD_DNS_H
#define STARSEAD_DNS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct starsead_config;
enum starsead_dns_phase { DNS_DISABLED, DNS_PREPARING, DNS_PREPARED, DNS_ACTIVE,
    DNS_INACTIVE, DNS_DRAINING, DNS_UNSUPPORTED, DNS_FAILED, DNS_WAITING_NETWORK };
static inline bool starsead_dns_serving(enum starsead_dns_phase phase) {
    return phase == DNS_ACTIVE || phase == DNS_WAITING_NETWORK;
}
struct starsead_dns_snapshot {
    bool required;
    enum starsead_dns_phase phase;
    uint64_t instance_id, cycle_id, generation;
    char reason[128];
};
#define STARSEAD_DNS_MAX_ROUTES 64U
struct starsead_dns_route { uint32_t netid, mark, family, scope; unsigned char address[16]; };
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
    unsigned char rx[68+32*STARSEAD_DNS_MAX_ROUTES];
    size_t route_count;
    struct starsead_dns_route routes[STARSEAD_DNS_MAX_ROUTES];
};
void starsead_dns_init(struct starsead_dns_client *);
void starsead_dns_close(struct starsead_dns_client *);
int starsead_dns_begin(struct starsead_dns_client *, uint64_t, uint64_t, uint64_t, uint64_t);
int starsead_dns_command(struct starsead_dns_client *, uint16_t,
    const struct starsead_config *, uint64_t);
int starsead_dns_step(struct starsead_dns_client *, uint64_t);
int starsead_dns_accept(struct starsead_dns_client *, const unsigned char *, size_t, uint64_t);
void starsead_dns_fail(struct starsead_dns_client *, const char *);
uint64_t starsead_dns_monotime(void);
int starsead_dns_info(void);
#endif
