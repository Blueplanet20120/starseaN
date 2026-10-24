// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead_supervisor.h"

#if defined(__linux__) || defined(__ANDROID__)

static int system_apply_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect) {
    char table[16U];
    char priority[16U];
    char mark[32U];
    if (snprintf(table, sizeof(table), "%" PRIu32, effect->table) <= 0 ||
        snprintf(priority, sizeof(priority), "%" PRIu32, effect->priority) <= 0 ||
        snprintf(mark, sizeof(mark), "0x%08" PRIx32 "/0x%08" PRIx32,
            effect->mark, effect->mark_mask) <= 0) return -1;
    if (effect->kind == STARSEAD_ROUTE_EFFECT_IP_RULE) {
        const char *normal[] = {"rule", "add", "priority", priority,
            "fwmark", mark, "table", table};
        const char *inverted[] = {"rule", "add", "priority", priority,
            "not", "from", "all", "fwmark", mark, "table", table};
        return starsead_supervisor_ip_zero(system, effect->family,
            effect->invert_from_all ? inverted : normal,
            effect->invert_from_all ? sizeof(inverted) / sizeof(inverted[0]) :
                sizeof(normal) / sizeof(normal[0]));
    }
    if (effect->kind == STARSEAD_ROUTE_EFFECT_ROUTE) {
        if (effect->loopback_source) {
            // Early DNS marks select the local route before IPv4 source selection.
            const char *arguments[] = {"route", "add", "local", effect->destination,
                "dev", effect->interface_name, "table", table, "src", "127.0.0.1"};
            return starsead_supervisor_ip_zero(system, effect->family, arguments,
                sizeof(arguments) / sizeof(arguments[0]));
        }
        const char *normal[] = {"route", "add", effect->destination,
            "dev", effect->interface_name, "table", table};
        const char *local[] = {"route", "add", "local", effect->destination,
            "dev", effect->interface_name, "table", table};
        return starsead_supervisor_ip_zero(system, effect->family,
            effect->local_route ? local : normal,
            effect->local_route ? sizeof(local) / sizeof(local[0]) :
                sizeof(normal) / sizeof(normal[0]));
    }
    return -1;
}

static int system_probe_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect, bool *present);
int starsead_supervisor_classify_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect,
    enum starsead_rules_slot_state *state);

int starsead_supervisor_remove_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect) {
    bool present = false;
    if (system_probe_route_effect(system, effect, &present) != 0) return -1;
    if (!present) return 0;
    char table[16U];
    char priority[16U];
    char mark[32U];
    if (snprintf(table, sizeof(table), "%" PRIu32, effect->table) <= 0 ||
        snprintf(priority, sizeof(priority), "%" PRIu32, effect->priority) <= 0 ||
        snprintf(mark, sizeof(mark), "0x%08" PRIx32 "/0x%08" PRIx32,
            effect->mark, effect->mark_mask) <= 0) return -1;
    if (effect->kind == STARSEAD_ROUTE_EFFECT_IP_RULE) {
        const char *normal[] = {"rule", "del", "priority", priority,
            "fwmark", mark, "table", table};
        const char *inverted[] = {"rule", "del", "priority", priority,
            "not", "from", "all", "fwmark", mark, "table", table};
        return starsead_supervisor_ip_zero(system, effect->family,
            effect->invert_from_all ? inverted : normal,
            effect->invert_from_all ? sizeof(inverted) / sizeof(inverted[0]) :
                sizeof(normal) / sizeof(normal[0]));
    }
    if (effect->kind == STARSEAD_ROUTE_EFFECT_ROUTE) {
        const char *normal[] = {"route", "del", effect->destination,
            "dev", effect->interface_name, "table", table};
        const char *local[] = {"route", "del", "local", effect->destination,
            "dev", effect->interface_name, "table", table};
        return starsead_supervisor_ip_zero(system, effect->family,
            effect->local_route ? local : normal,
            effect->local_route ? sizeof(local) / sizeof(local[0]) :
                sizeof(normal) / sizeof(normal[0]));
    }
    return -1;
}

static int system_probe_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect, bool *present) {
    enum starsead_rules_slot_state state = STARSEAD_RULES_SLOT_FOREIGN;
    if (present == NULL || starsead_supervisor_classify_route_effect(system, effect, &state) != 0 ||
        state == STARSEAD_RULES_SLOT_FOREIGN) return -1;
    *present = state == STARSEAD_RULES_SLOT_OWNED;
    return 0;
}

int starsead_supervisor_classify_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect,
    enum starsead_rules_slot_state *state) {
    if (system == NULL || effect == NULL || state == NULL) return -1;
    *state = STARSEAD_RULES_SLOT_ABSENT;
    if (system->rule_snapshot.phase == SYSTEM_RULE_SNAPSHOT_BEFORE_APPLY ||
        system->rule_snapshot.phase == SYSTEM_RULE_SNAPSHOT_AFTER_APPLY) {
        const struct system_rule_view *view =
            effect->kind == STARSEAD_ROUTE_EFFECT_IP_RULE
                ? &system->rule_snapshot.ip_rules[effect->family]
                : &system->rule_snapshot.ip_routes[effect->family];
        if (!view->present) return -1;
        return effect->kind == STARSEAD_ROUTE_EFFECT_IP_RULE
            ? starsead_ip_rule_output_classify(
                view->bytes, view->length, effect, state)
            : starsead_ip_route_output_classify(
                view->bytes, view->length, effect, state);
    }
    int exit_status = -1;
    if (effect->kind == STARSEAD_ROUTE_EFFECT_IP_RULE) {
        const char *arguments[] = {"rule", "show"};
        if (starsead_supervisor_ip_command(system, effect->family, arguments, 2U,
                true, &exit_status) != 0 || exit_status != 0 ||
            system->action_stdout_overflow) return -1;
        return starsead_ip_rule_output_classify(
            system->action_stdout, system->action_stdout_length, effect, state);
    }
    if (effect->kind == STARSEAD_ROUTE_EFFECT_ROUTE) {
        char table[16U];
        if (snprintf(table, sizeof(table), "%" PRIu32, effect->table) <= 0) return -1;
        const char *arguments[] = {"route", "show", "table", table, effect->destination};
        if (starsead_supervisor_ip_command(system, effect->family, arguments, 5U,
                true, &exit_status) != 0 || exit_status != 0 ||
            system->action_stdout_overflow) return -1;
        return starsead_ip_route_output_classify(
            system->action_stdout, system->action_stdout_length, effect, state);
    }
    return -1;
}

