// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#include "starsead_supervisor.h"

#if defined(__linux__) || defined(__ANDROID__)

static bool system_startup_components_verified(
    bool core_identity_ready, bool core_reaped,
    bool helper_required, bool helper_identity_ready, bool helper_reaped,
    bool matcher_required, bool matcher_verified,
    bool rules_initialized, bool rules_verified) {
    return core_identity_ready && !core_reaped &&
        (!helper_required || (helper_identity_ready && !helper_reaped)) &&
        (!matcher_required || matcher_verified) && rules_initialized && rules_verified;
}

static bool system_action_source_active_state(
    bool spawned, bool reaped, enum starsead_poll_source_kind kind) {
    (void)reaped;
    return spawned && kind >= STARSEAD_POLL_PROCESS_EXEC_ERROR &&
        kind <= STARSEAD_POLL_PROCESS_PIDFD;
}

static bool system_event_is_final(enum starsead_control_event_type type) {
    return type == STARSEAD_CONTROL_EVENT_STOPPED ||
        type == STARSEAD_CONTROL_EVENT_FAILED;
}

static bool system_cycle_failure_requires_shutdown(
    bool service_control_enabled, bool cleanup_complete) {
    return !service_control_enabled || !cleanup_complete;
}

static bool system_admission_reconcile_ready(
    bool listener_acquired, bool action_reactor_ready) {
    return listener_acquired && action_reactor_ready;
}

int starsead_supervisor_runtime_clock(void *opaque, int64_t *milliseconds) {
    (void)opaque;
    struct timespec now;
    if (milliseconds == NULL || clock_gettime(CLOCK_MONOTONIC, &now) != 0 ||
        now.tv_sec < 0 || now.tv_nsec < 0) return -1;
    if ((uint64_t)now.tv_sec > (uint64_t)INT64_MAX / UINT64_C(1000)) return -1;
    *milliseconds = (int64_t)now.tv_sec * INT64_C(1000) + now.tv_nsec / 1000000L;
    return 0;
}

static int system_runtime_local_time(void *opaque, struct starsead_local_time *output) {
    (void)opaque;
    if (output == NULL) return -1;
    struct timespec now;
    struct tm local;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
        localtime_r(&now.tv_sec, &local) == NULL) return -1;
    char offset[8U];
    if (strftime(offset, sizeof(offset), "%z", &local) != 5U ||
        (offset[0] != '+' && offset[0] != '-')) return -1;
    int hours = (offset[1] - '0') * 10 + offset[2] - '0';
    int minutes = (offset[3] - '0') * 10 + offset[4] - '0';
    if (hours > 23 || minutes > 59) return -1;
    int sign = offset[0] == '-' ? -1 : 1;
    *output = (struct starsead_local_time){
        .year = local.tm_year + 1900,
        .month = local.tm_mon + 1,
        .day = local.tm_mday,
        .hour = local.tm_hour,
        .minute = local.tm_min,
        .second = local.tm_sec,
        .millisecond = (int)(now.tv_nsec / 1000000L),
        .utc_offset_minutes = sign * (hours * 60 + minutes),
    };
    return 0;
}

static int system_runtime_add_source(struct starsead_poll_builder *builder, int fd,
    short events, enum starsead_poll_source_kind kind, uint32_t slot, uint64_t generation) {
    if (fd < 0) return 0;
    struct starsead_poll_source source = {
        .fd = fd,
        .events = events,
        .kind = kind,
        .slot = slot,
        .generation = generation,
    };
    return starsead_poll_builder_add(builder, &source);
}

static void system_runtime_min_deadline(
    struct starsead_deadline *deadline, uint64_t candidate) {
    if (candidate > (uint64_t)INT64_MAX) return;
    struct starsead_deadline next = {
        .armed = true,
        .monotonic_milliseconds = (int64_t)candidate,
    };
    starsead_deadline_min(deadline, &next);
}

static void system_service_apply_action(
    struct starsead_system_supervisor *system, enum starsead_service_action action) {
    if ((system->shutdown_requested || system->manual_stop_pending) &&
        (action == STARSEAD_SERVICE_ACTION_START || action == STARSEAD_SERVICE_ACTION_STOP)) return;
    if (system->runtime != NULL && system->runtime->supervising &&
        !system->runtime->lifecycle.stopped &&
        !atomic_load_explicit(&system->runtime->lifecycle.stop_was_requested, memory_order_acquire) &&
        !starsead_mode_core_managed(system->loaded_config.config.mode) &&
        (action == STARSEAD_SERVICE_ACTION_START || action == STARSEAD_SERVICE_ACTION_STOP)) {
        system->service_pause_requested = action == STARSEAD_SERVICE_ACTION_STOP;
        system->service_resume_requested = action == STARSEAD_SERVICE_ACTION_START;
        return;
    }
    if (action == STARSEAD_SERVICE_ACTION_START) {
        bool stopping = system->runtime && atomic_load_explicit(
            &system->runtime->lifecycle.stop_was_requested, memory_order_acquire);
        if (!system->shutdown_requested && (!system->service_running ||
                (stopping && system->automatic_stop_pending))) {
            system->service_start_requested = true;
        } else if (!system->shutdown_requested && !stopping) {
            system->stop_requested = false;
            system->automatic_stop_pending = false;
        }
    } else if (action == STARSEAD_SERVICE_ACTION_STOP) {
        system->service_start_requested = false;
        if (system->service_running) {
            system->stop_requested = true;
            system->automatic_stop_pending = true;
        }
    } else if (action == STARSEAD_SERVICE_ACTION_SHUTDOWN) {
        system->shutdown_requested = true;
        if (system->service_running) system->stop_requested = true;
    }
}

static void system_service_keyguard_changed(void *context, bool locked, bool baseline) {
    struct starsead_system_supervisor *system = context;
    system_service_apply_action(system,
        starsead_service_control_on_keyguard(&system->service_control, locked, baseline));
}

static void system_service_take_actions(struct starsead_system_supervisor *system,
    struct starsead_runtime_delta *delta) {
    if (system->service_pause_requested || system->service_resume_requested) {
        delta->flags &= ~(STARSEAD_DELTA_SERVICE_PAUSE | STARSEAD_DELTA_SERVICE_RESUME);
    }
    if (system->service_pause_requested) delta->flags |= STARSEAD_DELTA_SERVICE_PAUSE;
    if (system->service_resume_requested) delta->flags |= STARSEAD_DELTA_SERVICE_RESUME;
    system->service_pause_requested = false;
    system->service_resume_requested = false;
}

static int system_service_timer_arm(struct starsead_system_supervisor *system) {
    if (!system->service_timer_fd_owned || system->service_timer_fd < 0) return 0;
    struct itimerspec timer;
    memset(&timer, 0, sizeof(timer));
    const struct starsead_service_control_config *control =
        &system->loaded_config.config.service_control;
    if (control->enabled && control->schedule.enabled) {
        time_t now = time(NULL);
        time_t next_start = 0;
        time_t next_stop = 0;
        bool has_start = now != (time_t)-1 &&
            starsead_cron_next(&control->schedule.start, now, &next_start) == 0;
        bool has_stop = now != (time_t)-1 &&
            starsead_cron_next(&control->schedule.stop, now, &next_stop) == 0;
        time_t next = has_start && has_stop
            ? (next_start < next_stop ? next_start : next_stop)
            : has_start ? next_start : has_stop ? next_stop : 0;
        if (next > 0) timer.it_value.tv_sec = next;
    }
    return timerfd_settime(system->service_timer_fd,
        TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &timer, NULL);
}

static int system_service_reconcile_time(struct starsead_system_supervisor *system) {
    time_t now = time(NULL);
    if (now == (time_t)-1) return -1;
    system_service_apply_action(system,
        starsead_service_control_reconcile_time(&system->service_control, now));
    return system_service_timer_arm(system);
}

static int system_service_timer_dispatch(struct starsead_system_supervisor *system) {
    uint64_t expirations = 0U;
    ssize_t count;
    do {
        count = read(system->service_timer_fd, &expirations, sizeof(expirations));
    } while (count < 0 && errno == EINTR);
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ECANCELED) return -1;
    return system_service_reconcile_time(system);
}

static int system_service_wifi_dispatch(struct starsead_system_supervisor *system) {
    enum starsead_wifi_transition transition = STARSEAD_WIFI_TRANSITION_BASELINE_DISCONNECTED;
    struct starsead_wifi_identity identity;
    bool has_transition = false;
    char error[128U];
    if (starsead_wifi_monitor_handle(&system->wifi_monitor, &transition,
            &identity, &has_transition, error, sizeof(error)) != 0) return -1;
    if (has_transition) {
        system_service_apply_action(system,
            starsead_service_control_on_wifi(
                &system->service_control, transition, &identity));
    }
    return 0;
}

static int system_service_wifi_reconcile(
    struct starsead_system_supervisor *system, uint64_t now) {
    enum starsead_wifi_transition transition = STARSEAD_WIFI_TRANSITION_BASELINE_DISCONNECTED;
    struct starsead_wifi_identity identity;
    bool has_transition = false;
    char error[128U];
    if (starsead_wifi_monitor_take_reconcile(&system->wifi_monitor, now,
            &transition, &identity, &has_transition, error, sizeof(error)) != 0) return -1;
    if (has_transition) {
        system_service_apply_action(system,
            starsead_service_control_on_wifi(
                &system->service_control, transition, &identity));
    }
    return 0;
}

