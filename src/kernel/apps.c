#include "apps.h"
#include "../drivers/video.h"
#include "window.h"

// --- SNAKE GAME ---

typedef struct {
  int head_x, head_y;
  int dir_x, dir_y;
  int length;
  int body_x[100];
  int body_y[100];
  int apple_x, apple_y;
  int game_over;
} SnakeState;

static SnakeState snake_state;

void snake_reset() {
  snake_state.head_x = 10;
  snake_state.head_y = 10;
  snake_state.dir_x = 1;
  snake_state.dir_y = 0;
  snake_state.length = 3;
  snake_state.apple_x = 15;
  snake_state.apple_y = 10;
  snake_state.game_over = 0;
}

// --- SNAKE GAME ---
// Speed increase: frame % 3 instead of 5
// Wall collision: Explicitly set game_over

void snake_paint(Window *win) {
  draw_rect(win->x, win->y + 20, win->width, win->height - 20, 0x000000);

  if (snake_state.game_over) {
    draw_string(win->x + 80, win->y + 100, "GAME OVER", 0xFF0000);
    draw_string(win->x + 60, win->y + 120, "Press 'R' to Restart", 0xFFFFFF);
    return;
  }

  int cell = 10;
  // Apple
  draw_rect(win->x + snake_state.apple_x * cell,
            win->y + 25 + snake_state.apple_y * cell, cell, cell, 0xFF0000);

  // Snake
  for (int i = 0; i < snake_state.length; i++) {
    uint32_t col = (i == 0) ? 0x00FF00 : 0x008000;
    draw_rect(win->x + snake_state.body_x[i] * cell,
              win->y + 25 + snake_state.body_y[i] * cell, cell, cell, col);
  }

  static int frame = 0;
  frame++;
  // Speed: Every 3rd frame (Faster)
  if (frame % 3 == 0 && !snake_state.game_over) {
    for (int i = snake_state.length; i > 0; i--) {
      snake_state.body_x[i] = snake_state.body_x[i - 1];
      snake_state.body_y[i] = snake_state.body_y[i - 1];
    }
    snake_state.body_x[0] = snake_state.head_x;
    snake_state.body_y[0] = snake_state.head_y;

    snake_state.head_x += snake_state.dir_x;
    snake_state.head_y += snake_state.dir_y;

    // Wall Collision = Death
    if (snake_state.head_x < 0 || snake_state.head_x > 29 ||
        snake_state.head_y < 0 || snake_state.head_y > 19) {
      snake_state.game_over = 1;
    }

    // Self Collision
    for (int i = 1; i < snake_state.length; i++) {
      if (snake_state.head_x == snake_state.body_x[i] &&
          snake_state.head_y == snake_state.body_y[i]) {
        snake_state.game_over = 1;
      }
    }

    if (!snake_state.game_over) {
      if (snake_state.head_x == snake_state.apple_x &&
          snake_state.head_y == snake_state.apple_y) {
        snake_state.length++;
        snake_state.apple_x = (snake_state.apple_x + 7) % 30;
        snake_state.apple_y = (snake_state.apple_y + 3) % 20;
      }
    }
  }
}

// Input handled by snake_key below

void snake_key(Window *win, char c) {
  if (snake_state.game_over) {
    if (c == 'r' || c == 'R')
      snake_reset();
    return;
  }

  if (c == 'w' && snake_state.dir_y == 0) {
    snake_state.dir_x = 0;
    snake_state.dir_y = -1;
  }
  if (c == 's' && snake_state.dir_y == 0) {
    snake_state.dir_x = 0;
    snake_state.dir_y = 1;
  }
  if (c == 'a' && snake_state.dir_x == 0) {
    snake_state.dir_x = -1;
    snake_state.dir_y = 0;
  }
  if (c == 'd' && snake_state.dir_x == 0) {
    snake_state.dir_x = 1;
    snake_state.dir_y = 0;
  }
}

// --- NOTEPAD APP ---

// Old Code Removed. See bottom of file for new implementations.

// --- MINESWEEPER ---
#define MS_GRID_W 10
#define MS_GRID_H 10
#define MS_CELL_SIZE 20

typedef struct {
  int grid[MS_GRID_W][MS_GRID_H];    // 0-8: neighbors, 9: mine
  int visible[MS_GRID_W][MS_GRID_H]; // 0: hidden, 1: revealed, 2: flag
  int game_over;
} MineState;

