#include "gemlang.h"
#include "../drivers/video.h"
#include "apps.h"
#include "window.h"

// --- Utils ---
int str_eq(char *a, char *b) {
  if (!a || !b)
    return 0;
  while (*a && *b) {
    if (*a != *b)
      return 0;
    a++;
    b++;
  }
  return (*a == *b);
}

int str_to_int(char *s) {
  int res = 0;
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    res = res * 10 + (*s - '0');
    s++;
  }
  return res * sign;
}

void int_to_str(int v, char *buf) {
  if (v == 0) {
    buf[0] = '0';
    buf[1] = 0;
    return;
  }
  int i = 0, n = v;
  if (v < 0)
    n = -n;
  while (n > 0) {
    buf[i++] = (n % 10) + '0';
    n /= 10;
  }
  if (v < 0)
    buf[i++] = '-';
  buf[i] = 0;
  for (int j = 0; j < i / 2; j++) {
    char t = buf[j];
    buf[j] = buf[i - 1 - j];
    buf[i - 1 - j] = t;
  }
}

void str_cat_safe(char *dest, char *src) {
  int i = 0;
  while (dest[i])
    i++;
  while (*src)
    dest[i++] = *src++;
  dest[i] = 0;
}

// --- Tokenizer ---
#define MAX_TOKENS 800
#define TOKEN_LEN 64
char tokens[MAX_TOKENS][TOKEN_LEN];
int token_count = 0;

void tokenize(char *script) {
  token_count = 0;
  char *p = script;
  while (*p && token_count < MAX_TOKENS) {
    while (*p == ' ' || *p == '\n' || *p == '\t')
      p++;
    if (!*p)
      break;

    // Delimiters
    if (*p == '{' || *p == '}' || *p == '(' || *p == ')' || *p == ':' ||
        *p == '.' || *p == '=' || *p == '+' || *p == '-') {
      tokens[token_count][0] = *p;
      tokens[token_count][1] = 0;
      p++;
      token_count++;
      continue;
    }

    // String
    if (*p == '"') {
      p++;
      int i = 0;
      while (*p && *p != '"' && i < TOKEN_LEN - 1) {
        tokens[token_count][i++] = *p++;
      }
      tokens[token_count][i] = 0;
      if (*p == '"')
        p++;
      token_count++;
      continue;
    }

    // Word/Number
    int i = 0;
    while (*p && *p > 32 && *p != '{' && *p != '}' && *p != '(' && *p != ')' &&
           *p != ':' && *p != '.' && *p != '=' && *p != '+' && *p != '-' &&
           *p != '"' && i < TOKEN_LEN - 1) {
      tokens[token_count][i++] = *p++;
    }
    tokens[token_count][i] = 0;
    token_count++;
  }
}

// --- Interpreter Context ---
#define MAX_VARS 30
typedef struct {
  char name[32];
  int int_val;
  char str_val[32];
  int type; // 0=Int, 1=String
} GemVar;

GemVar g_vars[MAX_VARS];
int g_var_count = 0;

GemVar *find_var(char *name) {
  for (int i = 0; i < g_var_count; i++) {
    if (str_eq(g_vars[i].name, name))
      return &g_vars[i];
  }
  return 0;
}

void set_var_int(char *name, int v) {
  GemVar *fv = find_var(name);
  if (!fv) {
    if (g_var_count >= MAX_VARS)
      return;
    fv = &g_vars[g_var_count++];
    int i = 0;
    while (name[i]) {
      fv->name[i] = name[i];
      i++;
    }
    fv->name[i] = 0;
  }
  fv->type = 0;
  fv->int_val = v;
  fv->str_val[0] = 0;
}

void set_var_str(char *name, char *s) {
  GemVar *fv = find_var(name);
  if (!fv) {
    if (g_var_count >= MAX_VARS)
      return;
    fv = &g_vars[g_var_count++];
    int i = 0;
    while (name[i]) {
      fv->name[i] = name[i];
      i++;
    }
    fv->name[i] = 0;
  }
  fv->type = 1;
  fv->int_val = 0;
  // Copy
  int i = 0;
  while (s[i] && i < 31) {
    fv->str_val[i] = s[i];
    i++;
  }
  fv->str_val[i] = 0;
}

