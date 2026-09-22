// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <string.h>

static int copy_text(char *target, size_t capacity, const char *value) {
    size_t length = strnlen(value, capacity);
    if (length == 0U || length >= capacity) return STARSEAD_CONFIG_INVALID;
    memcpy(target, value, length + 1U);
    return 0;
}

static struct starsead_private_chain_group *add_private_group(
    struct starsead_rule_transaction_plan *plan,
    enum starsead_ip_family family,
    enum starsead_ip_table table,
    enum starsead_chain_id chain_id) {
    if (plan->private_group_count >= STARSEAD_RULE_TRANSACTION_MAX_GROUPS) return NULL;
    struct starsead_private_chain_group *group =
        &plan->private_groups[plan->private_group_count++];
    memset(group, 0, sizeof(*group));
    group->family = family;
    group->table = table;
    group->chain_id = chain_id;
    group->operation.kind = STARSEAD_RESOURCE_OPERATION_IPTABLES_CHAIN;
    group->operation.resource.iptables_chain.family = family;
    group->operation.resource.iptables_chain.table = table;
    group->operation.resource.iptables_chain.chain_id = chain_id;
    return group;
}

static int add_private_name(struct starsead_private_chain_group *group, const char *name) {
    if (group == NULL || group->name_count >= STARSEAD_RULE_TRANSACTION_MAX_NAMES) {
        return STARSEAD_CONFIG_INVALID;
    }
    return copy_text(group->names[group->name_count++], STARSEAD_MAX_CHAIN_NAME, name);
}

static struct starsead_traffic_hook_group *add_hook_group(
    struct starsead_rule_transaction_plan *plan,
    enum starsead_ip_family family,
    enum starsead_ip_table table,
    enum starsead_chain_id chain_id,
    enum starsead_rule_id rule_id) {
    if (plan->hook_group_count >= STARSEAD_RULE_TRANSACTION_MAX_GROUPS) return NULL;
    struct starsead_traffic_hook_group *group = &plan->hook_groups[plan->hook_group_count++];
    memset(group, 0, sizeof(*group));
    group->family = family;
    group->table = table;
    group->chain_id = chain_id;
    group->rule_id = rule_id;
    group->operation.kind = STARSEAD_RESOURCE_OPERATION_IPTABLES_RULE;
    group->operation.resource.iptables_rule.family = family;
    group->operation.resource.iptables_rule.table = table;
    group->operation.resource.iptables_rule.chain_id = chain_id;
    group->operation.resource.iptables_rule.rule_id = rule_id;
    return group;
}

static int add_jump(
    struct starsead_traffic_hook_group *group,
    enum starsead_builtin_chain builtin,
    bool insert_at_head,
    bool udp53,
    const char *target) {
    if (group == NULL || group->hook_count >= STARSEAD_RULE_TRANSACTION_MAX_HOOKS) {
        return STARSEAD_CONFIG_INVALID;
    }
    struct starsead_traffic_hook *hook = &group->hooks[group->hook_count++];
    memset(hook, 0, sizeof(*hook));
    hook->builtin_chain = builtin;
    hook->insert_at_head = insert_at_head;
    hook->udp_destination_port_53 = udp53;
    hook->verdict = STARSEAD_HOOK_JUMP;
    return copy_text(hook->jump_target, sizeof(hook->jump_target), target);
}

static struct starsead_route_effect *add_route(
    struct starsead_rule_transaction_plan *plan,
    enum starsead_route_effect_kind kind,
    enum starsead_ip_family family) {
    if (plan->route_count >= STARSEAD_RULE_TRANSACTION_MAX_ROUTES) return NULL;
    struct starsead_route_effect *effect = &plan->routes[plan->route_count++];
    memset(effect, 0, sizeof(*effect));
    effect->kind = kind;
    effect->family = family;
    return effect;
}

