// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

/* Private interface shared by the starsead_supervisor*.c modules. */
#ifndef STARSEAD_SUPERVISOR_H
#define STARSEAD_SUPERVISOR_H

#include "starsead.h"
#include "bpf_cgroup_owned.h"
#include "starsead_keyguard.h"
#include "starsead_compat.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__) || defined(__ANDROID__)
#include "udp6_recovery_netlink.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/syscall.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
#endif

/* Runtime reactor state, owned by starsead_runtime.c. */
struct starsead_runtime {
    struct starsead_runtime_reactor_backend backend;
    void *backend_context;
    bool dispatching;
    const struct starsead_config *config;
    struct starsead_state_document *state;
    struct starsead_control_live_context live;
    const struct starsead_runtime_effect_backend *effects;
    struct starsead_lifecycle lifecycle;
    struct starsead_runtime_delta pending_delta;
    bool supervising;
    bool starting_event_published;
};

bool starsead_runtime_lifecycle_stop_requested(void *opaque);

#if defined(__linux__) || defined(__ANDROID__)
/*
 * Captured action stdout starts at the initial size and doubles on demand.
 * Vendor ROMs can list far more than 64 KiB of rules in one table.
 */
#define STARSEAD_ACTION_STDOUT_INITIAL (64U * 1024U)
#define STARSEAD_ACTION_STDOUT_LIMIT STARSEAD_BYTE_BUFFER_MAX_BYTES

/* Supervisor-side waits for the DNS enhancement companion. */
#define SYSTEM_DNS_COMMAND_WAIT_MILLIS 2100
#define SYSTEM_DNS_WITHDRAW_BUDGET_MILLIS 5000
#define SYSTEM_DNS_TRANSITION_BUDGET_MILLIS 8000
#define SYSTEM_DNS_LEGACY_RELEASE_RETRY_MILLIS 20U

/* Steps of a hot DNS transition (suspend, resume or network rebuild). */
enum system_dns_hot_stage {
    SYSTEM_DNS_HOT_IDLE = 0,
    SYSTEM_DNS_HOT_DEACTIVATE = 1,
    SYSTEM_DNS_HOT_RELEASE = 2,
    SYSTEM_DNS_HOT_RELEASED = 3,
    SYSTEM_DNS_HOT_HELLO = 10,
    SYSTEM_DNS_HOT_PREPARE = 11,
    SYSTEM_DNS_HOT_ACTIVATE = 12,
    SYSTEM_DNS_HOT_ACTIVATED = 13,
    SYSTEM_DNS_HOT_WAIT_NETWORK = 14,
};

enum system_tc_netlink_query_kind {
    SYSTEM_TC_NETLINK_QUERY_NONE,
    SYSTEM_TC_NETLINK_QUERY_EXPECTED_FILTER,
    SYSTEM_TC_NETLINK_QUERY_SLOT_FILTER,
    SYSTEM_TC_NETLINK_QUERY_QDISC,
};

/*
 * Rule commands queued for one commit. iptables commands are kept as
 * iptables-restore lines per stage, family and table; stage 1 holds commands
 * queued after the first ip command, so hooks still follow routing. ip
 * commands are kept one per line as "<family> <arguments...>".
 */
#define SYSTEM_RULE_BATCH_STAGES 2U

struct system_rule_command_batch {
    struct starsead_byte_buffer
        xtables[SYSTEM_RULE_BATCH_STAGES][STARSEAD_IP_FAMILY_COUNT][STARSEAD_IP_TABLE_COUNT];
    struct starsead_byte_buffer ip;
    bool ip_started;
    bool active;
};

enum system_rule_snapshot_phase {
    SYSTEM_RULE_SNAPSHOT_NONE,
    SYSTEM_RULE_SNAPSHOT_BEFORE_APPLY,
    SYSTEM_RULE_SNAPSHOT_NEEDS_VERIFY,
    SYSTEM_RULE_SNAPSHOT_AFTER_APPLY,
};

struct system_rule_view {
    char *bytes;
    size_t length;
    bool present;
};

struct system_rule_snapshot {
    struct system_rule_view xtables[STARSEAD_IP_FAMILY_COUNT][STARSEAD_IP_TABLE_COUNT];
    struct system_rule_view ip_rules[STARSEAD_IP_FAMILY_COUNT];
    struct system_rule_view ip_routes[STARSEAD_IP_FAMILY_COUNT];
    enum system_rule_snapshot_phase phase;
    // Set by a completed quiesce: the full-plan listing is current for the
    // rule removal that follows it. Any capture or destroy clears it.
    bool removal_ready;
};

