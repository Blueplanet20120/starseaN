// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <errno.h>
#include <string.h>

static const char *const default_bypass_cidrs[] = {
    "0.0.0.0/8", "10.0.0.0/8", "100.0.0.0/8", "127.0.0.0/8",
    "169.254.0.0/16", "192.0.0.0/24", "192.0.2.0/24", "192.88.99.0/24",
    "192.168.0.0/16", "198.51.100.0/24", "203.0.113.0/24", "224.0.0.0/4",
    "240.0.0.0/4", "255.255.255.255/32", "::/128", "::1/128",
    "::ffff:0:0/96", "100::/64", "64:ff9b::/96", "2001::/32", "2001:10::/28",
    "2001:20::/28", "2001:db8::/32", "2002::/16", "fe80::/10", "ff00::/8",
};

size_t starsead_default_bypass_cidr_count(void) {
    return sizeof(default_bypass_cidrs) / sizeof(default_bypass_cidrs[0]);
}

const char *starsead_default_bypass_cidr(size_t index) {
    return index < starsead_default_bypass_cidr_count() ? default_bypass_cidrs[index] : NULL;
}

static int transaction_copy_text(char *target, size_t capacity, const char *value) {
    size_t length = strnlen(value, capacity);
    if (length == 0U || length >= capacity) return STARSEAD_CONFIG_INVALID;
    memcpy(target, value, length + 1U);
    return 0;
}

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
    hook->udp_destination_port_53 = udp53;
    hook->verdict = verdict;
    if (verdict == STARSEAD_HOOK_JUMP) {
        return transaction_copy_text(hook->jump_target, sizeof(hook->jump_target), target);
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
    if (group == NULL || transaction_copy_text(group->names[0], STARSEAD_MAX_CHAIN_NAME, output) != 0 ||
        transaction_copy_text(group->names[1], STARSEAD_MAX_CHAIN_NAME, prerouting) != 0) {
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

int starsead_rule_transaction_plan_build(
    const struct starsead_config *config,
    bool has_global_ipv6_address,
    struct starsead_rule_transaction_plan *plan) {
    if (plan != NULL) memset(plan, 0, sizeof(*plan));
    if (config == NULL || plan == NULL) return STARSEAD_CONFIG_INVALID;
    if (starsead_mode_core_managed(config->mode)) {
        bool supported_core = config->owner == STARSEAD_OWNER_BOX &&
            config->core_type == STARSEAD_CORE_SING_BOX;
        if (config->mode == STARSEAD_MODE_TUN) {
            supported_core |= config->owner == STARSEAD_OWNER_META &&
                config->core_type == STARSEAD_CORE_MIHOMO;
        }
        if (!supported_core) {
            return STARSEAD_CONFIG_UNSUPPORTED_COMBINATION;
        }
        plan->no_op = true;
        return 0;
    }
    int result = 0;
    if (config->mode == STARSEAD_MODE_TPROXY) {
        result = starsead_tproxy_rule_transaction_plan_build(
            config, has_global_ipv6_address, plan);
    } else if (config->mode == STARSEAD_MODE_TUN2SOCKS) {
        result = starsead_tun_rule_transaction_plan_build(config, plan);
    } else if (config->mode != STARSEAD_MODE_BPF2SOCKS) {
        result = STARSEAD_CONFIG_INVALID;
    }
    if (result != 0) return result;
    if (config->mode == STARSEAD_MODE_BPF2SOCKS && config->enable_ipv6) {
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
        if (transaction_copy_text(route->destination, sizeof(route->destination),
                "fd7a:7374:6572:6973::/64") != 0 ||
            transaction_copy_text(route->interface_name,
                sizeof(route->interface_name), "lo") != 0) {
            return STARSEAD_CONFIG_INVALID;
        }
    }
    if (config->enable_fake_dns && transaction_add_fake_dns(plan) != 0) {
        return STARSEAD_CONFIG_INVALID;
    }
    plan->hooks_are_last = true;
    return 0;
}

int starsead_rule_transaction_quiesce_plan_build(
    const struct starsead_rule_transaction_plan *active,
    struct starsead_rule_transaction_plan *plan) {
    if (plan != NULL) memset(plan, 0, sizeof(*plan));
    if (active == NULL || plan == NULL) return STARSEAD_CONFIG_INVALID;
    if (active->no_op) {
        plan->no_op = true;
        return 0;
    }
    plan->hook_group_count = active->hook_group_count;
    for (size_t index = 0U; index < active->hook_group_count; ++index) {
        plan->hook_groups[index] = active->hook_groups[active->hook_group_count - index - 1U];
    }
    plan->route_count = active->route_count;
    for (size_t index = 0U; index < active->route_count; ++index) {
        plan->routes[index] = active->routes[active->route_count - index - 1U];
    }
    plan->private_group_count = active->private_group_count;
    for (size_t index = 0U; index < active->private_group_count; ++index) {
        plan->private_groups[index] =
            active->private_groups[active->private_group_count - index - 1U];
    }
    return 0;
}

static int rule_plan_append(
    struct starsead_rule_plan *plan,
    enum starsead_rule_plan_operation_kind kind,
    bool traffic_activation) {
    if (plan->operation_count >= STARSEAD_RULE_PLAN_MAX_OPERATIONS) return STARSEAD_CONFIG_INVALID;
    plan->operations[plan->operation_count].kind = kind;
    plan->operations[plan->operation_count].traffic_activation = traffic_activation;
    ++plan->operation_count;
    return 0;
}

static int rule_plan_copy_tunnel(
    struct starsead_rule_plan *plan,
    const char *name) {
    size_t length = strnlen(name, sizeof(plan->tunnel_name));
    if (length == 0U || length >= sizeof(plan->tunnel_name)) return STARSEAD_CONFIG_INVALID;
    memcpy(plan->tunnel_name, name, length + 1U);
    return 0;
}

int starsead_rule_plan_build(
    const struct starsead_config *config,
    bool has_global_ipv6_address,
    struct starsead_rule_plan *plan) {
    (void)has_global_ipv6_address;
    if (plan != NULL) memset(plan, 0, sizeof(*plan));
    if (config == NULL || plan == NULL) return STARSEAD_CONFIG_INVALID;
    plan->enable_ipv6 = config->enable_ipv6;
    plan->iptables_wait_seconds = STARSEAD_IPTABLES_WAIT_SECONDS;
    plan->route_rule_priority = STARSEAD_ROUTE_RULE_PRIORITY;
    plan->primary_mark = STARSEAD_PRIMARY_MARK;
    plan->mark_mask = STARSEAD_MARK_MASK;
    if (starsead_mode_core_managed(config->mode)) {
        bool supported_core = config->owner == STARSEAD_OWNER_BOX &&
            config->core_type == STARSEAD_CORE_SING_BOX;
        if (config->mode == STARSEAD_MODE_TUN) {
            supported_core |= config->owner == STARSEAD_OWNER_META &&
                config->core_type == STARSEAD_CORE_MIHOMO;
        }
        if (!supported_core) {
            return STARSEAD_CONFIG_UNSUPPORTED_COMBINATION;
        }
        plan->no_op = true;
        return 0;
    }
    plan->uses_matcher = config->matcher.enabled;
    plan->uses_helper_tc = config->mode == STARSEAD_MODE_BPF2SOCKS;
    if (config->mode == STARSEAD_MODE_TPROXY) {
        plan->routing_table = STARSEAD_TPROXY_TABLE;
    } else if (config->mode == STARSEAD_MODE_TUN2SOCKS) {
        plan->routing_table = STARSEAD_TUN_TABLE;
        const char *tunnel = config->helper.value.hev.tunnel_name;
        if (rule_plan_copy_tunnel(plan, tunnel) != 0) return STARSEAD_CONFIG_INVALID;
    } else if (config->mode != STARSEAD_MODE_BPF2SOCKS) {
        return STARSEAD_CONFIG_INVALID;
    } else if (config->enable_ipv6) {
        plan->has_token_ipv6_route = true;
        (void)memcpy(plan->token_ipv6_prefix, "fd7a:7374:6572:6973::/64",
            sizeof("fd7a:7374:6572:6973::/64"));
    }

    if (config->mode == STARSEAD_MODE_BPF2SOCKS) {
        if (plan->has_token_ipv6_route &&
            rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_ROUTE, false) != 0) {
            return STARSEAD_CONFIG_INVALID;
        }
        if (rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_HELPER_TC, false) != 0) {
            return STARSEAD_CONFIG_INVALID;
        }
        if (config->enable_fake_dns &&
            (rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_PRIVATE, false) != 0 ||
             rule_plan_append(plan, STARSEAD_RULE_PLAN_POPULATE_FAKE_DNS, false) != 0)) {
            return STARSEAD_CONFIG_INVALID;
        }
        plan->first_activation = plan->operation_count;
        if (config->enable_fake_dns &&
            rule_plan_append(plan, STARSEAD_RULE_PLAN_ACTIVATE_FAKE_DNS, true) != 0) {
            return STARSEAD_CONFIG_INVALID;
        }
        return 0;
    }

    if (rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_PRIVATE, false) != 0 ||
        rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_LOCAL_BYPASS, false) != 0) return STARSEAD_CONFIG_INVALID;
    if (plan->uses_matcher &&
        rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_MATCHER, false) != 0) return STARSEAD_CONFIG_INVALID;
    if (rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_ROUTE, false) != 0) return STARSEAD_CONFIG_INVALID;
    if (plan->uses_helper_tc &&
        rule_plan_append(plan, STARSEAD_RULE_PLAN_PREPARE_HELPER_TC, false) != 0) return STARSEAD_CONFIG_INVALID;
    if (rule_plan_append(plan, STARSEAD_RULE_PLAN_POPULATE_POLICY, false) != 0) return STARSEAD_CONFIG_INVALID;
    if (config->enable_local_dns &&
        rule_plan_append(plan, STARSEAD_RULE_PLAN_POPULATE_DNS, false) != 0) return STARSEAD_CONFIG_INVALID;
    if (config->enable_fake_dns &&
        rule_plan_append(plan, STARSEAD_RULE_PLAN_POPULATE_FAKE_DNS, false) != 0) return STARSEAD_CONFIG_INVALID;
    plan->first_activation = plan->operation_count;
    if (rule_plan_append(plan, STARSEAD_RULE_PLAN_ACTIVATE_MAIN, true) != 0) return STARSEAD_CONFIG_INVALID;
    if (config->enable_local_dns &&
        rule_plan_append(plan, STARSEAD_RULE_PLAN_ACTIVATE_DNS, true) != 0) return STARSEAD_CONFIG_INVALID;
    if (config->enable_fake_dns &&
        rule_plan_append(plan, STARSEAD_RULE_PLAN_ACTIVATE_FAKE_DNS, true) != 0) return STARSEAD_CONFIG_INVALID;
    return 0;
}

