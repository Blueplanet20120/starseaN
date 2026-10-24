// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int render_hev(const struct starsead_config *config, struct starsead_anonymous_document *document) {
    const struct starsead_hev_helper_config *hev = &config->helper.value.hev;
    struct starsead_text_writer writer = {0};
    starsead_text_writer_literal(&writer, "tunnel:\n  name: ");
    starsead_text_writer_yaml_string(&writer, hev->tunnel_name);
    starsead_text_writer_format(&writer, "\n  mtu: %u\n  multi-queue: %s\n  ipv4: ", hev->mtu, hev->multi_queue ? "true" : "false");
    starsead_text_writer_yaml_string(&writer, hev->ipv4_address);
    if (hev->has_ipv6_address) {
        starsead_text_writer_literal(&writer, "\n  ipv6: ");
        starsead_text_writer_yaml_string(&writer, hev->ipv6_address);
    }
    starsead_text_writer_format(&writer, "\nsocks5:\n  port: %u\n  address: ", (unsigned)hev->socks_port);
    starsead_text_writer_yaml_string(&writer, hev->socks_host);
    starsead_text_writer_format(&writer,
        "\n  udp: 'udp'\n  tcp-fastopen: %s\nmisc:\n"
        "  tcp-read-write-timeout: %u\n  udp-read-write-timeout: %u\n"
        "  log-file: ",
        hev->tcp_fast_open ? "true" : "false", hev->tcp_read_write_timeout_milliseconds,
        hev->udp_read_write_timeout_milliseconds);
    char log_path[STARSEAD_MAX_PATH];
    if (starsead_log_sibling_path(config->log_path, "tun2socks.log",
            log_path, sizeof(log_path)) != 0) {
        free(writer.bytes);
        return STARSEAD_CONFIG_INVALID;
    }
    starsead_text_writer_yaml_string(&writer, log_path);
    starsead_text_writer_literal(&writer, "\n  log-level: warn\n");
    return starsead_text_writer_take(&writer, document);
}

static void writer_string_array(struct starsead_text_writer *writer, const char values[][STARSEAD_MAX_INTERFACE_NAME], size_t count) {
    starsead_text_writer_literal(writer, "[");
    for (size_t index = 0U; index < count; ++index) {
        if (index != 0U) starsead_text_writer_literal(writer, ",");
        starsead_text_writer_json_string(writer, values[index]);
    }
    starsead_text_writer_literal(writer, "]");
}

static void writer_cidr_array(struct starsead_text_writer *writer, const char values[][STARSEAD_MAX_CIDR], size_t count, bool ipv6) {
    starsead_text_writer_literal(writer, "[");
    bool first = true;
    for (size_t index = 0U; index < count; ++index) {
        if ((strchr(values[index], ':') != NULL) != ipv6) continue;
        if (!first) starsead_text_writer_literal(writer, ",");
        starsead_text_writer_json_string(writer, values[index]);
        first = false;
    }
    starsead_text_writer_literal(writer, "]");
}

static int render_policy_uids(struct starsead_text_writer *writer, const struct starsead_config *config) {
    size_t capacity = config->uid_count + (config->app_policy_mode == STARSEAD_APP_POLICY_WHITELIST ? 2U : 0U);
    uint32_t *uids = capacity == 0U ? NULL : malloc(capacity * sizeof(*uids));
    if (capacity != 0U && uids == NULL) return STARSEAD_CONFIG_NO_MEMORY;
    if (config->uid_count != 0U) memcpy(uids, config->uids, config->uid_count * sizeof(*uids));
    size_t count = config->uid_count;
    if (config->app_policy_mode == STARSEAD_APP_POLICY_WHITELIST) {
        uids[count++] = 0U;
        uids[count++] = 1052U;
    }
    if (count > 1U) qsort(uids, count, sizeof(*uids), starsead_compare_u32);
    starsead_text_writer_literal(writer, "[");
    bool first = true;
    uint32_t previous = 0U;
    for (size_t index = 0U; index < count; ++index) {
        if (index != 0U && uids[index] == previous) continue;
        if (!first) starsead_text_writer_literal(writer, ",");
        starsead_text_writer_format(writer, "%u", uids[index]);
        previous = uids[index];
        first = false;
    }
    starsead_text_writer_literal(writer, "]");
    free(uids);
    return writer->result;
}

static void writer_uid_array(struct starsead_text_writer *writer, const uint32_t *values, size_t count) {
    starsead_text_writer_literal(writer, "[");
    for (size_t index = 0U; index < count; ++index) {
        if (index != 0U) starsead_text_writer_literal(writer, ",");
        starsead_text_writer_format(writer, "%u", values[index]);
    }
    starsead_text_writer_literal(writer, "]");
}