static int system_runtime_prepare(
    void *opaque, struct starsead_poll_builder *builder, struct starsead_deadline *deadline) {
    struct starsead_system_supervisor *system = opaque;
    if (system->dns.snapshot.required) {
        int64_t dns_now = 0;
        if (starsead_supervisor_runtime_clock(system, &dns_now) != 0) return -1;
        uint64_t dns_time = starsead_dns_monotime();
        uint64_t next = 0;
        if (system->dns.pending) next = system->dns.deadline;
        else if (system->dns.fd >= 0 && starsead_dns_serving(system->dns.snapshot.phase) &&
            system->dns.network_changed && !system->dns.busy && !system->dns.rx_size) next = dns_time;
        if (system->dns_hot_stage) {
            if (system->dns.pending) {
                if (!next || next > system->dns_hot_deadline) next = system->dns_hot_deadline;
            } else if (starsead_supervisor_dns_waiting_for_rules(system)) {
                // Reconcile pumps this reactor while waiting for subprocesses.
                // Its completion makes the next prepare runnable; do not spin
                // inside the nested pump or extend the operation's budget.
                next = system->dns_hot_deadline;
            } else if (system->dns_hot_stage == SYSTEM_DNS_HOT_WAIT_NETWORK ||
                (system->dns_hot_stage == SYSTEM_DNS_HOT_RELEASE && system->dns_drain_waiting &&
                 system->dns.drain_events)) {
                // No periodic wakeups: only a module event (network change or
                // release progress) or this operation's absolute timeout can
                // advance waiting. A disarmed timeout (UINT64_MAX) adds none.
                next = system->dns.network_changed ? dns_time :
                    system->dns_hot_deadline == UINT64_MAX ? 0U : system->dns_hot_deadline;
            } else if ((system->dns_hot_stage == SYSTEM_DNS_HOT_RELEASE || system->dns_hot_stage == SYSTEM_DNS_HOT_RELEASED) &&
                (system->dns.retained || system->dns.snapshot.phase == STARSEAD_DNS_PHASE_DRAINING)) {
                next = dns_time + 20U; // Legacy module without release events.
            } else next = dns_time; // Local state transition, not a periodic timer.
        }
        // Bounded to the stopping cycle: a resident supervisor keeps both flags
        // until the next start, and must not wake every 20 ms while stopped.
        if (system->cleanup_in_progress && system->service_running) next = dns_time + 20U;
        if (next) system_runtime_min_deadline(deadline, (uint64_t)dns_now + (next > dns_time ? next - dns_time : 0U));
        if (system->dns.fd >= 0 && system_runtime_add_source(builder, system->dns.fd,
            POLLIN | (system->dns.pending && system->dns.tx_offset < system->dns.tx_size ? POLLOUT : 0),
            STARSEAD_POLL_DNS, 0U, system->dns.snapshot.generation) != 0) return -1;
    }
    if (system_runtime_add_source(builder, system->signal_fd, POLLIN,
            STARSEAD_POLL_SIGNAL, 0U, 1U) != 0) return -1;
    if (system->service_timer_fd_owned &&
        system_runtime_add_source(builder, system->service_timer_fd, POLLIN,
            STARSEAD_POLL_SERVICE_TIMER, 0U, 1U) != 0) return -1;
    if (system->wifi_monitor_opened &&
        system_runtime_add_source(builder, starsead_wifi_monitor_fd(&system->wifi_monitor), POLLIN,
            STARSEAD_POLL_WIFI, 0U, 1U) != 0) return -1;
    if (system->keyguard_monitor &&
        system_runtime_add_source(builder, starsead_keyguard_fd(system->keyguard_monitor), POLLIN,
            STARSEAD_POLL_KEYGUARD, 0U, 1U) != 0) return -1;
    uint64_t wifi_deadline = 0U;
    if (system->wifi_monitor_opened &&
        starsead_wifi_monitor_next_deadline(&system->wifi_monitor, &wifi_deadline)) {
        system_runtime_min_deadline(deadline, wifi_deadline);
    }
    if (system->core_spawned && !system->core_reaped) {
        uint64_t generation = (uint64_t)system->core_process.pid;
        if (system->core_process.owns_setup_status_fd &&
            system_runtime_add_source(builder, system->core_process.setup_status_fd, POLLIN,
                STARSEAD_POLL_PROCESS_EXEC_ERROR, 0U, generation) != 0) return -1;
        if (system->core_process.owns_stdout_fd &&
            system_runtime_add_source(builder, system->core_process.stdout_fd, POLLIN,
                STARSEAD_POLL_PROCESS_STDOUT, 0U, generation) != 0) return -1;
        if (system->core_process.owns_stderr_fd &&
            system_runtime_add_source(builder, system->core_process.stderr_fd, POLLIN,
                STARSEAD_POLL_PROCESS_STDERR, 0U, generation) != 0) return -1;
        if (system->core_process.owns_pidfd &&
            system_runtime_add_source(builder, system->core_process.pidfd, POLLIN,
                STARSEAD_POLL_PROCESS_PIDFD, 0U, generation) != 0) return -1;
    }
    if (system->helper_spawned && !system->helper_reaped) {
        uint64_t generation = (uint64_t)system->helper_process.pid;
        if (system->helper_process.owns_setup_status_fd &&
            system_runtime_add_source(builder, system->helper_process.setup_status_fd, POLLIN,
                STARSEAD_POLL_PROCESS_EXEC_ERROR, 1U, generation) != 0) return -1;
        if (system->helper_process.owns_stdout_fd &&
            system_runtime_add_source(builder, system->helper_process.stdout_fd, POLLIN,
                STARSEAD_POLL_PROCESS_STDOUT, 1U, generation) != 0) return -1;
        if (system->helper_process.owns_stderr_fd &&
            system_runtime_add_source(builder, system->helper_process.stderr_fd, POLLIN,
                STARSEAD_POLL_PROCESS_STDERR, 1U, generation) != 0) return -1;
        if (system->helper_process.owns_pidfd &&
            system_runtime_add_source(builder, system->helper_process.pidfd, POLLIN,
                STARSEAD_POLL_PROCESS_PIDFD, 1U, generation) != 0) return -1;
    }
    if (system->action_spawned) {
        uint64_t generation = (uint64_t)system->action_process.pid;
        if (system->action_process.owns_setup_status_fd &&
            system_runtime_add_source(builder, system->action_process.setup_status_fd, POLLIN,
                STARSEAD_POLL_PROCESS_EXEC_ERROR, 2U, generation) != 0) return -1;
        if (system->action_process.owns_stdout_fd &&
            system_runtime_add_source(builder, system->action_process.stdout_fd, POLLIN,
                STARSEAD_POLL_PROCESS_STDOUT, 2U, generation) != 0) return -1;
        if (system->action_process.owns_stderr_fd &&
            system_runtime_add_source(builder, system->action_process.stderr_fd, POLLIN,
                STARSEAD_POLL_PROCESS_STDERR, 2U, generation) != 0) return -1;
        if (!system->action_reaped && system->action_process.owns_pidfd &&
            system_runtime_add_source(builder, system->action_process.pidfd, POLLIN,
                STARSEAD_POLL_PROCESS_PIDFD, 2U, generation) != 0) return -1;
    }
    if (system->control != NULL) {
        struct starsead_control_interest interests[STARSEAD_CONTROL_MAX_CLIENTS + 1U];
        size_t count = starsead_control_server_interests(
            system->control, interests, sizeof(interests) / sizeof(interests[0]));
        if (count > sizeof(interests) / sizeof(interests[0])) return -1;
        int listener = starsead_control_server_listener_fd(system->control);
        for (size_t index = 0U; index < count; ++index) {
            short events = (short)((interests[index].readable ? POLLIN : 0) |
                (interests[index].writable ? POLLOUT : 0));
            enum starsead_poll_source_kind kind = interests[index].fd == listener
                ? STARSEAD_POLL_CONTROL_LISTENER : STARSEAD_POLL_CONTROL_CLIENT;
            if (system_runtime_add_source(builder, interests[index].fd, events, kind,
                    (uint32_t)interests[index].fd,
                    (uint64_t)(unsigned)interests[index].fd + UINT64_C(1)) != 0) return -1;
        }
        uint64_t control_deadline = 0U;
        if (starsead_control_server_next_deadline(system->control, &control_deadline)) {
            system_runtime_min_deadline(deadline, control_deadline);
        }
    }
    if (system->network_opened && system->network.fd_owned &&
        system_runtime_add_source(builder, system->network.fd, POLLIN,
            STARSEAD_POLL_NETWORK, 0U, 1U) != 0) return -1;
    if (system->tc_netlink_active && system->tc_netlink_fd_owned &&
        system_runtime_add_source(builder, system->tc_netlink_fd, POLLIN,
            STARSEAD_POLL_TC_NETLINK, 0U,
            system->tc_netlink_sequence) != 0) return -1;
    uint64_t network_deadline = 0U;
    if (system->network_opened &&
        starsead_network_next_deadline(&system->network, &network_deadline)) {
        system_runtime_min_deadline(deadline, network_deadline);
    }
    return 0;
}

static int system_runtime_wait(void *opaque, const struct starsead_poll_builder *builder,
    int64_t timeout_milliseconds, short *ready) {
    (void)opaque;
    struct pollfd descriptors[STARSEAD_MAX_POLL_SOURCES];
    for (size_t index = 0U; index < builder->count; ++index) {
        descriptors[index] = (struct pollfd){
            .fd = builder->sources[index].fd,
            .events = builder->sources[index].events,
        };
    }
    int timeout = timeout_milliseconds < 0 ? -1 :
        timeout_milliseconds > INT_MAX ? INT_MAX : (int)timeout_milliseconds;
    int result = poll(descriptors, (nfds_t)builder->count, timeout);
    if (result < 0) return errno == EINTR
        ? STARSEAD_REACTOR_WAIT_INTERRUPTED : STARSEAD_REACTOR_WAIT_ERROR;
    for (size_t index = 0U; index < builder->count; ++index) ready[index] = descriptors[index].revents;
    return result;
}

void starsead_supervisor_runtime_fatal(struct starsead_runtime_delta *delta,
    enum starsead_component component, const char *message) {
    if ((delta->flags & STARSEAD_DELTA_FATAL) != 0U) return;
    delta->flags |= STARSEAD_DELTA_FATAL;
    delta->has_error = true;
    delta->error_code = STARSEAD_FAILURE_IO_ERROR;
    delta->error_component = component;
    (void)snprintf(delta->error_message, sizeof(delta->error_message), "%s", message);
}

static void system_runtime_note_exit(struct starsead_system_supervisor *system,
    enum starsead_child_role role, int status, struct starsead_runtime_delta *delta) {
    bool *reaped = role == STARSEAD_CHILD_CORE ? &system->core_reaped : &system->helper_reaped;
    struct starsead_child_exit_status *exit_status = role == STARSEAD_CHILD_CORE
        ? &system->core_exit : &system->helper_exit;
    if (*reaped) return;
    *reaped = true;
    if (starsead_child_exit_status_from_wait(status, exit_status) != 0) {
        starsead_supervisor_runtime_fatal(delta,
            role == STARSEAD_CHILD_CORE ? STARSEAD_COMPONENT_CORE : STARSEAD_COMPONENT_HELPER,
            "invalid child wait status");
        return;
    }
    delta->flags |= STARSEAD_DELTA_CHILD_EXITED;
    delta->has_child_exit = true;
    delta->child_role = role;
    delta->child_exit = *exit_status;
}

void starsead_supervisor_runtime_note_action_exit(
    struct starsead_system_supervisor *system, int status) {
    if (system->action_reaped) return;
    system->action_reaped = true;
    if (starsead_child_exit_status_from_wait(status, &system->action_exit) != 0) {
        memset(&system->action_exit, 0, sizeof(system->action_exit));
    }
}

