#ifndef ELF_H
#define ELF_H

#include <stddef.h>
#include <stdint.h>

struct process;

int elf_load_into_process(struct process *process, const uint8_t *image,
                          size_t image_size);

#endif /* ELF_H */