static int render_direct_family(const char values[][STARSEAD_MAX_CIDR], size_t count, struct starsead_anonymous_document *document) {
    struct starsead_text_writer writer = {0};
    for (size_t index = 0U; index < count; ++index) {
        starsead_text_writer_literal(&writer, values[index]);
        starsead_text_writer_literal(&writer, "\n");
    }
    return starsead_text_writer_take(&writer, document);
}

static unsigned policy_mode(enum starsead_app_policy_mode mode) {
    switch (mode) {
        case STARSEAD_APP_POLICY_BLACKLIST: return 0U;
        case STARSEAD_APP_POLICY_WHITELIST: return 1U;
        case STARSEAD_APP_POLICY_GLOBAL: return 2U;
    }
    return 3U;
}

static int render_bpf(const struct starsead_config *config, int direct_ipv4_fd, int direct_ipv6_fd, struct starsead_helper_documents *documents) {
    const struct starsead_bpf_helper_config *bpf = &config->helper.value.bpf;
    const char *pin_namespace = starsead_owned_resource_catalog()->b2s_root;
    if (policy_mode(config->app_policy_mode) > 2U) return STARSEAD_CONFIG_INVALID;
    bool direct = config->direct_cidrs != NULL;
    if (direct && (direct_ipv4_fd < 0 || direct_ipv6_fd < 0 || direct_ipv4_fd == direct_ipv6_fd)) return STARSEAD_CONFIG_INVALID;
    if (direct) {
        int result = render_direct_family(config->direct_cidrs->ipv4, config->direct_cidrs->ipv4_count, &documents->direct_ipv4);
        if (result == 0) result = render_direct_family(config->direct_cidrs->ipv6, config->direct_cidrs->ipv6_count, &documents->direct_ipv6);
        if (result != 0) return result;
        documents->has_direct_cidrs = true;
    }

    struct starsead_text_writer writer = {0};
    starsead_text_writer_literal(&writer, "{\"version\":1,\"bridgeListenAddress\":");
    starsead_text_writer_json_string(&writer, bpf->bridge_listen_address);
    starsead_text_writer_format(&writer, ",\"bridgePort\":%u", (unsigned)bpf->bridge_port);
    starsead_text_writer_literal(&writer, ",\"tokenIpv4Prefix\":\"127.128.0.0/9\",\"tokenIpv6Prefix\":\"fd7a:7374:6572:6973::/64\",\"pinnedObjectDir\":");
    starsead_text_writer_json_string(&writer, pin_namespace);
    starsead_text_writer_literal(&writer, ",\"cgroupPath\":\"/sys/fs/cgroup\"");
    starsead_text_writer_format(&writer,
        ",\"workerCount\":%u,\"tcpBufferSize\":%u,\"maxTcpSessions\":%u,\"connectTimeoutMilliseconds\":%u"
        ",\"tcpIdleTimeoutMilliseconds\":%u,\"udpSocketBufferSize\":%u,\"udpBatchSize\":%u,\"maxUdpSessions\":%u"
        ",\"maxUdpBindings\":%u,\"udpIdleTimeoutSeconds\":%u,\"maxUdpPendingBytes\":%u,\"dnsTransactionTimeoutMilliseconds\":%u",
        bpf->worker_count, bpf->tcp_buffer_size, bpf->max_tcp_sessions, bpf->connect_timeout_milliseconds,
        bpf->tcp_idle_timeout_milliseconds, bpf->udp_socket_buffer_size, bpf->udp_batch_size, bpf->max_udp_sessions,
        bpf->max_udp_bindings, bpf->udp_idle_timeout_seconds, bpf->max_udp_pending_bytes,
        bpf->dns_transaction_timeout_milliseconds);
    starsead_text_writer_literal(&writer, ",\"socksHost\":");
    starsead_text_writer_json_string(&writer, bpf->socks_host);
    starsead_text_writer_format(&writer, ",\"socksPort\":%u,\"enableIpv6\":%s,\"enableDnsHijack\":%s",
        (unsigned)bpf->socks_port, config->enable_ipv6 ? "true" : "false", config->enable_local_dns ? "true" : "false");
    starsead_text_writer_literal(&writer, ",\"hotspotInterfacePrefixes\":");
    writer_string_array(&writer, config->hotspot_interface_prefixes, config->hotspot_interface_prefix_count);
    starsead_text_writer_literal(&writer, ",\"ignoredInterfaces\":");
    writer_string_array(&writer, config->ignored_interfaces, config->ignored_interface_count);
    starsead_text_writer_literal(&writer, ",\"proxyPrivateCidrsV4\":");
    writer_cidr_array(&writer, config->proxy_private_cidrs, config->proxy_private_cidr_count, false);
    starsead_text_writer_literal(&writer, ",\"bypassPrivateCidrsV4\":");
    writer_cidr_array(&writer, config->bypass_private_cidrs, config->bypass_private_cidr_count, false);
    starsead_text_writer_literal(&writer, ",\"proxyPrivateCidrsV6\":");
    writer_cidr_array(&writer, config->proxy_private_cidrs, config->proxy_private_cidr_count, true);
    starsead_text_writer_literal(&writer, ",\"bypassPrivateCidrsV6\":");
    writer_cidr_array(&writer, config->bypass_private_cidrs, config->bypass_private_cidr_count, true);
    starsead_text_writer_format(&writer, ",\"policy\":{\"mode\":%u,\"uids\":", policy_mode(config->app_policy_mode));
    int result = render_policy_uids(&writer, config);
    starsead_text_writer_literal(&writer, ",\"bypassUids\":");
    writer_uid_array(&writer, config->bypass_uids, config->bypass_uid_count);
    starsead_text_writer_format(&writer, ",\"bypassDirectCidrs\":%s,\"directCidrPathV4\":", direct ? "true" : "false");
    char path[64];
    if (direct) (void)snprintf(path, sizeof(path), "/proc/self/fd/%d", direct_ipv4_fd); else path[0] = '\0';
    starsead_text_writer_json_string(&writer, path);
    starsead_text_writer_literal(&writer, ",\"directCidrPathV6\":");
    if (direct) (void)snprintf(path, sizeof(path), "/proc/self/fd/%d", direct_ipv6_fd); else path[0] = '\0';
    starsead_text_writer_json_string(&writer, path);
    starsead_text_writer_literal(&writer, "},\"debugStats\":false}\n");
    if (result == 0) result = starsead_text_writer_take(&writer, &documents->config); else free(writer.bytes);
    return result;
}

