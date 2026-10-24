// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int render_uid_array(
    struct starsead_text_writer *writer,
    const uint32_t *values,
    size_t count,
    bool add_whitelist_system_uids) {
    if (count > SIZE_MAX / sizeof(uint32_t) - 2U) return STARSEAD_CONFIG_INVALID;
    size_t capacity = count + (add_whitelist_system_uids ? 2U : 0U);
    uint32_t *sorted = capacity == 0U ? NULL : malloc(capacity * sizeof(*sorted));
    if (capacity != 0U && sorted == NULL) return STARSEAD_CONFIG_NO_MEMORY;
    if (count != 0U) memcpy(sorted, values, count * sizeof(*sorted));
    size_t length = count;
    if (add_whitelist_system_uids) {
        sorted[length++] = 0U;
        sorted[length++] = 1052U;
    }
    qsort(sorted, length, sizeof(*sorted), starsead_compare_u32);
    starsead_text_writer_literal(writer, "[");
    bool first = true;
    for (size_t index = 0U; index < length; ++index) {
        if (index != 0U && sorted[index] == sorted[index - 1U]) continue;
        if (!first) starsead_text_writer_literal(writer, ",");
        starsead_text_writer_format(writer, "%u", sorted[index]);
        first = false;
    }
    starsead_text_writer_literal(writer, "]");
    free(sorted);
    return writer->result;
}

static int render_direct_family(
    const char values[][STARSEAD_MAX_CIDR],
    size_t count,
    struct starsead_anonymous_document *document) {
    struct starsead_text_writer writer = {0};
    for (size_t index = 0U; index < count; ++index) {
        starsead_text_writer_literal(&writer, values[index]);
        starsead_text_writer_literal(&writer, "\n");
    }
    return starsead_text_writer_take(&writer, document);
}

void starsead_matcher_documents_destroy(struct starsead_matcher_documents *documents) {
    if (documents == NULL) return;
    free(documents->direct_ipv6.bytes);
    free(documents->direct_ipv4.bytes);
    free(documents->policy.bytes);
    memset(documents, 0, sizeof(*documents));
}

int starsead_matcher_render_documents(
    const struct starsead_config *config,
    int direct_ipv4_fd,
    int direct_ipv6_fd,
    struct starsead_matcher_documents *documents,
    char *error,
    size_t error_size) {
    if (documents != NULL) memset(documents, 0, sizeof(*documents));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || documents == NULL || !config->matcher.enabled ||
        (config->mode != STARSEAD_MODE_TPROXY &&
         config->mode != STARSEAD_MODE_TUN2SOCKS)) {
        starsead_error_copy(error, error_size, "invalid matcher document configuration");
        return STARSEAD_CONFIG_INVALID;
    }
    const char *root = starsead_owned_resource_catalog()->bpf_root;
    if (config->direct_cidrs != NULL &&
        (direct_ipv4_fd < 0 || direct_ipv6_fd < 0)) {
        starsead_error_copy(error, error_size, "invalid matcher descriptor configuration");
        return STARSEAD_CONFIG_INVALID;
    }
    int mode = config->app_policy_mode == STARSEAD_APP_POLICY_BLACKLIST ? 0 :
        config->app_policy_mode == STARSEAD_APP_POLICY_WHITELIST ? 1 : 2;
    struct starsead_text_writer writer = {0};
    starsead_text_writer_format(&writer, "{\"version\":1,\"mode\":%d,\"uids\":", mode);
    int result = render_uid_array(&writer, config->uids, config->uid_count,
        config->app_policy_mode == STARSEAD_APP_POLICY_WHITELIST);
    if (result == 0) {
        starsead_text_writer_literal(&writer, ",\"bypassUids\":");
        result = render_uid_array(&writer, config->bypass_uids, config->bypass_uid_count, false);
    }
    if (result == 0) {
        starsead_text_writer_format(&writer,
            ",\"bypassDirectCidrs\":%s,\"enableIpv6\":%s,"
            "\"directCidrPathV4\":\"%s\",\"directCidrPathV6\":\"%s\","
            "\"xtOutputV4ProgramPath\":\"%s/xt_output_v4\","
            "\"xtOutputV6ProgramPath\":\"%s/xt_output_v6\","
            "\"xtPreroutingV4ProgramPath\":\"%s/xt_prerouting_v4\","
            "\"xtPreroutingV6ProgramPath\":\"%s/xt_prerouting_v6\",\"xtDnsUidProgramPath\":\"%s\"}\n",
            config->direct_cidrs != NULL ? "true" : "false",
            config->enable_ipv6 ? "true" : "false",
            config->direct_cidrs != NULL ? "/proc/self/fd/4" : "",
            config->direct_cidrs != NULL ? "/proc/self/fd/5" : "",
            root, root, root, root, config->dns_enhancement
                ? starsead_owned_pin_path(STARSEAD_PIN_MATCHER_DNS_UID) : "");
        result = starsead_text_writer_take(&writer, &documents->policy);
    }
    if (result == 0 && config->direct_cidrs != NULL) {
        result = render_direct_family((const char (*)[STARSEAD_MAX_CIDR])config->direct_cidrs->ipv4,
            config->direct_cidrs->ipv4_count, &documents->direct_ipv4);
        if (result == 0) result = render_direct_family(
            (const char (*)[STARSEAD_MAX_CIDR])config->direct_cidrs->ipv6,
            config->direct_cidrs->ipv6_count, &documents->direct_ipv6);
        if (result == 0) documents->has_direct_cidrs = true;
    }
    if (result != 0) {
        free(writer.bytes);
        starsead_matcher_documents_destroy(documents);
        starsead_error_copy(error, error_size, result == STARSEAD_CONFIG_NO_MEMORY ?
            "matcher document allocation failed" : "invalid matcher document configuration");
    }
    return result;
}

