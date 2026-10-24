// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <errno.h>
#include <string.h>

static struct starsead_private_chain_group *transaction_add_private_group(
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

static struct starsead_traffic_hook_group *transaction_add_hook_group(
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

static int transaction_add_hook(struct starsead_traffic_hook_group *group,
    enum starsead_builtin_chain builtin, bool insert_at_head,
    enum starsead_hook_verdict verdict, bool udp53, const char *target) {
    if (group == NULL || group->hook_count >= STARSEAD_RULE_TRANSACTION_MAX_HOOKS) {
        return STARSEAD_CONFIG_INVALID;
    }
    struct starsead_traffic_hook *hook = &group->hooks[group->hook_count++];
    hook->builtin_chain = builtin;
    hook->insert_at_head = insert_at_head;
    hook->dns_destination_protocol = udp53 ? STARSEAD_DNS_UDP : STARSEAD_DNS_NONE;
    hook->verdict = verdict;
    if (verdict == STARSEAD_HOOK_JUMP) {
        return starsead_copy_string(hook->jump_target, sizeof(hook->jump_target), target);
    }
    return target == NULL ? 0 : STARSEAD_CONFIG_INVALID;
}

static int transaction_add_fake_dns(
    struct starsead_rule_transaction_plan *plan) {
    const struct starsead_owned_resource_catalog *catalog =
        starsead_owned_resource_catalog();
    const char *output = catalog->fake_dns_output_chain;
    const char *prerouting = catalog->fake_dns_prerouting_chain;
    struct starsead_private_chain_group *group = transaction_add_private_group(plan,
        STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_NAT, STARSEAD_CHAIN_FAKE_DNS);
    if (group == NULL || starsead_copy_string(group->names[0], STARSEAD_MAX_CHAIN_NAME, output) != 0 ||
        starsead_copy_string(group->names[1], STARSEAD_MAX_CHAIN_NAME, prerouting) != 0) {
        return STARSEAD_CONFIG_INVALID;
    }
    group->name_count = 2U;
    struct starsead_traffic_hook_group *hooks = transaction_add_hook_group(plan,
        STARSEAD_IP_FAMILY_IPV4, STARSEAD_IP_TABLE_NAT,
        STARSEAD_CHAIN_FAKE_DNS, STARSEAD_RULE_FAKE_DNS_ENTRY);
    if (transaction_add_hook(hooks, STARSEAD_BUILTIN_OUTPUT, true,
            STARSEAD_HOOK_JUMP, false, output) != 0 ||
        transaction_add_hook(hooks, STARSEAD_BUILTIN_PREROUTING, true,
            STARSEAD_HOOK_JUMP, false, prerouting) != 0) return STARSEAD_CONFIG_INVALID;
    return 0;
}

int starsead_rule_transaction_plan_build(const struct starsead_config *config,
    bool has_global_ipv6_address, struct starsead_rule_transaction_plan *plan) {
    if (plan != NULL) memset(plan, 0, sizeof(*plan));
    if (config == NULL || plan == NULL) return STARSEAD_CONFIG_INVALID;
    if (starsead_mode_core_managed(config->mode)) {
        bool supported_core =
            config->owner == STARSEAD_OWNER_BOX && config->core_type == STARSEAD_CORE_SING_BOX;
        if (config->mode == STARSEAD_MODE_TUN) {
            supported_core |=
                config->owner == STARSEAD_OWNER_META && config->core_type == STARSEAD_CORE_MIHOMO;
        }
        if (!supported_core) {
            return STARSEAD_CONFIG_UNSUPPORTED_COMBINATION;
        }
        plan->no_op = true;
        return 0;
    }
    int result = 0;
    if (config->mode == STARSEAD_MODE_TPROXY) {
        result =
            starsead_tproxy_rule_transaction_plan_build(config, has_global_ipv6_address, plan);
    } else if (config->mode == STARSEAD_MODE_TUN2SOCKS) {
        result = starsead_tun_rule_transaction_plan_build(config, plan);
    } else if (config->mode != STARSEAD_MODE_BPF2SOCKS) {
        result = STARSEAD_CONFIG_INVALID;
    }
    if (result != 0) return result;
    // Explicit Android network selections have priority 13000. Only an
    // intercepted DNS packet, classified in OUTPUT after forced bypasses, may
    // override that route; ordinary traffic retains the existing priority.
    if ((config->mode == STARSEAD_MODE_TPROXY || config->mode == STARSEAD_MODE_TUN2SOCKS) &&
        (config->dns_enhancement || config->enable_local_dns)) {
        size_t original_count = plan->route_count;
        for (size_t i = 0; i < original_count; i++)
            if (plan->routes[i].kind == STARSEAD_ROUTE_EFFECT_IP_RULE) {
                if (plan->route_count >= STARSEAD_RULE_TRANSACTION_MAX_ROUTES)
                    return STARSEAD_CONFIG_INVALID;
                struct starsead_route_effect *rule = &plan->routes[plan->route_count++];
                *rule = plan->routes[i];
                rule->priority = STARSEAD_DNS_ROUTE_RULE_PRIORITY;
                rule->mark = STARSEAD_DNS_PROXY_ROUTE_MARK;
                rule->mark_mask = STARSEAD_DNS_MARK_MASK;
                rule->ip_rule_id = config->mode == STARSEAD_MODE_TPROXY
                    ? STARSEAD_IP_RULE_DNS_TPROXY
                    : STARSEAD_IP_RULE_DNS_TUNNEL;
            }
    }
    if (config->mode == STARSEAD_MODE_BPF2SOCKS &&
        (config->enable_ipv6 || config->enable_local_dns)) {
        if (plan->route_count >= STARSEAD_RULE_TRANSACTION_MAX_ROUTES) {
            return STARSEAD_CONFIG_INVALID;
        }
        struct starsead_route_effect *route = &plan->routes[plan->route_count++];
        memset(route, 0, sizeof(*route));
        route->kind = STARSEAD_ROUTE_EFFECT_ROUTE;
        route->family = STARSEAD_IP_FAMILY_IPV6;
        route->table = 255U;
        route->local_route = true;
        route->route_id = STARSEAD_ROUTE_TOKEN;
        if (starsead_copy_string(
                route->destination, sizeof(route->destination), "fd7a:7374:6572:6973::/64") != 0 ||
            starsead_copy_string(route->interface_name, sizeof(route->interface_name), "lo") !=
                0) {
            return STARSEAD_CONFIG_INVALID;
        }
    }
    if (config->enable_fake_dns && transaction_add_fake_dns(plan) != 0) {
        return STARSEAD_CONFIG_INVALID;
    }
    plan->hooks_are_last = true;
    return 0;
}