struct starsead_system_supervisor {
    struct starsead_loaded_config loaded_config;
    struct starsead_logger logger;
    struct starsead_state_store store;
    struct starsead_state_document state;
    struct starsead_control_live_context live;
    struct starsead_control_server *control;
    struct starsead_runtime *runtime;
    int signal_fd;
    bool signal_fd_owned;
    int service_timer_fd;
    bool service_timer_fd_owned;
    struct starsead_wifi_monitor wifi_monitor;
    bool wifi_monitor_opened;
    struct starsead_keyguard_monitor *keyguard_monitor;
    struct starsead_service_control_runtime service_control;
    bool service_running;
    bool shutdown_requested;
    bool service_start_requested;
    bool service_pause_requested;
    bool service_resume_requested;
    struct starsead_effect_journal volatile_effects;
    uint64_t dns_instance, dns_cycle, dns_generation;
    struct starsead_dns_client dns;
    uint64_t dns_hot_deadline, dns_startup_deadline;
    uint64_t dns_hot_completed_generation;
    bool dns_regenerating, dns_drain_waiting;
    int dns_hot_stage, dns_hot_result;
    struct starsead_control_request dns_hot_request;
    bool stop_requested;
    bool automatic_stop_pending;
    bool manual_stop_pending;
    bool stopping_children;
    struct starsead_process_spec core_spec;
    struct starsead_child_process core_process;
    struct starsead_child_setup_stream core_setup;
    struct starsead_child_identity core_identity;
    bool core_spec_ready;
    bool core_spawned;
    bool core_identity_ready;
    bool core_reaped;
    struct starsead_child_exit_status core_exit;
    struct starsead_helper_launch helper_launch;
    struct starsead_child_process helper_process;
    struct starsead_child_setup_stream helper_setup;
    struct starsead_child_identity helper_identity;
    bool helper_launch_ready;
    bool helper_spawned;
    bool helper_identity_ready;
    bool helper_reaped;
    struct starsead_child_exit_status helper_exit;
    struct starsead_readiness_tracker helper_readiness;
    int helper_readiness_result;
    struct starsead_tun_compat_state tun_compatibility;
    struct starsead_matcher_launch matcher_launch;
    struct starsead_matcher_pin_plan matcher_pin_plan;
    struct starsead_matcher_verification matcher_verification;
    bool matcher_launch_ready;
    bool matcher_plan_ready;
    bool matcher_verified;
    struct starsead_b2s_pin_plan b2s_pin_plan;
    struct starsead_b2s_verification b2s_verification;
    bool b2s_plan_ready;
    bool b2s_verified;
    char b2s_verify_error[128U];
    int tc_netlink_fd;
    uint32_t tc_netlink_port_id;
    bool tc_netlink_fd_owned;
    bool tc_netlink_active;
    bool tc_netlink_done;
    bool tc_netlink_failed;
    uint32_t tc_netlink_sequence;
    uint32_t tc_netlink_interface_index;
    enum system_tc_netlink_query_kind tc_netlink_query;
    struct starsead_tc_filter_expectation tc_filter_netlink_expectation;
    enum starsead_rules_slot_state tc_filter_netlink_state;
    enum starsead_rules_slot_state tc_qdisc_netlink_state;
    enum starsead_pin_batch_kind active_pin_batch;
    bool pin_batch_active;
    struct starsead_child_process action_process;
    struct starsead_child_setup_stream action_setup;
    struct starsead_child_identity action_identity;
    bool action_spawned;
    bool action_identity_ready;
    bool action_reaped;
    struct starsead_child_exit_status action_exit;
    bool action_capture_stdout;
    bool action_stdout_overflow;
    /* Captured stdout grows on demand up to STARSEAD_ACTION_STDOUT_LIMIT. */
    char *action_stdout;
    size_t action_stdout_capacity;
    size_t action_stdout_length;
    /* Leading stderr bytes, reported only when an action fails. */
    char action_stderr[512U];
    size_t action_stderr_length;
    struct system_rule_command_batch rule_commands;
    struct system_rule_snapshot rule_snapshot;
    /* Table listings shared by one owned-resource reconciliation. */
    struct system_rule_view owned_xtables[STARSEAD_IP_FAMILY_COUNT][STARSEAD_IP_TABLE_COUNT];
    bool owned_xtables_valid[STARSEAD_IP_FAMILY_COUNT][STARSEAD_IP_TABLE_COUNT];
    bool owned_snapshot_enabled;
    bool verify_private_rules;
    size_t verified_private_rule_count;
    struct starsead_system_process_context process_context;
    struct starsead_readiness_backend readiness_backend;
    struct starsead_stop_backend stop_backend;
    struct starsead_readiness_tracker readiness;
    int readiness_result;
    struct starsead_stop_coordinator stop_coordinator;
    int stop_result;
    struct starsead_network_runtime network;
    bool network_opened;
    struct starsead_network_effect_sink network_effect_sink;
    struct starsead_rules_runtime rules_runtime;
    struct starsead_rules_backend rules_backend;
    bool rules_initialized;
    bool rules_verified;
    bool rules_reconciling;
    bool local_address_snapshot_active;
    struct starsead_address_set local_ipv4_snapshot;
    struct starsead_address_set local_ipv6_snapshot;
    bool cleanup_in_progress;
    bool owned_cleanup_in_progress;
};