static MineState ms_state;

static unsigned int ms_seed = 123;
int ms_rand() {
  ms_seed = ms_seed * 1103515245 + 12345;
  return (unsigned int)(ms_seed / 65536) % 32768;
}

void mine_reset() {
  // Clear
  for (int x = 0; x < MS_GRID_W; x++)
    for (int y = 0; y < MS_GRID_H; y++) {
      ms_state.grid[x][y] = 0;
      ms_state.visible[x][y] = 0;
    }
  ms_state.game_over = 0;

  // Place Mines (10 mines)
  int mines = 10;
  while (mines > 0) {
    int rx = ms_rand() % MS_GRID_W;
    int ry = ms_rand() % MS_GRID_H;
    if (ms_state.grid[rx][ry] != 9) {
      ms_state.grid[rx][ry] = 9;
      mines--;
    }
  }

  // Calculate Numbers
  for (int x = 0; x < MS_GRID_W; x++) {
    for (int y = 0; y < MS_GRID_H; y++) {
      if (ms_state.grid[x][y] == 9)
        continue;
      int count = 0;
      for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
          int nx = x + dx;
          int ny = y + dy;
          if (nx >= 0 && nx < MS_GRID_W && ny >= 0 && ny < MS_GRID_H) {
            if (ms_state.grid[nx][ny] == 9)
              count++;
          }
        }
      }
      ms_state.grid[x][y] = count;
    }
  }
}

void mine_reveal(int x, int y) {
  if (x < 0 || x >= MS_GRID_W || y < 0 || y >= MS_GRID_H)
    return;
  if (ms_state.visible[x][y])
    return;

  ms_state.visible[x][y] = 1;
  if (ms_state.grid[x][y] == 0) {
    // Flood fill
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        mine_reveal(x + dx, y + dy);
  }
}

// Reset Button Logic
#define MS_RESET_X(w) (w->x + (w->width / 2) - 10)
#define MS_RESET_Y(w) (w->y + 24)

void mine_paint(Window *win) {
  // Background
  draw_rect(win->x, win->y + 20, win->width, win->height - 20, 0xC0C0C0);

  // Reset Button (Smiley or 'R')
  int rx = MS_RESET_X(win);
  int ry = MS_RESET_Y(win);
  draw_rect(rx, ry, 20, 20, 0xE0E0E0);
  draw_rect(rx, ry, 20, 1, 0xFFFFFF);
  draw_string(rx + 6, ry + 6, ms_state.game_over ? "X" : ":)", 0x000000);

  for (int x = 0; x < MS_GRID_W; x++) {
    for (int y = 0; y < MS_GRID_H; y++) {
      int px = win->x + 10 + x * MS_CELL_SIZE;
      int py = win->y + 50 + y * MS_CELL_SIZE;

      // Draw Cell
      if (ms_state.visible[x][y] == 1) {
        draw_rect(px, py, MS_CELL_SIZE - 1, MS_CELL_SIZE - 1,
                  0xFFFFFF); // Revealed
        if (ms_state.grid[x][y] == 9) {
          draw_rect(px + 4, py + 4, 12, 12, 0x000000); // Mine
        } else if (ms_state.grid[x][y] > 0) {
          char buf[2] = {'0' + ms_state.grid[x][y], 0};
          draw_string(px + 6, py + 6, buf, 0x000000);
        }
      } else {
        // Hidden (Button style)
        draw_rect(px, py, MS_CELL_SIZE - 1, MS_CELL_SIZE - 1, 0x888888);
        draw_rect(px, py, MS_CELL_SIZE - 1, 1, 0xFFFFFF); // Light Top
        draw_rect(px, py, 1, MS_CELL_SIZE - 1, 0xFFFFFF); // Light Left

        if (ms_state.visible[x][y] == 2) {
          // Flag
          draw_rect(px + 5, py + 5, 10, 10, 0xFF0000);
        }
      }
    }
  }

  if (ms_state.game_over) {
    draw_string(win->x + 10, win->y + 260, "GAME OVER!", 0xFF0000);
  }
}