static int add_normal_route(
    struct starsead_rule_transaction_plan *plan,
    enum starsead_ip_family family) {
    struct starsead_route_effect *rule = add_route(plan, STARSEAD_ROUTE_EFFECT_IP_RULE, family);
    struct starsead_route_effect *route = add_route(plan, STARSEAD_ROUTE_EFFECT_ROUTE, family);
    if (rule == NULL || route == NULL) return STARSEAD_CONFIG_INVALID;
    rule->table = STARSEAD_TPROXY_TABLE;
    rule->priority = STARSEAD_ROUTE_RULE_PRIORITY;
    rule->mark = STARSEAD_PRIMARY_MARK;
    rule->mark_mask = STARSEAD_MARK_MASK;
    rule->ip_rule_id = STARSEAD_IP_RULE_TPROXY;
    route->table = STARSEAD_TPROXY_TABLE;
    route->local_route = true;
    route->route_id = STARSEAD_ROUTE_TPROXY;
    if (copy_text(route->destination, sizeof(route->destination), "default") != 0 ||
        copy_text(route->interface_name, sizeof(route->interface_name), "lo") != 0) {
        return STARSEAD_CONFIG_INVALID;
    }
    return 0;
}

int starsead_tproxy_rule_transaction_plan_build(
    const struct starsead_config *config,
    bool has_global_ipv6_address,
    struct starsead_rule_transaction_plan *plan) {
    (void)has_global_ipv6_address;
    if (config == NULL || plan == NULL || config->mode != STARSEAD_MODE_TPROXY) {
        return STARSEAD_CONFIG_INVALID;
    }
    struct starsead_private_chain_group *local4 = add_private_group(plan,
        STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE, STARSEAD_CHAIN_LOCAL_BYPASS);
    struct starsead_private_chain_group *main4 = add_private_group(plan,
        STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE, STARSEAD_CHAIN_TPROXY);
    if (add_private_name(main4, "ASTERISK_TPROXY_PREROUTING") != 0 ||
        add_private_name(main4, "ASTERISK_TPROXY_OUTPUT") != 0 ||
        add_private_name(local4, "ASTERISK_LOCAL4_BEGIN") != 0 ||
        add_private_name(local4, "ASTERISK_LOCAL4_END") != 0 ||
        add_normal_route(plan, STARSEAD_IP_FAMILY_IPV4) != 0) {
        return STARSEAD_CONFIG_INVALID;
    }
    struct starsead_traffic_hook_group *hooks4 = add_hook_group(plan,
        STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_MANGLE,
        STARSEAD_CHAIN_TPROXY, STARSEAD_RULE_TPROXY_ENTRY);
    if (add_jump(hooks4, STARSEAD_BUILTIN_PREROUTING, true, false,
            "ASTERISK_TPROXY_PREROUTING") != 0 ||
        add_jump(hooks4, STARSEAD_BUILTIN_OUTPUT, false, false,
            "ASTERISK_TPROXY_OUTPUT") != 0) return STARSEAD_CONFIG_INVALID;

    bool dns_only = !config->enable_ipv6;
    if (dns_only && (!config->enable_local_dns || config->disable_system_ipv6)) return 0;
    struct starsead_private_chain_group *local6 = add_private_group(plan,
        STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE, STARSEAD_CHAIN_LOCAL_BYPASS);
    struct starsead_private_chain_group *main6 = add_private_group(plan,
        STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE, STARSEAD_CHAIN_TPROXY);
    if (add_private_name(main6, "ASTERISK_TPROXY6_PREROUTING") != 0 ||
        add_private_name(main6, "ASTERISK_TPROXY6_OUTPUT") != 0 ||
        add_private_name(local6, "ASTERISK_LOCAL6_BEGIN") != 0 ||
        add_private_name(local6, "ASTERISK_LOCAL6_END") != 0) return STARSEAD_CONFIG_INVALID;
    struct starsead_traffic_hook_group *hooks6 = add_hook_group(plan,
        STARSEAD_IP_FAMILY_IPV6, STARSEAD_IP_TABLE_MANGLE,
        STARSEAD_CHAIN_TPROXY, STARSEAD_RULE_TPROXY_ENTRY);
    if (add_jump(hooks6, STARSEAD_BUILTIN_PREROUTING, true, dns_only,
            "ASTERISK_TPROXY6_PREROUTING") != 0 ||
        add_jump(hooks6, STARSEAD_BUILTIN_OUTPUT, false, dns_only,
            "ASTERISK_TPROXY6_OUTPUT") != 0) return STARSEAD_CONFIG_INVALID;
    return add_normal_route(plan, STARSEAD_IP_FAMILY_IPV6);
}
