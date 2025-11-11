#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "config.h"

static Display *display;
static Window statusbar;
static GC gc;
static int running = 1;
static XftFont *font;
static XftDraw *xftdraw;
static XftColor xftcolor;
static Visual *visual;
static Colormap colormap;

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

  /* Get screen dimensions */
  width = DisplayWidth(display, screen);
  height = BAR_HEIGHT;
  x = 0;

  /* Position based on config */
  if (BAR_POSITION == 0) {
    y = 0;  /* Top */
  } else {
    y = DisplayHeight(display, screen) - height;  /* Bottom */
  }

  /* Create window attributes */
  attrs.background_pixel = BAR_BG_COLOR;
  attrs.override_redirect = True;
  attrs.event_mask = StructureNotifyMask | ExposureMask;
  valuemask = CWBackPixel | CWOverrideRedirect | CWEventMask;

  /* Create statusbar window */
  statusbar = XCreateWindow(
      display, root,
      x, y, width, height,
      0,
      CopyFromParent,
      CopyFromParent,
      CopyFromParent,
      valuemask, &attrs);

  /* Create graphics context */
  gc = XCreateGC(display, statusbar, 0, NULL);

  /* Load font */
  font = XftFontOpenName(display, screen, FONT_NAME);
  if (!font) {
    fprintf(stderr, "Cannot load font: %s\n", FONT_NAME);
    exit(1);
  }

  /* Create Xft draw context */
  xftdraw = XftDrawCreate(display, statusbar, visual, colormap);
  if (!xftdraw) {
    fprintf(stderr, "Cannot create Xft draw context\n");
    exit(1);
  }

  /* Allocate color */
  if (!XftColorAllocName(display, visual, colormap, "#ebdbb2", &xftcolor)) {
    fprintf(stderr, "Cannot allocate color\n");
    exit(1);
  }

  /* Map the window */
  XMapWindow(display, statusbar);
  XFlush(display);
}

void draw_statusbar(void) {
  char text[] = "obsidianbar";
  XGlyphInfo extents;
  int bar_width = DisplayWidth(display, DefaultScreen(display));
  int x, y;

  /* Get text extents */
  XftTextExtentsUtf8(display, font, (const FcChar8*)text, strlen(text), &extents);

  /* Center horizontally */
  x = (bar_width - extents.width) / 2;

  /* Center vertically */
  y = font->ascent + (BAR_HEIGHT - (font->ascent + font->descent)) / 2;

  /* Fill background */
  XFillRectangle(display, statusbar, gc, 0, 0, bar_width, BAR_HEIGHT);

  /* Draw text */
  XftDrawStringUtf8(xftdraw, &xftcolor, font, x, y,
      (const FcChar8*)text, strlen(text));

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
  draw_statusbar();

  while (running) {
    handle_events();
    sleep(UPDATE_INTERVAL);
  }

  cleanup();
  return 0;
}
