// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0

#ifndef STARSEAD_COMPAT_H
#define STARSEAD_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct starsead_bpf_pin_probe_backend {
    void *context;
    int (*inspect_path)(void *, const char *, bool *);
    int (*open_pinned)(void *, const char *, int *);
    int (*read_object_id)(void *, int, uint64_t *);
    int (*close_fd)(void *, int);
};

int starsead_bpf_pin_probe_with_backend(
    const struct starsead_bpf_pin_probe_backend *,
    const char *, bool *, uint64_t *);

struct starsead_tun_compat_paths {
    const char *tun_device;
    const char *network_directory;
    const char *compatibility_path;
};

struct starsead_tun_compat_state {
    bool directory_created;
    bool link_created;
    uint64_t link_device;
    uint64_t link_inode;
};

void starsead_tun_compat_state_init(struct starsead_tun_compat_state *);
int starsead_tun_compat_prepare(
    const struct starsead_tun_compat_paths *,
    struct starsead_tun_compat_state *, char *, size_t);
int starsead_tun_compat_cleanup(
    const struct starsead_tun_compat_paths *,
    struct starsead_tun_compat_state *, char *, size_t);

#endif
