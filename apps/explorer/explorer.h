#ifndef EXPLORER_H
#define EXPLORER_H

#include "../../kernel/app/app.h"

void explorer_init(void);
/* Publish completed directory reads and request a fresh view from the GUI task. */
void explorer_update(void);

#ifdef GEMOS_SELFTEST
bool explorer_listing_pending(void);
bool explorer_has_current_entry(const char *name);
#endif

#endif
