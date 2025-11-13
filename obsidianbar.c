#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <uvm/uvm_extern.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "config.h"


#define MAX_MODULE_LEN 256

typedef enum {
  POS_LEFT,
  POS_CENTER,
  POS_RIGHT
} ModulePos;

static Display *display;
static Window statusbar;
static GC gc;
static int running = 1;
static XftFont *font;
static XftDraw *xftdraw;
static XftColor xftcolor;
static Visual *visual;
static Colormap colormap;

typedef struct {
  char *(*update)(void);
  char buffer[MAX_MODULE_LEN];
  char *name;
  ModulePos position;
} Module;

/* Clock module */
char *clock_update(void) {
  static char buffer[MAX_MODULE_LEN];
  time_t now = time(NULL);
  struct tm *timeinfo = localtime(&now);

  strftime(buffer, MAX_MODULE_LEN, "%H:%M:%S", timeinfo);
  return buffer;
}

/* Volume module */
char *volume_update(void) {
  static char buffer[MAX_MODULE_LEN];
  FILE *fp;
  char line[64];
  float vol = 0.0;

  fp = popen("sndioctl -n output.level 2>/dev/null", "r");
  if (!fp) {
    snprintf(buffer, MAX_MODULE_LEN, "Vol: N/A");
    return buffer;
  }

  if (fgets(line, sizeof(line), fp) != NULL) {
    vol = atof(line);
    snprintf(buffer, MAX_MODULE_LEN, "Vol: %.0f%%", vol * 100);
  } else {
    snprintf(buffer, MAX_MODULE_LEN, "Vol: N/A");
  }

  pclose(fp);
  return buffer;
}

/* Battery module */
char *battery_update(void) {
  static char buffer[MAX_MODULE_LEN];
  FILE *fp;
  char line[64];
  int percent = 0, minutes = 0;

  /* Get battery percentage */
  fp = popen("apm -l 2>/dev/null", "r");
  if (fp) {
    if (fgets(line, sizeof(line), fp) != NULL) {
      percent = atoi(line);
    }
    pclose(fp);
  }

  /* Get battery time remaining */
  fp = popen("apm -m 2>/dev/null", "r");
  if (fp) {
    if (fgets(line, sizeof(line), fp) != NULL) {
      minutes = atoi(line);
    }
    pclose(fp);
  }

  snprintf(buffer, MAX_MODULE_LEN, "Bat: %d%% %dh%dm",
      percent, minutes / 60, minutes % 60);

  return buffer;
}

char *date_update(void) {
  static char buffer[MAX_MODULE_LEN];
  time_t now = time(NULL);
  struct tm *timeinfo = localtime(&now);

  strftime(buffer, MAX_MODULE_LEN, "%a %d %b", timeinfo);
  return buffer;
}

char *memory_update(void) {
  static char buffer[MAX_MODULE_LEN];
  FILE *fp;
  char line[256];
  long long mem_total = 0;
  int mem_used = 0;

  fp = popen("sysctl -n hw.physmem 2>/dev/null", "r");
  if (fp) {
    if (fgets(line, sizeof(line), fp) != NULL) {
      mem_total = strtoll(line, NULL, 10) / 1024 / 1024;
    }
    pclose(fp);
  }

  fp = popen("vmstat 2>/dev/null", "r");
  if (fp) {
    while (fgets(line, sizeof(line), fp) != NULL) {
      char avm_str[32];
      if (sscanf(line, "%*s %*s %31s", avm_str) == 1) {
        mem_used = atoi(avm_str);
      }
    }
    pclose(fp);
  }

  snprintf(buffer, MAX_MODULE_LEN, "Mem: %dM/%lldM", mem_used, mem_total);
  return buffer;
}

static Module modules[] = {
  { battery_update, "", "battery", POS_RIGHT },
  { volume_update, "", "volume", POS_RIGHT },
  { clock_update, "", "clock", POS_CENTER },
  { date_update, "", "date", POS_LEFT},
  { memory_update, "", "memory", POS_LEFT},
  { NULL, "", NULL, POS_LEFT }
};

void signal_handler(int sig) {
  if (sig == SIGTERM || sig == SIGINT)
    running = 0;
}

void setup(void) {
  XSetWindowAttributes attrs;
  Window root;
  int screen;
  unsigned long valuemask;
  int x, y, width, height;

  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "Cannot open display\n");
    exit(1);
  }

  screen = DefaultScreen(display);
  root = RootWindow(display, screen);
  visual = DefaultVisual(display, screen);
  colormap = DefaultColormap(display, screen);

  width = DisplayWidth(display, screen);
  height = BAR_HEIGHT;
  x = 0;

  if (BAR_POSITION == 0) {
    y = 0;
  } else {
    y = DisplayHeight(display, screen) - height;
  }

  attrs.background_pixel = BAR_BG_COLOR;
  attrs.override_redirect = True;
  attrs.event_mask = StructureNotifyMask | ExposureMask;
  valuemask = CWBackPixel | CWOverrideRedirect | CWEventMask;

  statusbar = XCreateWindow(
      display, root,
      x, y, width, height,
      0,
      CopyFromParent,
      CopyFromParent,
      CopyFromParent,
      valuemask, &attrs);

  gc = XCreateGC(display, statusbar, 0, NULL);

  font = XftFontOpenName(display, screen, FONT_NAME);
  if (!font) {
    fprintf(stderr, "Cannot load font: %s\n", FONT_NAME);
    exit(1);
  }

  xftdraw = XftDrawCreate(display, statusbar, visual, colormap);
  if (!xftdraw) {
    fprintf(stderr, "Cannot create Xft draw context\n");
    exit(1);
  }

  if (!XftColorAllocName(display, visual, colormap, "#ebdbb2", &xftcolor)) {
    fprintf(stderr, "Cannot allocate color\n");
    exit(1);
  }

  XMapWindow(display, statusbar);
  XFlush(display);
}

