/**
 * GemOS icons: 32x32 ARGB images defined in kernel/gfx/icons.c.
 */

#ifndef ICONS_H
#define ICONS_H

#include "primitives.h"

/* Applications (dock, launchers, hosted-app console) */
extern const icon_t icon_terminal;
extern const icon_t icon_about;
extern const icon_t icon_textedit;

/* Dock fallback for apps without an icon */
extern const icon_t icon_missing;

/* File explorer */
extern const icon_t icon_folder;
extern const icon_t icon_textfile;
extern const icon_t icon_generic_file;

#endif /* ICONS_H */
