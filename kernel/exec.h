/* SPDX-License-Identifier: MIT */
/*
 * Phoenix-8086 — Program Loader Header
 */

#ifndef PHOENIX_EXEC_H
#define PHOENIX_EXEC_H

#include "../include/types.h"

/* Priority given to programs started with exec_program */
#define EXEC_PRIORITY   6

/* Why a program could not be started */
#define EXEC_OK             0
#define EXEC_NOT_FOUND      1
#define EXEC_BAD_FORMAT     2
#define EXEC_NO_MEMORY      3
#define EXEC_READ_ERROR     4
#define EXEC_NO_THREAD      5

/*
 * Load a program file from the boot disk and start it as a thread.
 * Returns the thread ID, or -1 with the reason in *error (if not NULL).
 */
int exec_program(const char *name, uint8_t *error);

/* Text for an EXEC_ error code */
const char *exec_error_text(uint8_t error);

#endif /* PHOENIX_EXEC_H */