void mine_click(Window *win, int x, int y) {
  // Reset Button Click
  int rx = (win->width / 2) - 10;
  int ry = 24; // Relative to window content start of 0,0 but click event is 0,0
               // relative
  // Wait, on_click is relative X, Y.
  // Correct logic:
  if (x >= rx && x <= rx + 20 && y >= 24 && y <= 44) {
    mine_reset();
    return;
  }

  if (ms_state.game_over)
    return;

  // Correct offset
  int gx = (x - 10) / MS_CELL_SIZE;
  int gy = (y - 50) / MS_CELL_SIZE; // moved down for Reset btn

  if (gx >= 0 && gx < MS_GRID_W && gy >= 0 && gy < MS_GRID_H) {
    // Left click reveal
    if (ms_state.grid[gx][gy] == 9) {
      ms_state.game_over = 1;
      // Reveal All
      for (int i = 0; i < MS_GRID_W; i++)
        for (int j = 0; j < MS_GRID_H; j++)
          ms_state.visible[i][j] = 1;
    } else {
      mine_reveal(gx, gy);
    }
  }
}

void mine_key(Window *win, char c) {
  if (c == 'r')
    mine_reset();
}

void start_minesweeper() {
  mine_reset();
  Window *w = create_window(100, 100, 220, 260, "Minesweeper");
  if (w) {
    w->on_paint = mine_paint;
    w->on_click = mine_click;
    w->on_key = mine_key;
  }
}

// --- SOLITAIRE ---
// Simplified: 7 columns, 1 stock, 1 foundation (simplified).
// Cards: 0-51. Value = c % 13, Suit = c / 13.
// Suits: 0=H, 1=D, 2=C, 3=S
#define CARD_W 40
#define CARD_H 60

typedef struct {
  int stock[52];
  int stock_top;
  int waste[52];
  int waste_top;
  int tableau[7][20]; // up to 20 cards per col
  int tableau_cnt[7];
  int selected_col; // -1 none, 0-6 tableau, 10 waste
  int selected_idx;
} SolState;
static SolState sol;

void sol_init() {
  // Fill stock
  for (int i = 0; i < 52; i++)
    sol.stock[i] = i;
  // Shuffle
  for (int i = 0; i < 52; i++) {
    int r = ms_rand() % 52;
    int t = sol.stock[i];
    sol.stock[i] = sol.stock[r];
    sol.stock[r] = t;
  }

  sol.stock_top = 0; // Index of next card
  sol.waste_top = 0;

  // Deal
  for (int i = 0; i < 7; i++) {
    sol.tableau_cnt[i] = 0;
    for (int j = 0; j <= i; j++) { // i+1 cards
      sol.tableau[i][sol.tableau_cnt[i]++] = sol.stock[sol.stock_top++];
    }
  }
  sol.selected_col = -1;
}

void draw_card(Window *win, int x, int y, int card, int selected) {
  if (card == -1) {                                              // Back
    draw_rect(win->x + x, win->y + y, CARD_W, CARD_H, 0x000080); // Blue back
    draw_rect(win->x + x + 2, win->y + y + 2, CARD_W - 4, CARD_H - 4, 0x8080FF);
  } else {
    draw_rect(win->x + x, win->y + y, CARD_W, CARD_H,
              selected ? 0xFFFF00 : 0xFFFFFF);
    draw_rect(win->x + x, win->y + y, CARD_W, 1, 0x000000);
    draw_rect(win->x + x, win->y + y, 1, CARD_H, 0x000000);
    draw_rect(win->x + x + CARD_W, win->y + y, 1, CARD_H, 0x000000);
    draw_rect(win->x + x, win->y + y + CARD_H, CARD_W, 1, 0x000000);

    int val = card % 13;
    int suit = card / 13;
    uint32_t col = (suit < 2) ? 0xFF0000 : 0x000000;

    char v_char = '0';
    if (val == 0)
      v_char = 'A';
    else if (val == 9)
      v_char = 'X'; // 10
    else if (val == 10)
      v_char = 'J';
    else if (val == 11)
      v_char = 'Q';
    else if (val == 12)
      v_char = 'K';
    else
      v_char = '1' + val;

    char s_str[3] = {v_char, "HDCS"[suit], 0};
    draw_string(win->x + x + 5, win->y + y + 5, s_str, col);
  }
}

