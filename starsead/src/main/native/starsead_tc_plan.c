#include "starsead.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t capacity, const char *format, ...) {
    if (error == NULL || capacity == 0U) return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, capacity, format, arguments);
    va_end(arguments);
}

static bool interface_name_valid(const char *name) {
    if (name == NULL) return false;
    size_t length = strnlen(name, STARSEAD_MAX_INTERFACE_NAME);
    if (length == 0U || length >= STARSEAD_MAX_INTERFACE_NAME ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        strcmp(name, "all") == 0 || strcmp(name, "default") == 0 ||
        strcmp(name, "lo") == 0) return false;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char value = (unsigned char)name[index];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-' || value == '.')) {
            return false;
        }
    }
    return true;
}

static bool slot_valid(enum starsead_tc_slot_state slot) {
    return slot >= STARSEAD_TC_SLOT_ABSENT && slot <= STARSEAD_TC_SLOT_FOREIGN;
}

enum starsead_tc_qdisc_cleanup_decision starsead_tc_qdisc_cleanup_decide(
    bool ingress_occupied, bool egress_occupied) {
    return ingress_occupied || egress_occupied
        ? STARSEAD_TC_QDISC_CLEANUP_RETAIN_SHARED
        : STARSEAD_TC_QDISC_CLEANUP_DELETE;
}

bool starsead_tc_qdisc_cleanup_restored(bool qdisc_present,
    enum starsead_tc_qdisc_cleanup_decision decision) {
    return !qdisc_present || decision == STARSEAD_TC_QDISC_CLEANUP_RETAIN_SHARED;
}

static struct starsead_tc_plan_operation *append_operation(
    struct starsead_tc_plan *plan, enum starsead_tc_plan_operation_kind kind,
    enum starsead_resource_operation_kind operation_kind) {
    if (plan->operation_count >= sizeof(plan->operations) / sizeof(plan->operations[0])) return NULL;
    struct starsead_tc_plan_operation *operation = &plan->operations[plan->operation_count++];
    memset(operation, 0, sizeof(*operation));
    operation->kind = kind;
    operation->operation.kind = operation_kind;
    return operation;
}

static void copy_interface(char destination[STARSEAD_MAX_INTERFACE_NAME], const char *source) {
    (void)snprintf(destination, STARSEAD_MAX_INTERFACE_NAME, "%s", source);
}

static int append_route_localnet(
    struct starsead_tc_plan *plan, const struct starsead_tc_interface_probe *probe) {
    struct starsead_tc_plan_operation *operation = append_operation(
        plan, STARSEAD_TC_PLAN_SET_ROUTE_LOCALNET, STARSEAD_RESOURCE_OPERATION_SYSCTL);
    if (operation == NULL) return -1;
    struct starsead_sysctl_resource *resource = &operation->operation.resource.sysctl;
    resource->sysctl_id = STARSEAD_SYSCTL_ROUTE_LOCALNET;
    copy_interface(resource->interface_name, probe->interface_name);
    resource->interface_index = probe->interface_index;
    resource->original_value = 0U;
    resource->desired_value = 1U;
    return 0;
}

static int append_qdisc(
    struct starsead_tc_plan *plan, const struct starsead_tc_interface_probe *probe) {
    struct starsead_tc_plan_operation *operation = append_operation(
        plan, STARSEAD_TC_PLAN_CREATE_CLSACT, STARSEAD_RESOURCE_OPERATION_TC_QDISC);
    if (operation == NULL) return -1;
    struct starsead_tc_qdisc_resource *resource = &operation->operation.resource.tc_qdisc;
    resource->qdisc_id = STARSEAD_QDISC_HOTSPOT_CLSACT;
    copy_interface(resource->interface_name, probe->interface_name);
    resource->interface_index = probe->interface_index;
    resource->original_presence = false;
    return 0;
}

static int append_filter(
    struct starsead_tc_plan *plan, const struct starsead_tc_interface_probe *probe,
    enum starsead_tc_direction direction) {
    bool ingress = direction == STARSEAD_TC_DIRECTION_INGRESS;
    struct starsead_tc_plan_operation *operation = append_operation(plan,
        ingress ? STARSEAD_TC_PLAN_CREATE_INGRESS : STARSEAD_TC_PLAN_CREATE_EGRESS,
        STARSEAD_RESOURCE_OPERATION_TC_FILTER);
    if (operation == NULL) return -1;
    operation->priority = STARSEAD_HOTSPOT_TC_PRIORITY;
    operation->handle = STARSEAD_HOTSPOT_TC_HANDLE;
    operation->direct_action = true;
    struct starsead_tc_filter_resource *resource = &operation->operation.resource.tc_filter;
    resource->filter_id = ingress ? STARSEAD_FILTER_HOTSPOT_INGRESS :
        STARSEAD_FILTER_HOTSPOT_EGRESS;
    resource->direction = direction;
    copy_interface(resource->interface_name, probe->interface_name);
    resource->interface_index = probe->interface_index;
    resource->program_id = ingress ? STARSEAD_PROGRAM_BPF2SOCKS_INGRESS :
        STARSEAD_PROGRAM_BPF2SOCKS_EGRESS;
    resource->original_presence = false;
    return 0;
}