int starsead_supervisor_runtime_reap_children(
    struct starsead_system_supervisor *system, struct starsead_runtime_delta *delta) {
    for (;;) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid == 0) return 0;
        if (pid < 0) return errno == ECHILD ? 0 : -1;
        if (system->core_spawned && pid == (pid_t)system->core_process.pid) {
            system_runtime_note_exit(system, STARSEAD_CHILD_CORE, status, delta);
        } else if (system->helper_spawned && pid == (pid_t)system->helper_process.pid) {
            system_runtime_note_exit(system, STARSEAD_CHILD_HELPER, status, delta);
        } else if (system->action_spawned && pid == (pid_t)system->action_process.pid) {
            starsead_supervisor_runtime_note_action_exit(system, status);
        }
    }
}

static int system_runtime_dispatch_signal(
    struct starsead_system_supervisor *system, struct starsead_runtime_delta *delta) {
    for (;;) {
        struct signalfd_siginfo info;
        ssize_t count = read(system->signal_fd, &info, sizeof(info));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (count != (ssize_t)sizeof(info)) return -1;
        if (info.ssi_signo == SIGTERM || info.ssi_signo == SIGINT) {
            system->shutdown_requested = true;
            if (system->runtime->supervising &&
                (delta->flags & STARSEAD_DELTA_STOP_REQUESTED) == 0U) {
                delta->flags |= STARSEAD_DELTA_STOP_REQUESTED;
                delta->stop_reason = info.ssi_signo == SIGTERM
                    ? STARSEAD_LIFECYCLE_REASON_SIGTERM : STARSEAD_LIFECYCLE_REASON_SIGINT;
            }
        } else if (info.ssi_signo == SIGCHLD && !system->stopping_children &&
            starsead_supervisor_runtime_reap_children(system, delta) != 0) return -1;
    }
}

static void system_runtime_close_child_fd(int *fd, bool *owned) {
    if (*owned && *fd >= 0) (void)close(*fd);
    *fd = -1;
    *owned = false;
}

static int system_runtime_dispatch_setup(struct starsead_system_supervisor *system,
    uint32_t slot, struct starsead_runtime_delta *delta) {
    struct starsead_child_process *process = slot == 0U ? &system->core_process :
        slot == 1U ? &system->helper_process : &system->action_process;
    struct starsead_child_setup_stream *setup = slot == 0U ? &system->core_setup :
        slot == 1U ? &system->helper_setup : &system->action_setup;
    enum starsead_component component = slot == 0U
        ? STARSEAD_COMPONENT_CORE : slot == 1U
            ? STARSEAD_COMPONENT_HELPER : STARSEAD_COMPONENT_RUNTIME;
    unsigned char bytes[128U];
    for (;;) {
        ssize_t count = read(process->setup_status_fd, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        bool eof = count == 0;
        if (count < 0 || starsead_child_setup_stream_feed(setup,
                bytes, count > 0 ? (size_t)count : 0U, eof) < STARSEAD_CHILD_SETUP_PENDING) {
            if (slot != 2U) {
                starsead_supervisor_runtime_fatal(delta, component, "child exec setup failed");
            }
            return count < 0 ? -1 : 0;
        }
        if (eof) {
            system_runtime_close_child_fd(&process->setup_status_fd,
                &process->owns_setup_status_fd);
            return 0;
        }
    }
}

static int system_runtime_dispatch_log(struct starsead_system_supervisor *system,
    uint32_t slot, enum starsead_log_stream stream, struct starsead_runtime_delta *delta) {
    (void)delta;
    struct starsead_child_process *process = slot == 0U ? &system->core_process :
        slot == 1U ? &system->helper_process : &system->action_process;
    int *fd = stream == STARSEAD_LOG_STREAM_STDOUT
        ? &process->stdout_fd : &process->stderr_fd;
    bool *owned = stream == STARSEAD_LOG_STREAM_STDOUT
        ? &process->owns_stdout_fd : &process->owns_stderr_fd;
    unsigned char bytes[4096U];
    for (;;) {
        ssize_t count = read(*fd, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (count > 0 && slot == 2U && stream == STARSEAD_LOG_STREAM_STDOUT &&
            system->action_capture_stdout) {
            size_t available = sizeof(system->action_stdout) - system->action_stdout_length;
            size_t copied = (size_t)count < available ? (size_t)count : available;
            memcpy(&system->action_stdout[system->action_stdout_length], bytes, copied);
            system->action_stdout_length += copied;
            if (copied != (size_t)count) system->action_stdout_overflow = true;
        }
        if (count > 0 && slot == 2U && stream == STARSEAD_LOG_STREAM_STDERR) {
            size_t available = sizeof(system->action_stderr) - system->action_stderr_length;
            size_t copied = (size_t)count < available ? (size_t)count : available;
            memcpy(&system->action_stderr[system->action_stderr_length], bytes, copied);
            system->action_stderr_length += copied;
        }
        if (count < 0) return -1;
        if (count == 0) {
            system_runtime_close_child_fd(fd, owned);
            return 0;
        }
    }
}

static int system_runtime_dispatch(void *opaque, const struct starsead_poll_source *source,
    short ready, struct starsead_runtime_delta *delta) {
    struct starsead_system_supervisor *system = opaque;
    if (source->kind == STARSEAD_POLL_DNS) {
        if (source->fd != system->dns.fd || source->generation != system->dns.snapshot.generation) return 0;
        return starsead_supervisor_dns_tick(system, delta);
    }
    if (source->kind >= STARSEAD_POLL_PROCESS_EXEC_ERROR &&
        source->kind <= STARSEAD_POLL_PROCESS_PIDFD) {
        struct starsead_child_process *process = source->slot == 0U ? &system->core_process :
            source->slot == 1U ? &system->helper_process :
                source->slot == 2U ? &system->action_process : NULL;
        bool active = source->slot == 0U ? system->core_spawned && !system->core_reaped :
            source->slot == 1U ? system->helper_spawned && !system->helper_reaped :
                source->slot == 2U && system_action_source_active_state(
                    system->action_spawned, system->action_reaped, source->kind);
        if (!active || !starsead_process_poll_source_matches(process, source)) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_RUNTIME,
                "stale child poll source");
            return 0;
        }
    }
    if (source->kind == STARSEAD_POLL_SIGNAL) {
        if (system_runtime_dispatch_signal(system, delta) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_RUNTIME, "signal drain failed");
        }
    } else if (source->kind == STARSEAD_POLL_PROCESS_EXEC_ERROR) {
        (void)system_runtime_dispatch_setup(system, source->slot, delta);
    } else if (source->kind == STARSEAD_POLL_PROCESS_STDOUT) {
        (void)system_runtime_dispatch_log(
            system, source->slot, STARSEAD_LOG_STREAM_STDOUT, delta);
    } else if (source->kind == STARSEAD_POLL_PROCESS_STDERR) {
        (void)system_runtime_dispatch_log(
            system, source->slot, STARSEAD_LOG_STREAM_STDERR, delta);
    } else if (source->kind == STARSEAD_POLL_PROCESS_PIDFD && !system->stopping_children) {
        if (starsead_supervisor_runtime_reap_children(system, delta) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_CORE, "core reap failed");
        }
    } else if ((source->kind == STARSEAD_POLL_CONTROL_LISTENER ||
            source->kind == STARSEAD_POLL_CONTROL_CLIENT) && system->control != NULL) {
        int64_t now = 0;
        if (starsead_supervisor_runtime_clock(system, &now) != 0 ||
            starsead_control_server_dispatch(system->control, source->fd,
                (ready & POLLIN) != 0, (ready & POLLOUT) != 0,
                (ready & (POLLERR | POLLHUP | POLLNVAL)) != 0, (uint64_t)now) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_CONTROL, "control dispatch failed");
        }
    } else if (source->kind == STARSEAD_POLL_NETWORK && system->network_opened) {
        int64_t now = 0;
        char error[128U];
        if (starsead_supervisor_runtime_clock(system, &now) != 0 ||
            starsead_network_handle(
                &system->network, (uint64_t)now, error, sizeof(error)) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_NETWORK, "network drain failed");
        } else if (starsead_network_has_immediate(&system->network)) {
            delta->flags |= STARSEAD_DELTA_NETWORK_CHANGED;
        }
    } else if (source->kind == STARSEAD_POLL_TC_NETLINK &&
        system->tc_netlink_active) {
        if (source->generation != system->tc_netlink_sequence ||
            starsead_supervisor_tc_netlink_dispatch(system, ready) != 0) {
            system->tc_netlink_failed = true;
            system->tc_netlink_done = true;
        }
    } else if (source->kind == STARSEAD_POLL_SERVICE_TIMER) {
        if (system_service_timer_dispatch(system) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_RUNTIME,
                "service schedule dispatch failed");
        }
    } else if (source->kind == STARSEAD_POLL_KEYGUARD) {
        if ((ready & (POLLERR | POLLHUP | POLLNVAL)) ||
            starsead_keyguard_dispatch(system->keyguard_monitor) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_RUNTIME,
                "lock screen event source lost; restart service control to reconnect");
        }
    } else if (source->kind == STARSEAD_POLL_WIFI) {
        if (system_service_wifi_dispatch(system) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_NETWORK,
                "WiFi event dispatch failed");
        }
    }
    system_service_take_actions(system, delta);
    if (system->stop_requested &&
        (delta->flags & STARSEAD_DELTA_STOP_REQUESTED) == 0U) {
        system->stop_requested = false;
        delta->flags |= STARSEAD_DELTA_STOP_REQUESTED;
        delta->stop_reason = STARSEAD_LIFECYCLE_REASON_CONTROL_STOP;
    }
    return 0;
}

static int system_runtime_expire(
    void *opaque, int64_t now, struct starsead_runtime_delta *delta) {
    struct starsead_system_supervisor *system = opaque;
    if (starsead_supervisor_dns_tick(system, delta) != 0) return -1;
    if (system->control != NULL &&
        starsead_control_server_tick(system->control, (uint64_t)now) != 0) {
        starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_CONTROL, "control deadline failed");
    }
    if (system->logger.opened &&
        starsead_log_flush_expired(&system->logger, (uint64_t)now) != 0) {
        starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_LOG, "log deadline failed");
    }
    uint64_t network_deadline = 0U;
    if (system->network_opened &&
        starsead_network_next_deadline(&system->network, &network_deadline) &&
        (uint64_t)now >= network_deadline) {
        struct starsead_event_batch batch;
        bool integrity_loss = false;
        if (starsead_network_take_reconcile(
                &system->network, (uint64_t)now, &batch, &integrity_loss) != 0) {
            starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_NETWORK,
                "network debounce failed");
        } else {
            delta->flags |= STARSEAD_DELTA_RECONCILE_DUE;
            (void)integrity_loss;
        }
    }
    uint64_t wifi_deadline = 0U;
    if (system->wifi_monitor_opened &&
        starsead_wifi_monitor_next_deadline(&system->wifi_monitor, &wifi_deadline) &&
        (uint64_t)now >= wifi_deadline &&
        system_service_wifi_reconcile(system, (uint64_t)now) != 0) {
        starsead_supervisor_runtime_fatal(delta, STARSEAD_COMPONENT_NETWORK,
            "WiFi association reconcile failed");
    }
    system_service_take_actions(system, delta);
    return 0;
}