static const struct starsead_route_effect *system_find_route_effect(
    const struct starsead_system_supervisor *system,
    const struct starsead_resource_operation *record) {
    for (size_t index = 0U; index < system->rules_runtime.plan.route_count; ++index) {
        const struct starsead_route_effect *effect = &system->rules_runtime.plan.routes[index];
        if (record->kind == STARSEAD_RESOURCE_OPERATION_IP_RULE &&
            effect->kind == STARSEAD_ROUTE_EFFECT_IP_RULE &&
            effect->family == record->resource.ip_rule.family &&
            effect->ip_rule_id == record->resource.ip_rule.rule_id) return effect;
        if (record->kind == STARSEAD_RESOURCE_OPERATION_ROUTE &&
            effect->kind == STARSEAD_ROUTE_EFFECT_ROUTE &&
            effect->family == record->resource.route.family &&
            effect->route_id == record->resource.route.route_id) return effect;
    }
    return NULL;
}

static int system_route_record(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect, struct starsead_resource_operation *record) {
    (void)system;
    memset(record, 0, sizeof(*record));
    if (effect->kind == STARSEAD_ROUTE_EFFECT_IP_RULE) {
        record->kind = STARSEAD_RESOURCE_OPERATION_IP_RULE;
        record->resource.ip_rule.family = effect->family;
        record->resource.ip_rule.rule_id = effect->ip_rule_id;
    } else if (effect->kind == STARSEAD_ROUTE_EFFECT_ROUTE) {
        record->kind = STARSEAD_RESOURCE_OPERATION_ROUTE;
        record->resource.route.family = effect->family;
        record->resource.route.route_id = effect->route_id;
        if (snprintf(record->resource.route.interface_name,
                sizeof(record->resource.route.interface_name), "%s",
                effect->interface_name) <= 0) return -1;
        record->resource.route.interface_index = if_nametoindex(effect->interface_name);
        if (record->resource.route.interface_index == 0U) return -1;
    } else {
        return -1;
    }
    return 0;
}

const char *starsead_supervisor_pin_path(
    struct starsead_system_supervisor *system, enum starsead_pin_id pin_id) {
    if (!system->matcher_plan_ready && system->loaded_config.config.matcher.enabled &&
        starsead_matcher_pin_plan_build(
            &system->loaded_config.config, &system->matcher_pin_plan) == 0) {
        system->matcher_plan_ready = true;
    }
    if (system->matcher_plan_ready) {
        for (size_t index = 0U; index < system->matcher_pin_plan.pin_count; ++index) {
            if (system->matcher_pin_plan.pins[index].pin_id == pin_id) {
                return system->matcher_pin_plan.pins[index].path;
            }
        }
    }
    if (!system->b2s_plan_ready && system->loaded_config.config.mode == STARSEAD_MODE_BPF2SOCKS &&
        starsead_b2s_pin_plan_build(
            &system->loaded_config.config, &system->b2s_pin_plan) == 0) {
        system->b2s_plan_ready = true;
    }
    if (system->b2s_plan_ready) {
        for (size_t index = 0U; index < system->b2s_pin_plan.pin_count; ++index) {
            if (system->b2s_pin_plan.pins[index].pin_id == pin_id) {
                return system->b2s_pin_plan.pins[index].path;
            }
        }
    }
    return NULL;
}

uint64_t starsead_supervisor_verified_pin_id(
    const struct starsead_system_supervisor *system, enum starsead_pin_id pin_id) {
    if (system->matcher_verified) {
        for (size_t index = 0U; index < system->matcher_verification.pin_count; ++index) {
            if (system->matcher_verification.pins[index].pin_id == pin_id) {
                return system->matcher_verification.pins[index].object_id;
            }
        }
    }
    if (system->b2s_verified) {
        for (size_t index = 0U; index < system->b2s_verification.pin_count; ++index) {
            if (system->b2s_verification.pins[index].pin_id == pin_id) {
                return system->b2s_verification.pins[index].object_id;
            }
        }
    }
    return 0U;
}

const struct starsead_b2s_verified_pin *starsead_supervisor_b2s_verified_pin(
    const struct starsead_system_supervisor *system, enum starsead_pin_id pin_id) {
    if (!system->b2s_verified) return NULL;
    for (size_t index = 0U; index < system->b2s_verification.pin_count; ++index) {
        if (system->b2s_verification.pins[index].pin_id == pin_id) {
            return &system->b2s_verification.pins[index];
        }
    }
    return NULL;
}

