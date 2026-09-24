// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#ifndef STARSEAD_H
#define STARSEAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

struct starsead_resource_operation;

#define STARSEAD_CONFIG_VERSION 3U
#define STARSEAD_MAX_JSON_SIZE (8U * 1024U * 1024U)
#define STARSEAD_JSON_MAX_TOKENS 262144U
#define STARSEAD_JSON_MAX_DEPTH 64U
#define STARSEAD_MAX_PATH 512U
#define STARSEAD_MAX_SECRET_KEY 4096U
#define STARSEAD_MAX_INTERFACES 64U
#define STARSEAD_MAX_INTERFACE_NAME 64U
#define STARSEAD_MAX_CIDRS 512U
#define STARSEAD_MAX_DIRECT_CIDRS 32768U
#define STARSEAD_MAX_CIDR 64U
#define STARSEAD_MAX_UIDS 8192U
#define STARSEAD_MAX_HOST 256U
#define STARSEAD_MAX_TUNNEL_NAME 64U
#define STARSEAD_MAX_ADDRESSES 256U
#define STARSEAD_MAX_CHAIN_NAME 29U
#define STARSEAD_MAX_NETWORK_EVENTS 16U
#define STARSEAD_MAX_EMERGENCY_PROCESSES 8U
#define STARSEAD_MAX_COMMAND_MARKER 256U
#define STARSEAD_SYNC_DEBOUNCE_MILLIS 1500
#define STARSEAD_STATE_VERSION 2U
#define STARSEAD_LEGACY_ROUTE_LOCALNET_LEAF "starsead.state.route-localnet"
#define STARSEAD_MAX_CHILD_ARGV 16U
#define STARSEAD_MAX_PROCESS_ARGV 32U
#define STARSEAD_MAX_CHILD_ARG 512U
#define STARSEAD_MAX_STATE_MESSAGE 4096U
#define STARSEAD_MAX_HEX_ID 129U
#define STARSEAD_BPF_PROGRAM_TAG_HEX_LENGTH 16U
#define STARSEAD_TC_BPF_FLAG_ACT_DIRECT UINT32_C(1)
#define STARSEAD_TC_PARENT_CLSACT_INGRESS UINT32_C(0xfffffff2)
#define STARSEAD_TC_PARENT_CLSACT_EGRESS UINT32_C(0xfffffff3)
#define STARSEAD_TC_PARENT_CLSACT UINT32_C(0xfffffff1)
#define STARSEAD_TC_HANDLE_CLSACT UINT32_C(0xffff0000)
#define STARSEAD_LOG_MAX_CHILD_LINE 16384U
#define STARSEAD_LOG_MAX_BUFFERED_PER_CHILD 65536U
#define STARSEAD_LOG_PARTIAL_TIMEOUT_MILLIS 2000U
#define STARSEAD_LOG_REDACTION "[REDACTED]"
#define STARSEAD_CONTROL_PROTOCOL_VERSION 1U
#define STARSEAD_CONTROL_MAX_PAYLOAD 65536U
#define STARSEAD_CONTROL_REQUEST_TIMEOUT_MILLIS 2000U
#define STARSEAD_CONTROL_MAX_REQUEST_ID 64U
#define STARSEAD_CONTROL_MAX_CLIENTS 16U
#define STARSEAD_CONTROL_WATCH_QUEUE_CAPACITY 262144U
#define STARSEAD_CONTROL_WATCH_STALL_MILLIS 5000U
#define STARSEAD_PROCESS_MAX_ENV 256U
#define STARSEAD_PROCESS_MAX_INHERITED_ENV 1024U
#define STARSEAD_PROCESS_MAX_ENV_ENTRY 8192U
#define STARSEAD_PROCESS_MAX_INHERITED_FDS 8U
#define STARSEAD_PROCESS_TERM_GRACE_MILLIS 3000U
#define STARSEAD_PROCESS_KILL_REAP_MILLIS 1000U
#define STARSEAD_READINESS_POLL_INTERVAL_MILLIS 100U
#define STARSEAD_ANONYMOUS_CREATE_CLOEXEC UINT32_C(0x1)
#define STARSEAD_ANONYMOUS_CREATE_ALLOW_SEALING UINT32_C(0x2)
#define STARSEAD_ANONYMOUS_SEAL_WRITE UINT32_C(0x1)
#define STARSEAD_ANONYMOUS_SEAL_GROW UINT32_C(0x2)
#define STARSEAD_ANONYMOUS_SEAL_SHRINK UINT32_C(0x4)
#define STARSEAD_ANONYMOUS_SEAL_SEAL UINT32_C(0x8)
#define STARSEAD_ANONYMOUS_REQUIRED_SEALS (STARSEAD_ANONYMOUS_SEAL_WRITE | \
    STARSEAD_ANONYMOUS_SEAL_GROW | STARSEAD_ANONYMOUS_SEAL_SHRINK | \
    STARSEAD_ANONYMOUS_SEAL_SEAL)
#define STARSEAD_IPTABLES_WAIT_SECONDS 100U
#define STARSEAD_ROUTE_RULE_PRIORITY 14599U
#define STARSEAD_PRIMARY_MARK UINT32_C(0x20000000)
#define STARSEAD_MARK_MASK UINT32_C(0x60000000)
#define STARSEAD_TPROXY_TABLE 160U
#define STARSEAD_TUN_TABLE 168U
#define STARSEAD_RULE_PLAN_MAX_OPERATIONS 32U
#define STARSEAD_RULE_TRANSACTION_MAX_GROUPS 12U
#define STARSEAD_RULE_TRANSACTION_MAX_NAMES 4U
#define STARSEAD_RULE_TRANSACTION_MAX_HOOKS 4U
#define STARSEAD_XTABLES_MAX_HOOK_ARGUMENTS 10U
#define STARSEAD_RULE_TRANSACTION_MAX_ROUTES 8U
#define STARSEAD_MAX_POLL_SOURCES 64U
#define STARSEAD_MAX_NETWORK_IMMEDIATE_REQUESTS 64U
#define STARSEAD_MAX_CRON_EXPRESSION 256U
#define STARSEAD_MAX_WIFI_IDENTIFIERS 64U
#define STARSEAD_MAX_WIFI_SSID_BYTES 32U

#define STARSEAD_CONFIG_INVALID (-1)
#define STARSEAD_CONFIG_UNSUPPORTED_COMBINATION (-2)
#define STARSEAD_CONFIG_NOT_READY (-3)
#define STARSEAD_CONFIG_NO_MEMORY (-4)
#define STARSEAD_CONFIG_IO (-5)
#define STARSEAD_LIFECYCLE_START_FAILED (-1)
#define STARSEAD_LIFECYCLE_STOP_REQUESTED (-2)
#define STARSEAD_LIFECYCLE_PARTIAL_FAILURE (-3)
#define STARSEAD_LIFECYCLE_STOP_FAILED (-4)
#define STARSEAD_LIFECYCLE_CORE_EXITED (-5)
#define STARSEAD_LIFECYCLE_HELPER_EXITED (-6)

#define STARSEAD_STATE_OK 0
#define STARSEAD_STATE_NOT_FOUND 1
#define STARSEAD_STATE_INVALID (-20)
#define STARSEAD_STATE_INCOMPATIBLE (-21)
#define STARSEAD_STATE_IO (-22)
#define STARSEAD_STATE_NO_MEMORY (-23)
#define STARSEAD_STATE_WRITE_BLOCKED (-24)

#define STARSEAD_LOG_OK 0
#define STARSEAD_LOG_INVALID (-30)
#define STARSEAD_LOG_IO (-31)
#define STARSEAD_LOG_NO_MEMORY (-32)

enum starsead_json_type {
    STARSEAD_JSON_OBJECT,
    STARSEAD_JSON_ARRAY,
    STARSEAD_JSON_STRING,
    STARSEAD_JSON_NUMBER,
    STARSEAD_JSON_TRUE,
    STARSEAD_JSON_FALSE,
    STARSEAD_JSON_NULL,
};

struct starsead_cron_expression {
    uint64_t minutes;
    uint32_t hours;
    uint32_t days_of_month;
    uint16_t months;
    uint8_t days_of_week;
    bool any_day_of_month;
    bool any_day_of_week;
};

int starsead_cron_parse(const char *, struct starsead_cron_expression *);
bool starsead_cron_matches(
    const struct starsead_cron_expression *, const struct tm *);
int starsead_cron_next(
    const struct starsead_cron_expression *, time_t, time_t *);
int starsead_cron_latest_between(
    const struct starsead_cron_expression *, time_t, time_t, time_t *);

enum starsead_service_action {
    STARSEAD_SERVICE_ACTION_NONE,
    STARSEAD_SERVICE_ACTION_START,
    STARSEAD_SERVICE_ACTION_STOP,
    STARSEAD_SERVICE_ACTION_SHUTDOWN,
};

enum starsead_wifi_transition {
    STARSEAD_WIFI_TRANSITION_BASELINE_CONNECTED,
    STARSEAD_WIFI_TRANSITION_BASELINE_DISCONNECTED,
    STARSEAD_WIFI_TRANSITION_CONNECTED,
    STARSEAD_WIFI_TRANSITION_ROAMED,
    STARSEAD_WIFI_TRANSITION_DISCONNECTED,
};

struct starsead_wifi_identity {
    bool has_ssid;
    unsigned char ssid[STARSEAD_MAX_WIFI_SSID_BYTES];
    size_t ssid_length;
    bool has_bssid;
    uint8_t bssid[6U];
};

struct starsead_wifi_ssid_config {
    unsigned char bytes[STARSEAD_MAX_WIFI_SSID_BYTES];
    uint8_t length;
};

struct starsead_wifi_rule_config {
    bool enabled;
    struct starsead_wifi_ssid_config ssids[STARSEAD_MAX_WIFI_IDENTIFIERS];
    size_t ssid_count;
    uint8_t bssids[STARSEAD_MAX_WIFI_IDENTIFIERS][6U];
    size_t bssid_count;
};

struct starsead_schedule_control_config {
    bool enabled;
    struct starsead_cron_expression start;
    struct starsead_cron_expression stop;
};

struct starsead_wifi_control_config {
    bool enabled;
    struct starsead_wifi_rule_config connect_start;
    struct starsead_wifi_rule_config connect_stop;
    struct starsead_wifi_rule_config disconnect_start;
    struct starsead_wifi_rule_config disconnect_stop;
};

struct starsead_service_control_config {
    bool enabled;
    struct starsead_schedule_control_config schedule;
    struct starsead_wifi_control_config wifi;
};

struct starsead_service_control_runtime {
    const struct starsead_service_control_config *config;
    struct starsead_wifi_identity previous_wifi;
    bool wifi_baseline_established;
    bool wifi_connected;
    bool desired_running;
    time_t last_evaluated_time;
};

void starsead_service_control_init(
    struct starsead_service_control_runtime *,
    const struct starsead_service_control_config *, bool, time_t);
void starsead_service_control_set_service_running(
    struct starsead_service_control_runtime *, bool);
bool starsead_wifi_rule_matches(
    const struct starsead_wifi_rule_config *,
    const struct starsead_wifi_identity *);
enum starsead_service_action starsead_service_control_on_wifi(
    struct starsead_service_control_runtime *,
    enum starsead_wifi_transition,
    const struct starsead_wifi_identity *);
enum starsead_service_action starsead_service_control_reconcile_time(
    struct starsead_service_control_runtime *, time_t);

struct starsead_wifi_monitor {
    int fd;
    uint16_t family_id;
    uint32_t sequence;
    struct starsead_wifi_identity baseline_identity;
    uint64_t debounce_deadline_milliseconds;
    uint64_t startup_probe_until_milliseconds;
    bool baseline_connected;
    bool debounce_armed;
    bool integrity_lost;
    bool startup_probe;
    bool opened;
};

int starsead_wifi_monitor_open(struct starsead_wifi_monitor *, char *, size_t);
int starsead_wifi_monitor_fd(const struct starsead_wifi_monitor *);
int starsead_wifi_monitor_baseline(
    const struct starsead_wifi_monitor *, enum starsead_wifi_transition *,
    struct starsead_wifi_identity *);
int starsead_wifi_monitor_handle(
    struct starsead_wifi_monitor *, enum starsead_wifi_transition *,
    struct starsead_wifi_identity *, bool *, char *, size_t);
bool starsead_wifi_monitor_next_deadline(
    const struct starsead_wifi_monitor *, uint64_t *);
int starsead_wifi_monitor_take_reconcile(
    struct starsead_wifi_monitor *, uint64_t, enum starsead_wifi_transition *,
    struct starsead_wifi_identity *, bool *, char *, size_t);
void starsead_wifi_monitor_close(struct starsead_wifi_monitor *);

struct starsead_json_token {
    enum starsead_json_type type;
    size_t start;
    size_t end;
    size_t parent;
    size_t child_count;
};

struct starsead_json_document {
    const char *source;
    size_t source_length;
    struct starsead_json_token *tokens;
    size_t token_count;
    size_t token_capacity;
};

enum starsead_owner {
    STARSEAD_OWNER_NG,
    STARSEAD_OWNER_BOX,
    STARSEAD_OWNER_META,
};

enum starsead_core_type {
    STARSEAD_CORE_XRAY,
    STARSEAD_CORE_SING_BOX,
    STARSEAD_CORE_MIHOMO,
};

enum starsead_mode {
    STARSEAD_MODE_TPROXY,
    STARSEAD_MODE_TUN,
    STARSEAD_MODE_TUN2SOCKS,
    STARSEAD_MODE_BPF2SOCKS,
    STARSEAD_MODE_EBPF,
};

