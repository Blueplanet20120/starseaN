// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define inet_ntop InetNtopA
#else
#include <arpa/inet.h>
#endif

#define STARSEAD_NLMSG_ERROR 2U
#define STARSEAD_NLMSG_OVERRUN 4U
#define STARSEAD_RTM_NEWLINK 16U
#define STARSEAD_RTM_DELLINK 17U
#define STARSEAD_RTM_NEWADDR 20U
#define STARSEAD_RTM_DELADDR 21U
#define STARSEAD_IFLA_IFNAME 3U
#define STARSEAD_IFA_ADDRESS 1U
#define STARSEAD_IFA_LOCAL 2U

struct network_nlmsg_header {
    uint32_t length;
    uint16_t type;
    uint16_t flags;
    uint32_t sequence;
    uint32_t sender;
};

_Static_assert(sizeof(struct network_nlmsg_header) == 16U, "netlink header ABI");

static void network_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size != 0U) (void)snprintf(error, error_size, "%s", message);
}

static bool backend_valid(const struct starsead_network_backend *backend) {
    return backend != NULL && backend->open != NULL && backend->receive != NULL &&
        backend->interface_name != NULL && backend->close != NULL;
}

static uint32_t required_groups(const struct starsead_config *config) {
    if (starsead_mode_core_managed(config->mode)) {
        uint32_t groups = config->disable_system_ipv6 ?
            STARSEAD_NETWORK_GROUP_IPV6_ADDRESS | STARSEAD_NETWORK_GROUP_LINK : 0U;
        if (config->hotspot_interface_prefix_count != 0U) {
            groups |= STARSEAD_NETWORK_GROUP_LINK;
            if (config->enable_ipv6) groups |= STARSEAD_NETWORK_GROUP_IPV6_ADDRESS;
        }
        return groups;
    }
    uint32_t groups = STARSEAD_NETWORK_GROUP_IPV4_ADDRESS;
    if (config->enable_ipv6 || config->disable_system_ipv6) {
        groups |= STARSEAD_NETWORK_GROUP_IPV6_ADDRESS;
    }
    if (config->disable_system_ipv6 ||
        (config->hotspot_interface_prefix_count != 0U &&
         (config->enable_ipv6 || config->mode == STARSEAD_MODE_BPF2SOCKS))) {
        groups |= STARSEAD_NETWORK_GROUP_LINK;
    }
    return groups;
}

int starsead_network_open(
    const struct starsead_config *config,
    const struct starsead_network_backend *backend,
    struct starsead_network_runtime *runtime,
    char *error,
    size_t error_size) {
    if (runtime != NULL) memset(runtime, 0, sizeof(*runtime));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || !backend_valid(backend) || runtime == NULL) {
        network_error(error, error_size, "invalid network runtime input");
        return STARSEAD_CONFIG_INVALID;
    }
    runtime->fd = -1;
    runtime->config = config;
    runtime->backend = backend;
    runtime->groups = required_groups(config);
    runtime->pending_batch_storage = calloc(1U, sizeof(*runtime->pending_batch_storage));
    if (runtime->pending_batch_storage == NULL) {
        memset(runtime, 0, sizeof(*runtime));
        network_error(error, error_size, "network runtime allocation failed");
        return STARSEAD_CONFIG_NO_MEMORY;
    }
    if (runtime->groups == 0U) {
        runtime->no_op = true;
        return 0;
    }
    int fd = -1;
    if (backend->open(backend->context, runtime->groups,
            STARSEAD_NETWORK_RECEIVE_BUFFER_SIZE,
            STARSEAD_NETWORK_SOCKET_RAW | STARSEAD_NETWORK_SOCKET_NONBLOCK |
                STARSEAD_NETWORK_SOCKET_CLOEXEC,
            &fd) != 0 || fd < 0) {
        free(runtime->pending_batch_storage);
        memset(runtime, 0, sizeof(*runtime));
        network_error(error, error_size, "network socket open failed");
        return STARSEAD_CONFIG_IO;
    }
    runtime->fd = fd;
    runtime->fd_owned = true;
    return 0;
}