void update_modules(void) {
  int i;

  for (i = 0; modules[i].name != NULL; i++) {
    char *result = modules[i].update();
    strncpy(modules[i].buffer, result, MAX_MODULE_LEN - 1);
  }
}

void draw_decoration(int x, int width) {
  int y;
  XGCValues gcv;
  
  if (!DECORATION_ENABLED)
    return;
  
  gcv.foreground = DECORATION_COLOR;
  XChangeGC(display, gc, GCForeground, &gcv);
  
  if (BAR_POSITION == 0) {
    /* Bar at top - draw line at bottom of text area */
    y = BAR_HEIGHT - DECORATION_HEIGHT;
  } else {
    /* Bar at bottom - draw line at top of text area */
    y = 0;
  }
  
  XFillRectangle(display, statusbar, gc, x, y, width, DECORATION_HEIGHT);
  
  /* Reset GC */
  gcv.foreground = BAR_BG_COLOR;
  XChangeGC(display, gc, GCForeground, &gcv);
}

void draw_modules_by_position(ModulePos pos) {
  XGlyphInfo extents;
  int bar_width = DisplayWidth(display, DefaultScreen(display));
  int x, y;
  int i;
  int total_width;

  /* Center text vertically in bar */
  y = (BAR_HEIGHT + font->ascent - font->descent) / 2;

  if (pos == POS_RIGHT) {
    x = bar_width - PADDING;
    for (i = 0; modules[i].name != NULL; i++) {
      if (modules[i].position != POS_RIGHT)
        continue;

      XftTextExtentsUtf8(display, font, (const FcChar8*)modules[i].buffer,
          strlen(modules[i].buffer), &extents);
      x -= extents.width;

      draw_decoration(x, extents.width);

      XftDrawStringUtf8(xftdraw, &xftcolor, font, x, y,
          (const FcChar8*)modules[i].buffer, strlen(modules[i].buffer));

      x -= MODULE_PADDING;
    }
  } else if (pos == POS_LEFT) {
    x = PADDING;
    for (i = 0; modules[i].name != NULL; i++) {
      if (modules[i].position != POS_LEFT)
        continue;

      XftTextExtentsUtf8(display, font, (const FcChar8*)modules[i].buffer,
          strlen(modules[i].buffer), &extents);

      draw_decoration(x, extents.width);

      XftDrawStringUtf8(xftdraw, &xftcolor, font, x, y,
          (const FcChar8*)modules[i].buffer, strlen(modules[i].buffer));

      x += extents.width + MODULE_PADDING;
    }
  } else if (pos == POS_CENTER) {
    total_width = 0;
    for (i = 0; modules[i].name != NULL; i++) {
      if (modules[i].position != POS_CENTER)
        continue;

      XftTextExtentsUtf8(display, font, (const FcChar8*)modules[i].buffer,
          strlen(modules[i].buffer), &extents);
      total_width += extents.width + MODULE_PADDING;
    }

    x = (bar_width - total_width) / 2;
    for (i = 0; modules[i].name != NULL; i++) {
      if (modules[i].position != POS_CENTER)
        continue;

      XftTextExtentsUtf8(display, font, (const FcChar8*)modules[i].buffer,
          strlen(modules[i].buffer), &extents);

      draw_decoration(x, extents.width);

      XftDrawStringUtf8(xftdraw, &xftcolor, font, x, y,
          (const FcChar8*)modules[i].buffer, strlen(modules[i].buffer));

      x += extents.width + MODULE_PADDING;
    }
  }
}

void draw_statusbar(void) {
  int bar_width = DisplayWidth(display, DefaultScreen(display));

  XFillRectangle(display, statusbar, gc, 0, 0, bar_width, BAR_HEIGHT);

  draw_modules_by_position(POS_LEFT);
  draw_modules_by_position(POS_CENTER);
  draw_modules_by_position(POS_RIGHT);

  XFlush(display);
}

void handle_events(void) {
  XEvent event;

  while (XPending(display)) {
    XNextEvent(display, &event);

    switch (event.type) {
    case Expose:
      draw_statusbar();
      break;
    case ConfigureNotify:
      draw_statusbar();
      break;
    }
  }
}

void cleanup(void) {
  if (display) {
    if (xftdraw)
      XftDrawDestroy(xftdraw);
    XftColorFree(display, visual, colormap, &xftcolor);
    if (font)
      XftFontClose(display, font);
    XFreeGC(display, gc);
    XDestroyWindow(display, statusbar);
    XCloseDisplay(display);
  }
}

int main(void) {
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  setup();

  while (running) {
    update_modules();
    draw_statusbar();
    handle_events();
    sleep(UPDATE_INTERVAL);
  }

  cleanup();
  return 0;
}