// Supported TUN and eBPF cores own traffic policy and its lifecycle.
static inline bool starsead_mode_core_managed(enum starsead_mode mode) {
    return mode == STARSEAD_MODE_TUN || mode == STARSEAD_MODE_EBPF;
}

enum starsead_app_policy_mode {
    STARSEAD_APP_POLICY_GLOBAL,
    STARSEAD_APP_POLICY_BLACKLIST,
    STARSEAD_APP_POLICY_WHITELIST,
};

enum starsead_helper_type {
    STARSEAD_HELPER_NONE,
    STARSEAD_HELPER_HEV_SOCKS5_TUNNEL,
    STARSEAD_HELPER_BPF2SOCKS,
};

struct starsead_matcher_config {
    bool enabled;
    char executable_path[STARSEAD_MAX_PATH];
};

struct starsead_hev_helper_config {
    char executable_path[STARSEAD_MAX_PATH];
    char socks_host[STARSEAD_MAX_HOST];
    uint16_t socks_port;
    char tunnel_name[STARSEAD_MAX_TUNNEL_NAME];
    uint32_t mtu;
    char ipv4_address[STARSEAD_MAX_CIDR];
    bool has_ipv6_address;
    char ipv6_address[STARSEAD_MAX_CIDR];
    bool multi_queue;
    bool tcp_fast_open;
    uint32_t tcp_read_write_timeout_milliseconds;
    uint32_t udp_read_write_timeout_milliseconds;
};

struct starsead_bpf_helper_config {
    char executable_path[STARSEAD_MAX_PATH];
    char bridge_listen_address[STARSEAD_MAX_HOST];
    uint16_t bridge_port;
    char socks_host[STARSEAD_MAX_HOST];
    uint16_t socks_port;
    uint32_t worker_count;
    uint32_t tcp_buffer_size;
    uint32_t max_tcp_sessions;
    uint32_t tcp_connect_timeout_milliseconds;
    uint32_t tcp_idle_timeout_milliseconds;
    uint32_t udp_socket_buffer_size;
    uint32_t udp_batch_size;
    uint32_t max_udp_sessions;
    uint32_t max_udp_bindings;
    uint32_t udp_idle_timeout_seconds;
    uint32_t max_udp_pending_bytes;
    uint32_t dns_transaction_timeout_milliseconds;
};

struct starsead_helper_config {
    enum starsead_helper_type type;
    union {
        struct starsead_hev_helper_config hev;
        struct starsead_bpf_helper_config bpf;
    } value;
};

struct starsead_direct_cidrs {
    char (*ipv4)[STARSEAD_MAX_CIDR];
    size_t ipv4_count;
    char (*ipv6)[STARSEAD_MAX_CIDR];
    size_t ipv6_count;
};

struct starsead_config {
    uint32_t schema_version;
    uint32_t version;
    enum starsead_owner owner;
    enum starsead_core_type core_type;
    enum starsead_mode mode;
    char core_executable_path[STARSEAD_MAX_PATH];
    char core_config_path[STARSEAD_MAX_PATH];
    char state_path[STARSEAD_MAX_PATH];
    char log_path[STARSEAD_MAX_PATH];
    char working_directory[STARSEAD_MAX_PATH];
    uint32_t readiness_timeout_milliseconds;
    bool has_age_secret_key;
    char age_secret_key[STARSEAD_MAX_SECRET_KEY];

    bool enable_ipv6;
    bool disable_system_ipv6;
    bool enable_local_dns;
    bool enable_fake_dns;
    bool has_fake_dns_ipv4_pool;
    char fake_dns_ipv4_pool[STARSEAD_MAX_CIDR];
    char ignored_interfaces[STARSEAD_MAX_INTERFACES][STARSEAD_MAX_INTERFACE_NAME];
    size_t ignored_interface_count;
    char virtual_interfaces[STARSEAD_MAX_INTERFACES][STARSEAD_MAX_INTERFACE_NAME];
    size_t virtual_interface_count;
    char hotspot_interface_prefixes[STARSEAD_MAX_INTERFACES][STARSEAD_MAX_INTERFACE_NAME];
    size_t hotspot_interface_prefix_count;
    char proxy_private_cidrs[STARSEAD_MAX_CIDRS][STARSEAD_MAX_CIDR];
    size_t proxy_private_cidr_count;
    char bypass_private_cidrs[STARSEAD_MAX_CIDRS][STARSEAD_MAX_CIDR];
    size_t bypass_private_cidr_count;
    enum starsead_app_policy_mode app_policy_mode;
    uint32_t *uids;
    size_t uid_count;
    uint32_t *bypass_uids;
    size_t bypass_uid_count;
    bool has_direct_cidr_paths;
    char direct_cidr_path_v4[STARSEAD_MAX_PATH];
    char direct_cidr_path_v6[STARSEAD_MAX_PATH];
    struct starsead_direct_cidrs *direct_cidrs;

    bool has_transparent_port;
    uint16_t transparent_port;
    bool has_tunnel_name;
    char tunnel_name[STARSEAD_MAX_TUNNEL_NAME];
    struct starsead_matcher_config matcher;
    struct starsead_helper_config helper;
    struct starsead_service_control_config service_control;

};

enum starsead_rule_plan_operation_kind {
    STARSEAD_RULE_PLAN_PREPARE_PRIVATE,
    STARSEAD_RULE_PLAN_PREPARE_LOCAL_BYPASS,
    STARSEAD_RULE_PLAN_PREPARE_MATCHER,
    STARSEAD_RULE_PLAN_PREPARE_ROUTE,
    STARSEAD_RULE_PLAN_PREPARE_HELPER_TC,
    STARSEAD_RULE_PLAN_POPULATE_POLICY,
    STARSEAD_RULE_PLAN_POPULATE_DNS,
    STARSEAD_RULE_PLAN_POPULATE_FAKE_DNS,
    STARSEAD_RULE_PLAN_ACTIVATE_MAIN,
    STARSEAD_RULE_PLAN_ACTIVATE_DNS,
    STARSEAD_RULE_PLAN_ACTIVATE_FAKE_DNS,
    STARSEAD_RULE_PLAN_QUIESCE_MAIN,
    STARSEAD_RULE_PLAN_QUIESCE_DNS,
    STARSEAD_RULE_PLAN_QUIESCE_FAKE_DNS,
    STARSEAD_RULE_PLAN_REMOVE_PRIVATE,
};

struct starsead_rule_plan_operation {
    enum starsead_rule_plan_operation_kind kind;
    bool traffic_activation;
};

struct starsead_rule_plan {
    bool no_op;
    bool enable_ipv6;
    bool uses_matcher;
    bool uses_helper_tc;
    uint32_t iptables_wait_seconds;
    uint32_t route_rule_priority;
    uint32_t primary_mark;
    uint32_t mark_mask;
    uint32_t routing_table;
    char tunnel_name[STARSEAD_MAX_TUNNEL_NAME];
    bool has_token_ipv6_route;
    char token_ipv6_prefix[STARSEAD_MAX_CIDR];
    struct starsead_rule_plan_operation operations[STARSEAD_RULE_PLAN_MAX_OPERATIONS];
    size_t operation_count;
    size_t first_activation;
};

int starsead_rule_plan_build(
    const struct starsead_config *, bool, struct starsead_rule_plan *);
int starsead_rule_quiesce_plan_build(
    const struct starsead_rule_plan *, struct starsead_rule_plan *);
size_t starsead_default_bypass_cidr_count(void);
const char *starsead_default_bypass_cidr(size_t);

enum starsead_packet_direction {
    STARSEAD_PACKET_PREROUTING,
    STARSEAD_PACKET_OUTPUT,
};

enum starsead_packet_protocol {
    STARSEAD_PACKET_TCP,
    STARSEAD_PACKET_UDP,
    STARSEAD_PACKET_ICMP,
};

enum starsead_packet_action {
    STARSEAD_PACKET_NONE,
    STARSEAD_PACKET_RETURN,
    STARSEAD_PACKET_MARK_PRIMARY,
    STARSEAD_PACKET_TPROXY,
    STARSEAD_PACKET_DROP,
    STARSEAD_PACKET_REJECT,
    STARSEAD_PACKET_FAKE_DNS_REDIRECT,
};

struct starsead_packet_model_input {
    enum starsead_packet_direction direction;
    enum starsead_packet_protocol protocol;
    bool ipv6;
    bool destination_port_53;
    bool icmp_echo;
    bool bypass_uid;
    bool uid_listed;
    bool core_gid;
    bool local_address;
    bool proxy_private;
    bool bypass_private;
    bool primary_marked;
    bool output_virtual_interface;
    bool output_ignored_interface;
    bool hotspot_input;
    bool matcher_selected;
};

int starsead_packet_model_decide(
    const struct starsead_config *, const struct starsead_rule_plan *,
    const struct starsead_packet_model_input *, enum starsead_packet_action *);

struct starsead_lifecycle_backend {
    int (*acquire)(void *);
    int (*start_core)(void *);
    int (*wait_core)(void *);
    int (*ensure_platform_capability)(void *);
    int (*start_helper)(void *);
    int (*wait_helper)(void *);
    int (*start_matcher)(void *);
    int (*open_network)(void *);
    int (*apply_rules)(void *);
    int (*verify)(void *);
    bool (*stop_requested)(void *);
    int (*quiesce_traffic)(void *);
    int (*remove_rules)(void *);
    int (*close_network)(void *);
    int (*stop_matcher)(void *);
    int (*stop_helper)(void *);
    int (*stop_core)(void *);
    int (*restore_best_effort)(void *);
    int (*release)(void *);
};

struct starsead_lifecycle_effect {
    bool attempted;
    bool cleanup_required;
    bool succeeded;
};

struct starsead_lifecycle_capability {
    bool attempted;
    bool succeeded;
    bool partial;
};

struct starsead_lifecycle_options {
    bool has_helper;
    bool has_matcher;
    bool requires_platform_capability;
    bool core_managed_traffic;
};

enum starsead_child_role {
    STARSEAD_CHILD_CORE,
    STARSEAD_CHILD_HELPER,
};

enum starsead_lifecycle_reason {
    STARSEAD_LIFECYCLE_REASON_NONE,
    STARSEAD_LIFECYCLE_REASON_CONTROL_STOP,
    STARSEAD_LIFECYCLE_REASON_SIGTERM,
    STARSEAD_LIFECYCLE_REASON_SIGINT,
    STARSEAD_LIFECYCLE_REASON_START_FAILED,
    STARSEAD_LIFECYCLE_REASON_RUNTIME_FAILED,
    STARSEAD_LIFECYCLE_REASON_CORE_EXITED,
    STARSEAD_LIFECYCLE_REASON_HELPER_EXITED,
};

struct starsead_lifecycle {
    const struct starsead_lifecycle_backend *backend;
    void *backend_context;
    struct starsead_lifecycle_effect acquire;
    struct starsead_lifecycle_effect core;
    struct starsead_lifecycle_effect helper;
    struct starsead_lifecycle_effect matcher;
    struct starsead_lifecycle_effect network;
    struct starsead_lifecycle_effect rules;
    struct starsead_lifecycle_capability platform_capability;
    struct starsead_lifecycle_options options;
    bool initialized;
    bool traffic_may_be_active;
    atomic_bool stop_was_requested;
    bool starting;
    bool stopped;
    const char *failure_stage;
    atomic_bool terminal_latch_locked;
    _Atomic(enum starsead_lifecycle_reason) terminal_reason;
    atomic_bool has_child_exit_status;
    _Atomic(enum starsead_child_role) child_exit_role;
    atomic_int child_exit_status;
};

int starsead_json_parse(const char *, size_t, struct starsead_json_document *, char *, size_t);
void starsead_json_document_destroy(struct starsead_json_document *);
bool starsead_interface_selector_valid(const char *);
bool starsead_interface_matches_selector(const char *, const char *);
int starsead_config_parse(const char *, size_t, struct starsead_config *, char *, size_t);
int starsead_config_load_direct_cidrs(
    struct starsead_config *, const char *, size_t, const char *, size_t);
void starsead_config_destroy(struct starsead_config *);

struct starsead_runtime_directory {
    int fd;
    bool owned;
    uint64_t device;
    uint64_t inode;
    void (*close_owned_fd)(void *, int);
    void *close_context;
};

struct starsead_loaded_config {
    struct starsead_config config;
    struct starsead_runtime_directory directory;
    int config_fd;
    bool config_fd_owned;
    uint64_t config_device;
    uint64_t config_inode;
    void (*close_owned_fd)(void *, int);
    void *close_context;
};

struct starsead_config_load_backend {
    int (*open_directory)(void *, const char *, int *, uint64_t *, uint64_t *);
    int (*read_config)(void *, int, const char *, char **, size_t *, int *, uint64_t *, uint64_t *);
    int (*validate_files)(void *, const struct starsead_config *, int);
    int (*read_resource)(void *, int, const char *, char **, size_t *);
    void (*close_fd)(void *, int);
};

int starsead_config_load(const char *, struct starsead_loaded_config *, char *, size_t);
int starsead_config_load_with_backend(
    const char *,
    struct starsead_loaded_config *,
    const struct starsead_config_load_backend *,
    void *,
    char *,
    size_t);
void starsead_loaded_config_release(struct starsead_loaded_config *);
void starsead_runtime_directory_release(struct starsead_runtime_directory *);
int starsead_runtime_directory_open(
    const char *, struct starsead_runtime_directory *, char *, size_t);