void sol_paint(Window *win) {
  // Green Felt
  draw_rect(win->x, win->y + 20, win->width, win->height - 20, 0x008000);

  // Stock (Top Left)
  if (sol.stock_top < 52)
    draw_card(win, 10, 40, -1, 0);
  else {
    draw_rect(win->x + 10, win->y + 40, CARD_W, CARD_H, 0x004000);
  }

  // Waste
  if (sol.waste_top > 0)
    draw_card(win, 60, 40, sol.waste[sol.waste_top - 1],
              (sol.selected_col == 10));

  // Tableau
  for (int i = 0; i < 7; i++) {
    int x = 10 + i * (CARD_W + 10);
    int y = 120;
    if (sol.tableau_cnt[i] == 0) {
      draw_rect(win->x + x, win->y + y, CARD_W, CARD_H, 0x006000);
    } else {
      for (int j = 0; j < sol.tableau_cnt[i]; j++) {
        int selected = (sol.selected_col == i && j >= sol.selected_idx);
        // Draw back if not last? Simplified: All revealed for MVP except maybe
        // bottom? Real Solitaire: Only top is revealed. Let's reveal all for
        // simplicity or only top. Reveal ONLY TOP for "gameplay" challenge? Too
        // hard without logic. Reveal ALL.
        draw_card(win, x, y + j * 15, sol.tableau[i][j], selected);
      }
    }
  }
}

void sol_click(Window *win, int x, int y) {
  // 0. Stock click
  if (y >= 40 && y <= 40 + CARD_H && x >= 10 && x <= 10 + CARD_W) {
    if (sol.stock_top < 52) {
      sol.waste[sol.waste_top++] = sol.stock[sol.stock_top++];
      sol.selected_col = -1;
    } else {
      // Reset stock
      sol.stock_top = 0;
      sol.waste_top = 0; // Simplified reset (cheat: lost cards if not cycled
                         // logic correct) Real logic: move waste back to stock.
      // Impl: TODO. Just refill.
      sol_init(); // Reset game
    }
    return;
  }

  // 1. Waste click
  if (y >= 40 && y <= 40 + CARD_H && x >= 60 && x <= 60 + CARD_W) {
    if (sol.waste_top > 0) {
      sol.selected_col = 10;
    }
    return;
  }

  // 2. Tableau click
  for (int i = 0; i < 7; i++) {
    int tx = 10 + i * (CARD_W + 10);
    // Hit test simplified: entire column
    if (x >= tx && x <= tx + CARD_W && y >= 120) {
      // Clicked column i
      if (sol.selected_col != -1) {
        // Try Move
        if (sol.selected_col == 10) { // From Waste
          // Move to Col i
          sol.tableau[i][sol.tableau_cnt[i]++] = sol.waste[--sol.waste_top];
          sol.selected_col = -1;
        } else if (sol.selected_col != i) { // From Tableau
          // Move stack
          int src = sol.selected_col;
          int idx = sol.selected_idx;
          // Move idx..end to dest
          for (int k = idx; k < sol.tableau_cnt[src]; k++) {
            sol.tableau[i][sol.tableau_cnt[i]++] = sol.tableau[src][k];
          }
          sol.tableau_cnt[src] = idx;
          sol.selected_col = -1;
        } else {
          sol.selected_col = -1; // Deselect
        }
      } else {
        // Select
        if (sol.tableau_cnt[i] > 0) {
          sol.selected_col = i;
          // Select Top Card (simplest)
          sol.selected_idx = sol.tableau_cnt[i] - 1;
        }
      }
    }
  }
}

void start_solitaire() {
  sol_init();
  Window *w = create_window(150, 150, 400, 400, "Solitaire");
  if (w) {
    w->on_paint = sol_paint;
    w->on_click = sol_click;
  }
}

// --- CALCULATOR APP ---
// Simple stack or accumulator based.
// We will use a simple accumulator + op model.

static int calc_acc = 0;
static int calc_current = 0;
static int calc_op = 0; // 0=None, 1=+, 2=-, 3=*, 4=/
static bool calc_new_entry = true;

void calc_reset() {
  calc_acc = 0;
  calc_current = 0;
  calc_op = 0;
  calc_new_entry = true;
}

// Integer Sqrt (Newton)
int isqrt(int n) {
  if (n < 0)
    return 0;
  if (n == 0)
    return 0;
  int x = n;
  int y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return x;
}