/* starsead_supervisor.c */
int starsead_supervisor_runtime_clock(void *opaque, int64_t *milliseconds);
void starsead_supervisor_runtime_fatal(struct starsead_runtime_delta *delta,
    enum starsead_component component, const char *message);
void starsead_supervisor_runtime_note_action_exit(
    struct starsead_system_supervisor *system, int status);
int starsead_supervisor_runtime_reap_children(
    struct starsead_system_supervisor *system, struct starsead_runtime_delta *delta);
int starsead_supervisor_runtime_request_stop(void *opaque);
int starsead_supervisor_accept_pump_delta(struct starsead_system_supervisor *system,
    struct starsead_runtime_delta *delta);
int starsead_supervisor_tun_compatibility_prepare(struct starsead_system_supervisor *system);
int starsead_supervisor_tun_compatibility_cleanup(struct starsead_system_supervisor *system);
int starsead_supervisor_start_helper_process(
    struct starsead_system_supervisor *system, struct starsead_child_identity *identity);
bool starsead_supervisor_stop_done(void *opaque);

/* starsead_supervisor_action.c */
bool starsead_supervisor_action_should_cancel(
    bool stop_requested, bool cleanup_in_progress);
int starsead_supervisor_pump_condition(struct starsead_system_supervisor *system,
    const struct starsead_deadline *deadline, starsead_runtime_predicate predicate);
int starsead_supervisor_pump_condition_periodic(struct starsead_system_supervisor *system,
    const struct starsead_deadline *deadline, uint32_t interval,
    starsead_runtime_predicate predicate);
int starsead_supervisor_pump_stop(struct starsead_system_supervisor *system);
void starsead_supervisor_log_failure(struct starsead_system_supervisor *system,
    enum starsead_component component, const char *message);
void starsead_supervisor_action_stderr_summary(
    const struct starsead_system_supervisor *system, char *out, size_t size);
bool starsead_supervisor_message_fit(char *message, size_t size, int written);
int starsead_supervisor_action_stdout_reserve(
    struct starsead_system_supervisor *system, size_t wanted);
void starsead_supervisor_action_stdout_release(struct starsead_system_supervisor *system);
int starsead_supervisor_action_run_spec(struct starsead_system_supervisor *system,
    const struct starsead_process_spec *spec, bool capture_stdout, int *exit_status);
int starsead_supervisor_action_run_argv(struct starsead_system_supervisor *system,
    const char *const *argv, bool capture_stdout, int *exit_status);
int starsead_supervisor_rule_batch_append_line(
    struct starsead_byte_buffer *batch, const char *const *tokens);
void starsead_supervisor_rule_batch_destroy(struct system_rule_command_batch *batch);
int starsead_supervisor_rule_batch_begin(struct system_rule_command_batch *batch);
int starsead_supervisor_rule_batch_flush(
    struct starsead_system_supervisor *system);
int starsead_supervisor_effect_capability(void *opaque);

/* starsead_supervisor_rtnl.c */
int starsead_supervisor_rtnl_ip(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, const char *const *arguments,
    size_t argument_count, bool capture, int *exit_status);
int starsead_supervisor_rtnl_route_render(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, const uint32_t *tables, size_t table_count);
int starsead_supervisor_rtnl_tether_offload(struct starsead_system_supervisor *system,
    uint32_t interface_index, uint32_t priority, bool remove, bool *present);