bool starsead_mode_is_readable(uint32_t);
bool starsead_mode_is_writable(uint32_t);
bool starsead_mode_is_executable(uint32_t);

enum starsead_file_kind {
    STARSEAD_FILE_REGULAR,
    STARSEAD_FILE_DIRECTORY,
    STARSEAD_FILE_FIFO,
    STARSEAD_FILE_DEVICE,
    STARSEAD_FILE_SYMLINK,
    STARSEAD_FILE_OTHER,
};

bool starsead_file_requirements_valid(
    enum starsead_file_kind,
    uint32_t,
    enum starsead_file_kind,
    bool,
    bool,
    bool);
int starsead_file_requirements_validate(
    enum starsead_file_kind,
    uint32_t,
    enum starsead_file_kind,
    bool,
    bool,
    bool);
int starsead_load_config(const char *, struct starsead_config *, char *, size_t);

enum starsead_process_output_mode {
    STARSEAD_PROCESS_OUTPUT_CAPTURE,
    STARSEAD_PROCESS_OUTPUT_APPEND_CORE_LOG,
    STARSEAD_PROCESS_OUTPUT_DISCARD,
    STARSEAD_PROCESS_OUTPUT_COUNT,
};

struct starsead_process_spec {
    char executable_path[STARSEAD_MAX_PATH];
    char working_directory[STARSEAD_MAX_PATH];
    char output_path[STARSEAD_MAX_PATH];
    uint32_t uid;
    uint32_t gid;
    char argv[STARSEAD_MAX_PROCESS_ARGV][STARSEAD_MAX_CHILD_ARG];
    size_t argc;
    char **environment;
    size_t environment_count;
    int inherited_fds[STARSEAD_PROCESS_MAX_INHERITED_FDS];
    int inherited_fd_targets[STARSEAD_PROCESS_MAX_INHERITED_FDS];
    size_t inherited_fd_count;
    enum starsead_process_output_mode output_mode;
    bool unlimited_locked_memory;
};

struct starsead_anonymous_document {
    unsigned char *bytes;
    size_t length;
};

struct starsead_helper_documents {
    struct starsead_anonymous_document config;
    bool has_direct_cidrs;
    struct starsead_anonymous_document direct_ipv4;
    struct starsead_anonymous_document direct_ipv6;
};

struct starsead_matcher_documents {
    struct starsead_anonymous_document policy;
    bool has_direct_cidrs;
    struct starsead_anonymous_document direct_ipv4;
    struct starsead_anonymous_document direct_ipv6;
};

struct starsead_anonymous_file {
    int fd;
    bool owned;
    size_t length;
};

struct starsead_anonymous_file_backend {
    void *context;
    int (*create)(void *, const char *, uint32_t, int *);
    ptrdiff_t (*write)(void *, int, const void *, size_t);
    int (*rewind)(void *, int);
    int (*add_seals)(void *, int, uint32_t);
    int (*get_seals)(void *, int, uint32_t *);
    int (*close)(void *, int);
};

struct starsead_helper_launch {
    struct starsead_process_spec process;
    struct starsead_anonymous_file config_file;
    bool has_direct_cidrs;
    struct starsead_anonymous_file direct_ipv4_file;
    struct starsead_anonymous_file direct_ipv6_file;
};

struct starsead_matcher_launch {
    struct starsead_process_spec process;
    struct starsead_anonymous_file policy_file;
    bool has_direct_cidrs;
    struct starsead_anonymous_file direct_ipv4_file;
    struct starsead_anonymous_file direct_ipv6_file;
};

int starsead_core_process_spec(
    const struct starsead_config *, const char *const *,
    struct starsead_process_spec *, char *, size_t);
void starsead_process_spec_destroy(struct starsead_process_spec *);
/* Shared adapter helpers; callers still own the enclosing process spec. */
int starsead_process_environment_rebuild(
    const char *const *, struct starsead_process_spec *);
int starsead_process_environment_add(
    struct starsead_process_spec *, const char *, const char *);
int starsead_process_argument_add(
    struct starsead_process_spec *, const char *);
int starsead_process_core_log_path(
    const struct starsead_process_spec *, char *, size_t);
int starsead_log_sibling_path(const char *, const char *, char *, size_t);
int starsead_helper_render_documents(
    const struct starsead_config *, int, int,
    struct starsead_helper_documents *, char *, size_t);
void starsead_helper_documents_destroy(struct starsead_helper_documents *);
int starsead_helper_process_spec(
    const struct starsead_config *, const char *const *, int, int, int,
    struct starsead_process_spec *, char *, size_t);
int starsead_anonymous_file_create(
    const struct starsead_anonymous_file_backend *, const char *,
    const struct starsead_anonymous_document *, struct starsead_anonymous_file *,
    char *, size_t);
int starsead_anonymous_file_close(
    const struct starsead_anonymous_file_backend *, struct starsead_anonymous_file *);
const struct starsead_anonymous_file_backend *starsead_system_anonymous_file_backend(void);
int starsead_helper_launch_prepare(
    const struct starsead_config *, const char *const *,
    const struct starsead_anonymous_file_backend *,
    struct starsead_helper_launch *, char *, size_t);
int starsead_helper_launch_destroy(
    const struct starsead_anonymous_file_backend *, struct starsead_helper_launch *);
int starsead_matcher_render_documents(
    const struct starsead_config *, int, int,
    struct starsead_matcher_documents *, char *, size_t);
void starsead_matcher_documents_destroy(struct starsead_matcher_documents *);
int starsead_matcher_process_spec(
    const struct starsead_config *, const char *const *, int, int, int,
    struct starsead_process_spec *, char *, size_t);
int starsead_matcher_launch_prepare(
    const struct starsead_config *, const char *const *,
    const struct starsead_anonymous_file_backend *,
    struct starsead_matcher_launch *, char *, size_t);
int starsead_matcher_launch_destroy(
    const struct starsead_anonymous_file_backend *, struct starsead_matcher_launch *);

enum starsead_capability_tool_kind {
    STARSEAD_CAPABILITY_TOOL_NONE,
    STARSEAD_CAPABILITY_TOOL_LIVE_POLICY,
    STARSEAD_CAPABILITY_TOOL_KSUD,
};

struct starsead_platform_capability_backend {
    void *context;
    int (*find_on_path)(void *, const char *, char *, size_t);
    int (*inspect_executable)(void *, const char *, bool *, bool *);
    int (*execute)(void *, const char *const *, char *, size_t, int *);
};

struct starsead_platform_capability_result {
    bool required;
    bool tool_found;
    bool partial_application;
    enum starsead_capability_tool_kind tool_kind;
    char tool_path[STARSEAD_MAX_PATH];
    size_t applied_rule_count;
};

int starsead_platform_capability_ensure(
    const struct starsead_config *,
    const struct starsead_platform_capability_backend *,
    struct starsead_platform_capability_result *, char *, size_t);

struct starsead_child_setup_backend {
    void *context;
    int (*restore_signals)(void *);
    int (*create_session)(void *);
    int (*set_nofile_limit)(void *, uint64_t);
    int (*set_memlock_unlimited)(void *);
    int (*clear_supplementary_groups)(void *);
    int (*set_gid)(void *, uint32_t);
    int (*set_uid)(void *, uint32_t);
    int (*set_parent_death_signal)(void *, int);
    int (*get_parent_pid)(void *, int *);
    int (*prepare_descriptors)(void *, const struct starsead_process_spec *);
    int (*exec_process)(void *, const struct starsead_process_spec *);
};

int starsead_child_setup_run(
    const struct starsead_process_spec *, int,
    const struct starsead_child_setup_backend *, bool *, char *, size_t);

void starsead_lifecycle_init(struct starsead_lifecycle *);
int starsead_lifecycle_start(
    struct starsead_lifecycle *,
    const struct starsead_lifecycle_backend *,
    void *,
    const struct starsead_lifecycle_options *);
int starsead_lifecycle_stop(struct starsead_lifecycle *);
int starsead_lifecycle_request_stop(
    struct starsead_lifecycle *,
    enum starsead_lifecycle_reason);
int starsead_lifecycle_on_child_exit(struct starsead_lifecycle *, enum starsead_child_role, int);

enum starsead_phase {
    STARSEAD_PHASE_VALIDATING,
    STARSEAD_PHASE_ACQUIRING,
    STARSEAD_PHASE_STARTING,
    STARSEAD_PHASE_APPLYING_RULES,
    STARSEAD_PHASE_RUNNING,
    STARSEAD_PHASE_STOPPING,
    STARSEAD_PHASE_STOPPED,
    STARSEAD_PHASE_FAILED,
    STARSEAD_PHASE_COUNT,
};

enum starsead_child_type {
    STARSEAD_CHILD_TYPE_XRAY,
    STARSEAD_CHILD_TYPE_SING_BOX,
    STARSEAD_CHILD_TYPE_MIHOMO,
    STARSEAD_CHILD_TYPE_HEV_SOCKS5_TUNNEL,
    STARSEAD_CHILD_TYPE_BPF2SOCKS,
    STARSEAD_CHILD_TYPE_COUNT,
};

enum starsead_component {
    STARSEAD_COMPONENT_RUNTIME,
    STARSEAD_COMPONENT_CORE,
    STARSEAD_COMPONENT_HELPER,
    STARSEAD_COMPONENT_MATCHER,
    STARSEAD_COMPONENT_RULES,
    STARSEAD_COMPONENT_NETWORK,
    STARSEAD_COMPONENT_STATE,
    STARSEAD_COMPONENT_LOG,
    STARSEAD_COMPONENT_CONTROL,
    STARSEAD_COMPONENT_COUNT,
};

enum starsead_failure_code {
    STARSEAD_FAILURE_START_FAILED,
    STARSEAD_FAILURE_READINESS_TIMEOUT,
    STARSEAD_FAILURE_CHILD_EXITED,
    STARSEAD_FAILURE_STATE_INVALID,
    STARSEAD_FAILURE_STATE_INCOMPATIBLE,
    STARSEAD_FAILURE_RESOURCE_COLLISION,
    STARSEAD_FAILURE_IO_ERROR,
    STARSEAD_FAILURE_STOP_FAILED,
    STARSEAD_FAILURE_INTERNAL_ERROR,
    STARSEAD_FAILURE_CODE_COUNT,
};

enum starsead_rule_category {
    STARSEAD_RULE_CATEGORY_TPROXY,
    STARSEAD_RULE_CATEGORY_ROUTING,
    STARSEAD_RULE_CATEGORY_DNS,
    STARSEAD_RULE_CATEGORY_FAKE_DNS,
    STARSEAD_RULE_CATEGORY_LOCAL_BYPASS,
    STARSEAD_RULE_CATEGORY_HOTSPOT,
    STARSEAD_RULE_CATEGORY_TC,
    STARSEAD_RULE_CATEGORY_BPF,
    STARSEAD_RULE_CATEGORY_IPV6_GUARD,
    STARSEAD_RULE_CATEGORY_COUNT,
};

#define STARSEAD_RULE_CATEGORY_BIT(category) (UINT32_C(1) << (unsigned int)(category))
#define STARSEAD_RULE_CATEGORY_ALL \
    ((UINT32_C(1) << (unsigned int)STARSEAD_RULE_CATEGORY_COUNT) - UINT32_C(1))

struct starsead_child_identity {
    enum starsead_child_role role;
    enum starsead_child_type type;
    int pid;
    int process_group_id;
    uint64_t start_time_ticks;
    uint64_t exe_device;
    uint64_t exe_inode;
    char argv[STARSEAD_MAX_PROCESS_ARGV][STARSEAD_MAX_CHILD_ARG];
    size_t argc;
};

struct starsead_process_identity_backend {
    void *context;
    int (*read_stat)(void *, int, char *, size_t, size_t *);
    int (*read_exe_identity)(void *, int, uint64_t *, uint64_t *);
    int (*read_cmdline)(void *, int, unsigned char *, size_t, size_t *);
};

int starsead_process_identity_read(
    const struct starsead_process_identity_backend *, int,
    enum starsead_child_role, enum starsead_child_type,
    const struct starsead_process_spec *, struct starsead_child_identity *,
    char *, size_t);
const struct starsead_process_identity_backend *starsead_system_process_identity_backend(void);

enum starsead_readiness_result {
    STARSEAD_READINESS_PENDING = 0,
    STARSEAD_READINESS_READY = 1,
    STARSEAD_READINESS_TIMEOUT = -1,
    STARSEAD_READINESS_CONFLICT = -2,
    STARSEAD_READINESS_CHILD_LOST = -3,
    STARSEAD_READINESS_STOP_REQUESTED = -4,
    STARSEAD_READINESS_IO = -5,
};

struct starsead_readiness_tracker {
    enum starsead_child_role role;
    enum starsead_mode mode;
    uint64_t deadline_milliseconds;
    bool initialized;
};

struct starsead_readiness_backend {
    void *context;
    int (*child_alive)(void *, const struct starsead_child_identity *, bool *);
    int (*listener_ready)(void *, int, uint16_t, bool *);
    int (*interface_exists)(void *, const char *, bool *);
};

int starsead_readiness_preflight(
    const struct starsead_config *, enum starsead_child_role,
    const struct starsead_readiness_backend *);
int starsead_readiness_init(
    const struct starsead_config *, enum starsead_child_role, uint64_t,
    const struct starsead_readiness_backend *, struct starsead_readiness_tracker *);