// Interpolation
void resolve_string(char *src, char *dest) {
  int di = 0;
  while (*src) {
    if (*src == '{') {
      src++;
      char vname[32];
      int vi = 0;
      while (*src && *src != '}')
        vname[vi++] = *src++;
      vname[vi] = 0;
      if (*src == '}')
        src++;
      GemVar *gv = find_var(vname);
      if (gv) {
        if (gv->type == 1) { // string
          int k = 0;
          while (gv->str_val[k])
            dest[di++] = gv->str_val[k++];
        } else { // int
          char buf[16];
          int_to_str(gv->int_val, buf);
          int k = 0;
          while (buf[k])
            dest[di++] = buf[k++];
        }
      }
    } else {
      dest[di++] = *src++;
    }
  }
  dest[di] = 0;
}

// --- Component Tree ---
#define MAX_COMPS 100
typedef struct {
  int type; // 0=Label, 1=Button, 2=Container(VStack), 3=HStack
  int x, y, w, h;
  char text[64];
  int action_start;
  int action_end;
  // Layout props
  uint32_t bg_color;
  uint32_t fg_color;
  int padding;
} GemComp;

GemComp g_comps[MAX_COMPS];
int g_comp_count = 0;

// Layout Tracking
int layout_x = 0;
int layout_y = 0;
int layout_max_h_in_row = 0; // For HStack
int layout_stack_type = 0;   // 0=VStack, 1=HStack

// --- Parsing Logic ---

void parse_block_logic(int start, int end) {
  int t = start;
  while (t < end) {
    // Simple: VAR = VAL
    // Simple: VAR = VAR + VAL
    if (t + 1 < end && str_eq(tokens[t + 1], "=")) {
      char *vn = tokens[t];
      GemVar *gv = find_var(vn);
      // Right Hand Side
      char *op1 = tokens[t + 2];
      char *op = (t + 3 < end) ? tokens[t + 3] : 0;

      // Int Math?
      int v1 = 0;
      int is_int = 0;
      if (op1[0] >= '0' && op1[0] <= '9') {
        v1 = str_to_int(op1);
        is_int = 1;
      } else {
        GemVar *r = find_var(op1);
        if (r && r->type == 0) {
          v1 = r->int_val;
          is_int = 1;
        }
      }

      if (is_int) {
        if (op && (str_eq(op, "+") || str_eq(op, "-"))) {
          char *op2 = tokens[t + 4];
          int v2 = 0;
          if (op2[0] >= '0' && op2[0] <= '9')
            v2 = str_to_int(op2);
          else {
            GemVar *r = find_var(op2);
            if (r)
              v2 = r->int_val;
          }

          if (str_eq(op, "+"))
            v1 += v2;
          else
            v1 -= v2;
          t += 5;
        } else {
          t += 3;
        }
        set_var_int(vn, v1);
      } else {
        // String concat logic: var = var + "str"
        char buf[64];
        buf[0] = 0;
        GemVar *r = find_var(op1);
        if (r)
          str_cat_safe(buf, r->str_val);
        else
          str_cat_safe(buf, op1);

        if (op && str_eq(op, "+")) {
          char *op2 = tokens[t + 4];
          // Strip quotes? Tokenizer already did
          str_cat_safe(buf, op2);
          t += 5;
        } else {
          t += 3;
        }
        set_var_str(vn, buf);
      }
    } else if (str_eq(tokens[t], "if")) {
      // Very basic if: if ( x == y ) { ... }
      // Skip for now in prototype 2.0 to ensure stability first
      int brace = 0;
      while (t < end) {
        if (str_eq(tokens[t], "{"))
          brace++;
        if (str_eq(tokens[t], "}")) {
          brace--;
          if (brace == 0)
            break;
        }
        t++;
      }
      t++;
    } else {
      t++;
    }
  }
}

