// Copyright 2026, Asterisk4Magisk contributors
// SPDX-License-Identifier: GPL-3.0
#ifndef STARSEAD_KEYGUARD_H
#define STARSEAD_KEYGUARD_H
#include <stdbool.h>
#include <stddef.h>
struct starsead_keyguard_monitor;
typedef void (*starsead_keyguard_changed)(void *, bool locked, bool baseline);
int starsead_keyguard_open(struct starsead_keyguard_monitor **,
    starsead_keyguard_changed, void *, char *, size_t);
int starsead_keyguard_fd(const struct starsead_keyguard_monitor *);
int starsead_keyguard_dispatch(struct starsead_keyguard_monitor *);
void starsead_keyguard_close(struct starsead_keyguard_monitor *);
int starsead_keyguard_resolve_ids(int ids[3]);
int starsead_keyguard_ids_from_dex(const void *, size_t, int ids[3]);
#endif