int starsead_readiness_poll(
    const struct starsead_config *, struct starsead_readiness_tracker *,
    const struct starsead_child_identity *, const struct starsead_readiness_backend *,
    uint64_t, bool);

struct starsead_child_exit_status {
    bool has_exit_code;
    int exit_code;
    bool has_signal;
    int signal_number;
};

struct starsead_child_process {
    int pid;
    int process_group_id;
    int pidfd;
    int stdout_fd;
    int stderr_fd;
    int setup_status_fd;
    bool owns_pidfd;
    bool owns_stdout_fd;
    bool owns_stderr_fd;
    bool owns_setup_status_fd;
};

enum starsead_child_setup_message_kind {
    STARSEAD_CHILD_SETUP_WARNING_NOFILE = 1,
    STARSEAD_CHILD_SETUP_FATAL = 2,
};

struct starsead_child_setup_message {
    uint32_t magic;
    uint32_t kind;
    int32_t error_number;
};

#define STARSEAD_CHILD_SETUP_MESSAGE_MAGIC UINT32_C(0x41535432)

struct starsead_child_setup_stream {
    unsigned char partial[sizeof(struct starsead_child_setup_message)];
    size_t partial_length;
    bool nofile_warning;
    int nofile_error_number;
    bool fatal;
    int fatal_error_number;
    bool complete;
};

enum starsead_child_setup_stream_result {
    STARSEAD_CHILD_SETUP_PENDING = 0,
    STARSEAD_CHILD_SETUP_EXECUTED = 1,
    STARSEAD_CHILD_SETUP_FAILED = -1,
    STARSEAD_CHILD_SETUP_INVALID = -2,
};

int starsead_process_spawn_system(
    const struct starsead_process_spec *, struct starsead_child_process *, char *, size_t);
void starsead_child_process_close(struct starsead_child_process *);
int starsead_child_exit_status_from_wait(int, struct starsead_child_exit_status *);
void starsead_child_setup_stream_init(struct starsead_child_setup_stream *);
int starsead_child_setup_stream_feed(
    struct starsead_child_setup_stream *, const void *, size_t, bool);
#if defined(STARSEAD_TESTING)
bool starsead_test_action_identity_wait_can_retry(bool, int64_t, int64_t);
int starsead_test_action_post_setup(
    const struct starsead_child_setup_stream *, bool,
    const struct starsead_child_exit_status *, int *);
int starsead_test_action_post_identity(
    int, int, const struct starsead_child_setup_stream *, bool,
    const struct starsead_child_exit_status *, int *);
#endif

struct starsead_stop_role_state {
    bool present;
    bool cleanup_required;
    bool term_sent;
    bool kill_sent;
    bool signal_failed;
    bool reaped;
    struct starsead_child_identity identity;
    struct starsead_child_exit_status exit_status;
};

struct starsead_stop_coordinator {
    struct starsead_stop_role_state core;
    struct starsead_stop_role_state helper;
    uint64_t term_deadline_milliseconds;
    uint64_t kill_deadline_milliseconds;
    bool active;
    bool initialized;
};

struct starsead_stop_backend {
    void *context;
    int (*identity_valid)(void *, const struct starsead_child_identity *, bool *);
    int (*signal_group)(void *, const struct starsead_child_identity *, int);
    int (*reap)(void *, const struct starsead_child_identity *, bool *,
        struct starsead_child_exit_status *);
};

struct starsead_system_process_context {
    const struct starsead_process_identity_backend *identity_backend;
    const struct starsead_process_spec *core_spec;
    const struct starsead_process_spec *helper_spec;
};

enum starsead_stop_result {
    STARSEAD_STOP_PENDING = 0,
    STARSEAD_STOP_COMPLETE = 1,
    STARSEAD_STOP_FAILED = -1,
};

void starsead_stop_coordinator_init(struct starsead_stop_coordinator *);
int starsead_stop_coordinator_begin(
    struct starsead_stop_coordinator *, const struct starsead_child_identity *,
    const struct starsead_child_identity *, const struct starsead_stop_backend *, uint64_t);
int starsead_stop_coordinator_poll(
    struct starsead_stop_coordinator *, const struct starsead_stop_backend *, uint64_t);
int starsead_system_process_backends_init(
    struct starsead_system_process_context *, const struct starsead_process_spec *,
    const struct starsead_process_spec *, struct starsead_readiness_backend *,
    struct starsead_stop_backend *);

enum starsead_resource_operation_kind {
    STARSEAD_RESOURCE_OPERATION_IPTABLES_CHAIN,
    STARSEAD_RESOURCE_OPERATION_IPTABLES_RULE,
    STARSEAD_RESOURCE_OPERATION_IP_RULE,
    STARSEAD_RESOURCE_OPERATION_ROUTE,
    STARSEAD_RESOURCE_OPERATION_BPF_PIN,
    STARSEAD_RESOURCE_OPERATION_TC_QDISC,
    STARSEAD_RESOURCE_OPERATION_TC_FILTER,
    STARSEAD_RESOURCE_OPERATION_SYSCTL,
    STARSEAD_RESOURCE_OPERATION_TETHER_STATE,
    STARSEAD_RESOURCE_OPERATION_KIND_COUNT,
};

enum starsead_ip_family {
    STARSEAD_IP_FAMILY_IPV4,
    STARSEAD_IP_FAMILY_IPV6,
    STARSEAD_IP_FAMILY_COUNT,
};

enum starsead_ip_table {
    STARSEAD_IP_TABLE_FILTER,
    STARSEAD_IP_TABLE_NAT,
    STARSEAD_IP_TABLE_MANGLE,
    STARSEAD_IP_TABLE_COUNT,
};

enum starsead_chain_id {
    STARSEAD_CHAIN_TPROXY,
    STARSEAD_CHAIN_ROUTING,
    STARSEAD_CHAIN_DNS,
    STARSEAD_CHAIN_FAKE_DNS,
    STARSEAD_CHAIN_LOCAL_BYPASS,
    STARSEAD_CHAIN_HOTSPOT,
    STARSEAD_CHAIN_COUNT,
};

enum starsead_rule_id {
    STARSEAD_RULE_TPROXY_ENTRY,
    STARSEAD_RULE_ROUTING_ENTRY,
    STARSEAD_RULE_DNS_ENTRY,
    STARSEAD_RULE_FAKE_DNS_ENTRY,
    STARSEAD_RULE_LOCAL_BYPASS_ENTRY,
    STARSEAD_RULE_HOTSPOT_ENTRY,
    STARSEAD_RULE_COUNT,
};

enum starsead_ip_rule_id {
    STARSEAD_IP_RULE_TPROXY,
    STARSEAD_IP_RULE_TUNNEL,
    STARSEAD_IP_RULE_TOKEN,
    STARSEAD_IP_RULE_COUNT,
};

enum starsead_route_id {
    STARSEAD_ROUTE_TPROXY,
    STARSEAD_ROUTE_TUNNEL,
    STARSEAD_ROUTE_TOKEN,
    STARSEAD_ROUTE_COUNT,
};

enum starsead_pin_id {
    STARSEAD_PIN_MATCHER_OUTPUT_V4,
    STARSEAD_PIN_MATCHER_OUTPUT_V6,
    STARSEAD_PIN_MATCHER_PREROUTING_V4,
    STARSEAD_PIN_MATCHER_PREROUTING_V6,
    STARSEAD_PIN_BPF2SOCKS_LOCAL_ADDRESS_V4,
    STARSEAD_PIN_BPF2SOCKS_LOCAL_ADDRESS_V6,
    STARSEAD_PIN_BPF2SOCKS_TC_INGRESS,
    STARSEAD_PIN_BPF2SOCKS_TC_EGRESS,
    STARSEAD_PIN_COUNT,
};

struct starsead_owned_chain {
    enum starsead_ip_family family;
    enum starsead_ip_table table;
    const char *name;
};

struct starsead_owned_hook {
    enum starsead_ip_family family;
    enum starsead_ip_table table;
    const char *builtin_chain;
    bool udp_destination_port_53;
    const char *target;
};

struct starsead_owned_policy_rule {
    enum starsead_ip_family family;
    uint32_t table;
    uint32_t priority;
    uint32_t mark;
    uint32_t mark_mask;
    bool invert_from_all;
};

struct starsead_owned_token_route {
    enum starsead_ip_family family;
    uint32_t table;
    const char *destination;
    const char *interface_name;
};

struct starsead_owned_resource_catalog {
    const char *bpf_root;
    const char *b2s_root;
    const char *fake_dns_output_chain;
    const char *fake_dns_prerouting_chain;
    const struct starsead_owned_chain *chains;
    size_t chain_count;
    const struct starsead_owned_hook *hooks;
    size_t hook_count;
    const struct starsead_owned_policy_rule *policy_rules;
    size_t policy_rule_count;
    struct starsead_owned_token_route token_route;
};

const struct starsead_owned_resource_catalog *starsead_owned_resource_catalog(void);
const char *starsead_owned_pin_path(enum starsead_pin_id);

enum starsead_reconcile_phase {
    STARSEAD_RECONCILE_QUIESCE,
    STARSEAD_RECONCILE_PRIVATE_CHAINS,
    STARSEAD_RECONCILE_POLICY_ROUTING,
    STARSEAD_RECONCILE_BPF_PINS,
    STARSEAD_RECONCILE_PHASE_COUNT,
};

struct starsead_reconcile_backend {
    void *context;
    int (*remove_phase)(
        void *, enum starsead_reconcile_phase, char *, size_t);
    int (*verify_absent)(void *, char *, size_t);
    void (*warn)(void *, enum starsead_reconcile_phase, const char *);
};

struct starsead_reconcile_report {
    bool attempted[STARSEAD_RECONCILE_PHASE_COUNT];
    bool verified_absent;
    enum starsead_reconcile_phase failed_phase;
};

int starsead_reconcile_owned_resources(
    const struct starsead_reconcile_backend *,
    struct starsead_reconcile_report *, char *, size_t);
int starsead_owned_policy_rule_output_count(
    const char *, size_t, const struct starsead_owned_policy_rule *, size_t *);

struct starsead_matcher_pin_expectation {
    enum starsead_pin_id pin_id;
    char path[STARSEAD_MAX_PATH];
    char program_name[STARSEAD_MAX_COMMAND_MARKER];
};

struct starsead_matcher_pin_plan {
    struct starsead_matcher_pin_expectation pins[4U];
    size_t pin_count;
};

int starsead_matcher_pin_plan_build(
    const struct starsead_config *, struct starsead_matcher_pin_plan *);
int starsead_matcher_pin_records_build(
    const struct starsead_matcher_pin_plan *, struct starsead_resource_operation *,
    size_t, size_t *);

enum starsead_qdisc_id {
    STARSEAD_QDISC_HOTSPOT_CLSACT,
    STARSEAD_QDISC_COUNT,
};

enum starsead_filter_id {
    STARSEAD_FILTER_HOTSPOT_INGRESS,
    STARSEAD_FILTER_HOTSPOT_EGRESS,
    STARSEAD_FILTER_COUNT,
};

enum starsead_tc_direction {
    STARSEAD_TC_DIRECTION_INGRESS,
    STARSEAD_TC_DIRECTION_EGRESS,
    STARSEAD_TC_DIRECTION_COUNT,
};

enum starsead_program_id {
    STARSEAD_PROGRAM_BPF2SOCKS_INGRESS,
    STARSEAD_PROGRAM_BPF2SOCKS_EGRESS,
    STARSEAD_PROGRAM_COUNT,
};

enum starsead_sysctl_id {
    STARSEAD_SYSCTL_DISABLE_IPV6,
    STARSEAD_SYSCTL_ROUTE_LOCALNET,
    STARSEAD_SYSCTL_COUNT,
};

enum starsead_tether_id {
    STARSEAD_TETHER_DNSMASQ,
    STARSEAD_TETHER_COUNT,
};

struct starsead_iptables_chain_resource {
    enum starsead_ip_family family;
    enum starsead_ip_table table;
    enum starsead_chain_id chain_id;
    bool original_presence;
};

struct starsead_iptables_rule_resource {
    enum starsead_ip_family family;
    enum starsead_ip_table table;
    enum starsead_chain_id chain_id;
    enum starsead_rule_id rule_id;
    bool has_interface;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    bool original_presence;
};

struct starsead_ip_rule_resource {
    enum starsead_ip_family family;
    enum starsead_ip_rule_id rule_id;
    bool original_presence;
};

struct starsead_route_resource {
    enum starsead_ip_family family;
    enum starsead_route_id route_id;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    bool original_presence;
};

struct starsead_bpf_pin_resource {
    enum starsead_pin_id pin_id;
    bool has_object_id;
    uint64_t object_id;
    bool original_presence;
};

struct starsead_tc_qdisc_resource {
    enum starsead_qdisc_id qdisc_id;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    bool original_presence;
};

struct starsead_tc_filter_resource {
    enum starsead_filter_id filter_id;
    enum starsead_tc_direction direction;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    enum starsead_program_id program_id;
    bool original_presence;
};

struct starsead_sysctl_resource {
    enum starsead_sysctl_id sysctl_id;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    uint8_t original_value;
    uint8_t desired_value;
};

struct starsead_tether_state_resource {
    enum starsead_tether_id tether_id;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    bool original_active;
    bool desired_active;
};

#define STARSEAD_MAX_VOLATILE_EFFECTS 512U

enum starsead_effect_kind {
    STARSEAD_EFFECT_BPF_PIN,
    STARSEAD_EFFECT_TC_QDISC,
    STARSEAD_EFFECT_TC_FILTER,
    STARSEAD_EFFECT_SYSCTL,
    STARSEAD_EFFECT_TETHER_STATE,
};

