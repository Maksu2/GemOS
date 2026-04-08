#ifndef GEMOS_USERLAND_COMMON_TEXT_SURFACE_H
#define GEMOS_USERLAND_COMMON_TEXT_SURFACE_H

#include "format.h"

#include <gemos/console_abi.h>

#include <stdint.h>

static inline void gemos_text_surface_clear(gemos_console_cell_t *cells,
                                            uint32_t cols, uint32_t rows,
                                            uint8_t style) {
  uint32_t count = cols * rows;
  uint32_t index;

  if (cells == 0) {
    return;
  }

  for (index = 0; index < count; ++index) {
    cells[index].ch = ' ';
    cells[index].style = style;
    cells[index].flags = 0;
  }
}

static inline void gemos_text_surface_fill_row(gemos_console_cell_t *cells,
                                               uint32_t cols, uint32_t rows,
                                               uint32_t row, uint8_t style,
                                               char fill) {
  size_t index;
  size_t cell_index;

  if (cells == 0 || row >= rows || cols == 0U) {
    return;
  }

  cell_index = (size_t)row * cols;
  for (index = 0; index < cols; ++index) {
    cells[cell_index + index].ch = fill;
    cells[cell_index + index].style = style;
    cells[cell_index + index].flags = 0;
  }
}

static inline void gemos_text_surface_write_row(gemos_console_cell_t *cells,
                                                uint32_t cols, uint32_t rows,
                                                uint32_t row, uint8_t style,
                                                const char *text) {
  size_t index = 0;
  size_t length = 0;
  size_t cell_index;

  if (cells == 0 || row >= rows || cols == 0U) {
    return;
  }

  if (text != 0) {
    length = gemos_strlen(text);
  }

  cell_index = (size_t)row * cols;
  while (index < cols) {
    char ch = ' ';

    if (text != 0 && index < length) {
      ch = text[index];
    }

    cells[cell_index + index].ch = ch;
    cells[cell_index + index].style = style;
    cells[cell_index + index].flags = 0;
    index++;
  }
}

static inline void gemos_text_surface_write_text(gemos_console_cell_t *cells,
                                                 uint32_t cols, uint32_t rows,
                                                 uint32_t row, uint32_t col,
                                                 uint8_t style,
                                                 const char *text) {
  size_t index = 0;
  size_t length;
  size_t cell_index;

  if (cells == 0 || text == 0 || row >= rows || col >= cols) {
    return;
  }

  length = gemos_strlen(text);
  while (index < length && col + index < cols) {
    cell_index = (size_t)row * cols + col + index;
    cells[cell_index].ch = text[index];
    cells[cell_index].style = style;
    cells[cell_index].flags = 0;
    index++;
  }
}

static inline void
gemos_text_surface_write_centered(gemos_console_cell_t *cells, uint32_t cols,
                                  uint32_t rows, uint32_t row, uint8_t style,
                                  const char *text) {
  size_t length;
  uint32_t start_col;

  if (cells == 0 || text == 0 || row >= rows || cols == 0U) {
    return;
  }

  length = gemos_strlen(text);
  if (length >= cols) {
    start_col = 0;
  } else {
    start_col = (uint32_t)((cols - length) / 2U);
  }

  gemos_text_surface_write_text(cells, cols, rows, row, start_col, style, text);
}

static inline void gemos_text_surface_write_right(gemos_console_cell_t *cells,
                                                  uint32_t cols,
                                                  uint32_t rows,
                                                  uint32_t row,
                                                  uint32_t right_padding,
                                                  uint8_t style,
                                                  const char *text) {
  size_t length;
  uint32_t start_col;

  if (cells == 0 || text == 0 || row >= rows || cols == 0U) {
    return;
  }

  length = gemos_strlen(text);
  if (length + right_padding >= cols) {
    start_col = 0U;
  } else {
    start_col = cols - right_padding - (uint32_t)length;
  }

  gemos_text_surface_write_text(cells, cols, rows, row, start_col, style, text);
}

static inline void gemos_text_surface_write_hline(gemos_console_cell_t *cells,
                                                  uint32_t cols,
                                                  uint32_t rows,
                                                  uint32_t row, uint32_t col,
                                                  uint32_t length,
                                                  uint8_t style, char fill) {
  uint32_t index;
  size_t cell_index;

  if (cells == 0 || row >= rows || col >= cols || length == 0U) {
    return;
  }

  for (index = 0; index < length && col + index < cols; ++index) {
    cell_index = (size_t)row * cols + col + index;
    cells[cell_index].ch = fill;
    cells[cell_index].style = style;
    cells[cell_index].flags = 0;
  }
}

static inline void
gemos_text_surface_write_label_value(gemos_console_cell_t *cells,
                                     uint32_t cols, uint32_t rows,
                                     uint32_t row, uint32_t label_col,
                                     uint32_t value_col, uint8_t label_style,
                                     uint8_t value_style, const char *label,
                                     const char *value) {
  if (cells == 0 || row >= rows) {
    return;
  }

  gemos_text_surface_write_text(cells, cols, rows, row, label_col, label_style,
                                label);
  gemos_text_surface_write_text(cells, cols, rows, row, value_col, value_style,
                                value);
}

#endif /* GEMOS_USERLAND_COMMON_TEXT_SURFACE_H */