/* starsead_supervisor_xtables.c */
const char *starsead_supervisor_table_name(enum starsead_ip_table table);
int starsead_supervisor_xtables_run(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, enum starsead_ip_table table,
    const char *operation, const char *chain, const char *const *arguments,
    size_t argument_count, bool capture_stdout, int *exit_status);
int starsead_supervisor_xtables(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, enum starsead_ip_table table,
    const char *operation, const char *chain, const char *const *arguments,
    size_t argument_count, int *exit_status);
int starsead_supervisor_xtables_zero(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, enum starsead_ip_table table,
    const char *operation, const char *chain, const char *const *arguments,
    size_t argument_count);
int starsead_supervisor_create_private_group(struct starsead_system_supervisor *system,
    const struct starsead_private_chain_group *group);
int starsead_supervisor_verify_private_group_contents(
    struct starsead_system_supervisor *system,
    const struct starsead_private_chain_group *group);
int starsead_supervisor_remove_private_group(struct starsead_system_supervisor *system,
    const struct starsead_private_chain_group *group);
int starsead_supervisor_apply_hook_group(struct starsead_system_supervisor *system,
    const struct starsead_traffic_hook_group *group);
int starsead_supervisor_probe_hook_group(struct starsead_system_supervisor *system,
    const struct starsead_traffic_hook_group *group, bool *all_present,
    bool *any_present);
int starsead_supervisor_remove_hook_group(struct starsead_system_supervisor *system,
    const struct starsead_traffic_hook_group *group);
const struct starsead_private_chain_group *starsead_supervisor_find_private_group(
    const struct starsead_system_supervisor *system,
    const struct starsead_iptables_chain_resource *resource);
const struct starsead_traffic_hook_group *starsead_supervisor_find_hook_group(
    const struct starsead_system_supervisor *system,
    const struct starsead_iptables_rule_resource *resource);
int starsead_supervisor_probe_private_group(struct starsead_system_supervisor *system,
    const struct starsead_private_chain_group *group, bool *all_present,
    bool *any_present);
int starsead_supervisor_ip_command(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, const char *const *arguments,
    size_t argument_count, bool capture, int *exit_status);
int starsead_supervisor_ip_zero(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, const char *const *arguments, size_t argument_count);
void starsead_supervisor_rule_snapshot_destroy(struct system_rule_snapshot *snapshot);
void starsead_supervisor_rule_view_destroy(struct system_rule_view *view);
int starsead_supervisor_rule_view_capture_table(struct starsead_system_supervisor *system,
    enum starsead_ip_family family, enum starsead_ip_table table,
    struct system_rule_view *view);
void starsead_supervisor_rule_views_capture_family(
    struct starsead_system_supervisor *system, enum starsead_ip_family family,
    const bool *wanted, struct system_rule_view *views, bool *filled);
int starsead_supervisor_rule_snapshot_refresh_mangle(
    struct starsead_system_supervisor *system,
    const struct starsead_rule_transaction_plan *plan, const bool *changed);
int starsead_supervisor_rule_snapshot_capture(
    struct starsead_system_supervisor *system,
    const struct starsead_rule_transaction_plan *plan,
    enum system_rule_snapshot_phase phase);

/* starsead_supervisor_tc.c */
int starsead_supervisor_tc_command(struct starsead_system_supervisor *system,
    const char *const *arguments, size_t argument_count, bool capture,
    int *exit_status);
int starsead_supervisor_tc_zero(struct starsead_system_supervisor *system,
    const char *const *arguments, size_t argument_count);
void starsead_supervisor_tc_netlink_close(struct starsead_system_supervisor *system);
int starsead_supervisor_tc_netlink_dispatch(
    struct starsead_system_supervisor *system, short ready);
int starsead_supervisor_expected_tc_netlink_probe(
    struct starsead_system_supervisor *system,
    const struct starsead_tc_filter_resource *resource,
    enum starsead_rules_slot_state *state);
const char *starsead_supervisor_tc_direction(enum starsead_tc_direction direction);
int starsead_supervisor_tc_probe_qdisc(struct starsead_system_supervisor *system,
    const struct starsead_tc_qdisc_resource *resource, bool *present);
int starsead_supervisor_tc_probe_filter(struct starsead_system_supervisor *system,
    const struct starsead_tc_filter_resource *resource,
    enum starsead_rules_slot_state *state);
int starsead_supervisor_tc_apply_qdisc(struct starsead_system_supervisor *system,
    const struct starsead_tc_qdisc_resource *resource);
int starsead_supervisor_tc_apply_filter(struct starsead_system_supervisor *system,
    const struct starsead_tc_filter_resource *resource);