struct starsead_effect {
    enum starsead_effect_kind kind;
    union {
        struct starsead_bpf_pin_resource bpf_pin;
        struct starsead_tc_qdisc_resource tc_qdisc;
        struct starsead_tc_filter_resource tc_filter;
        struct starsead_sysctl_resource sysctl;
        struct starsead_tether_state_resource tether_state;
    } resource;
};

struct starsead_effect_backend {
    void *context;
    int (*probe_original)(void *, struct starsead_effect *, char *, size_t);
    int (*apply)(void *, const struct starsead_effect *, char *, size_t);
    int (*verify_applied)(void *, const struct starsead_effect *, char *, size_t);
    int (*undo)(void *, const struct starsead_effect *, char *, size_t);
    int (*verify_restored)(void *, const struct starsead_effect *, char *, size_t);
};

struct starsead_effect_journal {
    struct starsead_effect entries[STARSEAD_MAX_VOLATILE_EFFECTS];
    size_t count;
};

int starsead_effect_journal_apply(
    struct starsead_effect_journal *, const struct starsead_effect *,
    const struct starsead_effect_backend *, char *, size_t);
int starsead_effect_journal_rollback(
    struct starsead_effect_journal *, const struct starsead_effect_backend *,
    char *, size_t);

struct starsead_resource_operation {
    enum starsead_resource_operation_kind kind;
    union {
        struct starsead_iptables_chain_resource iptables_chain;
        struct starsead_iptables_rule_resource iptables_rule;
        struct starsead_ip_rule_resource ip_rule;
        struct starsead_route_resource route;
        struct starsead_bpf_pin_resource bpf_pin;
        struct starsead_tc_qdisc_resource tc_qdisc;
        struct starsead_tc_filter_resource tc_filter;
        struct starsead_sysctl_resource sysctl;
        struct starsead_tether_state_resource tether_state;
    } resource;
};

enum starsead_builtin_chain {
    STARSEAD_BUILTIN_PREROUTING,
    STARSEAD_BUILTIN_INPUT,
    STARSEAD_BUILTIN_FORWARD,
    STARSEAD_BUILTIN_OUTPUT,
    STARSEAD_BUILTIN_COUNT,
};

enum starsead_hook_verdict {
    STARSEAD_HOOK_JUMP,
    STARSEAD_HOOK_DROP,
    STARSEAD_HOOK_REJECT,
    STARSEAD_HOOK_COUNT,
};

struct starsead_private_chain_group {
    enum starsead_ip_family family;
    enum starsead_ip_table table;
    enum starsead_chain_id chain_id;
    char names[STARSEAD_RULE_TRANSACTION_MAX_NAMES][STARSEAD_MAX_CHAIN_NAME];
    size_t name_count;
    struct starsead_resource_operation operation;
};

struct starsead_traffic_hook {
    enum starsead_builtin_chain builtin_chain;
    bool insert_at_head;
    bool udp_destination_port_53;
    enum starsead_hook_verdict verdict;
    char jump_target[STARSEAD_MAX_CHAIN_NAME];
};

struct starsead_traffic_hook_group {
    enum starsead_ip_family family;
    enum starsead_ip_table table;
    enum starsead_chain_id chain_id;
    enum starsead_rule_id rule_id;
    struct starsead_traffic_hook hooks[STARSEAD_RULE_TRANSACTION_MAX_HOOKS];
    size_t hook_count;
    struct starsead_resource_operation operation;
};

enum starsead_route_effect_kind {
    STARSEAD_ROUTE_EFFECT_IP_RULE,
    STARSEAD_ROUTE_EFFECT_ROUTE,
};

struct starsead_route_effect {
    enum starsead_route_effect_kind kind;
    enum starsead_ip_family family;
    uint32_t table;
    uint32_t priority;
    uint32_t mark;
    uint32_t mark_mask;
    bool invert_from_all;
    bool local_route;
    enum starsead_ip_rule_id ip_rule_id;
    enum starsead_route_id route_id;
    char destination[STARSEAD_MAX_CIDR];
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
};

struct starsead_rule_transaction_plan {
    bool no_op;
    struct starsead_private_chain_group private_groups[STARSEAD_RULE_TRANSACTION_MAX_GROUPS];
    size_t private_group_count;
    struct starsead_route_effect routes[STARSEAD_RULE_TRANSACTION_MAX_ROUTES];
    size_t route_count;
    struct starsead_traffic_hook_group hook_groups[STARSEAD_RULE_TRANSACTION_MAX_GROUPS];
    size_t hook_group_count;
    bool hooks_are_last;
};

int starsead_rule_transaction_plan_build(
    const struct starsead_config *, bool, struct starsead_rule_transaction_plan *);
int starsead_rule_transaction_quiesce_plan_build(
    const struct starsead_rule_transaction_plan *, struct starsead_rule_transaction_plan *);
int starsead_tproxy_rule_transaction_plan_build(
    const struct starsead_config *, bool, struct starsead_rule_transaction_plan *);
int starsead_tun_rule_transaction_plan_build(
    const struct starsead_config *, struct starsead_rule_transaction_plan *);

enum starsead_rules_slot_state {
    STARSEAD_RULES_SLOT_ABSENT,
    STARSEAD_RULES_SLOT_OWNED,
    STARSEAD_RULES_SLOT_FOREIGN,
};

int starsead_ip_rule_output_classify(
    const char *, size_t, const struct starsead_route_effect *,
    enum starsead_rules_slot_state *);
int starsead_ip_route_output_classify(
    const char *, size_t, const struct starsead_route_effect *,
    enum starsead_rules_slot_state *);

struct starsead_rules_backend {
    void *ctx;
    int (*apply_plan)(void *, const struct starsead_rule_transaction_plan *);
    int (*apply_private)(void *, const struct starsead_private_chain_group *);
    int (*apply_route)(void *, const struct starsead_route_effect *);
    int (*apply_hook)(void *, const struct starsead_traffic_hook_group *);
    int (*probe_private)(void *, const struct starsead_private_chain_group *,
        enum starsead_rules_slot_state *);
    int (*probe_route)(void *, const struct starsead_route_effect *,
        enum starsead_rules_slot_state *);
    int (*probe_hook)(void *, const struct starsead_traffic_hook_group *,
        enum starsead_rules_slot_state *);
    int (*remove_private)(void *, const struct starsead_private_chain_group *);
    int (*remove_route)(void *, const struct starsead_route_effect *);
    int (*remove_hook)(void *, const struct starsead_traffic_hook_group *);
};

struct starsead_rules_runtime {
    struct starsead_rule_transaction_plan plan;
    bool private_cleanup_required[STARSEAD_RULE_TRANSACTION_MAX_GROUPS];
    bool route_cleanup_required[STARSEAD_RULE_TRANSACTION_MAX_ROUTES];
    bool hook_cleanup_required[STARSEAD_RULE_TRANSACTION_MAX_GROUPS];
    uint64_t generation;
    bool initialized;
    bool installed;
};

void starsead_rules_runtime_init(struct starsead_rules_runtime *);
bool starsead_xtables_private_chain_shape_valid(
    const char *, size_t, const char *, size_t);
size_t starsead_xtables_fake_dns_arguments(const char *, const char **);
int starsead_xtables_private_chain_counts(
    const char *, size_t, const char *, size_t *, size_t *);
size_t starsead_xtables_hook_arguments(
    const struct starsead_traffic_hook *, const char **);
int starsead_xtables_rule_output_count(
    const char *, size_t, const char *, const char *const *, size_t, size_t *);
int starsead_xtables_rule_output_locate(
    const char *, size_t, const char *, const char *const *, size_t, size_t *, size_t *);
int starsead_rules_install(struct starsead_rules_runtime *,
    const struct starsead_config *, bool, const struct starsead_rules_backend *);
int starsead_rules_verify(struct starsead_rules_runtime *,
    const struct starsead_rules_backend *);
int starsead_rules_remove(struct starsead_rules_runtime *,
    const struct starsead_rules_backend *);
int starsead_rules_reconcile(struct starsead_rules_runtime *,
    const struct starsead_rules_backend *);

enum starsead_poll_source_kind {
    STARSEAD_POLL_SIGNAL,
    STARSEAD_POLL_CONTROL_LISTENER,
    STARSEAD_POLL_CONTROL_CLIENT,
    STARSEAD_POLL_PROCESS_EXEC_ERROR,
    STARSEAD_POLL_PROCESS_STDOUT,
    STARSEAD_POLL_PROCESS_STDERR,
    STARSEAD_POLL_PROCESS_PIDFD,
    STARSEAD_POLL_NETWORK,
    STARSEAD_POLL_TC_NETLINK,
    STARSEAD_POLL_SERVICE_TIMER,
    STARSEAD_POLL_WIFI,
    STARSEAD_POLL_SOURCE_KIND_COUNT,
};

#if defined(STARSEAD_TESTING)
bool starsead_test_action_source_active(
    bool, bool, enum starsead_poll_source_kind);
bool starsead_test_action_setup_wait_done(
    const struct starsead_child_setup_stream *, bool);
bool starsead_test_action_io_drained(bool, bool, bool, bool);
#endif

struct starsead_poll_source {
    int fd;
    short events;
    enum starsead_poll_source_kind kind;
    uint32_t slot;
    uint64_t generation;
};

struct starsead_poll_builder {
    struct starsead_poll_source sources[STARSEAD_MAX_POLL_SOURCES];
    size_t count;
};

struct starsead_deadline {
    bool armed;
    int64_t monotonic_milliseconds;
};

enum starsead_runtime_delta_flag {
    STARSEAD_DELTA_STOP_REQUESTED = UINT32_C(1) << 0,
    STARSEAD_DELTA_CHILD_EXITED = UINT32_C(1) << 1,
    STARSEAD_DELTA_NETWORK_CHANGED = UINT32_C(1) << 2,
    STARSEAD_DELTA_RECONCILE_DUE = UINT32_C(1) << 3,
    STARSEAD_DELTA_RULES_CHANGED = UINT32_C(1) << 4,
    STARSEAD_DELTA_FATAL = UINT32_C(1) << 5,
};

struct starsead_runtime_delta {
    uint32_t flags;
    enum starsead_lifecycle_reason stop_reason;
    bool has_child_exit;
    enum starsead_child_role child_role;
    struct starsead_child_exit_status child_exit;
    bool has_rules_summary;
    uint64_t rules_generation;
    uint32_t rule_categories;
    bool has_error;
    enum starsead_failure_code error_code;
    enum starsead_component error_component;
    char error_message[256U];
};

#if defined(STARSEAD_TESTING)
int starsead_test_start_failure_detail(
    int, const char *, struct starsead_runtime_delta *);
int starsead_test_capability_path_search_result(bool, bool);
int starsead_test_capability_inspect_error(int);
int starsead_test_periodic_deadline(int64_t, int64_t, uint32_t, int64_t *);
unsigned starsead_test_runtime_dispatch_priority(enum starsead_poll_source_kind);
bool starsead_test_startup_components_verified(
    bool, bool, bool, bool, bool, bool, bool, bool, bool);
bool starsead_test_admission_reconcile_ready(bool, bool);
bool starsead_test_owned_hook_rule_probe_required(
    const struct starsead_owned_hook *, bool);
#endif

enum starsead_reactor_wait_result {
    STARSEAD_REACTOR_WAIT_ERROR = -1,
    STARSEAD_REACTOR_WAIT_INTERRUPTED = -2,
};

struct starsead_runtime_reactor_backend {
    int (*monotonic_milliseconds)(void *, int64_t *);
    int (*prepare)(void *, struct starsead_poll_builder *, struct starsead_deadline *);
    int (*wait)(void *, const struct starsead_poll_builder *, int64_t, short *);
    int (*dispatch)(void *, const struct starsead_poll_source *, short,
        struct starsead_runtime_delta *);
    int (*expire)(void *, int64_t, struct starsead_runtime_delta *);
};

struct starsead_runtime;
typedef bool (*starsead_runtime_predicate)(void *);

void starsead_poll_builder_init(struct starsead_poll_builder *);
int starsead_poll_builder_add(
    struct starsead_poll_builder *, const struct starsead_poll_source *);
bool starsead_process_poll_source_matches(
    const struct starsead_child_process *, const struct starsead_poll_source *);
void starsead_deadline_min(struct starsead_deadline *, const struct starsead_deadline *);
void starsead_runtime_delta_init(struct starsead_runtime_delta *);
int starsead_runtime_create(
    struct starsead_runtime **, const struct starsead_runtime_reactor_backend *, void *);
int starsead_runtime_pump_once(
    struct starsead_runtime *, const struct starsead_deadline *,
    struct starsead_runtime_delta *);
int starsead_runtime_pump_until(
    struct starsead_runtime *, const struct starsead_deadline *,
    starsead_runtime_predicate, void *, struct starsead_runtime_delta *);
int starsead_runtime_accept_delta(
    struct starsead_runtime *, const struct starsead_runtime_delta *);
void starsead_runtime_destroy(struct starsead_runtime *);

enum starsead_route_slot_state {
    STARSEAD_ROUTE_SLOT_ABSENT,
    STARSEAD_ROUTE_SLOT_OWNED,
    STARSEAD_ROUTE_SLOT_FOREIGN,
};

struct starsead_token_route_plan {
    bool create;
    char prefix[STARSEAD_MAX_CIDR];
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    bool local_table;
    struct starsead_resource_operation operation;
};