// Revised Calc Click (New)
void calc_click(Window *win, int x, int y) {
  // Grid layout
  char key = 0;

  // Check main grid (10,50) to (150, 190)
  if (x >= 10 && x < 150 && y >= 50 && y < 190) {
    int c = (x - 10) / 35;
    int r = (y - 50) / 35;
    char *keys = "789/456*123-C0=+";
    int idx = r * 4 + c;
    if (idx >= 0 && idx < 16)
      key = keys[idx];
  }

  // Check Sci Column
  if (x >= 150 && x < 200 && y >= 50) {
    int r = (y - 50) / 35;
    if (r == 0)
      key = 'S'; // Sq
    if (r == 1)
      key = 'R'; // Root
  }

  if (key) {
    if (key >= '0' && key <= '9') {
      if (calc_new_entry) {
        calc_current = 0;
        calc_new_entry = false;
      }
      calc_current = calc_current * 10 + (key - '0');
    } else if (key == 'C') {
      calc_reset();
    } else if (key == 'S') { // Square
      calc_current = calc_current * calc_current;
      calc_new_entry = true;
    } else if (key == 'R') { // Root
      calc_current = isqrt(calc_current);
      calc_new_entry = true;
    } else if (key == '=') {
      if (calc_op == 1)
        calc_acc += calc_current;
      if (calc_op == 2)
        calc_acc -= calc_current;
      if (calc_op == 3)
        calc_acc *= calc_current;
      if (calc_op == 4) {
        if (calc_current)
          calc_acc /= calc_current;
      }
      calc_current = calc_acc;
      calc_op = 0;
      calc_new_entry = true;
    } else {
      calc_acc = calc_current;
      calc_current = 0;
      if (key == '+')
        calc_op = 1;
      if (key == '-')
        calc_op = 2;
      if (key == '*')
        calc_op = 3;
      if (key == '/')
        calc_op = 4;
      calc_new_entry = true;
    }
  }
}

void calc_paint(Window *win) {
  // Screen
  draw_rect(win->x + 10, win->y + 10, 180, 30, 0x000000);
  draw_rect(win->x + 12, win->y + 12, 176, 26, 0xC0D0C0);

  // Draw Number
  char buf[32];
  int v = calc_new_entry && calc_op ? calc_acc : calc_current;

  if (v == 0) {
    buf[0] = '0';
    buf[1] = 0;
  } else {
    int i = 0;
    int n = v;
    if (n < 0) {
      buf[0] = '-';
      i++;
      n = -n;
    }
    char tmp[16];
    int ti = 0;
    while (n > 0) {
      tmp[ti++] = (n % 10) + '0';
      n /= 10;
    }
    while (ti > 0)
      buf[i++] = tmp[--ti];
    buf[i] = 0;
  }

  draw_string(win->x + 20, win->y + 20, buf, 0x000000);

  // Buttons
  char *labels = "789/456*123-C0=+SRP "; // S, R, P(unused), space
  // Actually drawing logic:

  // Main Grid
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int bx = win->x + 10 + c * 35;
      int by = win->y + 50 + r * 35;
      char lbl[2] = {labels[r * 4 + c], 0};
      draw_rect(bx, by, 30, 30, 0x808080);
      draw_rect(bx, by, 28, 28, 0xFFFFFF);
      draw_string(bx + 10, by + 10, lbl, 0x000000);
    }
  }
  // Sci Cols
  draw_rect(win->x + 150, win->y + 50, 30, 30, 0x808080);
  draw_rect(win->x + 150, win->y + 50, 28, 28, 0xFFFFFF);
  draw_string(win->x + 160, win->y + 60, "S", 0); // Sq

  draw_rect(win->x + 150, win->y + 85, 30, 30, 0x808080);
  draw_rect(win->x + 150, win->y + 85, 28, 28, 0xFFFFFF);
  draw_string(win->x + 160, win->y + 95, "R", 0); // Root
}

void start_calculator() {
  calc_reset();
  Window *w = create_window(150, 150, 200, 200, "Calc");
  if (w) {
    w->on_paint = calc_paint;
    w->on_click = calc_click;
  }
}
// --- NOTEPAD (Enhanced with Scroll) ---
typedef struct {
  char buffer[512];
  int cursor;
  int scroll_y; // Pixel offset
} NotepadState;

