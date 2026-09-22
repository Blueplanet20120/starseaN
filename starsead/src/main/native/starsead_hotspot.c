// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

enum starsead_hotspot_ipv6_offload_policy starsead_hotspot_ipv6_offload_policy_for(
    const struct starsead_config *config) {
    if (config == NULL || config->hotspot_interface_prefix_count == 0U ||
        config->mode == STARSEAD_MODE_EBPF) return STARSEAD_HOTSPOT_IPV6_OFFLOAD_KEEP;
    if (config->mode == STARSEAD_MODE_BPF2SOCKS) {
        return STARSEAD_HOTSPOT_IPV6_OFFLOAD_REMOVE_PREF1_REQUIRED;
    }
    if (config->mode == STARSEAD_MODE_TPROXY ||
        config->mode == STARSEAD_MODE_TUN ||
        config->mode == STARSEAD_MODE_TUN2SOCKS) {
        return STARSEAD_HOTSPOT_IPV6_OFFLOAD_REMOVE_PREF1_PREF2_BEST_EFFORT;
    }
    return STARSEAD_HOTSPOT_IPV6_OFFLOAD_KEEP;
}

bool starsead_hotspot_tc_output_has_android_offload(
    const void *output, size_t output_length) {
    static const char marker[] = "prog_offload_schedcls_tether_";
    const size_t marker_length = sizeof(marker) - 1U;
    if (output == NULL || output_length < marker_length) return false;
    const unsigned char *bytes = output;
    for (size_t offset = 0U; offset <= output_length - marker_length; ++offset) {
        size_t matched = 0U;
        while (matched < marker_length &&
            bytes[offset + matched] == (unsigned char)marker[matched]) ++matched;
        if (matched == marker_length) return true;
    }
    return false;
}