// Recursive UI Parser
int parse_ui(int t, int stop_token, int px, int py, int p_width) {
  int cx = px;
  int cy = py;
  int row_h = 0; // For HStack

  while (t < stop_token && !str_eq(tokens[t], "}")) {
    int is_vstack = str_eq(tokens[t], "VStack");
    int is_hstack = str_eq(tokens[t], "HStack");

    if (is_vstack || is_hstack) {
      t++;
      if (str_eq(tokens[t], "{"))
        t++;
      // Layout context switch?
      // Simple recursive call
      // We just continue drawing flow
      // Actually, stacks need to know their size.
      // Simplified: Just pass current pointer.
      // But HStack implies children arrange horizontally.
      // We need to pass a "mode"

      // For prototype 2.0: We just flatten parsing but track cursor.
      if (is_hstack) {
        // We need to capture the sub-block to layout horizontally
        // This is hard in single pass without building a tree.
        // Let's build the tree of components linearly but assign calculated
        // X/Y. HACK: Just change a global "direction" flag? No, nested.
      }

      // Re-use logic: just parse children.
      int t_end = t;
      int b = 1;
      while (b > 0 && t_end < token_count) {
        if (str_eq(tokens[t_end], "{"))
          b++;
        if (str_eq(tokens[t_end], "}"))
          b--;
        t_end++;
      }
      // Recurse?
      // Ideally we build an AST. Here we are executing layout immediately
      // during parse (Bad for resize). BETTER: Parse into GemComp list first,
      // then running a "layout()" pass. But let's stick to immediate parsing
      // for the demo.
    }

    if (str_eq(tokens[t], "Label")) {
      t++;
      if (str_eq(tokens[t], "("))
        t++;
      char *txt = tokens[t];
      t++;
      if (str_eq(tokens[t], ")"))
        t++;

      GemComp *c = &g_comps[g_comp_count++];
      c->type = 0;
      // c->text...
      int k = 0;
      while (txt[k]) {
        c->text[k] = txt[k];
        k++;
      }
      c->text[k] = 0;
      c->x = cx;
      c->y = cy;
      c->w = p_width - 20; // fill
      c->h = 24;

      cy += 30; // Auto-VStack
    } else if (str_eq(tokens[t], "Button")) {
      t++;
      if (str_eq(tokens[t], "("))
        t++;
      char *txt = tokens[t];
      t++;
      if (str_eq(tokens[t], ")"))
        t++;

      GemComp *c = &g_comps[g_comp_count++];
      c->type = 1;
      int k = 0;
      while (txt[k]) {
        c->text[k] = txt[k];
        k++;
      }
      c->text[k] = 0;
      c->x = cx;
      c->y = cy;
      c->w = 80; // Fixed for now
      c->h = 30;
      c->bg_color = 0xC0C0C0;
      c->fg_color = 0x000000;

      // Action block?
      if (str_eq(tokens[t], "{")) {
        t++;
        c->action_start = t;
        int b = 1;
        while (b > 0 && t < token_count) {
          if (str_eq(tokens[t], "{"))
            b++;
          if (str_eq(tokens[t], "}")) {
            b--;
            if (b == 0)
              break;
          }
          t++;
        }
        c->action_end = t;
        t++;
      }

      // HStack logic?
      // If we were in HStack, cx += c->w + 10;
      // Using a heuristic: If previous element was Button, place next to it?
      // No, let's just make it simple:
      // 2.0 Spec is VStack by default.
      // If "HStack" keyword was seen, we would increment cx instead of cy.

      cy += 40;
    } else {
      t++;
    }
  }
  return t;
}

void reload_layout() {
  // Re-run parsing of just the UI part?
  // In a real VDOM, we just re-evaluate bindings.
  // Here, we re-parse completely?
  // No, we just re-eval string bindings.
  for (int i = 0; i < g_comp_count; i++) {
    // Refresh?
  }
}

void gem_paint(Window *win) {
  // Re-parse layout on every paint? No, too slow.
  // Just draw display list.
  for (int i = 0; i < g_comp_count; i++) {
    GemComp *c = &g_comps[i];

    char final_text[64];
    resolve_string(c->text, final_text);

    int abs_x = win->x + c->x;
    int abs_y = win->y + c->y;

    if (c->type == 1) { // Button
      draw_rect(abs_x, abs_y, c->w, c->h, c->bg_color);
      // 3D Bevel
      draw_rect(abs_x, abs_y, c->w, 1, 0xFFFFFF);
      draw_rect(abs_x, abs_y, 1, c->h, 0xFFFFFF);
      draw_rect(abs_x + c->w, abs_y, 1, c->h + 1, 0x000000);
      draw_rect(abs_x, abs_y + c->h, c->w, 1, 0x000000);

      draw_string(abs_x + 10, abs_y + 8, final_text, c->fg_color);
    } else if (c->type == 0) {
      draw_string(abs_x, abs_y, final_text, 0x000000);
    }
  }
}

