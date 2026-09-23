// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void core_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size != 0U) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static size_t core_bounded_length(const char *value, size_t capacity) {
    if (value == NULL) return capacity;
    size_t length = 0U;
    while (length < capacity && value[length] != '\0') ++length;
    return length;
}

static int core_add_xray_library(
    const struct starsead_config *config,
    struct starsead_process_spec *spec) {
    const char *slash = strrchr(config->core_executable_path, '/');
    if (slash == NULL || slash == config->core_executable_path) return -1;
    static const char name[] = "libgojni.so";
    size_t directory_length = (size_t)(slash - config->core_executable_path);
    char library[STARSEAD_MAX_PATH];
    if (directory_length + 1U + sizeof(name) > sizeof(library)) return -1;
    memcpy(library, config->core_executable_path, directory_length);
    library[directory_length] = '/';
    memcpy(library + directory_length + 1U, name, sizeof(name));
    return starsead_process_environment_add(spec, "ANDROID_XRAY_LIBRARY", library);
}

static int core_build_environment(
    const struct starsead_config *config,
    const char *const *inherited,
    const char *asset_key,
    struct starsead_process_spec *spec) {
    if (starsead_process_environment_rebuild(inherited, spec) != 0 ||
        starsead_process_environment_add(spec, asset_key, config->working_directory) != 0) return -1;
    if (config->core_type == STARSEAD_CORE_XRAY &&
        core_add_xray_library(config, spec) != 0) return -1;
    if (config->core_type == STARSEAD_CORE_MIHOMO && config->has_age_secret_key &&
        starsead_process_environment_add(spec, "CLASH_AGE_SECRET_KEY", config->age_secret_key) != 0) return -1;
    return 0;
}

int starsead_core_process_spec(
    const struct starsead_config *config,
    const char *const *inherited_environment,
    struct starsead_process_spec *spec,
    char *error,
    size_t error_size) {
    if (spec != NULL) memset(spec, 0, sizeof(*spec));
    if (error != NULL && error_size != 0U) error[0] = '\0';
    if (config == NULL || spec == NULL) {
        core_error(error, error_size, "invalid core process input");
        return STARSEAD_CONFIG_INVALID;
    }
    size_t executable_length = core_bounded_length(
        config->core_executable_path, sizeof(spec->executable_path));
    size_t directory_length = core_bounded_length(
        config->working_directory, sizeof(spec->working_directory));
    if (executable_length == 0U || executable_length >= sizeof(spec->executable_path) ||
        directory_length == 0U || directory_length >= sizeof(spec->working_directory)) {
        core_error(error, error_size, "invalid core process path");
        return STARSEAD_CONFIG_INVALID;
    }
    memcpy(spec->executable_path, config->core_executable_path, executable_length + 1U);
    memcpy(spec->working_directory, config->working_directory, directory_length + 1U);
    if (starsead_log_sibling_path(config->log_path, "error.log",
            spec->output_path, sizeof(spec->output_path)) != 0) {
        core_error(error, error_size, "invalid core log path");
        return STARSEAD_CONFIG_INVALID;
    }
    spec->uid = 0U;
    spec->gid = 3005U;
    spec->output_mode = STARSEAD_PROCESS_OUTPUT_APPEND_CORE_LOG;
    const char *asset_key = NULL;
    if (config->core_type == STARSEAD_CORE_XRAY) {
        asset_key = "XRAY_LOCATION_ASSET";
        if (starsead_process_argument_add(spec, spec->executable_path) != 0 ||
            starsead_process_argument_add(spec, "run") != 0 ||
            starsead_process_argument_add(spec, "-config") != 0 ||
            starsead_process_argument_add(spec, config->core_config_path) != 0) goto invalid;
    } else if (config->core_type == STARSEAD_CORE_SING_BOX) {
        asset_key = "SING_BOX_LOCATION_ASSET";
        if (starsead_process_argument_add(spec, spec->executable_path) != 0 ||
            starsead_process_argument_add(spec, "run") != 0 ||
            starsead_process_argument_add(spec, "--disable-color") != 0 ||
            starsead_process_argument_add(spec, "-D") != 0 ||
            starsead_process_argument_add(spec, spec->working_directory) != 0 ||
            starsead_process_argument_add(spec, "-c") != 0 ||
            starsead_process_argument_add(spec, config->core_config_path) != 0) goto invalid;
    } else if (config->core_type == STARSEAD_CORE_MIHOMO) {
        asset_key = "MIHOMO_LOCATION_ASSET";
        if (starsead_process_argument_add(spec, spec->executable_path) != 0 ||
            starsead_process_argument_add(spec, "-d") != 0 ||
            starsead_process_argument_add(spec, spec->working_directory) != 0 ||
            starsead_process_argument_add(spec, "-f") != 0 ||
            starsead_process_argument_add(spec, config->core_config_path) != 0) goto invalid;
    } else {
        goto invalid;
    }
    if (core_build_environment(
            config, inherited_environment, asset_key, spec) != 0) {
        core_error(error, error_size, "core environment construction failed");
        starsead_process_spec_destroy(spec);
        return STARSEAD_CONFIG_NO_MEMORY;
    }
    return 0;

invalid:
    core_error(error, error_size, "invalid core process configuration");
    starsead_process_spec_destroy(spec);
    return STARSEAD_CONFIG_INVALID;
}