static int system_run_matcher_loader(struct starsead_system_supervisor *system) {
    int exit_status = -1;
    char error[256U] = {0};
    const char *stage = NULL;
    if (!system->matcher_launch_ready || !system->matcher_plan_ready) {
        stage = "prepare";
    } else if (starsead_supervisor_action_run_spec(
            system, &system->matcher_launch.process, false, &exit_status) != 0) {
        /* The action runner logs its own failure. */
        return -1;
    } else if (exit_status != 0) {
        stage = "exit";
    } else if (starsead_matcher_verify(
            &system->loaded_config.config, &system->matcher_pin_plan,
            starsead_system_bpf_program_backend(), &system->matcher_verification,
            error, sizeof(error)) != 0) {
        stage = "verify";
    }
    if (stage != NULL) {
        char stderr_summary[256U];
        starsead_supervisor_action_stderr_summary(system, stderr_summary, sizeof(stderr_summary));
        char message[640U];
        int written = snprintf(message, sizeof(message),
            "matcher loader failed: stage=%s exitStatus=%d detail=%s stderr=%s",
            stage, exit_status, error[0] == '\0' ? "unavailable" : error,
            strcmp(stage, "prepare") == 0 ? "none" : stderr_summary);
        if (written > 0 && (size_t)written < sizeof(message)) {
            starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_MATCHER, message);
        }
        return -1;
    }
    system->matcher_verified = true;
    return 0;
}

static bool system_b2s_pins_ready(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    if (system->helper_reaped) return true;
    if (starsead_b2s_verify(
            &system->loaded_config.config, &system->b2s_pin_plan,
            starsead_system_bpf_program_backend(), &system->b2s_verification,
            system->b2s_verify_error, sizeof(system->b2s_verify_error)) == 0) {
        system->b2s_verified = true;
        return true;
    }
    return false;
}

static int system_start_and_verify_b2s(struct starsead_system_supervisor *system) {
    if (starsead_supervisor_start_helper_process(system, &system->helper_identity) != 0) return -1;
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0) return -1;
    struct starsead_deadline deadline = {
        .armed = true,
        .monotonic_milliseconds = now +
            (int64_t)system->loaded_config.config.readiness_timeout_milliseconds,
    };
    system->b2s_verify_error[0] = '\0';
    if (starsead_supervisor_pump_condition_periodic(system, &deadline,
            STARSEAD_READINESS_POLL_INTERVAL_MILLIS, system_b2s_pins_ready) == 0 &&
        system->b2s_verified && !system->helper_reaped) return 0;
    char message[256U];
    int written = snprintf(message, sizeof(message),
        "bpf2socks pin verification failed: helperReaped=%d timeoutMs=%" PRIu32 " detail=%s",
        system->helper_reaped ? 1 : 0, system->loaded_config.config.readiness_timeout_milliseconds,
        system->b2s_verify_error[0] == '\0' ? "unavailable" : system->b2s_verify_error);
    if (written > 0 && (size_t)written < sizeof(message)) {
        starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_HELPER, message);
    }
    return -1;
}