int starsead_matcher_process_spec(
    const struct starsead_config *config,
    const char *const *inherited_environment,
    int policy_fd,
    int direct_ipv4_fd,
    int direct_ipv6_fd,
    struct starsead_process_spec *spec,
    char *error,
    size_t error_size) {
    if (spec != NULL) memset(spec, 0, sizeof(*spec));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || spec == NULL || !config->matcher.enabled || policy_fd < 0 ||
        starsead_copy_string(spec->working_directory, sizeof(spec->working_directory), config->working_directory) != 0 ||
        starsead_copy_string(spec->executable_path, sizeof(spec->executable_path),
            config->matcher.executable_path) != 0 ||
        starsead_process_environment_rebuild(inherited_environment, spec) != 0 ||
        starsead_process_argument_add(spec, spec->executable_path) != 0 ||
        starsead_process_argument_add(spec, "--start") != 0 ||
        starsead_process_argument_add(spec, "--policy") != 0 ||
        starsead_process_argument_add(spec, "/proc/self/fd/3") != 0 ||
        starsead_process_inherit_fd(spec, policy_fd, 3) != 0) goto invalid;
    spec->uid = 0U;
    spec->gid = 0U;
    spec->output_mode = STARSEAD_PROCESS_OUTPUT_DISCARD;
    spec->unlimited_locked_memory = true;
    if (config->direct_cidrs != NULL &&
        (starsead_process_inherit_fd(spec, direct_ipv4_fd, 4) != 0 ||
         starsead_process_inherit_fd(spec, direct_ipv6_fd, 5) != 0)) goto invalid;
    return 0;

invalid:
    starsead_error_copy(error, error_size, "invalid matcher process configuration");
    starsead_process_spec_destroy(spec);
    return STARSEAD_CONFIG_INVALID;
}

int starsead_matcher_launch_destroy(
    const struct starsead_anonymous_file_backend *backend,
    struct starsead_matcher_launch *launch) {
    if (launch == NULL) return STARSEAD_CONFIG_INVALID;
    int result = 0;
    if (starsead_anonymous_file_close(backend, &launch->direct_ipv6_file) != 0) result = STARSEAD_CONFIG_IO;
    if (starsead_anonymous_file_close(backend, &launch->direct_ipv4_file) != 0) result = STARSEAD_CONFIG_IO;
    if (starsead_anonymous_file_close(backend, &launch->policy_file) != 0) result = STARSEAD_CONFIG_IO;
    starsead_process_spec_destroy(&launch->process);
    memset(launch, 0, sizeof(*launch));
    return result;
}