static struct starsead_runtime_reactor_backend system_runtime_reactor_backend(void) {
    const struct starsead_runtime_reactor_backend backend = {
        .monotonic_milliseconds = starsead_supervisor_runtime_clock,
        .prepare = system_runtime_prepare,
        .wait = system_runtime_wait,
        .dispatch = system_runtime_dispatch,
        .expire = system_runtime_expire,
    };
    return backend;
}

static int system_runtime_snapshot(
    void *opaque, struct starsead_control_snapshot *snapshot) {
    struct starsead_system_supervisor *system = opaque;
    return starsead_control_snapshot_from_state(&system->state, &system->live, snapshot);
}

int starsead_supervisor_runtime_request_stop(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    if (!system->runtime->supervising) return 1;
    system->stop_requested = true;
    system->automatic_stop_pending = false;
    system->manual_stop_pending = true;
    system->service_start_requested = false;
    return 0;
}

static int system_runtime_request_shutdown(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    system->shutdown_requested = true;
    system->service_start_requested = false;
    if (system->runtime->supervising) {
        system->stop_requested = true;
    }
    return 0;
}

int starsead_supervisor_accept_pump_delta(struct starsead_system_supervisor *system,
    struct starsead_runtime_delta *delta) {
    if (system == NULL || delta == NULL) return -1;
    return delta->flags == 0U ? 0 :
        starsead_runtime_accept_delta(system->runtime, delta);
}

int starsead_supervisor_pump_condition(struct starsead_system_supervisor *,
    const struct starsead_deadline *, starsead_runtime_predicate);
int starsead_supervisor_pump_condition_periodic(struct starsead_system_supervisor *,
    const struct starsead_deadline *, uint32_t, starsead_runtime_predicate);
bool starsead_supervisor_stop_done(void *);

static int system_effect_save(
    void *opaque, const struct starsead_state_document *state) {
    struct starsead_system_supervisor *system = opaque;
    char error[256U];
    return starsead_state_store_save(&system->store, state, error, sizeof(error));
}

static int system_effect_event(void *opaque, enum starsead_control_event_type type,
    const struct starsead_control_snapshot *snapshot,
    const struct starsead_control_error *details, bool has_details) {
    struct starsead_system_supervisor *system = opaque;
    int64_t now = 0;
    if (has_details != (details != NULL) || starsead_supervisor_runtime_clock(system, &now) != 0) return -1;
    if (type == STARSEAD_CONTROL_EVENT_RUNNING || type == STARSEAD_CONTROL_EVENT_PAUSED) {
        system->service_running = type == STARSEAD_CONTROL_EVENT_RUNNING;
    } else if (type == STARSEAD_CONTROL_EVENT_STOPPED) {
        // Once STOPPED is visible, a new automatic edge may start a new cycle.
        system->service_running = false;
        system->manual_stop_pending = false;
    }
    if (details != NULL && (type == STARSEAD_CONTROL_EVENT_FAILED ||
            type == STARSEAD_CONTROL_EVENT_CORE_EXITED ||
            type == STARSEAD_CONTROL_EVENT_HELPER_FAILED)) {
        char exit_text[24U] = "none";
        if (details->has_exit_code) {
            (void)snprintf(exit_text, sizeof(exit_text), "%d", details->exit_code);
        }
        char signal_text[24U] = "none";
        if (details->has_signal) {
            (void)snprintf(signal_text, sizeof(signal_text), "%d", details->signal);
        }
        char message[384U];
        int written = snprintf(message, sizeof(message),
            "service failed: code=%s exitCode=%s signal=%s message=%.*s",
            starsead_failure_code_name(details->code), exit_text, signal_text,
            details->message == NULL ? 0 : (int)(details->message_length < 256U
                ? details->message_length : 256U),
            details->message == NULL ? "" : details->message);
        if (written > 0 && (size_t)written < sizeof(message)) {
            (void)starsead_log_line(&system->logger, STARSEAD_LOG_LEVEL_ERROR,
                details->component, STARSEAD_LOG_EVENT_DIAGNOSTIC, message);
        }
    }
    return starsead_control_server_publish_event(
        system->control, type, snapshot, details,
        system_event_is_final(type), (uint64_t)now);
}

int starsead_supervisor_reconcile_owned_resources(
    struct starsead_system_supervisor *, char *, size_t);
void starsead_supervisor_reconcile_error(char *, size_t, const char *);
static int system_effect_stop_core(void *opaque);

static bool system_core_setup_done(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    return system->core_setup.complete || system->core_setup.fatal || system->core_reaped;
}

static int system_core_start_failed(
    struct starsead_system_supervisor *system,
    enum starsead_core_start_failure_stage stage,
    const char *detail) {
    struct starsead_core_start_diagnostic diagnostic = {
        .stage = stage,
        .setup_complete = system->core_setup.complete,
        .setup_fatal = system->core_setup.fatal,
        .setup_error_number = system->core_setup.fatal_error_number,
        .reaped = system->core_reaped,
        .detail = detail,
    };
    char message[512U];
    if (starsead_core_start_diagnostic_format(
            &diagnostic, message, sizeof(message)) == STARSEAD_LOG_OK) {
        int log_result = starsead_log_line(&system->logger, STARSEAD_LOG_LEVEL_ERROR,
            STARSEAD_COMPONENT_RUNTIME, STARSEAD_LOG_EVENT_DIAGNOSTIC, message);
        if (log_result != STARSEAD_LOG_OK) {
            (void)fprintf(stderr, "%s structuredLogResult=%d\n", message, log_result);
            (void)fflush(stderr);
        }
    } else {
        (void)fprintf(stderr,
            "core start failed: stage=diagnostic-format setupComplete=%d setupFatal=%d "
            "setupErrno=%d reaped=%d detail=formatting-failed\n",
            system->core_setup.complete ? 1 : 0,
            system->core_setup.fatal ? 1 : 0,
            system->core_setup.fatal_error_number,
            system->core_reaped ? 1 : 0);
        (void)fflush(stderr);
    }
    return -1;
}

static int system_effect_start_core(
    void *opaque, struct starsead_child_identity *identity) {
    struct starsead_system_supervisor *system = opaque;
    char error[256U] = {0};
    if (!system->core_spec_ready && starsead_core_process_spec(
            &system->loaded_config.config, (const char *const *)environ,
            &system->core_spec, error, sizeof(error)) != 0) {
        return system_core_start_failed(system,
            STARSEAD_CORE_START_FAILURE_PROCESS_SPEC,
            error[0] == '\0' ? "process specification failed" : error);
    }
    system->core_spec_ready = true;
    if (starsead_system_process_backends_init(&system->process_context,
            &system->core_spec, NULL, &system->readiness_backend,
            &system->stop_backend) != 0) {
        return system_core_start_failed(system,
            STARSEAD_CORE_START_FAILURE_BACKEND_INIT,
            "process backend initialization failed");
    }
    if (starsead_readiness_preflight(&system->loaded_config.config,
            STARSEAD_CHILD_CORE, &system->readiness_backend) != 0) {
        return system_core_start_failed(system,
            STARSEAD_CORE_START_FAILURE_READINESS_PREFLIGHT,
            "readiness preflight failed");
    }
    if (starsead_process_spawn_system(
            &system->core_spec, &system->core_process, error, sizeof(error)) != 0) {
        return system_core_start_failed(system, STARSEAD_CORE_START_FAILURE_SPAWN,
            error[0] == '\0' ? "process spawn failed" : error);
    }
    system->core_spawned = true;
    starsead_child_setup_stream_init(&system->core_setup);
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0 ||
        (uint64_t)now > UINT64_MAX - system->loaded_config.config.readiness_timeout_milliseconds) {
        return system_core_start_failed(system, STARSEAD_CORE_START_FAILURE_CLOCK,
            "setup deadline clock failed");
    }
    struct starsead_deadline deadline = {
        .armed = true,
        .monotonic_milliseconds = now +
            (int64_t)system->loaded_config.config.readiness_timeout_milliseconds,
    };
    int pumped = starsead_supervisor_pump_condition(
        system, &deadline, system_core_setup_done);
    if (pumped != 0) {
        return system_core_start_failed(system, STARSEAD_CORE_START_FAILURE_SETUP_WAIT,
            "setup status wait failed");
    }
    if (!system->core_setup.complete || system->core_setup.fatal || system->core_reaped) {
        return system_core_start_failed(system, STARSEAD_CORE_START_FAILURE_SETUP_RESULT,
            "child setup failed");
    }
    enum starsead_child_type type = system->loaded_config.config.core_type == STARSEAD_CORE_XRAY
        ? STARSEAD_CHILD_TYPE_XRAY :
        system->loaded_config.config.core_type == STARSEAD_CORE_SING_BOX
            ? STARSEAD_CHILD_TYPE_SING_BOX : STARSEAD_CHILD_TYPE_MIHOMO;
    if (starsead_process_identity_read(starsead_system_process_identity_backend(),
            system->core_process.pid, STARSEAD_CHILD_CORE, type, &system->core_spec,
            &system->core_identity, error, sizeof(error)) != 0) {
        return system_core_start_failed(system, STARSEAD_CORE_START_FAILURE_IDENTITY,
            error[0] == '\0' ? "process identity verification failed" : error);
    }
    system->core_identity_ready = true;
    *identity = system->core_identity;
    return 0;
}

static bool system_core_readiness_done(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0) {
        system->readiness_result = STARSEAD_READINESS_IO;
        return true;
    }
    bool stop = atomic_load_explicit(
        &system->runtime->lifecycle.stop_was_requested, memory_order_acquire);
    system->readiness_result = starsead_readiness_poll(
        &system->loaded_config.config, &system->readiness, &system->core_identity,
        &system->readiness_backend, (uint64_t)now, stop);
    return system->readiness_result != STARSEAD_READINESS_PENDING;
}

void starsead_supervisor_log_failure(struct starsead_system_supervisor *system,
    enum starsead_component component, const char *message);