static int system_sysctl_open(
    const struct starsead_sysctl_resource *resource, int flags, int *output) {
    const char *root_path = resource->sysctl_id == STARSEAD_SYSCTL_DISABLE_IPV6
        ? "/proc/sys/net/ipv6/conf" : "/proc/sys/net/ipv4/conf";
    const char *leaf = resource->sysctl_id == STARSEAD_SYSCTL_DISABLE_IPV6
        ? "disable_ipv6" : "route_localnet";
    if (resource->interface_name[0] == '\0' || strchr(resource->interface_name, '/') != NULL ||
        strcmp(resource->interface_name, ".") == 0 ||
        strcmp(resource->interface_name, "..") == 0 || output == NULL) return -1;
    if (resource->interface_index != 0U &&
        if_nametoindex(resource->interface_name) != resource->interface_index) return 1;
    int root = open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0) return errno == ENOENT || errno == ENODEV ? 1 : -1;
    int directory = openat(root, resource->interface_name,
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int saved = errno;
    (void)close(root);
    if (directory < 0) {
        errno = saved;
        return errno == ENOENT || errno == ENODEV ? 1 : -1;
    }
    int fd = openat(directory, leaf, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    saved = errno;
    (void)close(directory);
    if (fd < 0) {
        errno = saved;
        return errno == ENOENT || errno == ENODEV ? 1 : -1;
    }
    *output = fd;
    return 0;
}

int starsead_supervisor_sysctl_read(
    const struct starsead_sysctl_resource *resource, uint8_t *value) {
    int fd = -1;
    int opened = system_sysctl_open(resource, O_RDONLY, &fd);
    if (opened != 0) return opened;
    char bytes[3U] = {0, 0, 0};
    ssize_t count;
    do {
        count = pread(fd, bytes, sizeof(bytes), 0);
    } while (count < 0 && errno == EINTR);
    int saved = errno;
    (void)close(fd);
    errno = saved;
    if (count < 1 || count > 2 || (bytes[0] != '0' && bytes[0] != '1') ||
        (count == 2 && bytes[1] != '\n')) return -1;
    *value = (uint8_t)(bytes[0] - '0');
    return 0;
}

static int system_sysctl_write(
    const struct starsead_sysctl_resource *resource, uint8_t value) {
    if (value > 1U) return -1;
    int fd = -1;
    int opened = system_sysctl_open(resource, O_WRONLY, &fd);
    if (opened != 0) return -1;
    char bytes[2U] = {(char)('0' + value), '\n'};
    ssize_t count;
    do {
        count = pwrite(fd, bytes, sizeof(bytes), 0);
    } while (count < 0 && errno == EINTR);
    int saved = errno;
    (void)close(fd);
    errno = saved;
    return count == (ssize_t)sizeof(bytes) ? 0 : -1;
}

static int system_effect_probe_original(
    void *opaque, struct starsead_effect *effect,
    char *error, size_t error_size) {
    uint8_t value = 0U;
    (void)opaque;
    if (effect == NULL || effect->kind != STARSEAD_EFFECT_SYSCTL ||
        starsead_supervisor_sysctl_read(&effect->resource.sysctl, &value) != 0) {
        starsead_supervisor_reconcile_error(error, error_size,
            "failed to read original sysctl value");
        return -1;
    }
    effect->resource.sysctl.original_value = value;
    return 0;
}

static int system_effect_apply_one(
    void *opaque, const struct starsead_effect *effect,
    char *error, size_t error_size) {
    (void)opaque;
    if (effect == NULL || effect->kind != STARSEAD_EFFECT_SYSCTL ||
        system_sysctl_write(&effect->resource.sysctl,
            effect->resource.sysctl.desired_value) != 0) {
        starsead_supervisor_reconcile_error(error, error_size, "failed to apply sysctl value");
        return -1;
    }
    return 0;
}

static int system_effect_verify_value(
    const struct starsead_effect *effect, uint8_t expected,
    char *error, size_t error_size) {
    uint8_t value = 0U;
    if (effect == NULL || effect->kind != STARSEAD_EFFECT_SYSCTL ||
        starsead_supervisor_sysctl_read(&effect->resource.sysctl, &value) != 0 ||
        value != expected) {
        starsead_supervisor_reconcile_error(error, error_size, "sysctl verification failed");
        return -1;
    }
    return 0;
}

static int system_effect_verify_applied_one(
    void *opaque, const struct starsead_effect *effect,
    char *error, size_t error_size) {
    (void)opaque;
    return system_effect_verify_value(effect,
        effect->resource.sysctl.desired_value, error, error_size);
}

static int system_effect_undo_one(
    void *opaque, const struct starsead_effect *effect,
    char *error, size_t error_size) {
    (void)opaque;
    if (effect == NULL || effect->kind != STARSEAD_EFFECT_SYSCTL ||
        system_sysctl_write(&effect->resource.sysctl,
            effect->resource.sysctl.original_value) != 0) {
        starsead_supervisor_reconcile_error(error, error_size, "failed to restore sysctl value");
        return -1;
    }
    return 0;
}

static int system_effect_verify_restored_one(
    void *opaque, const struct starsead_effect *effect,
    char *error, size_t error_size) {
    (void)opaque;
    return system_effect_verify_value(effect,
        effect->resource.sysctl.original_value, error, error_size);
}

struct starsead_effect_backend starsead_supervisor_effect_backend(
    struct starsead_system_supervisor *system) {
    const struct starsead_effect_backend backend = {
        .context = system,
        .probe_original = system_effect_probe_original,
        .apply = system_effect_apply_one,
        .verify_applied = system_effect_verify_applied_one,
        .undo = system_effect_undo_one,
        .verify_restored = system_effect_verify_restored_one,
    };
    return backend;
}

int starsead_supervisor_effect_apply_sysctl(
    struct starsead_system_supervisor *system,
    const struct starsead_sysctl_resource *resource,
    char *error, size_t error_size) {
    struct starsead_effect effect;
    struct starsead_effect_backend backend = starsead_supervisor_effect_backend(system);
    if (system == NULL || resource == NULL) return -1;
    memset(&effect, 0, sizeof(effect));
    effect.kind = STARSEAD_EFFECT_SYSCTL;
    effect.resource.sysctl = *resource;
    return starsead_effect_journal_apply(
        &system->volatile_effects, &effect, &backend, error, error_size);
}

static bool system_rule_operation_kind(enum starsead_resource_operation_kind kind) {
    return kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_CHAIN ||
        kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_RULE ||
        kind == STARSEAD_RESOURCE_OPERATION_IP_RULE ||
        kind == STARSEAD_RESOURCE_OPERATION_ROUTE;
}

static int system_apply_operations(void *opaque, const struct starsead_resource_operation *records,
    size_t count, char *error, size_t error_size) {
    struct starsead_system_supervisor *system = opaque;
    if (records == NULL || count == 0U) return -1;
    const char *single_failure = NULL;
    int single_result = 0;
    if (count == 1U && records[0].kind == STARSEAD_RESOURCE_OPERATION_SYSCTL) {
        single_result = system_sysctl_write(&records[0].resource.sysctl,
            records[0].resource.sysctl.desired_value);
        single_failure = "sysctl write failed";
    } else if (count == 1U && records[0].kind == STARSEAD_RESOURCE_OPERATION_TC_QDISC) {
        single_result = starsead_supervisor_tc_apply_qdisc(system, &records[0].resource.tc_qdisc);
        single_failure = "tc qdisc apply failed";
    } else if (count == 1U && records[0].kind == STARSEAD_RESOURCE_OPERATION_TC_FILTER) {
        single_result = starsead_supervisor_tc_apply_filter(system, &records[0].resource.tc_filter);
        single_failure = "tc filter apply failed";
    }
    if (single_failure != NULL) {
        if (single_result != 0 && error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", single_failure);
        }
        return single_result;
    }
    bool rule_batch = system_rule_operation_kind(records[0].kind);
    for (size_t index = 0U; rule_batch && index < count; ++index) {
        enum starsead_resource_operation_kind kind = records[index].kind;
        rule_batch = kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_CHAIN ||
            kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_RULE ||
            kind == STARSEAD_RESOURCE_OPERATION_IP_RULE ||
            kind == STARSEAD_RESOURCE_OPERATION_ROUTE;
    }
    if (rule_batch) {
        if (starsead_supervisor_rule_batch_begin(&system->rule_commands) != 0) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "rule operation batch is already active");
            }
            return -1;
        }
        for (size_t index = 0U; index < count; ++index) {
            const struct starsead_resource_operation *record = &records[index];
            int result = -1;
            if (record->kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_CHAIN) {
                const struct starsead_private_chain_group *group =
                    starsead_supervisor_find_private_group(system, &record->resource.iptables_chain);
                result = group == NULL ? -1 : starsead_supervisor_create_private_group(system, group);
            } else if (record->kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_RULE) {
                const struct starsead_traffic_hook_group *group =
                    starsead_supervisor_find_hook_group(system, &record->resource.iptables_rule);
                result = group == NULL ? -1 : starsead_supervisor_apply_hook_group(system, group);
            } else if (record->kind == STARSEAD_RESOURCE_OPERATION_IP_RULE ||
                record->kind == STARSEAD_RESOURCE_OPERATION_ROUTE) {
                const struct starsead_route_effect *effect =
                    system_find_route_effect(system, record);
                result = effect == NULL ? -1 : system_apply_route_effect(system, effect);
            }
            if (result != 0) {
                if (error != NULL && error_size != 0U) {
                    (void)snprintf(error, error_size,
                        "rule operation batch apply failed at index %zu", index);
                }
                goto rule_batch_failed;
            }
        }
        if (starsead_supervisor_rule_batch_flush(system) != 0) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "rule operation batch commit failed");
            }
            goto rule_batch_failed;
        }
        starsead_supervisor_rule_batch_destroy(&system->rule_commands);
        starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
        system->rule_snapshot.phase = SYSTEM_RULE_SNAPSHOT_NEEDS_VERIFY;
        return 0;
