// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <string.h>

static bool starsead_wifi_identity_valid(
    const struct starsead_wifi_identity *identity) {
    return identity != NULL &&
        (!identity->has_ssid ||
            (identity->ssid_length > 0U &&
                identity->ssid_length <= STARSEAD_MAX_WIFI_SSID_BYTES));
}

static bool starsead_wifi_identity_equal(
    const struct starsead_wifi_identity *left,
    const struct starsead_wifi_identity *right) {
    if (left->has_ssid != right->has_ssid ||
        left->has_bssid != right->has_bssid) {
        return false;
    }
    if (left->has_ssid &&
        (left->ssid_length != right->ssid_length ||
            memcmp(left->ssid, right->ssid, left->ssid_length) != 0)) {
        return false;
    }
    return !left->has_bssid ||
        memcmp(left->bssid, right->bssid, sizeof(left->bssid)) == 0;
}

void starsead_service_control_init(
    struct starsead_service_control_runtime *runtime,
    const struct starsead_service_control_config *config,
    bool service_running,
    time_t now) {
    if (runtime == NULL) return;
    memset(runtime, 0, sizeof(*runtime));
    runtime->config = config;
    runtime->desired_running = service_running;
    runtime->last_evaluated_time = now;
}

void starsead_service_control_set_service_running(
    struct starsead_service_control_runtime *runtime, bool running) {
    if (runtime == NULL) return;
    runtime->desired_running = running;
}

bool starsead_wifi_rule_matches(
    const struct starsead_wifi_rule_config *rule,
    const struct starsead_wifi_identity *identity) {
    size_t index;

    if (rule == NULL || rule->ssid_count > STARSEAD_MAX_WIFI_IDENTIFIERS ||
        rule->bssid_count > STARSEAD_MAX_WIFI_IDENTIFIERS) {
        return false;
    }
    if (rule->ssid_count == 0U && rule->bssid_count == 0U) return true;
    if (!starsead_wifi_identity_valid(identity)) return false;
    if (identity->has_ssid) {
        for (index = 0U; index < rule->ssid_count; ++index) {
            if (rule->ssids[index].length == identity->ssid_length &&
                memcmp(
                    rule->ssids[index].bytes,
                    identity->ssid,
                    identity->ssid_length) == 0) {
                return true;
            }
        }
    }
    if (identity->has_bssid) {
        for (index = 0U; index < rule->bssid_count; ++index) {
            if (memcmp(rule->bssids[index], identity->bssid, 6U) == 0) return true;
        }
    }
    return false;
}

static enum starsead_service_action starsead_service_control_apply(
    struct starsead_service_control_runtime *runtime,
    enum starsead_service_action action) {
    if (action == STARSEAD_SERVICE_ACTION_START) {
        if (runtime->desired_running) return STARSEAD_SERVICE_ACTION_NONE;
        runtime->desired_running = true;
        return action;
    }
    if (action == STARSEAD_SERVICE_ACTION_STOP) {
        if (!runtime->desired_running) return STARSEAD_SERVICE_ACTION_NONE;
        runtime->desired_running = false;
        return action;
    }
    return action;
}

static enum starsead_service_action starsead_service_control_rules(
    struct starsead_service_control_runtime *runtime,
    const struct starsead_wifi_rule_config *start,
    const struct starsead_wifi_rule_config *stop,
    const struct starsead_wifi_identity *identity) {
    bool stop_matches = stop->enabled && starsead_wifi_rule_matches(stop, identity);
    bool start_matches = start->enabled && starsead_wifi_rule_matches(start, identity);

    if (stop_matches) {
        return starsead_service_control_apply(
            runtime, STARSEAD_SERVICE_ACTION_STOP);
    }
    if (start_matches) {
        return starsead_service_control_apply(
            runtime, STARSEAD_SERVICE_ACTION_START);
    }
    return STARSEAD_SERVICE_ACTION_NONE;
}

static void starsead_service_control_baseline(
    struct starsead_service_control_runtime *runtime,
    bool connected,
    const struct starsead_wifi_identity *identity) {
    runtime->wifi_baseline_established = true;
    runtime->wifi_connected = connected;
    memset(&runtime->previous_wifi, 0, sizeof(runtime->previous_wifi));
    if (connected && starsead_wifi_identity_valid(identity)) {
        runtime->previous_wifi = *identity;
    }
}