int starsead_rule_quiesce_plan_build(
    const struct starsead_rule_plan *active,
    struct starsead_rule_plan *plan) {
    if (plan != NULL) memset(plan, 0, sizeof(*plan));
    if (active == NULL || plan == NULL) return STARSEAD_CONFIG_INVALID;
    if (active->no_op) {
        plan->no_op = true;
        return 0;
    }
    for (size_t index = active->first_activation; index < active->operation_count; ++index) {
        if (active->operations[index].kind == STARSEAD_RULE_PLAN_ACTIVATE_MAIN &&
            rule_plan_append(plan, STARSEAD_RULE_PLAN_QUIESCE_MAIN, false) != 0) {
            return STARSEAD_CONFIG_INVALID;
        }
        if (active->operations[index].kind == STARSEAD_RULE_PLAN_ACTIVATE_DNS &&
            rule_plan_append(plan, STARSEAD_RULE_PLAN_QUIESCE_DNS, false) != 0) return STARSEAD_CONFIG_INVALID;
        if (active->operations[index].kind == STARSEAD_RULE_PLAN_ACTIVATE_FAKE_DNS &&
            rule_plan_append(plan, STARSEAD_RULE_PLAN_QUIESCE_FAKE_DNS, false) != 0) return STARSEAD_CONFIG_INVALID;
    }
    if (rule_plan_append(plan, STARSEAD_RULE_PLAN_REMOVE_PRIVATE, false) != 0) return STARSEAD_CONFIG_INVALID;
    plan->first_activation = plan->operation_count;
    return 0;
}