rule_batch_failed:
        starsead_supervisor_rule_batch_destroy(&system->rule_commands);
        starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
        return -1;
    }
    if (records[0].kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_CHAIN) {
        const struct starsead_private_chain_group *group =
            starsead_supervisor_find_private_group(system, &records[0].resource.iptables_chain);
        if (group != NULL && starsead_supervisor_create_private_group(system, group) == 0) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", "iptables private group creation failed");
        }
        return -1;
    }
    if (count == 1U && records[0].kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_RULE) {
        const struct starsead_traffic_hook_group *group =
            starsead_supervisor_find_hook_group(system, &records[0].resource.iptables_rule);
        if (group == NULL) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "iptables hook group is unknown");
            }
            return -1;
        }
        if (starsead_supervisor_apply_hook_group(system, group) == 0) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", "iptables hook group apply failed");
        }
        return -1;
    }
    if (count == 1U && (records[0].kind == STARSEAD_RESOURCE_OPERATION_IP_RULE ||
            records[0].kind == STARSEAD_RESOURCE_OPERATION_ROUTE)) {
        const struct starsead_route_effect *effect = system_find_route_effect(system, &records[0]);
        if (effect != NULL && system_apply_route_effect(system, effect) == 0) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s",
                effect == NULL ? "route effect is unknown" : "route effect apply failed");
        }
        return -1;
    }
    for (size_t index = 0U; index < count; ++index) {
        if (records[index].kind != STARSEAD_RESOURCE_OPERATION_BPF_PIN) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "unsupported operation batch");
            }
            return -1;
        }
    }
    if (!system->pin_batch_active) {
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", "BPF pin batch is inactive");
        }
        return -1;
    }
    bool matcher_batch = system->active_pin_batch == STARSEAD_PIN_BATCH_MATCHER_IPV4 ||
        system->active_pin_batch == STARSEAD_PIN_BATCH_MATCHER_DUAL_STACK;
    bool b2s_batch = system->active_pin_batch == STARSEAD_PIN_BATCH_BPF2SOCKS_IPV4 ||
        system->active_pin_batch == STARSEAD_PIN_BATCH_BPF2SOCKS_DUAL_STACK;
    if ((!matcher_batch && !b2s_batch) ||
        (matcher_batch ? system_run_matcher_loader(system) :
            system_start_and_verify_b2s(system)) != 0) {
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s",
                matcher_batch ? "matcher loader or pin verification failed" :
                    "bpf2socks helper or pin verification failed");
        }
        return -1;
    }
    return 0;
}