static NotepadState note_state;

void note_paint(Window *win) {
  // White background
  draw_rect(win->x + 2, win->y + 22, win->width - 4, win->height - 24,
            0xFFFFFF);

  int x = win->x + 8;
  int start_y = win->y + 28 - note_state.scroll_y;
  int y = start_y;
  int line_height = 12;
  int max_width = win->width - 16;
  int current_w = 0;

  int cursor_x = x;
  int cursor_y = y;

  char *p = note_state.buffer;
  int idx = 0;

  int min_y = win->y + 22;
  int max_y = win->y + win->height - 5;

  while (*p) {
    char c = *p;

    if (idx == note_state.cursor) {
      cursor_x = x + current_w;
      cursor_y = y;
    }

    // Draw Logic with "Clipping"
    int draw = (y >= min_y && y <= max_y);

    if (c == '\n') {
      y += line_height;
      current_w = 0;
    } else {
      if (draw) {
        char buf[2] = {c, 0};
        draw_string(x + current_w, y, buf, 0x000000);
      }
      current_w += 8;
      if (current_w >= max_width) {
        y += line_height;
        current_w = 0;
      }
    }
    p++;
    idx++;
  }

  if (idx == note_state.cursor) {
    cursor_x = x + current_w;
    cursor_y = y;
  }

  // Auto-scroll logic
  if (cursor_y > max_y)
    note_state.scroll_y += line_height;
  if (cursor_y < min_y)
    note_state.scroll_y -= line_height;
  if (note_state.scroll_y < 0)
    note_state.scroll_y = 0;

  // Draw Cursor (if visible)
  if (cursor_y >= min_y && cursor_y <= max_y)
    draw_rect(cursor_x, cursor_y, 2, 10, 0x000000);
}

void note_key(Window *win, char c) {
  if (c == '\b') {
    if (note_state.cursor > 0) {
      // Shift left
      for (int i = note_state.cursor; i < 512; i++) {
        note_state.buffer[i - 1] = note_state.buffer[i];
      }
      note_state.cursor--;
    }
  } else if (c == '\n') { // Enter
    if (note_state.cursor < 511) {
      // Shift right
      for (int i = 511; i > note_state.cursor; i--) {
        note_state.buffer[i] = note_state.buffer[i - 1];
      }
      note_state.buffer[note_state.cursor++] = '\n';
    }
  } else if (c >= 32 && c <= 126) {
    if (note_state.cursor < 511) {
      // Insert mode: Shift right
      for (int i = 511; i > note_state.cursor; i--) {
        note_state.buffer[i] = note_state.buffer[i - 1];
      }
      note_state.buffer[note_state.cursor++] = c;
    }
  }
}

// --- PAINT APP ---
#define PAINT_W 200
#define PAINT_H 150
static uint8_t paint_canvas[PAINT_W * PAINT_H];

void paint_reset() {
  for (int i = 0; i < PAINT_W * PAINT_H; i++)
    paint_canvas[i] = 0;
}

void paint_paint(Window *win) {
  // Canvas Bg
  draw_rect(win->x + 10, win->y + 30, PAINT_W, PAINT_H, 0xFFFFFF);
  draw_rect(win->x + 10, win->y + 30, PAINT_W, 1, 0x000000);
  draw_rect(win->x + 10, win->y + 30, 1, PAINT_H, 0x000000);
  draw_rect(win->x + 10 + PAINT_W, win->y + 30, 1, PAINT_H, 0x000000);
  draw_rect(win->x + 10, win->y + 30 + PAINT_H, PAINT_W, 1, 0x000000);

  // Draw Pixels
  for (int y = 0; y < PAINT_H; y++) {
    for (int x = 0; x < PAINT_W; x++) {
      if (paint_canvas[y * PAINT_W + x]) {
        int screen_x = win->x + 10 + x;
        int screen_y = win->y + 30 + y;
        if (screen_x < 1024 && screen_y < 768)
          draw_rect(screen_x, screen_y, 2, 2, 0x000000);
      }
    }
  }
  draw_string(win->x + 10, win->y + 190, "Drag to Draw. C: Clear", 0x000000);
}