static bool event_valid(const struct starsead_network_event *event) {
    if (event == NULL || event->action > STARSEAD_EVENT_UPDATED ||
        event->interface_name[0] == '\0' ||
        strnlen(event->interface_name, sizeof(event->interface_name)) >= sizeof(event->interface_name)) {
        return false;
    }
    if (!event->is_address) return event->family == 0 && event->address[0] == '\0';
    return (event->family == STARSEAD_ADDRESS_IPV4 || event->family == STARSEAD_ADDRESS_IPV6) &&
        event->address[0] != '\0' &&
        strnlen(event->address, sizeof(event->address)) < sizeof(event->address);
}

static bool same_event(
    const struct starsead_network_event *left,
    const struct starsead_network_event *right) {
    return left->is_address == right->is_address && left->action == right->action &&
        left->family == right->family &&
        strcmp(left->interface_name, right->interface_name) == 0 &&
        strcmp(left->address, right->address) == 0;
}

int starsead_network_note(
    struct starsead_network_runtime *runtime,
    const struct starsead_network_event *event,
    bool integrity_loss,
    uint64_t now_milliseconds) {
    if (runtime == NULL || runtime->pending_batch_storage == NULL ||
        (event == NULL && !integrity_loss) || (event != NULL && !event_valid(event)) ||
        now_milliseconds > UINT64_MAX - STARSEAD_SYNC_DEBOUNCE_MILLIS) {
        return STARSEAD_CONFIG_INVALID;
    }
    bool changed = false;
    struct starsead_event_batch *batch = runtime->pending_batch_storage;
    if (event != NULL) {
        bool duplicate = false;
        for (size_t index = 0U; index < batch->count; ++index) {
            if (same_event(&batch->events[index], event)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            changed = true;
            if (batch->count == STARSEAD_MAX_NETWORK_EVENTS) {
                batch->truncated = true;
            } else {
                batch->events[batch->count++] = *event;
            }
        }
    }
    if (integrity_loss) {
        runtime->integrity_loss = true;
        batch->truncated = true;
        changed = true;
    }
    if (changed) {
        runtime->deadline_present = true;
        runtime->deadline_milliseconds = now_milliseconds + STARSEAD_SYNC_DEBOUNCE_MILLIS;
    }
    return 0;
}

static size_t align4(size_t value) {
    return (value + 3U) & ~((size_t)3U);
}

static bool interface_name_valid(const char *name) {
    size_t length = name == NULL ? 0U : strnlen(name, STARSEAD_MAX_INTERFACE_NAME);
    if (length == 0U || length >= STARSEAD_MAX_INTERFACE_NAME ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char byte = (unsigned char)name[index];
        if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') || byte == '_' || byte == '.' || byte == '-')) return false;
    }
    return true;
}

static bool hotspot_interface(const struct starsead_config *config, const char *name) {
    for (size_t index = 0U; index < config->hotspot_interface_prefix_count; ++index) {
        if (starsead_interface_matches_selector(
                name, config->hotspot_interface_prefixes[index])) return true;
    }
    return false;
}

static bool tracked_interface(const struct starsead_config *config, const char *name) {
    if (starsead_mode_core_managed(config->mode) || strcmp(name, "all") == 0 ||
        strcmp(name, "default") == 0 || strcmp(name, "lo") == 0) return false;
    for (size_t index = 0U; index < config->ignored_interface_count; ++index) {
        if (starsead_interface_matches_selector(
                name, config->ignored_interfaces[index])) return false;
    }
    for (size_t index = 0U; index < config->virtual_interface_count; ++index) {
        if (strcmp(name, config->virtual_interfaces[index]) == 0) return false;
    }
    return true;
}

static bool security_interface(const char *name) {
    return strcmp(name, "all") != 0 && strcmp(name, "default") != 0 &&
        strcmp(name, "lo") != 0 &&
        !starsead_interface_matches_selector(name, "ipsec+");
}

static bool immediate_request_same(
    const struct starsead_network_effect_request *left,
    const struct starsead_network_effect_request *right) {
    return left->effect.kind == STARSEAD_EFFECT_SYSCTL &&
        right->effect.kind == STARSEAD_EFFECT_SYSCTL &&
        left->effect.resource.sysctl.sysctl_id ==
            right->effect.resource.sysctl.sysctl_id &&
        left->effect.resource.sysctl.interface_index ==
            right->effect.resource.sysctl.interface_index &&
        strcmp(left->effect.resource.sysctl.interface_name,
            right->effect.resource.sysctl.interface_name) == 0;
}

static int queue_immediate_request(void *opaque,
    const struct starsead_network_effect_request *request,
    char *error, size_t error_size) {
    struct starsead_network_runtime *runtime = opaque;
    if (runtime == NULL || request == NULL ||
        request->effect.kind != STARSEAD_EFFECT_SYSCTL ||
        request->effect.resource.sysctl.sysctl_id !=
            STARSEAD_SYSCTL_DISABLE_IPV6) {
        network_error(error, error_size, "invalid immediate network request");
        return STARSEAD_CONFIG_INVALID;
    }
    for (size_t index = 0U; index < runtime->immediate_request_count; ++index) {
        if (immediate_request_same(&runtime->immediate_requests[index], request)) return 0;
    }
    if (runtime->immediate_request_count >= STARSEAD_MAX_NETWORK_IMMEDIATE_REQUESTS) {
        network_error(error, error_size, "immediate network request capacity exceeded");
        return STARSEAD_CONFIG_INVALID;
    }
    runtime->immediate_requests[runtime->immediate_request_count++] = *request;
    return 0;
}

static int enforce_ipv6_security(
    struct starsead_network_runtime *runtime, const char *interface_name,
    uint32_t interface_index) {
    if (!runtime->config->disable_system_ipv6) return 0;
    if (runtime->backend->ipv6_disabled == NULL) {
        return STARSEAD_CONFIG_IO;
    }
    uint8_t current = 0U;
    bool exists = false;
    if (runtime->backend->ipv6_disabled(runtime->backend->context, interface_name,
        interface_index, &current, &exists) != 0) return STARSEAD_CONFIG_IO;
    if (!exists || current == 1U) return 0;
    if (current != 0U) return STARSEAD_CONFIG_IO;
    struct starsead_network_effect_request request;
    memset(&request, 0, sizeof(request));
    request.effect.kind = STARSEAD_EFFECT_SYSCTL;
    request.effect.resource.sysctl.sysctl_id = STARSEAD_SYSCTL_DISABLE_IPV6;
    (void)snprintf(request.effect.resource.sysctl.interface_name,
        sizeof(request.effect.resource.sysctl.interface_name),
        "%s", interface_name);
    request.effect.resource.sysctl.interface_index = interface_index;
    request.effect.resource.sysctl.desired_value = 1U;
    return queue_immediate_request(runtime, &request, NULL, 0U);
}

static int attribute_payload(
    const unsigned char *attributes,
    size_t attributes_length,
    uint16_t wanted_type,
    const unsigned char **payload,
    size_t *payload_length,
    bool prefer_last) {
    size_t offset = 0U;
    while (offset < attributes_length) {
        if (attributes_length - offset < 4U) return -1;
        uint16_t length;
        uint16_t type;
        memcpy(&length, attributes + offset, sizeof(length));
        memcpy(&type, attributes + offset + 2U, sizeof(type));
        if (length < 4U || length > attributes_length - offset ||
            align4(length) > attributes_length - offset) return -1;
        if (type == wanted_type && (*payload == NULL || prefer_last)) {
            *payload = attributes + offset + 4U;
            *payload_length = length - 4U;
        }
        offset += align4(length);
    }
    return offset == attributes_length ? 0 : -1;
}

static int record_address_message(
    struct starsead_network_runtime *runtime,
    const unsigned char *message,
    size_t message_length,
    uint16_t message_type,
    uint64_t now_milliseconds) {
    if (message_length < sizeof(struct network_nlmsg_header) + 8U) return -1;
    const unsigned char *info = message + sizeof(struct network_nlmsg_header);
    int family = info[0] == 2U ? STARSEAD_ADDRESS_IPV4 :
        info[0] == 10U ? STARSEAD_ADDRESS_IPV6 : 0;
    if (family == 0) return 0;
    uint32_t interface_index;
    memcpy(&interface_index, info + 4U, sizeof(interface_index));
    if (interface_index == 0U) return -1;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    memset(interface_name, 0, sizeof(interface_name));
    if (runtime->backend->interface_name(runtime->backend->context, interface_index,
            interface_name, sizeof(interface_name)) != 0 || !interface_name_valid(interface_name)) return -1;
    const unsigned char *attributes = info + 8U;
    size_t attributes_length = message_length - sizeof(struct network_nlmsg_header) - 8U;
    const unsigned char *address = NULL;
    size_t address_length = 0U;
    if (attribute_payload(attributes, attributes_length, STARSEAD_IFA_ADDRESS,
            &address, &address_length, false) != 0) return -1;
    const unsigned char *local = NULL;
    size_t local_length = 0U;
    if (attribute_payload(attributes, attributes_length, STARSEAD_IFA_LOCAL,
            &local, &local_length, false) != 0) return -1;
    if (local != NULL) {
        address = local;
        address_length = local_length;
    }
    size_t expected_length = family == STARSEAD_ADDRESS_IPV4 ? 4U : 16U;
    if (address == NULL || address_length != expected_length) return -1;
    if (message_type == STARSEAD_RTM_NEWADDR && family == STARSEAD_ADDRESS_IPV6 &&
        runtime->config->disable_system_ipv6 && security_interface(interface_name)) {
        int security = enforce_ipv6_security(runtime, interface_name, interface_index);
        if (security != 0) return STARSEAD_CONFIG_IO;
    }
    bool relevant = tracked_interface(runtime->config, interface_name) ||
        hotspot_interface(runtime->config, interface_name) ||
        (runtime->config->disable_system_ipv6 && family == STARSEAD_ADDRESS_IPV6 &&
         security_interface(interface_name));
    if (!relevant) return 0;
    struct starsead_network_event event;
    memset(&event, 0, sizeof(event));
    event.is_address = true;
    event.action = message_type == STARSEAD_RTM_DELADDR ?
        STARSEAD_EVENT_REMOVED : STARSEAD_EVENT_ADDED;
    event.family = family;
    memcpy(event.interface_name, interface_name, strlen(interface_name) + 1U);
    int native_family = family == STARSEAD_ADDRESS_IPV4 ? AF_INET : AF_INET6;
    if (inet_ntop(native_family, address, event.address, sizeof(event.address)) == NULL) return -1;
    return starsead_network_note(runtime, &event, false, now_milliseconds);
}

static int record_link_message(
    struct starsead_network_runtime *runtime,
    const unsigned char *message,
    size_t message_length,
    uint16_t message_type,
    uint64_t now_milliseconds) {
    if (message_length < sizeof(struct network_nlmsg_header) + 16U) return -1;
    const unsigned char *info = message + sizeof(struct network_nlmsg_header);
    int32_t signed_index;
    memcpy(&signed_index, info + 4U, sizeof(signed_index));
    if (signed_index <= 0) return -1;
    const unsigned char *attributes = info + 16U;
    size_t attributes_length = message_length - sizeof(struct network_nlmsg_header) - 16U;
    const unsigned char *name_bytes = NULL;
    size_t name_length = 0U;
    if (attribute_payload(attributes, attributes_length, STARSEAD_IFLA_IFNAME,
            &name_bytes, &name_length, false) != 0) return -1;
    char interface_name[STARSEAD_MAX_INTERFACE_NAME];
    memset(interface_name, 0, sizeof(interface_name));
    if (name_bytes != NULL) {
        size_t length = strnlen((const char *)name_bytes, name_length);
        if (length == 0U || length >= name_length || length >= sizeof(interface_name)) return -1;
        memcpy(interface_name, name_bytes, length);
    } else if (runtime->backend->interface_name(runtime->backend->context,
            (uint32_t)signed_index, interface_name, sizeof(interface_name)) != 0) {
        return -1;
    }
    if (!interface_name_valid(interface_name)) return -1;
    if (message_type == STARSEAD_RTM_NEWLINK && runtime->config->disable_system_ipv6 &&
        security_interface(interface_name)) {
        int security = enforce_ipv6_security(
            runtime, interface_name, (uint32_t)signed_index);
        if (security != 0) return STARSEAD_CONFIG_IO;
    }
    if (!runtime->config->disable_system_ipv6 &&
        !hotspot_interface(runtime->config, interface_name)) return 0;
    struct starsead_network_event event;
    memset(&event, 0, sizeof(event));
    event.action = message_type == STARSEAD_RTM_DELLINK ?
        STARSEAD_EVENT_REMOVED : STARSEAD_EVENT_UPDATED;
    memcpy(event.interface_name, interface_name, strlen(interface_name) + 1U);
    return starsead_network_note(runtime, &event, false, now_milliseconds);
}

static int parse_datagram(
    struct starsead_network_runtime *runtime,
    const unsigned char *bytes,
    size_t length,
    uint32_t sender_pid,
    bool truncated,
    uint64_t now_milliseconds) {
    if (truncated || sender_pid != 0U) {
        return starsead_network_note(runtime, NULL, true, now_milliseconds);
    }
    size_t offset = 0U;
    while (offset < length) {
        if (length - offset < sizeof(struct network_nlmsg_header)) {
            return starsead_network_note(runtime, NULL, true, now_milliseconds);
        }
        struct network_nlmsg_header header;
        memcpy(&header, bytes + offset, sizeof(header));
        if (header.length < sizeof(header) || header.length > length - offset ||
            align4(header.length) > length - offset) {
            return starsead_network_note(runtime, NULL, true, now_milliseconds);
        }
        int result = 0;
        if (header.type == STARSEAD_NLMSG_ERROR || header.type == STARSEAD_NLMSG_OVERRUN) {
            result = starsead_network_note(runtime, NULL, true, now_milliseconds);
        } else if (header.type == STARSEAD_RTM_NEWADDR || header.type == STARSEAD_RTM_DELADDR) {
            result = record_address_message(
                runtime, bytes + offset, header.length, header.type, now_milliseconds);
        } else if (header.type == STARSEAD_RTM_NEWLINK || header.type == STARSEAD_RTM_DELLINK) {
            result = record_link_message(
                runtime, bytes + offset, header.length, header.type, now_milliseconds);
        }
        if (result == STARSEAD_CONFIG_IO) return result;
        if (result != 0) return starsead_network_note(runtime, NULL, true, now_milliseconds);
        offset += align4(header.length);
    }
    return 0;
}

int starsead_network_handle(
    struct starsead_network_runtime *runtime,
    uint64_t now_milliseconds,
    char *error,
    size_t error_size) {
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (runtime == NULL || runtime->backend == NULL || runtime->pending_batch_storage == NULL) {
        network_error(error, error_size, "invalid network handle input");
        return STARSEAD_CONFIG_INVALID;
    }
    if (runtime->no_op) return 0;
    unsigned char buffer[65536U];
    while (true) {
        size_t length = 0U;
        uint32_t sender_pid = 0U;
        bool truncated = false;
        enum starsead_network_receive_result received = runtime->backend->receive(
            runtime->backend->context, runtime->fd, buffer, sizeof(buffer),
            &length, &sender_pid, &truncated);
        if (received == STARSEAD_NETWORK_RECEIVE_AGAIN) return 0;
        if (received == STARSEAD_NETWORK_RECEIVE_INTERRUPTED) continue;
        if (received == STARSEAD_NETWORK_RECEIVE_ENOBUFS) {
            if (starsead_network_note(runtime, NULL, true, now_milliseconds) != 0) break;
            continue;
        }
        if (received == STARSEAD_NETWORK_RECEIVE_DATA && length != 0U &&
            parse_datagram(runtime, buffer, length, sender_pid, truncated, now_milliseconds) == 0) {
            continue;
        }
        if (received == STARSEAD_NETWORK_RECEIVE_FATAL &&
            starsead_network_reopen(runtime, now_milliseconds, error, error_size) == 0) {
            continue;
        }
        network_error(error, error_size, "network receive failed");
        return STARSEAD_CONFIG_IO;
    }
    network_error(error, error_size, "network event deadline overflow");
    return STARSEAD_CONFIG_INVALID;
}

int starsead_network_reopen(
    struct starsead_network_runtime *runtime, uint64_t now_milliseconds,
    char *error, size_t error_size) {
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (runtime == NULL || runtime->backend == NULL || runtime->no_op ||
        !runtime->fd_owned || runtime->backend->open == NULL || runtime->backend->close == NULL) {
        network_error(error, error_size, "invalid network reopen input");
        return STARSEAD_CONFIG_INVALID;
    }
    int old_fd = runtime->fd;
    runtime->fd = -1;
    runtime->fd_owned = false;
    if (runtime->backend->close(runtime->backend->context, old_fd) != 0) {
        network_error(error, error_size, "network close before reopen failed");
        return STARSEAD_CONFIG_IO;
    }
    int new_fd = -1;
    if (runtime->backend->open(runtime->backend->context, runtime->groups,
        STARSEAD_NETWORK_RECEIVE_BUFFER_SIZE,
        STARSEAD_NETWORK_SOCKET_RAW | STARSEAD_NETWORK_SOCKET_NONBLOCK |
            STARSEAD_NETWORK_SOCKET_CLOEXEC,
        &new_fd) != 0 || new_fd < 0) {
        network_error(error, error_size, "network reopen failed");
        return STARSEAD_CONFIG_IO;
    }
    runtime->fd = new_fd;
    runtime->fd_owned = true;
    if (starsead_network_note(runtime, NULL, true, now_milliseconds) != 0) {
        network_error(error, error_size, "network reopen deadline overflow");
        return STARSEAD_CONFIG_INVALID;
    }
    return 0;
}

int starsead_network_set_effect_sink(
    struct starsead_network_runtime *runtime,
    const struct starsead_network_effect_sink *sink) {
    if (runtime == NULL || sink == NULL || sink->dispatch == NULL) return STARSEAD_CONFIG_INVALID;
    runtime->effect_sink = sink;
    return 0;
}

bool starsead_network_has_immediate(const struct starsead_network_runtime *runtime) {
    return runtime != NULL && runtime->immediate_request_count != 0U;
}

int starsead_network_apply_immediate(
    struct starsead_network_runtime *runtime, char *error, size_t error_size) {
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (runtime == NULL ||
        (runtime->immediate_request_count != 0U &&
            (runtime->effect_sink == NULL ||
             runtime->effect_sink->dispatch == NULL))) {
        network_error(error, error_size, "invalid immediate network apply input");
        return STARSEAD_CONFIG_INVALID;
    }
    while (runtime->immediate_request_count != 0U) {
        if (runtime->effect_sink->dispatch(runtime->effect_sink->context,
                &runtime->immediate_requests[0], error, error_size) != 0) {
            return STARSEAD_CONFIG_IO;
        }
        --runtime->immediate_request_count;
        if (runtime->immediate_request_count != 0U) {
            memmove(&runtime->immediate_requests[0], &runtime->immediate_requests[1],
                runtime->immediate_request_count * sizeof(runtime->immediate_requests[0]));
        }
        memset(&runtime->immediate_requests[runtime->immediate_request_count], 0,
            sizeof(runtime->immediate_requests[0]));
    }
    return 0;
}

bool starsead_network_next_deadline(
    const struct starsead_network_runtime *runtime,
    uint64_t *deadline_milliseconds) {
    if (runtime == NULL || deadline_milliseconds == NULL || !runtime->deadline_present) return false;
    *deadline_milliseconds = runtime->deadline_milliseconds;
    return true;
}

int starsead_network_take_reconcile(
    struct starsead_network_runtime *runtime,
    uint64_t now_milliseconds,
    struct starsead_event_batch *batch,
    bool *integrity_loss) {
    if (batch != NULL) memset(batch, 0, sizeof(*batch));
    if (integrity_loss != NULL) *integrity_loss = false;
    if (runtime == NULL || runtime->pending_batch_storage == NULL || batch == NULL ||
        integrity_loss == NULL) return STARSEAD_CONFIG_INVALID;
    if (!runtime->deadline_present || now_milliseconds < runtime->deadline_milliseconds) {
        return STARSEAD_CONFIG_NOT_READY;
    }
    *batch = *runtime->pending_batch_storage;
    *integrity_loss = runtime->integrity_loss;
    memset(runtime->pending_batch_storage, 0, sizeof(*runtime->pending_batch_storage));
    runtime->integrity_loss = false;
    runtime->deadline_present = false;
    runtime->deadline_milliseconds = 0U;
    return 0;
}

int starsead_network_close(struct starsead_network_runtime *runtime) {
    if (runtime == NULL) return STARSEAD_CONFIG_INVALID;
    int result = 0;
    if (runtime->fd_owned && runtime->backend != NULL &&
        runtime->backend->close(runtime->backend->context, runtime->fd) != 0) {
        result = STARSEAD_CONFIG_IO;
    }
    free(runtime->pending_batch_storage);
    memset(runtime, 0, sizeof(*runtime));
    return result;
}