static int system_verify_operation(void *opaque,
    const struct starsead_resource_operation *record,
    char *error, size_t error_size) {
    struct starsead_system_supervisor *system = opaque;
    if (record == NULL) return -1;
    if (system_rule_operation_kind(record->kind) &&
        system->rule_snapshot.phase == SYSTEM_RULE_SNAPSHOT_NEEDS_VERIFY &&
        starsead_supervisor_rule_snapshot_capture(system, &system->rules_runtime.plan,
            SYSTEM_RULE_SNAPSHOT_AFTER_APPLY) != 0) {
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", "post-apply rule snapshot failed");
        }
        return -1;
    }
    if (record->kind == STARSEAD_RESOURCE_OPERATION_SYSCTL) {
        uint8_t value = 0U;
        if (starsead_supervisor_sysctl_read(&record->resource.sysctl, &value) == 0 &&
            value == record->resource.sysctl.desired_value) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", "sysctl verification failed");
        }
        return -1;
    }
    if (record->kind == STARSEAD_RESOURCE_OPERATION_BPF_PIN) {
        uint64_t object_id = starsead_supervisor_verified_pin_id(
            system, record->resource.bpf_pin.pin_id);
        if (object_id == 0U) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "verified BPF identity is missing");
            }
            return -1;
        }
        return 0;
    }
    if (record->kind == STARSEAD_RESOURCE_OPERATION_TC_QDISC) {
        bool present = false;
        if (starsead_supervisor_tc_probe_qdisc(system, &record->resource.tc_qdisc, &present) == 0 &&
            present) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", "tc qdisc verification failed");
        }
        return -1;
    }
    if (record->kind == STARSEAD_RESOURCE_OPERATION_TC_FILTER) {
        enum starsead_rules_slot_state state = STARSEAD_RULES_SLOT_FOREIGN;
        int probed = starsead_supervisor_tc_probe_filter(system, &record->resource.tc_filter, &state);
        if (probed == 0 && state == STARSEAD_RULES_SLOT_OWNED) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "tc filter verification failed: %s",
                probed != 0 ? "probe-error" :
                    state == STARSEAD_RULES_SLOT_ABSENT ? "absent" : "foreign");
        }
        return -1;
    }
    if (record->kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_CHAIN) {
        const struct starsead_private_chain_group *group =
            starsead_supervisor_find_private_group(system, &record->resource.iptables_chain);
        bool all_present = false;
        bool any_present = false;
        if (group == NULL) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "iptables private group is unknown");
            }
            return -1;
        }
        if (starsead_supervisor_probe_private_group(system, group, &all_present, &any_present) != 0 ||
            !all_present) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "iptables private group probe failed");
            }
            return -1;
        }
        if (starsead_supervisor_verify_private_group_contents(system, group) != 0) {
            if (error != NULL && error_size != 0U) {
                (void)snprintf(error, error_size, "%s", "iptables private group verification failed");
            }
            return -1;
        }
        return 0;
    }
    if (record->kind == STARSEAD_RESOURCE_OPERATION_IPTABLES_RULE) {
        const struct starsead_traffic_hook_group *group =
            starsead_supervisor_find_hook_group(system, &record->resource.iptables_rule);
        bool all_present = false;
        bool any_present = false;
        if (group != NULL && starsead_supervisor_probe_hook_group(
                system, group, &all_present, &any_present) == 0 && all_present) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size, "%s", "iptables hook group verification failed");
        }
        return -1;
    }
    if (record->kind == STARSEAD_RESOURCE_OPERATION_IP_RULE ||
        record->kind == STARSEAD_RESOURCE_OPERATION_ROUTE) {
        const struct starsead_route_effect *effect = system_find_route_effect(system, record);
        bool present = false;
        int probed = effect == NULL ? -1 : system_probe_route_effect(system, effect, &present);
        if (probed == 0 && present) return 0;
        if (error != NULL && error_size != 0U) {
            (void)snprintf(error, error_size,
                "%s verification failed: family=%s table=%" PRIu32 " state=%s",
                record->kind == STARSEAD_RESOURCE_OPERATION_IP_RULE ? "ip rule" : "route",
                effect != NULL && effect->family == STARSEAD_IP_FAMILY_IPV6 ? "ipv6" : "ipv4",
                effect != NULL ? effect->table : 0U,
                effect == NULL ? "unknown" : probed != 0 ? "foreign-or-error" : "absent");
        }
        return -1;
    }
    if (error != NULL && error_size != 0U) {
        (void)snprintf(error, error_size, "%s", "unsupported operation verification");
    }
    return -1;
}

int starsead_supervisor_apply_and_verify(
    struct starsead_system_supervisor *system,
    const struct starsead_resource_operation *records,
    size_t count, char *error, size_t error_size) {
    size_t index;
    if (system_apply_operations(system, records, count, error, error_size) != 0) return -1;
    for (index = 0U; index < count; ++index) {
        if (system_verify_operation(
                system, &records[index], error, error_size) != 0) return -1;
    }
    return 0;
}

static int system_rules_apply_plan(
    void *opaque, const struct starsead_rule_transaction_plan *plan) {
    struct starsead_system_supervisor *system = opaque;
    if (plan == NULL || plan->no_op) return plan != NULL ? 0 : -1;
    struct starsead_resource_operation records[
        STARSEAD_RULE_TRANSACTION_MAX_GROUPS * 2U +
        STARSEAD_RULE_TRANSACTION_MAX_ROUTES];
    size_t count = 0U;
    for (size_t index = 0U; index < plan->private_group_count; ++index) {
        records[count++] = plan->private_groups[index].operation;
    }
    for (size_t index = 0U; index < plan->route_count; ++index) {
        if (system_route_record(system, &plan->routes[index], &records[count]) != 0) return -1;
        ++count;
    }
    for (size_t index = 0U; index < plan->hook_group_count; ++index) {
        records[count++] = plan->hook_groups[index].operation;
    }
    if (count == 0U) return -1;
    if (starsead_supervisor_rule_snapshot_capture(
            system, plan, SYSTEM_RULE_SNAPSHOT_BEFORE_APPLY) != 0) return -1;
    char error[256U] = {0};
    int applied = starsead_supervisor_apply_and_verify(
        system, records, count, error, sizeof(error));
    if (applied == 0) return 0;
    starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
    char message[384U];
    int written = snprintf(message, sizeof(message),
        "rule transaction failed: result=%d records=%zu detail=%s",
        applied, count, error[0] == '\0' ? "unavailable" : error);
    if (written > 0 && (size_t)written < sizeof(message)) {
        starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RULES, message);
    }
    return -1;
}

static int system_rules_apply_private(
    void *opaque, const struct starsead_private_chain_group *group) {
    struct starsead_system_supervisor *system = opaque;
    struct starsead_resource_operation record = group->operation;
    char error[128U] = {0};
    int applied = starsead_supervisor_apply_and_verify(
        system, &record, 1U, error, sizeof(error));
    if (applied == 0) return 0;
    char message[256U];
    int written = snprintf(message, sizeof(message),
        "private rules apply failed: result=%d detail=%s", applied,
        error[0] == '\0' ? "unavailable" : error);
    if (written > 0 && (size_t)written < sizeof(message)) {
        starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RULES, message);
    }
    return -1;
}

