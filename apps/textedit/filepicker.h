#ifndef FILEPICKER_H
#define FILEPICKER_H

typedef void (*filepicker_callback_t)(const char *filename);

/* Show a simple file picker */
void filepicker_show(filepicker_callback_t callback);

#endif