int starsead_b2s_token_route_plan_build(
    const struct starsead_config *, enum starsead_route_slot_state, uint32_t,
    struct starsead_token_route_plan *, char *, size_t);

struct starsead_state_children {
    bool core_present;
    struct starsead_child_identity core;
    bool helper_present;
    struct starsead_child_identity helper;
};

struct starsead_state_matcher {
    bool configured;
    bool active;
};

struct starsead_state_rules {
    bool active;
    uint64_t generation;
    uint32_t categories;
};

struct starsead_state_failure {
    bool present;
    enum starsead_failure_code code;
    enum starsead_component component;
    char message[STARSEAD_MAX_STATE_MESSAGE];
    bool has_exit_code;
    int exit_code;
    bool has_signal;
    int signal;
};

struct starsead_state_document {
    uint32_t schema_version;
    enum starsead_phase phase;
    enum starsead_owner owner;
    enum starsead_core_type core_type;
    enum starsead_mode mode;
    struct starsead_state_children children;
    struct starsead_state_matcher matcher;
    struct starsead_state_rules rules;
    struct starsead_state_failure failure;
    bool initialized;
};

enum starsead_state_open_flags {
    STARSEAD_STATE_OPEN_READ = 1U << 0,
    STARSEAD_STATE_OPEN_WRITE = 1U << 1,
    STARSEAD_STATE_OPEN_CREATE = 1U << 2,
    STARSEAD_STATE_OPEN_EXCLUSIVE = 1U << 3,
    STARSEAD_STATE_OPEN_NOFOLLOW = 1U << 4,
    STARSEAD_STATE_OPEN_CLOEXEC = 1U << 5,
    STARSEAD_STATE_OPEN_NONBLOCK = 1U << 6,
    STARSEAD_STATE_OPEN_TRUNCATE = 1U << 7,
};

struct starsead_state_file_backend {
    int (*fstat_fd)(void *, int, uint64_t *, uint64_t *, enum starsead_file_kind *);
    int (*dup_cloexec)(void *, int, int *);
    int (*openat_fd)(void *, int, const char *, uint32_t, uint32_t, int *);
    ptrdiff_t (*read_fd)(void *, int, void *, size_t);
    ptrdiff_t (*write_fd)(void *, int, const void *, size_t);
    int (*close_fd)(void *, int);
};

struct starsead_state_store {
    int directory_fd;
    char file_name[STARSEAD_MAX_PATH];
    bool directory_fd_owned;
    uint64_t directory_device;
    uint64_t directory_inode;
    const struct starsead_state_file_backend *backend;
    void *backend_context;
    bool initialized;
};

enum starsead_pin_batch_kind {
    STARSEAD_PIN_BATCH_MATCHER_IPV4,
    STARSEAD_PIN_BATCH_MATCHER_DUAL_STACK,
    STARSEAD_PIN_BATCH_BPF2SOCKS_IPV4,
    STARSEAD_PIN_BATCH_BPF2SOCKS_DUAL_STACK,
};

int starsead_state_document_init(
    struct starsead_state_document *, enum starsead_owner, enum starsead_core_type, enum starsead_mode);
void starsead_state_document_destroy(struct starsead_state_document *);
int starsead_state_set_phase(struct starsead_state_document *, enum starsead_phase);
int starsead_state_set_child(
    struct starsead_state_document *, const struct starsead_child_identity *, char *, size_t);
int starsead_state_clear_child(struct starsead_state_document *, enum starsead_child_role);
int starsead_state_set_matcher(struct starsead_state_document *, bool, bool);
int starsead_state_set_rules(struct starsead_state_document *, bool, uint64_t, uint32_t);
int starsead_state_set_failure(
    struct starsead_state_document *, enum starsead_failure_code, enum starsead_component,
    const char *, bool, int, bool, int, char *, size_t);
void starsead_state_clear_failure(struct starsead_state_document *);
int starsead_state_mark_stopped(struct starsead_state_document *, char *, size_t);
bool starsead_state_is_stopped(const struct starsead_state_document *);
int starsead_state_serialize(
    const struct starsead_state_document *, char **, size_t *, char *, size_t);
int starsead_state_store_init(
    struct starsead_state_store *, const char *, char *, size_t);
int starsead_state_store_init_with_backend(
    struct starsead_state_store *, int, uint64_t, uint64_t, const char *,
    const struct starsead_state_file_backend *, void *, char *, size_t);
int starsead_state_store_save(
    struct starsead_state_store *, const struct starsead_state_document *, char *, size_t);
void starsead_state_store_close(struct starsead_state_store *);

enum starsead_control_method {
    STARSEAD_CONTROL_METHOD_STATUS,
    STARSEAD_CONTROL_METHOD_STOP,
    STARSEAD_CONTROL_METHOD_SHUTDOWN,
    STARSEAD_CONTROL_METHOD_WATCH,
    STARSEAD_CONTROL_METHOD_COUNT,
};

enum starsead_control_result_code {
    STARSEAD_CONTROL_RESULT_OK,
    STARSEAD_CONTROL_RESULT_ALREADY_RUNNING,
    STARSEAD_CONTROL_RESULT_NOT_RUNNING,
    STARSEAD_CONTROL_RESULT_PERMISSION_DENIED,
    STARSEAD_CONTROL_RESULT_INVALID_REQUEST,
    STARSEAD_CONTROL_RESULT_CONFIG_INVALID,
    STARSEAD_CONTROL_RESULT_UNSUPPORTED_COMBINATION,
    STARSEAD_CONTROL_RESULT_START_FAILED,
    STARSEAD_CONTROL_RESULT_STOP_FAILED,
    STARSEAD_CONTROL_RESULT_INTERNAL_ERROR,
    STARSEAD_CONTROL_RESULT_CODE_COUNT,
};

enum starsead_control_event_type {
    STARSEAD_CONTROL_EVENT_STARTING,
    STARSEAD_CONTROL_EVENT_RUNNING,
    STARSEAD_CONTROL_EVENT_RULES_CHANGED,
    STARSEAD_CONTROL_EVENT_STOPPING,
    STARSEAD_CONTROL_EVENT_STOPPED,
    STARSEAD_CONTROL_EVENT_CORE_EXITED,
    STARSEAD_CONTROL_EVENT_HELPER_FAILED,
    STARSEAD_CONTROL_EVENT_FAILED,
    STARSEAD_CONTROL_EVENT_TYPE_COUNT,
};

enum starsead_control_decode_outcome {
    STARSEAD_CONTROL_DECODE_VALID,
    STARSEAD_CONTROL_DECODE_INVALID_REQUEST,
    STARSEAD_CONTROL_DECODE_SILENT_REJECT,
};

struct starsead_control_request {
    char request_id[STARSEAD_CONTROL_MAX_REQUEST_ID + 1U];
    enum starsead_control_method method;
};

struct starsead_control_error {
    enum starsead_failure_code code;
    enum starsead_component component;
    char *message;
    size_t message_length;
    bool has_exit_code;
    int exit_code;
    bool has_signal;
    int signal;
};

struct starsead_control_rules {
    bool active;
    uint64_t generation;
    uint32_t categories;
};

struct starsead_control_network {
    bool ipv4_ready;
    bool ipv6_enabled;
    bool ipv6_ready;
};

struct starsead_control_snapshot {
    enum starsead_phase phase;
    enum starsead_owner owner;
    enum starsead_core_type core_type;
    enum starsead_mode mode;
    int supervisor_pid;
    bool has_core_pid;
    int core_pid;
    enum starsead_helper_type helper_type;
    bool has_helper_pid;
    int helper_pid;
    bool matcher_configured;
    bool matcher_active;
    struct starsead_control_rules rules;
    struct starsead_control_network network;
    bool has_error;
    struct starsead_control_error error;
};

struct starsead_control_live_context {
    int supervisor_pid;
    bool ipv6_enabled;
};

struct starsead_control_result {
    enum starsead_control_result_code code;
    bool has_snapshot;
    struct starsead_control_snapshot snapshot;
    bool has_message;
    char *message;
    size_t message_length;
};

struct starsead_control_response {
    char request_id[STARSEAD_CONTROL_MAX_REQUEST_ID + 1U];
    struct starsead_control_result result;
};

struct starsead_control_event {
    uint64_t sequence;
    enum starsead_control_event_type type;
    struct starsead_control_snapshot snapshot;
    bool has_details;
    struct starsead_control_error details;
};

int starsead_control_encode_request_line(
    const struct starsead_control_request *, char *, size_t, size_t *);
enum starsead_control_decode_outcome starsead_control_decode_request_payload(
    const char *, size_t, struct starsead_control_request *);
int starsead_control_encode_response_line(
    const struct starsead_control_response *, char *, size_t, size_t *);
int starsead_control_decode_response_payload(
    const char *, size_t, struct starsead_control_response *);
int starsead_control_encode_event_line(
    const struct starsead_control_event *, char *, size_t, size_t *);
int starsead_control_decode_event_payload(
    const char *, size_t, struct starsead_control_event *);
int starsead_control_snapshot_from_state(
    const struct starsead_state_document *, const struct starsead_control_live_context *,
    struct starsead_control_snapshot *);
bool starsead_control_snapshot_valid(const struct starsead_control_snapshot *);
bool starsead_control_result_valid(const struct starsead_control_result *);
int starsead_control_result_exit_code(enum starsead_control_result_code);
int starsead_control_error_set_message(
    struct starsead_control_error *, const char *, size_t);
int starsead_control_result_set_message(
    struct starsead_control_result *, const char *, size_t);
int starsead_control_snapshot_copy(
    struct starsead_control_snapshot *, const struct starsead_control_snapshot *);
void starsead_control_error_destroy(struct starsead_control_error *);
void starsead_control_snapshot_destroy(struct starsead_control_snapshot *);
void starsead_control_result_destroy(struct starsead_control_result *);
void starsead_control_response_destroy(struct starsead_control_response *);
void starsead_control_event_destroy(struct starsead_control_event *);

struct starsead_runtime_effect_backend {
    void *context;
    int (*save_state)(void *, const struct starsead_state_document *);
    int (*publish_event)(void *, enum starsead_control_event_type,
        const struct starsead_control_snapshot *, const struct starsead_control_error *, bool);
    int (*start_core)(void *, struct starsead_child_identity *);
    int (*wait_core)(void *);
    int (*ensure_platform_capability)(void *);
    int (*start_helper)(void *, struct starsead_child_identity *);
    int (*wait_helper)(void *);
    int (*start_matcher)(void *);
    int (*open_network)(void *);
    int (*apply_rules)(void *, bool *, uint64_t *, uint32_t *);
    int (*verify)(void *);
    int (*network_immediate)(void *);
    int (*reconcile)(void *, bool *, uint64_t *, uint32_t *);
    int (*quiesce_traffic)(void *);
    int (*remove_rules)(void *);
    int (*close_network)(void *);
    int (*stop_matcher)(void *);
    int (*stop_helper)(void *);
    int (*stop_core)(void *);
    int (*restore_best_effort)(void *);
    int (*release)(void *);
};

int starsead_rule_batch_document_render(
    const unsigned char *, size_t,
    const unsigned char *, size_t,
    const unsigned char *, size_t,
    const unsigned char *, size_t,
    unsigned char **, size_t *);

#if defined(STARSEAD_TESTING)
int starsead_test_rule_batch_document(
    const char *, const char *, const char *, const char *, char **, size_t *);
#endif

enum starsead_runtime_effect_result {
    STARSEAD_RUNTIME_EFFECT_READINESS_TIMEOUT = -100,
};

bool starsead_runtime_remove_tc_before_helper_stop(enum starsead_mode);
int starsead_runtime_supervise(
    struct starsead_runtime *, const struct starsead_config *,
    struct starsead_state_document *, const struct starsead_control_live_context *,
    const struct starsead_runtime_effect_backend *);
int starsead_runtime_start_system(
    const char *, bool *, struct starsead_control_result *);
int starsead_runtime_monitor_system(
    const char *, bool *, struct starsead_control_result *);
#if defined(STARSEAD_TESTING)
bool starsead_test_cycle_failure_requires_shutdown(bool, bool);
#endif

enum starsead_control_backend_result {
    STARSEAD_CONTROL_BACKEND_OK = 0,
    STARSEAD_CONTROL_BACKEND_ERROR = -1,
    STARSEAD_CONTROL_BACKEND_AGAIN = -2,
    STARSEAD_CONTROL_BACKEND_INTERRUPTED = -3,
};

enum starsead_control_listener_result {
    STARSEAD_CONTROL_LISTENER_ERROR = -1,
    STARSEAD_CONTROL_LISTENER_OK = 0,
    STARSEAD_CONTROL_LISTENER_IN_USE = 1,
};

int starsead_reconcile_after_listener(
    enum starsead_control_listener_result,
    const struct starsead_reconcile_backend *,
    struct starsead_reconcile_report *, char *, size_t);

struct starsead_control_listener_backend {
    int (*open_stream)(void *, int *);
    enum starsead_control_listener_result (*bind_abstract)(
        void *, int, const unsigned char *, size_t);
    int (*listen_socket)(void *, int, int);
    int (*close_fd)(void *, int);
};

enum starsead_control_listener_result starsead_control_listener_open_with_backend(
    int *, const struct starsead_control_listener_backend *, void *);
enum starsead_control_listener_result starsead_control_listener_open(int *);