static int system_rules_apply_route(
    void *opaque, const struct starsead_route_effect *effect) {
    struct starsead_system_supervisor *system = opaque;
    struct starsead_resource_operation record;
    if (system_route_record(system, effect, &record) != 0) {
        starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RULES,
            "route rules apply failed: detail=route record is invalid");
        return -1;
    }
    char error[128U] = {0};
    int applied = starsead_supervisor_apply_and_verify(
        system, &record, 1U, error, sizeof(error));
    if (applied == 0) return 0;
    char message[256U];
    int written = snprintf(message, sizeof(message),
        "route rules apply failed: result=%d detail=%s", applied,
        error[0] == '\0' ? "unavailable" : error);
    if (written > 0 && (size_t)written < sizeof(message)) {
        starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RULES, message);
    }
    return -1;
}

static int system_rules_apply_hook(
    void *opaque, const struct starsead_traffic_hook_group *group) {
    struct starsead_system_supervisor *system = opaque;
    struct starsead_resource_operation record = group->operation;
    char error[128U] = {0};
    int applied = starsead_supervisor_apply_and_verify(
        system, &record, 1U, error, sizeof(error));
    if (applied == 0) return 0;
    const char *table_name = starsead_supervisor_table_name(group->table);
    char message[256U];
    int written = snprintf(message, sizeof(message),
        "hook rules apply failed: result=%d family=%s table=%s detail=%s",
        applied, group->family == STARSEAD_IP_FAMILY_IPV6 ? "ipv6" : "ipv4",
        table_name == NULL ? "invalid" : table_name,
        error[0] == '\0' ? "unavailable" : error);
    if (written > 0 && (size_t)written < sizeof(message)) {
        starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RULES, message);
    }
    return -1;
}

static int system_rules_probe_private(void *opaque,
    const struct starsead_private_chain_group *group,
    enum starsead_rules_slot_state *state) {
    struct starsead_system_supervisor *system = opaque;
    bool all_present = false;
    bool any_present = false;
    if (state == NULL || starsead_supervisor_probe_private_group(
            system, group, &all_present, &any_present) != 0) return -1;
    if (!any_present) *state = STARSEAD_RULES_SLOT_ABSENT;
    else if (all_present) {
        *state = starsead_supervisor_verify_private_group_contents(system, group) == 0
            ? STARSEAD_RULES_SLOT_OWNED : STARSEAD_RULES_SLOT_FOREIGN;
    } else {
        *state = STARSEAD_RULES_SLOT_FOREIGN;
    }
    return 0;
}

static int system_rules_probe_route(void *opaque,
    const struct starsead_route_effect *effect,
    enum starsead_rules_slot_state *state) {
    struct starsead_system_supervisor *system = opaque;
    if (state == NULL ||
        starsead_supervisor_classify_route_effect(system, effect, state) != 0) return -1;
    return 0;
}

static int system_rules_probe_hook(void *opaque,
    const struct starsead_traffic_hook_group *group,
    enum starsead_rules_slot_state *state) {
    struct starsead_system_supervisor *system = opaque;
    bool all_present = false;
    bool any_present = false;
    if (state == NULL || starsead_supervisor_probe_hook_group(
            system, group, &all_present, &any_present) != 0) return -1;
    if (!any_present) *state = STARSEAD_RULES_SLOT_ABSENT;
    else if (all_present) {
        *state = STARSEAD_RULES_SLOT_OWNED;
    } else {
        *state = STARSEAD_RULES_SLOT_FOREIGN;
    }
    return 0;
}

static int system_rules_remove_private(
    void *opaque, const struct starsead_private_chain_group *group) {
    return starsead_supervisor_remove_private_group(opaque, group);
}

static int system_rules_remove_route(
    void *opaque, const struct starsead_route_effect *effect) {
    return starsead_supervisor_remove_route_effect(opaque, effect);
}

static int system_rules_remove_hook(
    void *opaque, const struct starsead_traffic_hook_group *group) {
    return starsead_supervisor_remove_hook_group(opaque, group);
}

void starsead_supervisor_rules_backend_init(struct starsead_system_supervisor *system) {
    starsead_rules_runtime_init(&system->rules_runtime);
    system->rules_backend = (struct starsead_rules_backend){
        .ctx = system,
        .apply_plan = system_rules_apply_plan,
        .apply_private = system_rules_apply_private,
        .apply_route = system_rules_apply_route,
        .apply_hook = system_rules_apply_hook,
        .probe_private = system_rules_probe_private,
        .probe_route = system_rules_probe_route,
        .probe_hook = system_rules_probe_hook,
        .remove_private = system_rules_remove_private,
        .remove_route = system_rules_remove_route,
        .remove_hook = system_rules_remove_hook,
    };
    system->rules_initialized = true;
}