/* starsead_supervisor_rules.c */
int starsead_supervisor_remove_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect);
int starsead_supervisor_classify_route_effect(struct starsead_system_supervisor *system,
    const struct starsead_route_effect *effect,
    enum starsead_rules_slot_state *state);
const char *starsead_supervisor_pin_path(
    struct starsead_system_supervisor *system, enum starsead_pin_id pin_id);
uint64_t starsead_supervisor_verified_pin_id(
    const struct starsead_system_supervisor *system, enum starsead_pin_id pin_id);
const struct starsead_b2s_verified_pin *starsead_supervisor_b2s_verified_pin(
    const struct starsead_system_supervisor *system, enum starsead_pin_id pin_id);
/* 0: name still resolves to index; 1: that link is gone or was replaced by a
 * same-name link; -1: the lookup itself failed, so the link may still be live. */
int starsead_supervisor_interface_state(const char *name, uint32_t index);
int starsead_supervisor_sysctl_read(
    const struct starsead_sysctl_resource *resource, uint8_t *value);
struct starsead_effect_backend starsead_supervisor_effect_backend(
    struct starsead_system_supervisor *system);
int starsead_supervisor_effect_apply_sysctl(
    struct starsead_system_supervisor *system,
    const struct starsead_sysctl_resource *resource,
    char *error, size_t error_size);
int starsead_supervisor_apply_and_verify(
    struct starsead_system_supervisor *system,
    const struct starsead_resource_operation *records,
    size_t count, char *error, size_t error_size);
void starsead_supervisor_rules_backend_init(struct starsead_system_supervisor *system);
int starsead_supervisor_network_effect_dispatch(void *opaque,
    const struct starsead_network_effect_request *request,
    char *error, size_t error_size);
int starsead_supervisor_sync_ipv6_guard(struct starsead_system_supervisor *system);
int starsead_supervisor_effect_rules(
    void *opaque, bool *active, uint64_t *generation, uint32_t *categories);

/* starsead_supervisor_network.c */
int starsead_supervisor_effect_start_matcher(void *opaque);
int starsead_supervisor_effect_start_helper(
    void *opaque, struct starsead_child_identity *identity);
int starsead_supervisor_effect_open_network(void *opaque);
int starsead_supervisor_collect_local_address_set(
    struct starsead_system_supervisor *system,
    int family,
    struct starsead_address_set *set);
int starsead_supervisor_capture_local_address_snapshot(
    struct starsead_system_supervisor *system);
void starsead_supervisor_clear_local_address_snapshot(
    struct starsead_system_supervisor *system);
int starsead_supervisor_reconcile_iptables_local_bypass(
    struct starsead_system_supervisor *system, bool *changed);
int starsead_supervisor_reconcile_b2s_local_maps(
    struct starsead_system_supervisor *system);
int starsead_supervisor_reconcile_hotspot_tc(struct starsead_system_supervisor *system);

/* starsead_supervisor_reconcile.c */
uint32_t starsead_supervisor_rule_categories(const struct starsead_config *config);
void starsead_supervisor_reconcile_error(
    char *error, size_t error_size, const char *message);
int starsead_supervisor_reconcile_remove_phase(
    void *opaque,
    enum starsead_reconcile_phase phase,
    char *error,
    size_t error_size);
int starsead_supervisor_reconcile_remove_tc_filters(
    struct starsead_system_supervisor *system,
    char *error,
    size_t error_size);
int starsead_supervisor_reconcile_owned_resources(
    struct starsead_system_supervisor *system,
    char *error,
    size_t error_size);

/* starsead_supervisor_dns.c */
int starsead_supervisor_dns_exchange(struct starsead_system_supervisor *system, uint16_t opcode);
int starsead_supervisor_dns_prepare(struct starsead_system_supervisor *system);
int starsead_supervisor_dns_withdraw(struct starsead_system_supervisor *system);
int starsead_supervisor_runtime_request_dns(
    void *opaque, const struct starsead_control_request *request, bool poll);
bool starsead_supervisor_dns_waiting_for_rules(struct starsead_system_supervisor *system);
int starsead_supervisor_dns_activate(struct starsead_system_supervisor *system, uint64_t now);
int starsead_supervisor_dns_verify_startup(struct starsead_system_supervisor *system);
int starsead_supervisor_dns_tick(
    struct starsead_system_supervisor *system, struct starsead_runtime_delta *delta);

#endif

#endif
