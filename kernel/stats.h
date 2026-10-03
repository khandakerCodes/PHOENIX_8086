/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Runtime Statistics Header
 */

#ifndef PHOENIX_STATS_H
#define PHOENIX_STATS_H

#include "../include/types.h"

/* Print runtime statistics to console */
void stats_print(void);

/* Get uptime in seconds (timer runs at HZ ticks per second) */
uint16_t stats_uptime_seconds(void);

#endif /* PHOENIX_STATS_H */