void starsead_helper_documents_destroy(struct starsead_helper_documents *documents) {
    if (documents == NULL) return;
    free(documents->config.bytes);
    free(documents->direct_ipv4.bytes);
    free(documents->direct_ipv6.bytes);
    memset(documents, 0, sizeof(*documents));
}

int starsead_helper_render_documents(const struct starsead_config *config, int direct_ipv4_fd, int direct_ipv6_fd,
    struct starsead_helper_documents *documents, char *error, size_t error_size) {
    if (documents != NULL) memset(documents, 0, sizeof(*documents));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || documents == NULL) {
        starsead_error_copy(error, error_size, "invalid helper document input");
        return STARSEAD_CONFIG_INVALID;
    }
    int result;
    if (config->helper.type == STARSEAD_HELPER_HEV_SOCKS5_TUNNEL) result = render_hev(config, &documents->config);
    else if (config->helper.type == STARSEAD_HELPER_BPF2SOCKS) result = render_bpf(config, direct_ipv4_fd, direct_ipv6_fd, documents);
    else result = STARSEAD_CONFIG_INVALID;
    if (result != 0) {
        starsead_helper_documents_destroy(documents);
        starsead_error_copy(error, error_size, result == STARSEAD_CONFIG_NO_MEMORY ?
            "helper document allocation failed" : "invalid helper document configuration");
    }
    return result;
}