static const char *system_readiness_result_name(int result) {
    switch (result) {
        case STARSEAD_READINESS_PENDING: return "pending";
        case STARSEAD_READINESS_READY: return "ready";
        case STARSEAD_READINESS_TIMEOUT: return "timeout";
        case STARSEAD_READINESS_CONFLICT: return "conflict";
        case STARSEAD_READINESS_CHILD_LOST: return "child-lost";
        case STARSEAD_READINESS_STOP_REQUESTED: return "stop-requested";
        case STARSEAD_READINESS_IO: return "io-error";
        default: return "invalid";
    }
}

/* Name the probe target so a readiness failure can be checked on the device. */
static void system_log_readiness_failure(struct starsead_system_supervisor *system,
    enum starsead_child_role role, int result) {
    if (result == STARSEAD_READINESS_READY || result == STARSEAD_READINESS_STOP_REQUESTED) return;
    char target[96U];
    if (starsead_readiness_target_describe(
            &system->loaded_config.config, role, target, sizeof(target)) != 0) {
        (void)snprintf(target, sizeof(target), "%s", "unknown");
    }
    char message[256U];
    int written = snprintf(message, sizeof(message),
        "%s readiness failed: result=%s mode=%s target=%s timeoutMs=%" PRIu32,
        role == STARSEAD_CHILD_CORE ? "core" : "helper",
        system_readiness_result_name(result),
        starsead_mode_name(system->loaded_config.config.mode), target,
        system->loaded_config.config.readiness_timeout_milliseconds);
    if (written > 0 && (size_t)written < sizeof(message)) {
        starsead_supervisor_log_failure(system,
            role == STARSEAD_CHILD_CORE ? STARSEAD_COMPONENT_CORE : STARSEAD_COMPONENT_HELPER,
            message);
    }
}

static int system_effect_wait_core(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0 ||
        starsead_system_process_backends_init(&system->process_context,
            &system->core_spec, NULL, &system->readiness_backend, &system->stop_backend) != 0 ||
        starsead_readiness_init(&system->loaded_config.config, STARSEAD_CHILD_CORE,
            (uint64_t)now, &system->readiness_backend, &system->readiness) != 0) return -1;
    struct starsead_deadline deadline = {
        .armed = true,
        .monotonic_milliseconds = (int64_t)system->readiness.deadline_milliseconds,
    };
    int pumped = starsead_supervisor_pump_condition_periodic(system, &deadline,
        STARSEAD_READINESS_POLL_INTERVAL_MILLIS, system_core_readiness_done);
    if (pumped == 0 && system->readiness_result == STARSEAD_READINESS_READY) return 0;
    system_log_readiness_failure(system, STARSEAD_CHILD_CORE, system->readiness_result);
    return system->readiness_result == STARSEAD_READINESS_TIMEOUT
        ? STARSEAD_RUNTIME_EFFECT_READINESS_TIMEOUT : -1;
}

static bool system_helper_setup_done(void *opaque);

static const struct starsead_tun_compat_paths system_tun_compatibility_paths = {
    .tun_device = "/dev/tun",
    .network_directory = "/dev/net",
    .compatibility_path = "/dev/net/tun",
};

static int system_tun_compatibility_log(
    struct starsead_system_supervisor *system, const char *operation, const char *detail) {
    char message[384U];
    int written = snprintf(message, sizeof(message),
        "TUN compatibility path %s failed: %s", operation,
        detail != NULL && detail[0] != '\0' ? detail : "unknown error");
    if (written > 0 && (size_t)written < sizeof(message)) {
        (void)starsead_log_line(&system->logger, STARSEAD_LOG_LEVEL_ERROR,
            STARSEAD_COMPONENT_HELPER, STARSEAD_LOG_EVENT_DIAGNOSTIC, message);
    }
    return -1;
}

int starsead_supervisor_tun_compatibility_prepare(struct starsead_system_supervisor *system) {
    char error[256U];
    if (starsead_tun_compat_prepare(&system_tun_compatibility_paths,
            &system->tun_compatibility, error, sizeof(error)) == 0) return 0;
    (void)system_tun_compatibility_log(system, "prepare", error);
    if (starsead_tun_compat_cleanup(&system_tun_compatibility_paths,
            &system->tun_compatibility, error, sizeof(error)) != 0) {
        (void)system_tun_compatibility_log(system, "prepare rollback", error);
    }
    return -1;
}

int starsead_supervisor_tun_compatibility_cleanup(struct starsead_system_supervisor *system) {
    char error[256U];
    if (starsead_tun_compat_cleanup(&system_tun_compatibility_paths,
            &system->tun_compatibility, error, sizeof(error)) == 0) return 0;
    return system_tun_compatibility_log(system, "cleanup", error);
}

int starsead_supervisor_start_helper_process(
    struct starsead_system_supervisor *system, struct starsead_child_identity *identity) {
    char error[256U];
    system->helper_reaped = false;
    system->helper_identity_ready = false;
    memset(&system->helper_identity, 0, sizeof(system->helper_identity));
    memset(&system->helper_exit, 0, sizeof(system->helper_exit));
    if (!system->helper_launch_ready && starsead_helper_launch_prepare(
            &system->loaded_config.config, (const char *const *)environ,
            starsead_system_anonymous_file_backend(), &system->helper_launch,
            error, sizeof(error)) != 0) return -1;
    system->helper_launch_ready = true;
    if (starsead_system_process_backends_init(&system->process_context,
            &system->core_spec, &system->helper_launch.process,
            &system->readiness_backend, &system->stop_backend) != 0 ||
        starsead_readiness_preflight(&system->loaded_config.config,
            STARSEAD_CHILD_HELPER, &system->readiness_backend) != 0) return -1;
    if (starsead_process_spawn_system(&system->helper_launch.process,
            &system->helper_process, error, sizeof(error)) != 0) return -1;
    system->helper_spawned = true;
    starsead_child_setup_stream_init(&system->helper_setup);
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0) return -1;
    struct starsead_deadline deadline = {
        .armed = true,
        .monotonic_milliseconds = now +
            (int64_t)system->loaded_config.config.readiness_timeout_milliseconds,
    };
    int pumped = starsead_supervisor_pump_condition(
        system, &deadline, system_helper_setup_done);
    if (pumped != 0 || !system->helper_setup.complete || system->helper_setup.fatal ||
        system->helper_reaped) return -1;
    const struct starsead_anonymous_file_backend *anonymous =
        starsead_system_anonymous_file_backend();
    if (starsead_anonymous_file_close(anonymous, &system->helper_launch.config_file) != 0 ||
        starsead_anonymous_file_close(anonymous, &system->helper_launch.direct_ipv4_file) != 0 ||
        starsead_anonymous_file_close(anonymous, &system->helper_launch.direct_ipv6_file) != 0) {
        return -1;
    }
    enum starsead_child_type type = system->loaded_config.config.helper.type ==
        STARSEAD_HELPER_HEV_SOCKS5_TUNNEL
            ? STARSEAD_CHILD_TYPE_HEV_SOCKS5_TUNNEL : STARSEAD_CHILD_TYPE_BPF2SOCKS;
    if (starsead_process_identity_read(starsead_system_process_identity_backend(),
            system->helper_process.pid, STARSEAD_CHILD_HELPER, type,
            &system->helper_launch.process, &system->helper_identity,
            error, sizeof(error)) != 0) return -1;
    system->helper_identity_ready = true;
    *identity = system->helper_identity;
    return 0;
}

static bool system_helper_setup_done(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    return system->helper_setup.complete || system->helper_setup.fatal || system->helper_reaped;
}

static bool system_helper_readiness_done(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0) {
        system->helper_readiness_result = STARSEAD_READINESS_IO;
        return true;
    }
    bool stop = atomic_load_explicit(
        &system->runtime->lifecycle.stop_was_requested, memory_order_acquire);
    system->helper_readiness_result = starsead_readiness_poll(
        &system->loaded_config.config, &system->helper_readiness, &system->helper_identity,
        &system->readiness_backend, (uint64_t)now, stop);
    return system->helper_readiness_result != STARSEAD_READINESS_PENDING;
}

static int system_effect_wait_helper(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    int result = -1;
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0 ||
        starsead_system_process_backends_init(&system->process_context,
            &system->core_spec, &system->helper_launch.process,
            &system->readiness_backend, &system->stop_backend) != 0 ||
        starsead_readiness_init(&system->loaded_config.config, STARSEAD_CHILD_HELPER,
            (uint64_t)now, &system->readiness_backend, &system->helper_readiness) != 0) goto done;
    {
        struct starsead_deadline deadline = {
            .armed = true,
            .monotonic_milliseconds = (int64_t)system->helper_readiness.deadline_milliseconds,
        };
        int pumped = starsead_supervisor_pump_condition_periodic(system, &deadline,
            STARSEAD_READINESS_POLL_INTERVAL_MILLIS, system_helper_readiness_done);
        if (pumped == 0 && system->helper_readiness_result == STARSEAD_READINESS_READY) {
            result = 0;
        } else {
            system_log_readiness_failure(
                system, STARSEAD_CHILD_HELPER, system->helper_readiness_result);
            if (system->helper_readiness_result == STARSEAD_READINESS_TIMEOUT) {
                result = STARSEAD_RUNTIME_EFFECT_READINESS_TIMEOUT;
            }
        }
    }

done:
    if (system->loaded_config.config.helper.type == STARSEAD_HELPER_HEV_SOCKS5_TUNNEL &&
        starsead_supervisor_tun_compatibility_cleanup(system) != 0) result = -1;
    return result;
}

