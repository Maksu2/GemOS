#ifndef INPUTBOX_H
#define INPUTBOX_H

typedef void (*inputbox_callback_t)(const char *result);

/* Show a simple modal input box */
void inputbox_show(const char *title, const char *initial_text,
                   inputbox_callback_t callback);

#endif