int starsead_helper_process_spec(
    const struct starsead_config *config,
    const char *const *inherited_environment,
    int config_fd,
    int direct_ipv4_fd,
    int direct_ipv6_fd,
    struct starsead_process_spec *spec,
    char *error,
    size_t error_size) {
    if (spec != NULL) memset(spec, 0, sizeof(*spec));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || spec == NULL || config_fd < 0 ||
        starsead_copy_string(spec->working_directory, sizeof(spec->working_directory),
            config != NULL ? config->working_directory : NULL) != 0) goto invalid;
    spec->uid = 0U;
    const char *executable = NULL;
    if (config->helper.type == STARSEAD_HELPER_HEV_SOCKS5_TUNNEL) {
        executable = config->helper.value.hev.executable_path;
        spec->gid = 0U;
        spec->output_mode = STARSEAD_PROCESS_OUTPUT_DISCARD;
    } else if (config->helper.type == STARSEAD_HELPER_BPF2SOCKS) {
        executable = config->helper.value.bpf.executable_path;
        spec->gid = 3005U;
        spec->output_mode = STARSEAD_PROCESS_OUTPUT_APPEND_CORE_LOG;
        if (starsead_log_sibling_path(config->log_path, "error.log",
                spec->output_path, sizeof(spec->output_path)) != 0) goto invalid;
    } else {
        goto invalid;
    }
    if (starsead_copy_string(spec->executable_path, sizeof(spec->executable_path), executable) != 0 ||
        starsead_process_environment_rebuild(inherited_environment, spec) != 0 ||
        starsead_process_argument_add(spec, spec->executable_path) != 0 ||
        starsead_process_inherit_fd(spec, config_fd, 3) != 0) goto invalid;
    if (config->helper.type == STARSEAD_HELPER_HEV_SOCKS5_TUNNEL) {
        if (starsead_process_argument_add(spec, "/proc/self/fd/3") != 0) goto invalid;
    } else {
        if (starsead_process_argument_add(spec, "--start") != 0 ||
            starsead_process_argument_add(spec, "--config") != 0 ||
            starsead_process_argument_add(spec, "/proc/self/fd/3") != 0 ||
            starsead_process_argument_add(spec, "--pid") != 0 ||
            starsead_process_argument_add(spec, "/dev/null") != 0) goto invalid;
        if (config->dns_enhancement && starsead_process_argument_add(spec, "--dns-enhancement") != 0) goto invalid;
        if (config->direct_cidrs != NULL) {
            if (starsead_process_inherit_fd(spec, direct_ipv4_fd, 4) != 0 ||
                starsead_process_inherit_fd(spec, direct_ipv6_fd, 5) != 0) goto invalid;
        }
    }
    return 0;

invalid:
    starsead_error_copy(error, error_size, "invalid helper process configuration");
    starsead_process_spec_destroy(spec);
    return STARSEAD_CONFIG_INVALID;
}

int starsead_helper_launch_destroy(
    const struct starsead_anonymous_file_backend *backend,
    struct starsead_helper_launch *launch) {
    if (launch == NULL) return STARSEAD_CONFIG_INVALID;
    int result = 0;
    if (starsead_anonymous_file_close(backend, &launch->direct_ipv6_file) != 0) result = STARSEAD_CONFIG_IO;
    if (starsead_anonymous_file_close(backend, &launch->direct_ipv4_file) != 0) result = STARSEAD_CONFIG_IO;
    if (starsead_anonymous_file_close(backend, &launch->config_file) != 0) result = STARSEAD_CONFIG_IO;
    starsead_process_spec_destroy(&launch->process);
    memset(launch, 0, sizeof(*launch));
    return result;
}

int starsead_helper_launch_prepare(
    const struct starsead_config *config,
    const char *const *inherited_environment,
    const struct starsead_anonymous_file_backend *backend,
    struct starsead_helper_launch *launch,
    char *error,
    size_t error_size) {
    if (launch != NULL) memset(launch, 0, sizeof(*launch));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || backend == NULL || launch == NULL) {
        starsead_error_copy(error, error_size, "invalid helper launch input");
        return STARSEAD_CONFIG_INVALID;
    }
    struct starsead_helper_documents documents;
    memset(&documents, 0, sizeof(documents));
    int result = starsead_helper_render_documents(
        config, 4, 5, &documents, error, error_size);
    if (result != 0) return result;
    result = starsead_anonymous_file_create(
        backend, "starsead.helper.config", &documents.config,
        &launch->config_file, error, error_size);
    if (result == 0 && documents.has_direct_cidrs) {
        result = starsead_anonymous_file_create(
            backend, "starsead.direct.ipv4", &documents.direct_ipv4,
            &launch->direct_ipv4_file, error, error_size);
        if (result == 0) result = starsead_anonymous_file_create(
            backend, "starsead.direct.ipv6", &documents.direct_ipv6,
            &launch->direct_ipv6_file, error, error_size);
        if (result == 0) launch->has_direct_cidrs = true;
    }
    if (result == 0) result = starsead_helper_process_spec(
        config, inherited_environment, launch->config_file.fd,
        launch->has_direct_cidrs ? launch->direct_ipv4_file.fd : -1,
        launch->has_direct_cidrs ? launch->direct_ipv6_file.fd : -1,
        &launch->process, error, error_size);
    starsead_helper_documents_destroy(&documents);
    if (result != 0) (void)starsead_helper_launch_destroy(backend, launch);
    return result;
}