static enum starsead_packet_action selected_packet_action(
    const struct starsead_config *config,
    enum starsead_packet_direction direction) {
    if (config->mode == STARSEAD_MODE_TPROXY) {
        return direction == STARSEAD_PACKET_PREROUTING ?
            STARSEAD_PACKET_TPROXY : STARSEAD_PACKET_MARK_PRIMARY;
    }
    if (config->mode == STARSEAD_MODE_TUN2SOCKS) {
        return STARSEAD_PACKET_MARK_PRIMARY;
    }
    return STARSEAD_PACKET_NONE;
}

static bool packet_is_selected(
    const struct starsead_config *config,
    const struct starsead_rule_plan *plan,
    const struct starsead_packet_model_input *input) {
    if (plan->uses_matcher) return input->matcher_selected;
    if (config->app_policy_mode == STARSEAD_APP_POLICY_GLOBAL) return true;
    if (config->app_policy_mode == STARSEAD_APP_POLICY_BLACKLIST) return !input->uid_listed;
    return input->uid_listed;
}

int starsead_packet_model_decide(
    const struct starsead_config *config,
    const struct starsead_rule_plan *plan,
    const struct starsead_packet_model_input *input,
    enum starsead_packet_action *action) {
    if (action != NULL) *action = STARSEAD_PACKET_NONE;
    if (config == NULL || plan == NULL || input == NULL || action == NULL ||
        input->direction > STARSEAD_PACKET_OUTPUT || input->protocol > STARSEAD_PACKET_ICMP) {
        return STARSEAD_CONFIG_INVALID;
    }
    if (plan->no_op) return 0;

    if (config->enable_fake_dns && !input->ipv6 && input->protocol == STARSEAD_PACKET_ICMP &&
        input->icmp_echo) {
        *action = STARSEAD_PACKET_FAKE_DNS_REDIRECT;
        return 0;
    }
    if (input->protocol != STARSEAD_PACKET_TCP && input->protocol != STARSEAD_PACKET_UDP) return 0;
    bool forced_ipv6_dns = config->enable_local_dns && !config->disable_system_ipv6 &&
        input->ipv6 && input->protocol == STARSEAD_PACKET_UDP && input->destination_port_53;
    if (input->ipv6 && !config->enable_ipv6 && !forced_ipv6_dns) return 0;

    enum starsead_packet_action normal = STARSEAD_PACKET_NONE;
    if (input->direction == STARSEAD_PACKET_OUTPUT) {
        if (input->output_virtual_interface) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (input->bypass_uid) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (config->enable_local_dns && input->protocol == STARSEAD_PACKET_UDP &&
            input->destination_port_53 && !input->core_gid) {
            normal = STARSEAD_PACKET_MARK_PRIMARY;
        } else if (input->local_address) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (input->proxy_private) {
            normal = STARSEAD_PACKET_MARK_PRIMARY;
        } else if (input->bypass_private) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (input->output_ignored_interface || input->core_gid) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (!plan->uses_matcher &&
            config->app_policy_mode == STARSEAD_APP_POLICY_BLACKLIST && input->uid_listed) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (packet_is_selected(config, plan, input)) {
            normal = selected_packet_action(config, input->direction);
        }
    } else {
        if (config->enable_local_dns && input->protocol == STARSEAD_PACKET_UDP &&
            input->destination_port_53) {
            normal = selected_packet_action(config, input->direction);
        } else if (input->local_address) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (input->proxy_private) {
            normal = selected_packet_action(config, input->direction);
        } else if (input->bypass_private) {
            normal = STARSEAD_PACKET_RETURN;
        } else if (input->primary_marked || input->hotspot_input ||
            (plan->uses_matcher && input->matcher_selected)) {
            normal = selected_packet_action(config, input->direction);
        }
    }
    *action = normal;
    return 0;
}