enum starsead_cli_command {
    STARSEAD_CLI_START,
    STARSEAD_CLI_MONITOR,
    STARSEAD_CLI_STATUS,
    STARSEAD_CLI_STOP,
    STARSEAD_CLI_SHUTDOWN,
    STARSEAD_CLI_WATCH,
};

struct starsead_cli_invocation {
    enum starsead_cli_command command;
    bool watch_until_running;
    char path[STARSEAD_MAX_PATH];
};

int starsead_cli_parse(
    int, const char *const *, struct starsead_cli_invocation *);

enum starsead_control_connect_result {
    STARSEAD_CONTROL_CONNECT_ERROR = -1,
    STARSEAD_CONTROL_CONNECT_OK = 0,
    STARSEAD_CONTROL_CONNECT_IN_PROGRESS = 1,
    STARSEAD_CONTROL_CONNECT_ABSENT = 2,
};

enum starsead_control_wait_result {
    STARSEAD_CONTROL_WAIT_ERROR = -1,
    STARSEAD_CONTROL_WAIT_READY = 0,
    STARSEAD_CONTROL_WAIT_TIMEOUT = 1,
    STARSEAD_CONTROL_WAIT_INTERRUPTED = 2,
};

enum starsead_control_client_result {
    STARSEAD_CONTROL_CLIENT_OK = 0,
    STARSEAD_CONTROL_CLIENT_ABSENT = 1,
    STARSEAD_CONTROL_CLIENT_TIMEOUT = 2,
    STARSEAD_CONTROL_CLIENT_PROTOCOL_ERROR = 3,
    STARSEAD_CONTROL_CLIENT_IO_ERROR = 4,
};

struct starsead_control_client_backend {
    int (*monotonic_milliseconds)(void *, uint64_t *);
    enum starsead_control_connect_result (*connect_abstract)(
        void *, const unsigned char *, size_t, int *);
    enum starsead_control_connect_result (*finish_connect)(void *, int);
    enum starsead_control_wait_result (*wait_ready)(
        void *, int, bool, bool, uint64_t, bool *, bool *);
    ptrdiff_t (*read_fd)(void *, int, void *, size_t);
    ptrdiff_t (*write_fd)(void *, int, const void *, size_t);
    int (*close_fd)(void *, int);
};

enum starsead_control_sink_result {
    STARSEAD_CONTROL_SINK_ERROR = -1,
    STARSEAD_CONTROL_SINK_CONTINUE = 0,
    STARSEAD_CONTROL_SINK_STOP = 1,
};

typedef int (*starsead_control_line_sink)(void *, const char *, size_t);

enum starsead_control_client_result starsead_control_client_run_with_backend(
    enum starsead_control_method, const char *,
    const struct starsead_control_client_backend *, void *,
    starsead_control_line_sink, void *, struct starsead_control_response *);
enum starsead_control_client_result starsead_control_client_run(
    enum starsead_control_method, const char *,
    starsead_control_line_sink, void *, struct starsead_control_response *);

struct starsead_cli_backend {
    uint32_t (*effective_uid)(void *);
    enum starsead_control_client_result (*control_client)(
        void *, enum starsead_control_method, const char *,
        starsead_control_line_sink, void *, struct starsead_control_response *);
    int (*run_start)(
        void *, const char *, bool *, struct starsead_control_result *);
    int (*run_monitor)(
        void *, const char *, bool *, struct starsead_control_result *);
    int (*write_stdout)(void *, const char *, size_t);
    int (*write_stderr)(void *, const char *, size_t);
};

int starsead_cli_main_with_backend(
    int, const char *const *, const struct starsead_cli_backend *, void *);
int starsead_cli_main(int, const char *const *);

struct starsead_control_transport_backend {
    int (*accept_client)(void *, int, int *, uint32_t *);
    ptrdiff_t (*read_client)(void *, int, void *, size_t);
    ptrdiff_t (*write_client)(void *, int, const void *, size_t);
    int (*close_fd)(void *, int);
};

struct starsead_control_callbacks {
    int (*snapshot)(void *, struct starsead_control_snapshot *);
    int (*request_stop)(void *);
    int (*request_shutdown)(void *);
    void *context;
};

struct starsead_control_interest {
    int fd;
    bool readable;
    bool writable;
};

struct starsead_control_server;

int starsead_control_server_create_with_backend(
    struct starsead_control_server **, int,
    const struct starsead_control_transport_backend *, void *,
    const struct starsead_control_callbacks *);
int starsead_control_server_create(
    struct starsead_control_server **, int,
    const struct starsead_control_callbacks *);
void starsead_control_server_enable_accepting(
    struct starsead_control_server *, bool);
void starsead_control_server_close_listener(
    struct starsead_control_server *);
int starsead_control_server_listener_fd(
    const struct starsead_control_server *);
size_t starsead_control_server_interests(
    const struct starsead_control_server *,
    struct starsead_control_interest *, size_t);
bool starsead_control_server_next_deadline(
    const struct starsead_control_server *, uint64_t *);
int starsead_control_server_dispatch(
    struct starsead_control_server *, int, bool, bool, bool, uint64_t);
int starsead_control_server_tick(
    struct starsead_control_server *, uint64_t);
int starsead_control_server_publish_event(
    struct starsead_control_server *, enum starsead_control_event_type,
    const struct starsead_control_snapshot *,
    const struct starsead_control_error *, bool, uint64_t);
int starsead_control_server_finish_stop(
    struct starsead_control_server *,
    const struct starsead_control_result *, uint64_t);
bool starsead_control_server_drained(
    const struct starsead_control_server *);
uint64_t starsead_control_server_sequence(
    const struct starsead_control_server *);
void starsead_control_server_destroy(
    struct starsead_control_server *);

struct starsead_network_effect_request {
    struct starsead_effect effect;
};

struct starsead_network_effect_sink {
    int (*dispatch)(void *, const struct starsead_network_effect_request *,
        char *, size_t);
    void *context;
};

enum starsead_log_level {
    STARSEAD_LOG_LEVEL_DEBUG,
    STARSEAD_LOG_LEVEL_INFO,
    STARSEAD_LOG_LEVEL_WARNING,
    STARSEAD_LOG_LEVEL_ERROR,
    STARSEAD_LOG_LEVEL_COUNT,
};

enum starsead_log_event {
    STARSEAD_LOG_EVENT_STARTING,
    STARSEAD_LOG_EVENT_RUNNING,
    STARSEAD_LOG_EVENT_STOPPING,
    STARSEAD_LOG_EVENT_STOPPED,
    STARSEAD_LOG_EVENT_CHILD_OUTPUT,
    STARSEAD_LOG_EVENT_STATE_LOADED,
    STARSEAD_LOG_EVENT_STATE_SAVED,
    STARSEAD_LOG_EVENT_STATE_INVALID,
    STARSEAD_LOG_EVENT_NETWORK_CHANGED,
    STARSEAD_LOG_EVENT_CAPABILITY_ADJUSTED,
    STARSEAD_LOG_EVENT_IO_ERROR,
    STARSEAD_LOG_EVENT_DIAGNOSTIC,
    STARSEAD_LOG_EVENT_COUNT,
};

enum starsead_log_stream {
    STARSEAD_LOG_STREAM_STDOUT,
    STARSEAD_LOG_STREAM_STDERR,
    STARSEAD_LOG_STREAM_COUNT,
};

enum starsead_core_start_failure_stage {
    STARSEAD_CORE_START_FAILURE_PROCESS_SPEC = 0,
    STARSEAD_CORE_START_FAILURE_BACKEND_INIT = 1,
    STARSEAD_CORE_START_FAILURE_READINESS_PREFLIGHT = 2,
    STARSEAD_CORE_START_FAILURE_SPAWN = 3,
    STARSEAD_CORE_START_FAILURE_CLOCK = 4,
    STARSEAD_CORE_START_FAILURE_SETUP_WAIT = 5,
    STARSEAD_CORE_START_FAILURE_SETUP_RESULT = 6,
    STARSEAD_CORE_START_FAILURE_IDENTITY = 7,
};

struct starsead_core_start_diagnostic {
    enum starsead_core_start_failure_stage stage;
    bool setup_complete;
    bool setup_fatal;
    int setup_error_number;
    bool reaped;
    const char *detail;
};

int starsead_core_start_diagnostic_format(
    const struct starsead_core_start_diagnostic *, char *, size_t);

struct starsead_local_time {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int millisecond;
    int utc_offset_minutes;
};

struct starsead_clock_backend {
    int (*local_time)(void *, struct starsead_local_time *);
    void *context;
};

enum starsead_log_open_flags {
    STARSEAD_LOG_OPEN_APPEND = 1U << 0,
    STARSEAD_LOG_OPEN_CREATE = 1U << 1,
    STARSEAD_LOG_OPEN_NOFOLLOW = 1U << 2,
    STARSEAD_LOG_OPEN_CLOEXEC = 1U << 3,
    STARSEAD_LOG_OPEN_DIRECTORY = 1U << 4,
    STARSEAD_LOG_OPEN_NONBLOCK = 1U << 5,
};

struct starsead_log_file_metadata {
    enum starsead_file_kind kind;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
};

struct starsead_log_file_backend {
    int (*open_root)(void *, uint32_t, int *);
    int (*openat_fd)(void *, int, const char *, uint32_t, uint32_t, int *);
    int (*fstat_fd)(void *, int, struct starsead_log_file_metadata *);
    int (*fchown_fd)(void *, int, uint32_t, uint32_t);
    int (*fchmod_fd)(void *, int, uint32_t);
    ptrdiff_t (*write_fd)(void *, int, const void *, size_t);
    int (*close_fd)(void *, int);
};

int starsead_log_open_append_fd(
    const char *, int *, char *, size_t);
int starsead_log_open_append_fd_with_backend(
    const char *, const struct starsead_log_file_backend *, void *,
    int *, char *, size_t);

struct starsead_log_partial {
    unsigned char bytes[STARSEAD_LOG_MAX_CHILD_LINE + STARSEAD_MAX_SECRET_KEY - 1U];
    size_t kept_length;
    size_t raw_length;
    uint64_t first_byte_milliseconds;
    bool has_first_byte;
    bool last_raw_was_cr;
    bool truncated;
};

struct starsead_logger {
    int fd;
    bool fd_owned;
    int parent_fd;
    bool parent_fd_owned;
    uint32_t parent_mode;
    uint32_t parent_uid;
    uint32_t parent_gid;
    bool opened;
    bool failed;
    unsigned char age_secret[STARSEAD_MAX_SECRET_KEY];
    size_t age_secret_length;
    struct starsead_clock_backend clock;
    const struct starsead_log_file_backend *file_backend;
    void *file_context;
    struct starsead_log_partial partials[2][STARSEAD_LOG_STREAM_COUNT];
};

int starsead_log_open(
    struct starsead_logger *, const char *, const unsigned char *, size_t,
    const struct starsead_clock_backend *, char *, size_t);
int starsead_log_open_with_backend(
    struct starsead_logger *, const char *, const unsigned char *, size_t,
    const struct starsead_clock_backend *, const struct starsead_log_file_backend *, void *, char *, size_t);
int starsead_log_line(
    struct starsead_logger *, enum starsead_log_level, enum starsead_component,
    enum starsead_log_event, const char *);
void starsead_log_diagnostic(
    enum starsead_log_level, enum starsead_component, const char *,
    int (*)(void *, const char *, size_t), void *);
void starsead_log_stderr(
    enum starsead_log_level, enum starsead_component, const char *);
int starsead_log_child_bytes(
    struct starsead_logger *, enum starsead_child_role, enum starsead_log_stream,
    const unsigned char *, size_t, uint64_t, bool);
int starsead_log_flush_expired(struct starsead_logger *, uint64_t);
size_t starsead_log_buffered_bytes(const struct starsead_logger *, enum starsead_child_role);
int starsead_log_close(struct starsead_logger *);

struct starsead_address_set {
    int family;
    char values[STARSEAD_MAX_ADDRESSES][64];
    size_t count;
};

struct starsead_interface_address {
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    char address[64U];
};

enum starsead_local_bypass_operation_kind {
    STARSEAD_LOCAL_BYPASS_INSERT,
    STARSEAD_LOCAL_BYPASS_DELETE,
};

struct starsead_local_bypass_operation {
    enum starsead_local_bypass_operation_kind kind;
    size_t rule_number;
    char address[64U];
};

#define STARSEAD_LOCAL_BYPASS_MAX_OPERATIONS (STARSEAD_MAX_ADDRESSES * 2U)

struct starsead_local_bypass_plan {
    struct starsead_local_bypass_operation
        operations[STARSEAD_LOCAL_BYPASS_MAX_OPERATIONS];
    size_t operation_count;
};

int starsead_local_address_set_build(
    const struct starsead_config *, int,
    const struct starsead_interface_address *, size_t,
    struct starsead_address_set *, char *, size_t);
int starsead_local_bypass_plan_build(
    int, const char *, const char *, const char *,
    const char *, size_t, const struct starsead_address_set *,
    struct starsead_local_bypass_plan *, char *, size_t);

#define STARSEAD_ADDRESS_IPV4 4
#define STARSEAD_ADDRESS_IPV6 6