int starsead_matcher_launch_prepare(
    const struct starsead_config *config,
    const char *const *inherited_environment,
    const struct starsead_anonymous_file_backend *backend,
    struct starsead_matcher_launch *launch,
    char *error,
    size_t error_size) {
    if (launch != NULL) memset(launch, 0, sizeof(*launch));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || backend == NULL || launch == NULL) {
        starsead_error_copy(error, error_size, "invalid matcher launch input");
        return STARSEAD_CONFIG_INVALID;
    }
    struct starsead_matcher_documents documents = {0};
    int result = starsead_matcher_render_documents(config, 4, 5, &documents, error, error_size);
    if (result != 0) return result;
    result = starsead_anonymous_file_create(
        backend, "starsead.matcher.policy", &documents.policy,
        &launch->policy_file, error, error_size);
    if (result == 0 && documents.has_direct_cidrs) {
        result = starsead_anonymous_file_create(
            backend, "starsead.direct.ipv4", &documents.direct_ipv4,
            &launch->direct_ipv4_file, error, error_size);
        if (result == 0) result = starsead_anonymous_file_create(
            backend, "starsead.direct.ipv6", &documents.direct_ipv6,
            &launch->direct_ipv6_file, error, error_size);
        if (result == 0) launch->has_direct_cidrs = true;
    }
    if (result == 0) result = starsead_matcher_process_spec(
        config, inherited_environment, launch->policy_file.fd,
        launch->has_direct_cidrs ? launch->direct_ipv4_file.fd : -1,
        launch->has_direct_cidrs ? launch->direct_ipv6_file.fd : -1,
        &launch->process, error, error_size);
    starsead_matcher_documents_destroy(&documents);
    if (result != 0) (void)starsead_matcher_launch_destroy(backend, launch);
    return result;
}

int starsead_matcher_pin_plan_build(
    const struct starsead_config *config,
    struct starsead_matcher_pin_plan *plan) {
    if (plan != NULL) memset(plan, 0, sizeof(*plan));
    if (config == NULL || plan == NULL || !config->matcher.enabled) return STARSEAD_CONFIG_INVALID;
    static const enum starsead_pin_id ids[] = {
        STARSEAD_PIN_MATCHER_OUTPUT_V4, STARSEAD_PIN_MATCHER_OUTPUT_V6,
        STARSEAD_PIN_MATCHER_PREROUTING_V4, STARSEAD_PIN_MATCHER_PREROUTING_V6,
        STARSEAD_PIN_MATCHER_DNS_UID,
    };
    static const char *const names[] = {
        "ast_xt_out4", "ast_xt_out6", "ast_xt_pre4", "ast_xt_pre6", "ast_xt_dns_uid",
    };
    static const size_t ipv4_indices[] = {0U, 2U, 4U};
    size_t count = (config->enable_ipv6 ? 4U : 2U) + (config->dns_enhancement ? 1U : 0U);
    for (size_t index = 0U; index < count; ++index) {
        size_t source = config->enable_ipv6 ? index : ipv4_indices[index];
        struct starsead_matcher_pin_expectation *pin = &plan->pins[index];
        pin->pin_id = ids[source];
        if (starsead_copy_string(pin->path, sizeof(pin->path),
                starsead_owned_pin_path(ids[source])) != 0 ||
            starsead_copy_string(pin->program_name, sizeof(pin->program_name), names[source]) != 0) {
            memset(plan, 0, sizeof(*plan));
            return STARSEAD_CONFIG_INVALID;
        }
    }
    plan->pin_count = count;
    return 0;
}

int starsead_matcher_pin_records_build(
    const struct starsead_matcher_pin_plan *plan, struct starsead_resource_operation *records,
    size_t capacity, size_t *count) {
    if (count != NULL) *count = 0U;
    if (plan == NULL || records == NULL || count == NULL ||
        (plan->pin_count < 2U || plan->pin_count > 5U) || capacity < plan->pin_count) {
        return STARSEAD_CONFIG_INVALID;
    }
    memset(records, 0, capacity * sizeof(*records));
    for (size_t index = 0U; index < plan->pin_count; ++index) {
        records[index].kind = STARSEAD_RESOURCE_OPERATION_BPF_PIN;
        records[index].resource.bpf_pin.pin_id = plan->pins[index].pin_id;
        records[index].resource.bpf_pin.original_presence = false;
    }
    *count = plan->pin_count;
    return 0;
}