static int system_effect_reconcile(void *opaque, bool *active,
    uint64_t *generation, uint32_t *categories) {
    struct starsead_system_supervisor *system = opaque;
    if (active == NULL || generation == NULL || categories == NULL ||
        !system->rules_initialized) return -1;
    system->rules_verified = false;
    system->rules_reconciling = true;
    const char *failed_stage = NULL;
    bool local_bypass_changed[STARSEAD_IP_FAMILY_COUNT] = {false};
    // Local bypass planning reads the captured mangle tables; only the mangle
    // listing of a family whose bypass rules actually changed is read again.
    // Rule repairs move the snapshot out of BEFORE_APPLY, so an unchanged
    // snapshot is current.
    if (starsead_supervisor_capture_local_address_snapshot(system) != 0) {
        failed_stage = "local-address-snapshot";
    } else if (starsead_supervisor_rule_snapshot_capture(
            system, &system->rules_runtime.plan,
            SYSTEM_RULE_SNAPSHOT_BEFORE_APPLY) != 0) {
        failed_stage = "rules-snapshot-before-reconcile";
    } else if (starsead_supervisor_reconcile_iptables_local_bypass(
            system, local_bypass_changed) != 0) {
        failed_stage = "iptables-local-bypass";
    } else if (starsead_supervisor_rule_snapshot_refresh_mangle(
            system, &system->rules_runtime.plan, local_bypass_changed) != 0) {
        failed_stage = "rules-snapshot-before-reconcile";
    } else if (starsead_rules_reconcile(
            &system->rules_runtime, &system->rules_backend) != 0) {
        failed_stage = "rules-reconcile";
    } else if (system->rule_snapshot.phase != SYSTEM_RULE_SNAPSHOT_BEFORE_APPLY &&
        starsead_supervisor_rule_snapshot_capture(
            system, &system->rules_runtime.plan,
            SYSTEM_RULE_SNAPSHOT_AFTER_APPLY) != 0) {
        failed_stage = "rules-snapshot-after-reconcile";
    } else if (starsead_supervisor_reconcile_b2s_local_maps(system) != 0) {
        failed_stage = "bpf2-local-map";
    } else if (starsead_supervisor_reconcile_hotspot_tc(system) != 0) {
        failed_stage = "hotspot-tc";
    } else if (starsead_rules_verify(
            &system->rules_runtime, &system->rules_backend) != 0) {
        failed_stage = "rules-verify";
    }
    starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
    starsead_supervisor_clear_local_address_snapshot(system);
    system->rules_reconciling = false;
    if (failed_stage != NULL) {
        char message[160U];
        int written = snprintf(message, sizeof(message),
            "rules reconcile failed: stage=%s", failed_stage);
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

static int system_effect_network_immediate(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    char error[128U];
    return system->network_opened && starsead_network_apply_immediate(
        &system->network, error, sizeof(error)) == 0 ? 0 : -1;
}

static int system_effect_verify(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    int result = system_startup_components_verified(
        system->core_identity_ready, system->core_reaped,
        system->loaded_config.config.helper.type != STARSEAD_HELPER_NONE,
        system->helper_identity_ready, system->helper_reaped,
        system->loaded_config.config.matcher.enabled, system->matcher_verified,
        system->rules_initialized, system->rules_verified) ? 0 : -1;
    starsead_supervisor_clear_local_address_snapshot(system);
    if (result == 0 && system->loaded_config.config.dns_enhancement) {
        result = starsead_supervisor_dns_verify_startup(system);
        system->dns_startup_deadline = 0;
        system->dns.busy = false;
    }
    return result;
}

bool starsead_supervisor_stop_done(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0) {
        system->stop_result = STARSEAD_STOP_FAILED;
        return true;
    }
    system->stop_result = starsead_stop_coordinator_poll(
        &system->stop_coordinator, &system->stop_backend, (uint64_t)now);
    return system->stop_result != STARSEAD_STOP_PENDING;
}

static int system_effect_stop_children(
    struct starsead_system_supervisor *system, bool include_core, bool include_helper) {
    const struct starsead_child_identity *core =
        include_core && system->core_spawned && !system->core_reaped ? &system->core_identity : NULL;
    const struct starsead_child_identity *helper = include_helper &&
        system->helper_spawned && !system->helper_reaped ? &system->helper_identity : NULL;
    if (core == NULL && helper == NULL) {
        if (include_helper) starsead_child_process_close(&system->helper_process);
        if (include_core) starsead_child_process_close(&system->core_process);
        return 0;
    }
    if ((core != NULL && !system->core_identity_ready) ||
        (helper != NULL && !system->helper_identity_ready) ||
        starsead_system_process_backends_init(&system->process_context,
            &system->core_spec,
            system->helper_launch_ready ? &system->helper_launch.process : NULL,
            &system->readiness_backend, &system->stop_backend) != 0) {
        char message[160U];
        int written = snprintf(message, sizeof(message),
            "child stop failed: stage=prepare coreIdentity=%d helperIdentity=%d",
            core == NULL ? -1 : system->core_identity_ready ? 1 : 0,
            helper == NULL ? -1 : system->helper_identity_ready ? 1 : 0);
        if (written > 0 && (size_t)written < sizeof(message)) {
            starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RUNTIME, message);
        }
        return -1;
    }
    int64_t now = 0;
    if (starsead_supervisor_runtime_clock(system, &now) != 0) return -1;
    starsead_stop_coordinator_init(&system->stop_coordinator);
    system->stopping_children = true;
    system->stop_result = starsead_stop_coordinator_begin(
        &system->stop_coordinator, core, helper,
        &system->stop_backend, (uint64_t)now);
    if (system->stop_result == STARSEAD_STOP_PENDING) {
        struct starsead_deadline deadline = {
            .armed = true,
            .monotonic_milliseconds = (int64_t)system->stop_coordinator.kill_deadline_milliseconds,
        };
        if (starsead_supervisor_pump_condition(system, &deadline, starsead_supervisor_stop_done) != 0) {
            system->stop_result = STARSEAD_STOP_FAILED;
        }
    }
    system->stopping_children = false;
    bool killed = system->stop_coordinator.core.kill_sent ||
        system->stop_coordinator.helper.kill_sent;
    if (system->stop_result != STARSEAD_STOP_COMPLETE || killed) {
        const struct starsead_stop_role_state *core_state = &system->stop_coordinator.core;
        const struct starsead_stop_role_state *helper_state = &system->stop_coordinator.helper;
        char message[320U];
        int written = snprintf(message, sizeof(message),
            "child stop %s: core=present:%d,term:%d,kill:%d,signalFailed:%d,reaped:%d "
            "helper=present:%d,term:%d,kill:%d,signalFailed:%d,reaped:%d",
            system->stop_result == STARSEAD_STOP_COMPLETE ? "required SIGKILL" : "failed",
            core_state->present ? 1 : 0, core_state->term_sent ? 1 : 0,
            core_state->kill_sent ? 1 : 0, core_state->signal_failed ? 1 : 0,
            core_state->reaped ? 1 : 0,
            helper_state->present ? 1 : 0, helper_state->term_sent ? 1 : 0,
            helper_state->kill_sent ? 1 : 0, helper_state->signal_failed ? 1 : 0,
            helper_state->reaped ? 1 : 0);
        if (written > 0 && (size_t)written < sizeof(message)) {
            if (system->stop_result == STARSEAD_STOP_COMPLETE) {
                (void)starsead_log_line(&system->logger, STARSEAD_LOG_LEVEL_WARNING,
                    STARSEAD_COMPONENT_RUNTIME, STARSEAD_LOG_EVENT_DIAGNOSTIC, message);
            } else {
                starsead_supervisor_log_failure(system, STARSEAD_COMPONENT_RUNTIME, message);
            }
        }
    }
    if (system->stop_result == STARSEAD_STOP_COMPLETE) {
        if (core != NULL) system->core_reaped = true;
        if (helper != NULL) system->helper_reaped = true;
    }
    /* The coordinator consumes SIGCHLD while it waits. Reap retained children
     * as soon as that wait ends, including on kernels without pidfd support. */
    struct starsead_runtime_delta delta;
    starsead_runtime_delta_init(&delta);
    if (starsead_supervisor_runtime_reap_children(system, &delta) != 0 ||
        starsead_supervisor_accept_pump_delta(system, &delta) != 0) return -1;
    if (include_helper) starsead_child_process_close(&system->helper_process);
    if (include_core) starsead_child_process_close(&system->core_process);
    return system->stop_result == STARSEAD_STOP_COMPLETE && !killed ? 0 : -1;
}

static int system_effect_stop_helper(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    int stopped = system_effect_stop_children(opaque, false, true);
    if (stopped == 0 && system->helper_launch_ready) {
        stopped = starsead_helper_launch_destroy(
            starsead_system_anonymous_file_backend(), &system->helper_launch);
        system->helper_launch_ready = false;
    }
    int cleaned = starsead_supervisor_tun_compatibility_cleanup(system);
    char error[128U];
    int pins_cleaned = system->loaded_config.config.helper.type == STARSEAD_HELPER_BPF2SOCKS
        ? starsead_supervisor_reconcile_remove_phase(
            system, STARSEAD_RECONCILE_BPF_PINS, error, sizeof(error))
        : 0;
    return stopped == 0 && cleaned == 0 && pins_cleaned == 0 ? 0 : -1;
}

static int system_effect_stop_matcher(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    char error[128U];
    if (!system->loaded_config.config.matcher.enabled) return 0;
    return starsead_supervisor_reconcile_remove_phase(
        system, STARSEAD_RECONCILE_BPF_PINS, error, sizeof(error));
}

static int system_effect_stop_core(void *opaque) {
    return system_effect_stop_children(opaque, true, false);
}

static int system_effect_quiesce(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    system->cleanup_in_progress = true;
    system->dns_regenerating=false;
    system->dns_hot_stage = SYSTEM_DNS_HOT_IDLE; system->dns_hot_result = -1;
    if (starsead_supervisor_dns_withdraw(system) != 0) return -1;
    if (starsead_mode_core_managed(system->loaded_config.config.mode)) {
        return system_effect_stop_core(opaque);
    }
    if (!system->rules_initialized) return 0;
    bool cleanup_required = false;
    for (size_t index = 0U;
            index < system->rules_runtime.plan.hook_group_count; ++index) {
        if (system->rules_runtime.hook_cleanup_required[index]) {
            cleanup_required = true;
            break;
        }
    }
    if (!cleanup_required) return 0;
    struct starsead_rule_transaction_plan snapshot_plan =
        system->rules_runtime.plan;
    snapshot_plan.private_group_count = 0U;
    snapshot_plan.route_count = 0U;
    if (starsead_supervisor_rule_snapshot_capture(system, &snapshot_plan,
            SYSTEM_RULE_SNAPSHOT_BEFORE_APPLY) != 0 ||
        starsead_supervisor_rule_batch_begin(&system->rule_commands) != 0) goto failed;
    for (size_t remaining = system->rules_runtime.plan.hook_group_count;
            remaining > 0U; --remaining) {
        size_t index = remaining - 1U;
        if (system->rules_runtime.hook_cleanup_required[index]) {
            if (starsead_supervisor_remove_hook_group(system,
                    &system->rules_runtime.plan.hook_groups[index]) != 0) goto failed;
        }
    }
    if (starsead_supervisor_rule_batch_flush(system) != 0) goto failed;
    starsead_supervisor_rule_batch_destroy(&system->rule_commands);
    // The verification listing covers the full plan so rule removal, which
    // runs next with no rule change in between, can reuse it.
    if (starsead_supervisor_rule_snapshot_capture(system, &system->rules_runtime.plan,
            SYSTEM_RULE_SNAPSHOT_AFTER_APPLY) != 0) goto failed;
    for (size_t index = 0U;
            index < system->rules_runtime.plan.hook_group_count; ++index) {
        if (!system->rules_runtime.hook_cleanup_required[index]) continue;
        bool all_present = false;
        bool any_present = false;
        if (starsead_supervisor_probe_hook_group(system,
                &system->rules_runtime.plan.hook_groups[index],
                &all_present, &any_present) != 0 || any_present) goto failed;
        system->rules_runtime.hook_cleanup_required[index] = false;
    }
    system->rule_snapshot.removal_ready = true;
    return 0;

failed:
    starsead_supervisor_rule_batch_destroy(&system->rule_commands);
    starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
    return -1;
}

