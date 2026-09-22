// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

static const char *const owned_pin_paths[STARSEAD_PIN_COUNT] = {
    [STARSEAD_PIN_MATCHER_OUTPUT_V4] =
        "/sys/fs/bpf/starsea/xt_output_v4",
    [STARSEAD_PIN_MATCHER_OUTPUT_V6] =
        "/sys/fs/bpf/starsea/xt_output_v6",
    [STARSEAD_PIN_MATCHER_PREROUTING_V4] =
        "/sys/fs/bpf/starsea/xt_prerouting_v4",
    [STARSEAD_PIN_MATCHER_PREROUTING_V6] =
        "/sys/fs/bpf/starsea/xt_prerouting_v6",
    [STARSEAD_PIN_BPF2SOCKS_LOCAL_ADDRESS_V4] =
        "/sys/fs/bpf/starsea/bpf2socks/local_addr_v4",
    [STARSEAD_PIN_BPF2SOCKS_LOCAL_ADDRESS_V6] =
        "/sys/fs/bpf/starsea/bpf2socks/local_addr_v6",
    [STARSEAD_PIN_BPF2SOCKS_TC_INGRESS] =
        "/sys/fs/bpf/starsea/bpf2socks/tc_ingress",
    [STARSEAD_PIN_BPF2SOCKS_TC_EGRESS] =
        "/sys/fs/bpf/starsea/bpf2socks/tc_egress",
};

static const struct starsead_owned_chain owned_chains[] = {
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TPROXY_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TPROXY_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_LOCAL4_BEGIN"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_LOCAL4_END"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TUN_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TUN_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_FILTER,
        "ASTERISK_TUN_FORWARD"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_NAT,
        "STARSEA_FAKE_IP_ICMP"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_NAT,
        "STARSEA_FAKE_IP_ICMP_PRE"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TPROXY6_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TPROXY6_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_LOCAL6_BEGIN"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_LOCAL6_END"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TUN6_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "ASTERISK_TUN6_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_FILTER,
        "ASTERISK_TUN6_FORWARD"},
};

static const struct starsead_owned_hook owned_hooks[] = {
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "PREROUTING", false, "ASTERISK_TPROXY_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "OUTPUT", false, "ASTERISK_TPROXY_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "PREROUTING", false, "ASTERISK_TPROXY6_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "OUTPUT", false, "ASTERISK_TPROXY6_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "PREROUTING", false, "ASTERISK_TUN_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        "OUTPUT", false, "ASTERISK_TUN_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_FILTER,
        "FORWARD", false, "ASTERISK_TUN_FORWARD"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "PREROUTING", false, "ASTERISK_TUN6_PREROUTING"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "OUTPUT", false, "ASTERISK_TUN6_OUTPUT"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_FILTER,
        "FORWARD", false, "ASTERISK_TUN6_FORWARD"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_NAT,
        "OUTPUT", false, "STARSEA_FAKE_IP_ICMP"},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_NAT,
        "PREROUTING", false, "STARSEA_FAKE_IP_ICMP_PRE"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        "PREROUTING", true, "DROP"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_FILTER,
        "INPUT", true, "REJECT"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_FILTER,
        "FORWARD", true, "REJECT"},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_FILTER,
        "OUTPUT", true, "REJECT"},
};

static const struct starsead_owned_policy_rule owned_policy_rules[] = {
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_TPROXY_TABLE,
        STARSEAD_ROUTE_RULE_PRIORITY, STARSEAD_PRIMARY_MARK,
        STARSEAD_MARK_MASK, false},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_TPROXY_TABLE,
        STARSEAD_ROUTE_RULE_PRIORITY, STARSEAD_PRIMARY_MARK,
        STARSEAD_MARK_MASK, false},
    {STARSEAD_IP_FAMILY_IPV4, STARSEAD_TUN_TABLE,
        STARSEAD_ROUTE_RULE_PRIORITY, STARSEAD_PRIMARY_MARK,
        STARSEAD_MARK_MASK, false},
    {STARSEAD_IP_FAMILY_IPV6, STARSEAD_TUN_TABLE,
        STARSEAD_ROUTE_RULE_PRIORITY, STARSEAD_PRIMARY_MARK,
        STARSEAD_MARK_MASK, false},
};

static const struct starsead_owned_resource_catalog owned_resource_catalog = {
    .bpf_root = "/sys/fs/bpf/starsea",
    .b2s_root = "/sys/fs/bpf/starsea/bpf2socks",
    .fake_dns_output_chain = "STARSEA_FAKE_IP_ICMP",
    .fake_dns_prerouting_chain = "STARSEA_FAKE_IP_ICMP_PRE",
    .chains = owned_chains,
    .chain_count = sizeof(owned_chains) / sizeof(owned_chains[0]),
    .hooks = owned_hooks,
    .hook_count = sizeof(owned_hooks) / sizeof(owned_hooks[0]),
    .policy_rules = owned_policy_rules,
    .policy_rule_count =
        sizeof(owned_policy_rules) / sizeof(owned_policy_rules[0]),
    .token_route = {
        .family = STARSEAD_IP_FAMILY_IPV6,
        .table = 255U,
        .destination = "fd7a:7374:6572:6973::/64",
        .interface_name = "lo",
    },
};

const struct starsead_owned_resource_catalog *starsead_owned_resource_catalog(void) {
    return &owned_resource_catalog;
}

const char *starsead_owned_pin_path(enum starsead_pin_id pin_id) {
    if ((unsigned int)pin_id >= STARSEAD_PIN_COUNT) return NULL;
    return owned_pin_paths[pin_id];
}