int starsead_supervisor_network_effect_dispatch(void *opaque,
    const struct starsead_network_effect_request *request,
    char *error, size_t error_size) {
    struct starsead_system_supervisor *system = opaque;
    if (request == NULL ||
        request->effect.kind != STARSEAD_EFFECT_SYSCTL) {
        return STARSEAD_STATE_INVALID;
    }
    const struct starsead_sysctl_resource *sysctl = &request->effect.resource.sysctl;
    char message[160U];
    uint8_t current = 0U;
    int present = starsead_supervisor_sysctl_read(sysctl, &current);
    if (!request->restore) {
        // The interface may disappear between the netlink event and this request.
        if (present == 1) return 0;
        int applied = starsead_supervisor_effect_apply_sysctl(system, sysctl, error, error_size);
        if (applied == 0 && sysctl->sysctl_id == STARSEAD_SYSCTL_DISABLE_IPV6 &&
            snprintf(message, sizeof(message), "system IPv6 disabled: interface=%s",
                sysctl->interface_name) > 0) {
            (void)starsead_log_line(&system->logger, STARSEAD_LOG_LEVEL_INFO,
                STARSEAD_COMPONENT_NETWORK, STARSEAD_LOG_EVENT_DIAGNOSTIC, message);
        }
        return applied;
    }
    struct starsead_effect_backend backend = starsead_supervisor_effect_backend(system);
    if (present == 1) {
        // The interface is gone, so there is nothing left to restore.
        starsead_effect_journal_forget(&system->volatile_effects, &request->effect);
        return 0;
    }
    char restore_error[128U] = {0};
    int restored = starsead_effect_journal_restore(&system->volatile_effects,
        &request->effect, &backend, restore_error, sizeof(restore_error));
    // A failed restore stays journaled; graceful cleanup retries it.
    int written = restored == 0
        ? snprintf(message, sizeof(message), "system IPv6 restored: interface=%s",
            sysctl->interface_name)
        : snprintf(message, sizeof(message), "system IPv6 restore deferred: interface=%s detail=%s",
            sysctl->interface_name, restore_error[0] == '\0' ? "unavailable" : restore_error);
    if (written > 0) {
        (void)starsead_log_line(&system->logger,
            restored == 0 ? STARSEAD_LOG_LEVEL_INFO : STARSEAD_LOG_LEVEL_WARNING,
            STARSEAD_COMPONENT_NETWORK, STARSEAD_LOG_EVENT_DIAGNOSTIC, message);
    }
    return 0;
}

int starsead_supervisor_sync_ipv6_guard(struct starsead_system_supervisor *system) {
    if (!system->loaded_config.config.disable_system_ipv6) return 0;
    char error[128U] = {0};
    if (starsead_network_sync_ipv6_guard(&system->network, error, sizeof(error)) != 0 ||
        starsead_network_apply_immediate(&system->network, error, sizeof(error)) != 0) {
        char message[192U];
        if (snprintf(message, sizeof(message), "system IPv6 guard sync failed: detail=%s",
                error[0] == '\0' ? "unavailable" : error) > 0) {
            starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_NETWORK, message);
        }
        return -1;
    }
    char networks[256U];
    starsead_network_describe_ipv6_guard(&system->network, networks, sizeof(networks));
    char message[320U];
    if (snprintf(message, sizeof(message), "system IPv6 guard networks: %s",
            networks[0] == '\0' ? "none" : networks) > 0) {
        (void)starsead_log_line(&system->logger, STARSEAD_LOG_LEVEL_INFO,
            STARSEAD_COMPONENT_NETWORK, STARSEAD_LOG_EVENT_DIAGNOSTIC, message);
    }
    return 0;
}

int starsead_supervisor_effect_rules(
    void *opaque, bool *active, uint64_t *generation, uint32_t *categories) {
    struct starsead_system_supervisor *system = opaque;
    if (active == NULL || generation == NULL || categories == NULL) return -1;
    system->rules_verified = false;
    // PREPARE can fail before rule installation. The lifecycle still invokes
    // remove_rules, so initialize its empty ownership journal before that boundary.
    if (!system->rules_initialized) starsead_supervisor_rules_backend_init(system);
    if (system->loaded_config.config.dns_enhancement)
        system->dns_startup_deadline =
            starsead_dns_monotime() + SYSTEM_DNS_TRANSITION_BUDGET_MILLIS;
    if (starsead_supervisor_dns_prepare(system) != 0) return -1;
    if (system->network_opened && !system->network.no_op) {
        int64_t now = 0;
        char error[128U];
        if (starsead_supervisor_runtime_clock(system, &now) != 0 ||
            starsead_network_handle(&system->network, (uint64_t)now, error, sizeof(error)) != 0 ||
            starsead_network_apply_immediate(&system->network, error, sizeof(error)) != 0)
            return -1;
    }
    const char *failed_stage = NULL;
    if (starsead_supervisor_capture_local_address_snapshot(system) != 0) {
        failed_stage = "local-address-snapshot";
    } else if (starsead_rules_install(&system->rules_runtime, &system->loaded_config.config, false,
                   &system->rules_backend) != 0) {
        failed_stage = "install";
    } else if (starsead_supervisor_reconcile_b2s_local_maps(system) != 0) {
        failed_stage = "bpf2-local-map-reconcile";
    } else if (starsead_supervisor_reconcile_hotspot_tc(system) != 0) {
        failed_stage = "hotspot-tc-reconcile";
    } else if (starsead_rules_verify(&system->rules_runtime, &system->rules_backend) != 0) {
        failed_stage = "verify";
    }
    starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
    if (failed_stage != NULL) {
        char message[160U];
        int written =
            snprintf(message, sizeof(message), "rules apply failed: stage=%s", failed_stage);
        if (written > 0 && (size_t)written < sizeof(message)) {
            starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RULES, message);
        }
        return -1;
    }
    system->rules_verified = true;
    if (starsead_mode_core_managed(system->loaded_config.config.mode)) {
        *active = false;
        *generation = 0U;
        *categories = 0U;
    } else {
        *active = true;
        *generation = system->rules_runtime.generation;
        *categories = starsead_supervisor_rule_categories(&system->loaded_config.config);
    }
    return 0;
}

#endif