static int system_effect_remove_rules(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    system->cleanup_in_progress = true;
    if (starsead_mode_core_managed(system->loaded_config.config.mode)) return 0;
    if (!system->rules_initialized) return -1;
    // Probes read one snapshot and every removal is committed in one batch. The
    // journal is restored if the commit fails, so a later cleanup retries it.
    // A snapshot left by the quiesce that just ran is current and is reused.
    struct starsead_rules_runtime journal = system->rules_runtime;
    int removed = -1;
    bool snapshot_ready = system->rule_snapshot.removal_ready &&
        system->rule_snapshot.phase != SYSTEM_RULE_SNAPSHOT_NONE;
    system->rule_snapshot.removal_ready = false;
    if (!snapshot_ready && starsead_supervisor_rule_snapshot_capture(
            system, &system->rules_runtime.plan, SYSTEM_RULE_SNAPSHOT_BEFORE_APPLY) != 0) {
        // Without a snapshot, each object is still probed and removed on its own.
        removed = starsead_rules_remove(&system->rules_runtime, &system->rules_backend);
    } else if (starsead_supervisor_rule_batch_begin(&system->rule_commands) == 0) {
        removed = starsead_rules_remove(&system->rules_runtime, &system->rules_backend);
        if (starsead_supervisor_rule_batch_flush(system) != 0) {
            memcpy(system->rules_runtime.private_cleanup_required,
                journal.private_cleanup_required, sizeof(journal.private_cleanup_required));
            memcpy(system->rules_runtime.route_cleanup_required,
                journal.route_cleanup_required, sizeof(journal.route_cleanup_required));
            memcpy(system->rules_runtime.hook_cleanup_required,
                journal.hook_cleanup_required, sizeof(journal.hook_cleanup_required));
            system->rules_runtime.installed = journal.installed;
            removed = -1;
        }
    }
    starsead_supervisor_rule_batch_destroy(&system->rule_commands);
    starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
    if (removed != 0) return -1;
    if (starsead_runtime_remove_tc_before_helper_stop(
            system->loaded_config.config.mode)) {
        char error[128U];
        if (starsead_supervisor_reconcile_remove_tc_filters(
                system, error, sizeof(error)) != 0) {
            return -1;
        }
    }
    return 0;
}

static int system_effect_noop(void *opaque) {
    (void)opaque;
    return 0;
}

static int system_effect_close_network(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    if (!system->network_opened) return 0;
    int result = starsead_network_close(&system->network);
    if (result == 0) system->network_opened = false;
    return result;
}

static int system_effect_restore_best_effort(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    system->cleanup_in_progress = true;
    char error[128U];
    struct starsead_effect_backend backend = starsead_supervisor_effect_backend(system);
    return starsead_effect_journal_rollback(
        &system->volatile_effects, &backend, error, sizeof(error));
}

static struct starsead_runtime_effect_backend system_runtime_effects(
    struct starsead_system_supervisor *system) {
    const struct starsead_runtime_effect_backend effects = {
        .context = system,
        .save_state = system_effect_save,
        .publish_event = system_effect_event,
        .start_core = system_effect_start_core,
        .wait_core = system_effect_wait_core,
        .ensure_platform_capability = starsead_supervisor_effect_capability,
        .start_helper = starsead_supervisor_effect_start_helper,
        .wait_helper = system_effect_wait_helper,
        .start_matcher = starsead_supervisor_effect_start_matcher,
        .open_network = starsead_supervisor_effect_open_network,
        .apply_rules = starsead_supervisor_effect_rules,
        .verify = system_effect_verify,
        .network_immediate = system_effect_network_immediate,
        .reconcile = system_effect_reconcile,
        .quiesce_traffic = system_effect_quiesce,
        .remove_rules = system_effect_remove_rules,
        .close_network = system_effect_close_network,
        .stop_matcher = system_effect_stop_matcher,
        .stop_helper = system_effect_stop_helper,
        .stop_core = system_effect_stop_core,
        .restore_best_effort = system_effect_restore_best_effort,
        .release = system_effect_noop,
    };
    return effects;
}

static int system_start_result(struct starsead_control_result *result,
    enum starsead_control_result_code code, const char *message,
    const struct starsead_control_snapshot *snapshot) {
    starsead_control_result_destroy(result);
    result->code = code;
    if (snapshot != NULL) {
        result->has_snapshot = true;
        if (starsead_control_snapshot_copy(&result->snapshot, snapshot) != 0) return -1;
    }
    return starsead_control_result_set_message(result, message, strlen(message));
}

static int system_probe_existing(struct starsead_control_result *result) {
    struct starsead_control_response response;
    memset(&response, 0, sizeof(response));
    enum starsead_control_client_result probed = starsead_control_client_run(
        STARSEAD_CONTROL_METHOD_STATUS, "start", NULL, NULL, &response);
    int status;
    if (probed == STARSEAD_CONTROL_CLIENT_OK && response.result.has_snapshot) {
        status = system_start_result(result, STARSEAD_CONTROL_RESULT_ALREADY_RUNNING,
            "protocol-valid supervisor owns the runtime", &response.result.snapshot);
    } else {
        status = system_start_result(result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "control address is occupied without a valid supervisor response", NULL);
    }
    starsead_control_response_destroy(&response);
    return status;
}

static bool system_control_drained(void *opaque) {
    struct starsead_system_supervisor *system = opaque;
    return starsead_control_server_drained(system->control);
}

static void system_child_process_defaults(struct starsead_child_process *process) {
    memset(process, 0, sizeof(*process));
    process->pid = -1;
    process->process_group_id = -1;
    process->pidfd = -1;
    process->stdout_fd = -1;
    process->stderr_fd = -1;
    process->setup_status_fd = -1;
}

static void system_runtime_cleanup_cycle(struct starsead_system_supervisor *system) {
    starsead_dns_close(&system->dns);
    starsead_supervisor_rule_batch_destroy(&system->rule_commands);
    starsead_supervisor_rule_snapshot_destroy(&system->rule_snapshot);
    starsead_supervisor_tc_netlink_close(system);
    if (system->network_opened) (void)starsead_network_close(&system->network);
    system->network_opened = false;
    starsead_child_process_close(&system->core_process);
    starsead_child_process_close(&system->helper_process);
    starsead_child_process_close(&system->action_process);
    if (system->core_spec_ready) starsead_process_spec_destroy(&system->core_spec);
    system->core_spec_ready = false;
    if (system->helper_launch_ready) {
        (void)starsead_helper_launch_destroy(
            starsead_system_anonymous_file_backend(), &system->helper_launch);
    }
    system->helper_launch_ready = false;
    (void)starsead_supervisor_tun_compatibility_cleanup(system);
    if (system->matcher_launch_ready) {
        (void)starsead_matcher_launch_destroy(
            starsead_system_anonymous_file_backend(), &system->matcher_launch);
    }
    system->matcher_launch_ready = false;
}

static void system_runtime_reset_cycle(struct starsead_system_supervisor *system) {
    system_runtime_cleanup_cycle(system);
    size_t offset = offsetof(struct starsead_system_supervisor, stop_requested);
    memset((unsigned char *)system + offset, 0, sizeof(*system) - offset);
    starsead_dns_init(&system->dns);
    ++system->dns_cycle;
    system->dns_hot_completed_generation = 0;
    system->dns_regenerating = false;
    system->dns_hot_stage = SYSTEM_DNS_HOT_IDLE; system->dns_hot_result = -1;
    starsead_tun_compat_state_init(&system->tun_compatibility);
    system->tc_netlink_fd = -1;
    system_child_process_defaults(&system->core_process);
    system_child_process_defaults(&system->helper_process);
    system_child_process_defaults(&system->action_process);
}

static int system_reset_telemetry_state(
    struct starsead_system_supervisor *system,
    char *error, size_t error_size) {
    struct starsead_state_document replacement;
    int result = starsead_state_document_init(&replacement,
        system->loaded_config.config.owner,
        system->loaded_config.config.core_type,
        system->loaded_config.config.mode);
    if (result != STARSEAD_STATE_OK) return result;
    result = starsead_state_store_save(
        &system->store, &replacement, error, error_size);
    if (result != STARSEAD_STATE_OK) {
        starsead_state_document_destroy(&replacement);
        return result;
    }
    if (system->state.initialized) {
        starsead_state_document_destroy(&system->state);
    }
    system->state = replacement;
    return STARSEAD_STATE_OK;
}

static void system_runtime_cleanup(struct starsead_system_supervisor *system) {
    system_runtime_cleanup_cycle(system);
    if (system->control != NULL) {
        starsead_control_server_destroy(system->control);
        system->control = NULL;
    }
    if (system->signal_fd_owned && system->signal_fd >= 0) (void)close(system->signal_fd);
    system->signal_fd = -1;
    system->signal_fd_owned = false;
    if (system->service_timer_fd_owned && system->service_timer_fd >= 0) {
        (void)close(system->service_timer_fd);
    }
    system->service_timer_fd = -1;
    system->service_timer_fd_owned = false;
    starsead_keyguard_close(system->keyguard_monitor);
    system->keyguard_monitor = NULL;
    if (system->wifi_monitor_opened) starsead_wifi_monitor_close(&system->wifi_monitor);
    system->wifi_monitor_opened = false;
    if (system->runtime != NULL) starsead_runtime_destroy(system->runtime);
    system->runtime = NULL;
    starsead_state_document_destroy(&system->state);
    starsead_state_store_close(&system->store);
    if (system->logger.opened) (void)starsead_log_close(&system->logger);
    starsead_loaded_config_release(&system->loaded_config);
}

static void system_runtime_finish_control_stop(
    struct starsead_system_supervisor *system) {
    struct starsead_control_snapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    struct starsead_control_result result;
    memset(&result, 0, sizeof(result));
    if (starsead_control_snapshot_from_state(&system->state, &system->live, &snapshot) == 0) {
        result.code = starsead_state_is_stopped(&system->state)
            ? STARSEAD_CONTROL_RESULT_OK : STARSEAD_CONTROL_RESULT_STOP_FAILED;
        result.has_snapshot = true;
        (void)starsead_control_snapshot_copy(&result.snapshot, &snapshot);
        if (result.code == STARSEAD_CONTROL_RESULT_STOP_FAILED) {
            (void)starsead_control_result_set_message(&result, "cleanup incomplete", 18U);
        }
        int64_t now = 0;
        if (starsead_supervisor_runtime_clock(system, &now) == 0) {
            (void)starsead_control_server_finish_stop(
                system->control, &result, (uint64_t)now);
        }
    }
    starsead_control_result_destroy(&result);
    starsead_control_snapshot_destroy(&snapshot);
}