// Unified handler for click and move
void paint_draw_action(int lx, int ly) {
  int cx = lx - 10;
  int cy = ly - 30;

  if (cx >= 0 && cx < PAINT_W && cy >= 0 && cy < PAINT_H) {
    paint_canvas[cy * PAINT_W + cx] = 1;
    if (cx + 1 < PAINT_W)
      paint_canvas[cy * PAINT_W + cx + 1] = 1;
    if (cy + 1 < PAINT_H)
      paint_canvas[(cy + 1) * PAINT_W + cx] = 1;
  }
}

void paint_click(Window *win, int x, int y) { paint_draw_action(x, y); }

void paint_mouse_move(Window *win, int x, int y, int b) {
  if (b & 1)
    paint_draw_action(x, y);
}

void paint_key(Window *win, char c) {
  if (c == 'c' || c == 'C')
    paint_reset();
}

void start_paint() {
  Window *w = create_window(250, 250, 240, 220, "Paint");
  if (w) {
    w->on_paint = paint_paint;
    w->on_click = paint_click;
    w->on_mouse_move = paint_mouse_move;
    w->on_key = paint_key;
  }
}

// --- ABOUT APP ---

void about_paint(Window *win) {
  draw_rect(win->x + 20, win->y + 40, 60, 60, 0x000000);
  draw_rect(win->x + 22, win->y + 42, 56, 56, 0xFFFFFF);
  draw_rect(win->x + 35, win->y + 55, 30, 30, 0x404040);

  draw_string(win->x + 100, win->y + 40, "GemOS 1.0", 0x000000);
  draw_string(win->x + 100, win->y + 60, "GemCore Kernel / GemShell", 0x000000);
  draw_string(win->x + 100, win->y + 80, "(c) 2026 GemOS Project", 0x000000);
  draw_string(win->x + 100, win->y + 110, "Stable R1 Release", 0x808080);
}

void start_about() {
  Window *w = create_window(250, 200, 400, 200, "About GemOS");
  if (w) {
    w->on_paint = about_paint;
  }
}

void start_snake() {
  snake_reset();
  Window *w = create_window(200, 150, 320, 240, "Snake");
  if (w) {
    w->on_paint = snake_paint;
    w->on_key = snake_key;
  }
}

void start_notepad() {
  note_state.cursor = 0;
  note_state.buffer[0] = 0;
  note_state.scroll_y = 0;
  Window *w = create_window(50, 400, 300, 200, "Notes");
  if (w) {
    w->on_paint = note_paint;
    w->on_key = note_key;
  }
}

// --- SETTINGS APP ---
// Global externs from window.h
// extern uint32_t theme_desktop;

void settings_paint(Window *win) {
  draw_string(win->x + 20, win->y + 35, "Select System Theme:", 0x000000);

  // Theme 1: Slate
  draw_rect(win->x + 20, win->y + 60, 30, 30, 0x405060);
  draw_string(win->x + 60, win->y + 70, "Professional Slate", 0x000000);

  // Theme 2: Forest
  draw_rect(win->x + 20, win->y + 100, 30, 30, 0x004000);
  draw_string(win->x + 60, win->y + 110, "Forest Green", 0x000000);

  // Theme 3: Crimson
  draw_rect(win->x + 20, win->y + 140, 30, 30, 0x600000);
  draw_string(win->x + 60, win->y + 150, "Crimson Red", 0x000000);

  // Theme 4: High Contrast
  draw_rect(win->x + 20, win->y + 180, 30, 30, 0x000000);
  draw_string(win->x + 60, win->y + 190, "High Contrast", 0x000000);
}

void settings_click(Window *win, int x, int y) {
  // Check buttons
  if (x >= 20 && x <= 50) {
    if (y >= 60 && y <= 90)
      theme_desktop = 0x405060;
    if (y >= 100 && y <= 130)
      theme_desktop = 0x004000;
    if (y >= 140 && y <= 170)
      theme_desktop = 0x600000;
    if (y >= 180 && y <= 210)
      theme_desktop = 0x000000;
  }
}

void start_settings() {
  Window *w = create_window(300, 300, 250, 250, "Settings");
  if (w) {
    w->on_paint = settings_paint;
    w->on_click = settings_click;
  }
}

#include "gemlang.h"

void init_apps() {
  start_about();
  load_extension_apps();
}

void start_paint_wrapper() { start_paint(); }
void start_settings_wrapper() { start_settings(); }