#define STARSEAD_BPF_MAP_TYPE_LPM_TRIE 11U
#define STARSEAD_BPF_MAP_TYPE_HASH 1U
#define STARSEAD_BPF_PROGRAM_TYPE_SOCKET_FILTER 1U
#define STARSEAD_BPF_PROGRAM_TYPE_SCHED_CLS 3U
#define STARSEAD_BPF_MAP_FLAG_NO_PREALLOC 1U
#define STARSEAD_BPF_LOCAL_MAP_MAX_ENTRIES 512U
#define STARSEAD_MATCHER_UID_MAP_MAX_ENTRIES 8192U
#define STARSEAD_MATCHER_DIRECT_MAP_MAX_ENTRIES 32768U
#define STARSEAD_BPF_PROGRAM_TAG_SIZE 8U
#define STARSEAD_BPF_PROGRAM_MAX_MAPS 16U

struct starsead_bpf_map_info {
    uint64_t object_id;
    uint32_t type;
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
    uint32_t flags;
};

struct starsead_bpf_map_backend {
    void *context;
    int (*open_pinned)(void *, const char *, int *);
    int (*get_info)(void *, int, struct starsead_bpf_map_info *);
    int (*update)(void *, int, const void *, size_t, const void *, size_t);
    int (*get_next)(void *, int, const void *, size_t, void *, bool *);
    int (*delete_key)(void *, int, const void *, size_t);
    int (*close)(void *, int);
};

int starsead_bpf_local_map_reconcile(
    const struct starsead_bpf_map_backend *, const char *, uint64_t,
    const struct starsead_address_set *, char *, size_t);
const struct starsead_bpf_map_backend *starsead_system_bpf_map_backend(void);

struct starsead_bpf_program_info {
    uint64_t object_id;
    uint32_t type;
    char name[STARSEAD_MAX_INTERFACE_NAME];
    unsigned char tag[STARSEAD_BPF_PROGRAM_TAG_SIZE];
    uint32_t map_ids[STARSEAD_BPF_PROGRAM_MAX_MAPS];
    size_t map_count;
};

struct starsead_bpf_program_backend {
    void *context;
    int (*open_program)(void *, const char *, int *);
    int (*program_info)(void *, int, struct starsead_bpf_program_info *);
    int (*open_pinned_map)(void *, const char *, int *);
    int (*open_map)(void *, uint32_t, int *);
    int (*map_info)(void *, int, struct starsead_bpf_map_info *);
    int (*map_next)(void *, int, const void *, size_t, void *, bool *);
    int (*map_lookup)(void *, int, const void *, size_t, void *, size_t, bool *);
    int (*pin_program)(void *, int, const char *);
    int (*close)(void *, int);
};

struct starsead_matcher_verified_pin {
    enum starsead_pin_id pin_id;
    uint64_t object_id;
    unsigned char tag[STARSEAD_BPF_PROGRAM_TAG_SIZE];
};

struct starsead_matcher_verification {
    struct starsead_matcher_verified_pin pins[4U];
    size_t pin_count;
};

int starsead_matcher_verify(
    const struct starsead_config *, const struct starsead_matcher_pin_plan *,
    const struct starsead_bpf_program_backend *, struct starsead_matcher_verification *,
    char *, size_t);

struct starsead_b2s_pin_expectation {
    enum starsead_pin_id pin_id;
    char path[STARSEAD_MAX_PATH];
    bool program;
    char program_name[STARSEAD_MAX_INTERFACE_NAME];
};

struct starsead_b2s_pin_plan {
    struct starsead_b2s_pin_expectation pins[4U];
    size_t pin_count;
};

struct starsead_b2s_verified_pin {
    enum starsead_pin_id pin_id;
    uint64_t object_id;
    unsigned char tag[STARSEAD_BPF_PROGRAM_TAG_SIZE];
};

struct starsead_b2s_verification {
    struct starsead_b2s_verified_pin pins[4U];
    size_t pin_count;
};

int starsead_b2s_pin_plan_build(
    const struct starsead_config *, struct starsead_b2s_pin_plan *);
const char *starsead_b2s_tc_filter_attachment_name(enum starsead_program_id);
int starsead_b2s_pin_records_build(
    const struct starsead_b2s_pin_plan *, struct starsead_resource_operation *,
    size_t, size_t *);

struct starsead_bpf_pin_ownership_backend {
    void *context;
    int (*probe)(void *, const char *, bool *, uint64_t *);
    int (*unlink_exact)(void *, const char *);
};

int starsead_matcher_pin_preflight(
    const struct starsead_matcher_pin_plan *,
    const struct starsead_bpf_pin_ownership_backend *, char *, size_t);
int starsead_b2s_pin_preflight(
    const struct starsead_b2s_pin_plan *,
    const struct starsead_bpf_pin_ownership_backend *, char *, size_t);
int starsead_bpf_pin_cleanup_owned(
    const char *, uint64_t, const struct starsead_bpf_pin_ownership_backend *, char *, size_t);
int starsead_matcher_verify_residue(
    const struct starsead_config *, const struct starsead_matcher_pin_plan *,
    const struct starsead_bpf_program_backend *,
    const struct starsead_bpf_pin_ownership_backend *,
    struct starsead_matcher_verification *, char *, size_t);
int starsead_b2s_verify_residue(
    const struct starsead_config *, const struct starsead_b2s_pin_plan *,
    const struct starsead_bpf_program_backend *,
    const struct starsead_bpf_pin_ownership_backend *,
    struct starsead_b2s_verification *, char *, size_t);
const struct starsead_bpf_pin_ownership_backend *
    starsead_system_bpf_pin_ownership_backend(void);
int starsead_b2s_verify(
    const struct starsead_config *, const struct starsead_b2s_pin_plan *,
    const struct starsead_bpf_program_backend *, struct starsead_b2s_verification *,
    char *, size_t);
const struct starsead_bpf_program_backend *starsead_system_bpf_program_backend(void);

#define STARSEAD_NETWORK_GROUP_LINK UINT32_C(0x00000001)
#define STARSEAD_NETWORK_GROUP_IPV4_ADDRESS UINT32_C(0x00000010)
#define STARSEAD_NETWORK_GROUP_IPV6_ADDRESS UINT32_C(0x00000100)
#define STARSEAD_NETWORK_RECEIVE_BUFFER_SIZE (1024U * 1024U)
#define STARSEAD_NETWORK_SOCKET_RAW UINT32_C(0x1)
#define STARSEAD_NETWORK_SOCKET_NONBLOCK UINT32_C(0x2)
#define STARSEAD_NETWORK_SOCKET_CLOEXEC UINT32_C(0x4)

struct starsead_network_event;
struct starsead_event_batch;

enum starsead_network_receive_result {
    STARSEAD_NETWORK_RECEIVE_DATA,
    STARSEAD_NETWORK_RECEIVE_AGAIN,
    STARSEAD_NETWORK_RECEIVE_INTERRUPTED,
    STARSEAD_NETWORK_RECEIVE_ENOBUFS,
    STARSEAD_NETWORK_RECEIVE_FATAL,
};

struct starsead_network_backend {
    void *context;
    int (*open)(void *, uint32_t, size_t, uint32_t, int *);
    enum starsead_network_receive_result (*receive)(
        void *, int, void *, size_t, size_t *, uint32_t *, bool *);
    int (*interface_name)(void *, uint32_t, char *, size_t);
    int (*ipv6_disabled)(void *, const char *, uint32_t, uint8_t *, bool *);
    int (*close)(void *, int);
};

struct starsead_network_runtime {
    const struct starsead_config *config;
    const struct starsead_network_backend *backend;
    int fd;
    bool fd_owned;
    bool no_op;
    uint32_t groups;
    struct starsead_event_batch *pending_batch_storage;
    bool deadline_present;
    uint64_t deadline_milliseconds;
    bool integrity_loss;
    const struct starsead_network_effect_sink *effect_sink;
    struct starsead_network_effect_request
        immediate_requests[STARSEAD_MAX_NETWORK_IMMEDIATE_REQUESTS];
    size_t immediate_request_count;
};

int starsead_network_open(
    const struct starsead_config *, const struct starsead_network_backend *,
    struct starsead_network_runtime *, char *, size_t);
int starsead_network_note(
    struct starsead_network_runtime *, const struct starsead_network_event *,
    bool, uint64_t);
int starsead_network_handle(
    struct starsead_network_runtime *, uint64_t, char *, size_t);
bool starsead_network_next_deadline(
    const struct starsead_network_runtime *, uint64_t *);
int starsead_network_take_reconcile(
    struct starsead_network_runtime *, uint64_t,
    struct starsead_event_batch *, bool *);
int starsead_network_close(struct starsead_network_runtime *);
int starsead_network_set_effect_sink(
    struct starsead_network_runtime *, const struct starsead_network_effect_sink *);
bool starsead_network_has_immediate(const struct starsead_network_runtime *);
int starsead_network_apply_immediate(
    struct starsead_network_runtime *, char *, size_t);
int starsead_network_reopen(
    struct starsead_network_runtime *, uint64_t, char *, size_t);
const struct starsead_network_backend *starsead_system_network_backend(void);

#define STARSEAD_HOTSPOT_TC_PRIORITY 1U
#define STARSEAD_HOTSPOT_TC_HANDLE 1U
#define STARSEAD_ETH_PROTOCOL_IPV6 UINT32_C(0x86dd)
#define STARSEAD_ETH_PROTOCOL_ALL UINT32_C(0x0003)

enum starsead_hotspot_ipv6_offload_policy {
    STARSEAD_HOTSPOT_IPV6_OFFLOAD_KEEP,
    STARSEAD_HOTSPOT_IPV6_OFFLOAD_REMOVE_PREF1_REQUIRED,
    STARSEAD_HOTSPOT_IPV6_OFFLOAD_REMOVE_PREF1_PREF2_BEST_EFFORT,
};

bool starsead_hotspot_tc_output_has_android_offload(const void *, size_t);
enum starsead_hotspot_ipv6_offload_policy starsead_hotspot_ipv6_offload_policy_for(
    const struct starsead_config *);

enum starsead_tc_slot_state {
    STARSEAD_TC_SLOT_ABSENT,
    STARSEAD_TC_SLOT_OWNED,
    STARSEAD_TC_SLOT_COMPATIBLE,
    STARSEAD_TC_SLOT_FOREIGN,
};

enum starsead_tc_qdisc_cleanup_decision {
    STARSEAD_TC_QDISC_CLEANUP_DELETE,
    STARSEAD_TC_QDISC_CLEANUP_RETAIN_SHARED,
};

enum starsead_tc_plan_operation_kind {
    STARSEAD_TC_PLAN_SET_ROUTE_LOCALNET,
    STARSEAD_TC_PLAN_CREATE_CLSACT,
    STARSEAD_TC_PLAN_CREATE_EGRESS,
    STARSEAD_TC_PLAN_CREATE_INGRESS,
    STARSEAD_TC_PLAN_REMOVE_INGRESS,
    STARSEAD_TC_PLAN_REMOVE_EGRESS,
    STARSEAD_TC_PLAN_REMOVE_CLSACT,
    STARSEAD_TC_PLAN_RESTORE_ROUTE_LOCALNET,
};

struct starsead_tc_interface_probe {
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t interface_index;
    uint8_t route_localnet_value;
    enum starsead_tc_slot_state qdisc;
    enum starsead_tc_slot_state egress;
    enum starsead_tc_slot_state ingress;
};

struct starsead_tc_plan_operation {
    enum starsead_tc_plan_operation_kind kind;
    uint32_t priority;
    uint32_t handle;
    bool direct_action;
    struct starsead_resource_operation operation;
};

struct starsead_tc_plan {
    struct starsead_tc_plan_operation operations[4U];
    size_t operation_count;
};

int starsead_tc_install_plan_build(
    const struct starsead_config *, const struct starsead_tc_interface_probe *,
    struct starsead_tc_plan *, char *, size_t);
int starsead_tc_cleanup_plan_build(
    const struct starsead_tc_plan *, struct starsead_tc_plan *);
enum starsead_tc_qdisc_cleanup_decision starsead_tc_qdisc_cleanup_decide(
    bool ingress_occupied, bool egress_occupied);
bool starsead_tc_qdisc_cleanup_restored(bool qdisc_present,
    enum starsead_tc_qdisc_cleanup_decision decision);

struct starsead_tc_filter_expectation {
    uint32_t interface_index;
    uint32_t parent;
    uint32_t protocol;
    uint32_t priority;
    uint32_t handle;
    char bpf_name[STARSEAD_MAX_INTERFACE_NAME];
    uint32_t bpf_flags;
    uint32_t bpf_flags_gen;
    uint32_t bpf_flags_gen_mask;
    uint64_t program_object_id;
    unsigned char program_tag[STARSEAD_BPF_PROGRAM_TAG_SIZE];
};

int starsead_tc_filter_netlink_decode(
    const void *, size_t, uint32_t, uint32_t,
    const struct starsead_tc_filter_expectation *,
    enum starsead_rules_slot_state *, bool *, char *, size_t);
int starsead_tc_filter_slot_netlink_decode(
    const void *, size_t, uint32_t, uint32_t,
    const struct starsead_tc_filter_expectation *,
    enum starsead_rules_slot_state *, bool *, char *, size_t);
int starsead_tc_qdisc_netlink_decode(
    const void *, size_t, uint32_t, uint32_t, uint32_t,
    enum starsead_rules_slot_state *, bool *, char *, size_t);
enum starsead_event_action {
    STARSEAD_EVENT_ADDED,
    STARSEAD_EVENT_REMOVED,
    STARSEAD_EVENT_UPDATED,
};

struct starsead_network_event {
    bool is_address;
    enum starsead_event_action action;
    int family;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    char address[64];
};

struct starsead_event_batch {
    struct starsead_network_event events[STARSEAD_MAX_NETWORK_EVENTS];
    size_t count;
    bool truncated;
};

#endif
