#ifndef WINDOW_TEST_H
#define WINDOW_TEST_H

#include "../kernel/gfx/context.h"

/* Initialize the Window Test (Create 3 windows) */
void window_test_init(gfx_context_t *ctx);

/* Update loop for validation (Process events, render) */
void window_test_update(void);

#endif /* WINDOW_TEST_H */