void gem_click(Window *win, int x, int y) {
  int redraw = 0;
  for (int i = 0; i < g_comp_count; i++) {
    GemComp *c = &g_comps[i];
    if (c->type == 1) {
      if (x >= c->x && x <= c->x + c->w && y >= c->y && y <= c->y + c->h) {
        if (c->action_end > c->action_start) {
          parse_block_logic(c->action_start, c->action_end);
          redraw = 1;
        }
      }
    }
  }
  // If State Changed, UI might need re-layout (e.g. if conditions added).
  // For now, just repaint bindings.
}

void run_gem_script(char *script) {
  tokenize(script);
  g_comp_count = 0;
  g_var_count = 0;

  // Parse
  int t = 0;
  // App "Name" {
  if (str_eq(tokens[t], "App")) {
    t += 2;
    if (str_eq(tokens[t], "{"))
      t++;

    // Inside App
    while (t < token_count && !str_eq(tokens[t], "}")) {

      if (str_eq(tokens[t], "var")) {
        t++;
        char *nm = tokens[t];
        t += 2; // skip =
        char *val = tokens[t];
        if (val[0] == '"')
          set_var_str(nm, val);
        else
          set_var_int(nm, str_to_int(val));
        t++;
      } else if (str_eq(tokens[t], "Window")) {
        t += 2; // {
        // Scan properties
        char *title = "App";
        int w = 300, h = 200;

        while (!str_eq(tokens[t], "VStack") && !str_eq(tokens[t], "HStack") &&
               !str_eq(tokens[t], "Body")) {
          if (str_eq(tokens[t], "title")) {
            title = tokens[t + 2];
            t += 3;
          } else if (str_eq(tokens[t], "width")) {
            w = str_to_int(tokens[t + 2]);
            t += 3;
          } else if (str_eq(tokens[t], "height")) {
            h = str_to_int(tokens[t + 2]);
            t += 3;
          } else
            t++;
          if (t >= token_count)
            break;
        }

        Window *win = create_window(150, 100, w, h, title);
        win->on_paint = gem_paint;
        win->on_click = gem_click;

        // Now Parse Body / VStack
        if (str_eq(tokens[t], "Body") || str_eq(tokens[t], "VStack")) {
          t += 2; // skip name and {
          parse_ui(t, token_count, 10, 10, w);
        }

        // Break after window for now (1 window app)
        break;
      } else {
        t++;
      }
    }
  }
}

void load_extension_apps() {
  // 2.0 SciCalc Demo
  char *scicalc = "App \"SciCalc\" { "
                  "  var display = \"0\" "
                  "  var acc = 0 "
                  "  Window { "
                  "    title: \"GemLang Calc\" "
                  "    width: 250 "
                  "    height: 600 " // Taller
                  "    VStack { "
                  "      Label( \"{display}\" ) "
                  "      Button( \"7\" ) { display = display + \"7\" } "
                  "      Button( \"8\" ) { display = display + \"8\" } "
                  "      Button( \"9\" ) { display = display + \"9\" } "
                  "      Button( \"+\" ) { display = display + \"+\" } "
                  "      Button( \"4\" ) { display = display + \"4\" } "
                  "      Button( \"5\" ) { display = display + \"5\" } "
                  "      Button( \"6\" ) { display = display + \"6\" } "
                  "      Button( \"-\" ) { display = display + \"-\" } "
                  "      Button( \"1\" ) { display = display + \"1\" } "
                  "      Button( \"2\" ) { display = display + \"2\" } "
                  "      Button( \"3\" ) { display = display + \"3\" } "
                  "      Button( \"=\" ) { "
                  "         display = \"Error\" "
                  "      } "
                  "      Button( \"C\" ) { display = \"\" } "
                  "    } "
                  "  } "
                  "}";

  run_gem_script(scicalc);
}