static int system_runtime_run(const char *config_path, bool initial_start,
    bool *has_early_result, struct starsead_control_result *early_result) {
    if (has_early_result == NULL || early_result == NULL) return -1;
    *has_early_result = false;
    memset(early_result, 0, sizeof(*early_result));
    if (config_path == NULL) {
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "verified publication locks are required", NULL);
        return 1;
    }

    sigset_t blocked;
    sigset_t previous;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) != 0) {
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "failed to block supervisor signals", NULL);
        return 1;
    }

    struct starsead_system_supervisor system;
    memset(&system, 0, sizeof(system));
    starsead_dns_init(&system.dns);
    if (syscall(SYS_getrandom, &system.dns_instance, sizeof(system.dns_instance), 0) != sizeof(system.dns_instance))
        system.dns_instance = 0; // Required enhancement fails closed if a fresh identity is unavailable.
    system.dns_cycle = 1;
    system.live.dns_enhancement = &system.dns.snapshot;
    system.signal_fd = -1;
    system.service_timer_fd = -1;
    system.wifi_monitor.fd = -1;
    system.tc_netlink_fd = -1;
    system_child_process_defaults(&system.core_process);
    system_child_process_defaults(&system.helper_process);
    system_child_process_defaults(&system.action_process);
    char error[256U];
    int loaded_config = starsead_config_load(
        config_path, &system.loaded_config, error, sizeof(error));
    if (loaded_config != 0) {
        *has_early_result = true;
        enum starsead_control_result_code code =
            loaded_config == STARSEAD_CONFIG_UNSUPPORTED_COMBINATION
                ? STARSEAD_CONTROL_RESULT_UNSUPPORTED_COMBINATION
                : STARSEAD_CONTROL_RESULT_CONFIG_INVALID;
        (void)system_start_result(early_result, code,
            loaded_config == STARSEAD_CONFIG_UNSUPPORTED_COMBINATION
                ? "unsupported owner/core/mode combination" : "invalid runtime configuration", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return starsead_control_result_exit_code(code);
    }

    system.signal_fd = signalfd(-1, &blocked, SFD_NONBLOCK | SFD_CLOEXEC);
    system.signal_fd_owned = system.signal_fd >= 0;
    int listener = -1;
    enum starsead_control_listener_result listened = system.signal_fd_owned
        ? starsead_control_listener_open(&listener) : STARSEAD_CONTROL_LISTENER_ERROR;
    if (listened != STARSEAD_CONTROL_LISTENER_OK) {
        *has_early_result = true;
        if (listened == STARSEAD_CONTROL_LISTENER_IN_USE) {
            (void)system_probe_existing(early_result);
        } else {
            (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
                system.signal_fd_owned ? "control bind failed" : "signalfd initialization failed", NULL);
        }
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return starsead_control_result_exit_code(early_result->code);
    }

    struct starsead_clock_backend clock = {
        .local_time = system_runtime_local_time,
        .context = &system,
    };
    const unsigned char *secret = system.loaded_config.config.has_age_secret_key
        ? (const unsigned char *)system.loaded_config.config.age_secret_key : NULL;
    size_t secret_length = secret == NULL ? 0U : strlen((const char *)secret);
    if (starsead_log_open(&system.logger, system.loaded_config.config.log_path,
            secret, secret_length, &clock, error, sizeof(error)) != 0 ||
        starsead_state_store_init(&system.store, system.loaded_config.config.state_path,
            error, sizeof(error)) != STARSEAD_STATE_OK) {
        (void)close(listener);
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "runtime logger or state store initialization failed", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }

    int state_result = system_reset_telemetry_state(
        &system, error, sizeof(error));
    if (state_result != STARSEAD_STATE_OK) {
        (void)close(listener);
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "telemetry state initialization failed", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }

    system.live.supervisor_pid = (int)getpid();
    system.live.ipv6_enabled = system.loaded_config.config.enable_ipv6;
    time_t service_now = time(NULL);
    if (service_now == (time_t)-1) {
        (void)close(listener);
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "service control clock initialization failed", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }
    system.service_running = initial_start;
    starsead_service_control_init(&system.service_control,
        &system.loaded_config.config.service_control, initial_start, service_now);
    const struct starsead_service_control_config *service =
        &system.loaded_config.config.service_control;
    if (service->enabled && service->schedule.enabled) {
        system.service_timer_fd = timerfd_create(
            CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
        system.service_timer_fd_owned = system.service_timer_fd >= 0;
    }
    enum starsead_service_action wifi_baseline_action = STARSEAD_SERVICE_ACTION_NONE;
    if (service->enabled && service->wifi.enabled) {
        system.wifi_monitor_opened = starsead_wifi_monitor_open(
            &system.wifi_monitor, error, sizeof(error)) == 0;
    }
    if (system.wifi_monitor_opened) {
        enum starsead_wifi_transition transition;
        struct starsead_wifi_identity identity;
        if (starsead_wifi_monitor_baseline(
                &system.wifi_monitor, &transition, &identity) != 0) {
            system.wifi_monitor_opened = false;
        } else {
            wifi_baseline_action = starsead_service_control_on_wifi(
                &system.service_control, transition, &identity);
        }
    }
    if (service->enabled && service->keyguard.enabled &&
        starsead_keyguard_open(&system.keyguard_monitor, system_service_keyguard_changed,
            &system, error, sizeof(error)) != 0) {
        (void)close(listener);
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            error, NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }
    if ((service->enabled && service->schedule.enabled && !system.service_timer_fd_owned) ||
        (service->enabled && service->wifi.enabled && !system.wifi_monitor_opened) ||
        system_service_timer_arm(&system) != 0) {
        (void)close(listener);
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "service control event source initialization failed", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }
    struct starsead_control_callbacks callbacks = {
        .snapshot = system_runtime_snapshot,
        .request_stop = starsead_supervisor_runtime_request_stop,
        .request_shutdown = system_runtime_request_shutdown,
        .request_dns = starsead_supervisor_runtime_request_dns,
        .context = &system,
    };
    if (starsead_control_server_create(&system.control, listener, &callbacks) != 0) {
        (void)close(listener);
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "control server initialization failed", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }
    struct starsead_runtime_reactor_backend reactor_backend = system_runtime_reactor_backend();
    if (starsead_runtime_create(&system.runtime, &reactor_backend, &system) != 0) {
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            "reactor initialization failed", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }
    if (!system_admission_reconcile_ready(
            listened == STARSEAD_CONTROL_LISTENER_OK, system.runtime != NULL) ||
        starsead_supervisor_reconcile_owned_resources(&system, error, sizeof(error)) != 0) {
        *has_early_result = true;
        (void)system_start_result(early_result, STARSEAD_CONTROL_RESULT_START_FAILED,
            error[0] != '\0' ? error : "owned resource reconciliation failed", NULL);
        system_runtime_cleanup(&system);
        (void)sigprocmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }
    starsead_control_server_enable_accepting(system.control, true);
    struct starsead_runtime_effect_backend effects = system_runtime_effects(&system);
    int status = 0;
    bool should_start = initial_start;
    if (wifi_baseline_action == STARSEAD_SERVICE_ACTION_STOP) {
        should_start = false;
        system.service_running = false;
    } else if (wifi_baseline_action == STARSEAD_SERVICE_ACTION_START) {
        should_start = true;
    }
    while (!system.shutdown_requested) {
        if (should_start || system.service_start_requested) {
            if (!should_start) {
                if (system_reset_telemetry_state(
                        &system, error, sizeof(error)) != STARSEAD_STATE_OK) {
                    status = 1;
                    system.shutdown_requested = true;
                    break;
                }
            }
            should_start = false;
            system.service_start_requested = false;
            system_runtime_reset_cycle(&system);
            system.service_running = true;
            starsead_service_control_set_service_running(&system.service_control, true);
            int cycle_status = starsead_runtime_supervise(
                system.runtime, &system.loaded_config.config,
                &system.state, &system.live, &effects);
            system.service_running = false;
            system.manual_stop_pending = false;
            starsead_service_control_set_service_running(
                &system.service_control, system.service_start_requested);
            system_runtime_finish_control_stop(&system);
            if (cycle_status != 0) {
                if (system_cycle_failure_requires_shutdown(
                        service->enabled, system.runtime->lifecycle.stopped)) {
                    status = cycle_status;
                    system.shutdown_requested = true;
                }
            }
            if (!service->enabled) break;
            if (system.shutdown_requested) break;
            if (system_service_reconcile_time(&system) != 0) {
                status = 1;
                system.shutdown_requested = true;
            }
            continue;
        }
        if (!service->enabled) break;
        if (system_service_reconcile_time(&system) != 0) {
            status = 1;
            system.shutdown_requested = true;
            break;
        }
        if (system.service_start_requested) continue;
        struct starsead_runtime_delta delta;
        if (starsead_runtime_pump_once(system.runtime, NULL, &delta) != 0 ||
            (delta.flags & STARSEAD_DELTA_FATAL) != 0U) {
            status = 1;
            system.shutdown_requested = true;
        }
    }
    system_runtime_finish_control_stop(&system);
    starsead_control_server_close_listener(system.control);
    if (!starsead_control_server_drained(system.control)) {
        int64_t now = 0;
        if (starsead_supervisor_runtime_clock(&system, &now) == 0) {
            struct starsead_deadline deadline = {
                .armed = true,
                .monotonic_milliseconds = now + STARSEAD_CONTROL_WATCH_STALL_MILLIS,
            };
            struct starsead_runtime_delta ignored;
            (void)starsead_runtime_pump_until(system.runtime, &deadline,
                system_control_drained, &system, &ignored);
        }
    }
    system_runtime_cleanup(&system);
    (void)sigprocmask(SIG_SETMASK, &previous, NULL);
    return status;
}

int starsead_runtime_start_system(const char *config_path,
    bool *has_early_result, struct starsead_control_result *early_result) {
    return system_runtime_run(config_path, true, has_early_result, early_result);
}

int starsead_runtime_monitor_system(const char *config_path,
    bool *has_early_result, struct starsead_control_result *early_result) {
    return system_runtime_run(config_path, false, has_early_result, early_result);
}

#endif