enum starsead_service_action starsead_service_control_on_wifi(
    struct starsead_service_control_runtime *runtime,
    enum starsead_wifi_transition transition,
    const struct starsead_wifi_identity *identity) {
    struct starsead_wifi_identity disconnected_identity;
    const struct starsead_wifi_identity *match_identity = identity;
    bool connected_transition;
    bool duplicate;

    if (runtime == NULL) return STARSEAD_SERVICE_ACTION_NONE;
    connected_transition = transition == STARSEAD_WIFI_TRANSITION_CONNECTED ||
        transition == STARSEAD_WIFI_TRANSITION_ROAMED;
    if (transition == STARSEAD_WIFI_TRANSITION_BASELINE_CONNECTED ||
        transition == STARSEAD_WIFI_TRANSITION_BASELINE_DISCONNECTED) {
        bool connected = transition == STARSEAD_WIFI_TRANSITION_BASELINE_CONNECTED;
        starsead_service_control_baseline(runtime, connected, identity);
        if (runtime->config == NULL || !runtime->config->enabled ||
            !runtime->config->wifi.enabled) {
            return STARSEAD_SERVICE_ACTION_NONE;
        }
        if (connected) {
            if (!starsead_wifi_identity_valid(identity)) {
                return STARSEAD_SERVICE_ACTION_NONE;
            }
            return starsead_service_control_rules(
                runtime,
                &runtime->config->wifi.connect_start,
                &runtime->config->wifi.connect_stop,
                identity);
        }
        return starsead_service_control_rules(
            runtime,
            &runtime->config->wifi.disconnect_start,
            &runtime->config->wifi.disconnect_stop,
            identity);
    }
    if (!runtime->wifi_baseline_established) {
        starsead_service_control_baseline(runtime, connected_transition, identity);
        return STARSEAD_SERVICE_ACTION_NONE;
    }
    if (transition == STARSEAD_WIFI_TRANSITION_DISCONNECTED) {
        if (runtime->wifi_connected) {
            disconnected_identity = runtime->previous_wifi;
            match_identity = &disconnected_identity;
        }
        duplicate = !runtime->wifi_connected;
        runtime->wifi_connected = false;
        memset(&runtime->previous_wifi, 0, sizeof(runtime->previous_wifi));
    } else if (connected_transition) {
        if (!starsead_wifi_identity_valid(identity)) return STARSEAD_SERVICE_ACTION_NONE;
        duplicate = runtime->wifi_connected &&
            starsead_wifi_identity_equal(&runtime->previous_wifi, identity);
        runtime->wifi_connected = true;
        runtime->previous_wifi = *identity;
    } else {
        return STARSEAD_SERVICE_ACTION_NONE;
    }
    if (duplicate || runtime->config == NULL || !runtime->config->enabled ||
        !runtime->config->wifi.enabled) {
        return STARSEAD_SERVICE_ACTION_NONE;
    }
    if (connected_transition) {
        return starsead_service_control_rules(
            runtime,
            &runtime->config->wifi.connect_start,
            &runtime->config->wifi.connect_stop,
            match_identity);
    }
    return starsead_service_control_rules(
        runtime,
        &runtime->config->wifi.disconnect_start,
        &runtime->config->wifi.disconnect_stop,
        match_identity);
}

enum starsead_service_action starsead_service_control_reconcile_time(
    struct starsead_service_control_runtime *runtime, time_t now) {
    time_t latest_start = 0;
    time_t latest_stop = 0;
    bool has_start;
    bool has_stop;

    if (runtime == NULL) return STARSEAD_SERVICE_ACTION_NONE;
    if (now <= runtime->last_evaluated_time) {
        runtime->last_evaluated_time = now;
        return STARSEAD_SERVICE_ACTION_NONE;
    }
    if (runtime->config == NULL || !runtime->config->enabled ||
        !runtime->config->schedule.enabled) {
        runtime->last_evaluated_time = now;
        return STARSEAD_SERVICE_ACTION_NONE;
    }
    has_start = starsead_cron_latest_between(
        &runtime->config->schedule.start,
        runtime->last_evaluated_time,
        now,
        &latest_start) == 0;
    has_stop = starsead_cron_latest_between(
        &runtime->config->schedule.stop,
        runtime->last_evaluated_time,
        now,
        &latest_stop) == 0;
    runtime->last_evaluated_time = now;
    if (!has_start && !has_stop) return STARSEAD_SERVICE_ACTION_NONE;
    if (has_stop && (!has_start || latest_stop >= latest_start)) {
        return starsead_service_control_apply(
            runtime, STARSEAD_SERVICE_ACTION_STOP);
    }
    return starsead_service_control_apply(runtime, STARSEAD_SERVICE_ACTION_START);
}