int starsead_tc_install_plan_build(
    const struct starsead_config *config, const struct starsead_tc_interface_probe *probe,
    struct starsead_tc_plan *plan, char *error, size_t error_capacity) {
    if (plan == NULL) {
        set_error(error, error_capacity, "tc plan output is required");
        return -1;
    }
    memset(plan, 0, sizeof(*plan));
    if (config == NULL || probe == NULL || config->mode != STARSEAD_MODE_BPF2SOCKS ||
        config->helper.type != STARSEAD_HELPER_BPF2SOCKS ||
        !interface_name_valid(probe->interface_name) || probe->interface_index == 0U ||
        probe->route_localnet_value > 1U || !slot_valid(probe->qdisc) ||
        !slot_valid(probe->egress) || !slot_valid(probe->ingress)) {
        set_error(error, error_capacity, "invalid bpf2socks TC probe");
        return -1;
    }
    if (probe->qdisc == STARSEAD_TC_SLOT_FOREIGN ||
        probe->egress == STARSEAD_TC_SLOT_FOREIGN ||
        probe->ingress == STARSEAD_TC_SLOT_FOREIGN ||
        probe->egress == STARSEAD_TC_SLOT_COMPATIBLE ||
        probe->ingress == STARSEAD_TC_SLOT_COMPATIBLE) {
        set_error(error, error_capacity, "foreign TC resource collision");
        return -1;
    }
    if ((probe->egress == STARSEAD_TC_SLOT_OWNED ||
         probe->ingress == STARSEAD_TC_SLOT_OWNED) &&
        probe->qdisc == STARSEAD_TC_SLOT_ABSENT) {
        set_error(error, error_capacity, "TC filter exists without clsact");
        return -1;
    }
    if (probe->route_localnet_value == 0U && append_route_localnet(plan, probe) != 0) goto failed;
    if (probe->qdisc == STARSEAD_TC_SLOT_ABSENT && append_qdisc(plan, probe) != 0) goto failed;
    if (probe->egress == STARSEAD_TC_SLOT_ABSENT &&
        append_filter(plan, probe, STARSEAD_TC_DIRECTION_EGRESS) != 0) goto failed;
    if (probe->ingress == STARSEAD_TC_SLOT_ABSENT &&
        append_filter(plan, probe, STARSEAD_TC_DIRECTION_INGRESS) != 0) goto failed;
    return 0;

failed:
    memset(plan, 0, sizeof(*plan));
    set_error(error, error_capacity, "too many TC plan operations");
    return -1;
}

int starsead_tc_cleanup_plan_build(
    const struct starsead_tc_plan *install, struct starsead_tc_plan *cleanup) {
    if (cleanup == NULL) return -1;
    memset(cleanup, 0, sizeof(*cleanup));
    if (install == NULL || install->operation_count >
        sizeof(install->operations) / sizeof(install->operations[0])) return -1;
    for (size_t index = install->operation_count; index > 0U; --index) {
        const struct starsead_tc_plan_operation *source = &install->operations[index - 1U];
        struct starsead_tc_plan_operation *destination =
            &cleanup->operations[cleanup->operation_count++];
        *destination = *source;
        switch (source->kind) {
            case STARSEAD_TC_PLAN_SET_ROUTE_LOCALNET:
                destination->kind = STARSEAD_TC_PLAN_RESTORE_ROUTE_LOCALNET;
                break;
            case STARSEAD_TC_PLAN_CREATE_CLSACT:
                destination->kind = STARSEAD_TC_PLAN_REMOVE_CLSACT;
                break;
            case STARSEAD_TC_PLAN_CREATE_EGRESS:
                destination->kind = STARSEAD_TC_PLAN_REMOVE_EGRESS;
                break;
            case STARSEAD_TC_PLAN_CREATE_INGRESS:
                destination->kind = STARSEAD_TC_PLAN_REMOVE_INGRESS;
                break;
            default:
                memset(cleanup, 0, sizeof(*cleanup));
                return -1;
        }
    }
    return 0;
}
