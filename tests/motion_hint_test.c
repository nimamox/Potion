/* Behavioral model of GDK's one-outstanding-hint delivery protocol, running
 * the actual native adapter. No GTK dependency is needed on the host. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#define dlsym potion_test_dlsym
#include "../src/mesquite_whisper_touch.c"
#undef dlsym

static PotionEventHandler installed;
static PotionDestroyNotify destroy;
static void *installed_data;
static int allocations, frees, acknowledged, waiting, moves, releases, destroyed;
struct Event { int type; int hint; };
void *g_malloc(size_t size) { ++allocations; return malloc(size); }
void g_free(void *p) { ++frees; free(p); }
void *win_mgr_utils_new_name(int type, const char *name) { (void)type; (void)name; return 0; }
void win_mgr_utils_add_is_wisper_touch_supported(void *name, int enabled) { (void)name; (void)enabled; }
void gdk_event_request_motions(const void *value) {
  const struct Event *event = value;
  assert(event->type == 3);
  if (event->hint) { waiting = 0; ++acknowledged; }
}
static void native_set(PotionEventHandler handler, void *data, PotionDestroyNotify notify) {
  if (destroy) destroy(installed_data);
  installed = handler; installed_data = data; destroy = notify;
}
void *potion_test_dlsym(void *handle, const char *name) {
  (void)handle; assert(strcmp(name, "gdk_event_handler_set") == 0);
  return (void *)native_set;
}
static void receive(void *value, void *data) {
  struct Event *event = value;
  assert(data == &moves);
  if (event->type == 3) ++moves;
  if (event->type == 7) ++releases;
}
static void dispose(void *data) { assert(data == &moves); ++destroyed; }
static void motion(int hint) {
  struct Event event = {3, hint};
  if (hint && waiting) return;
  waiting = hint;
  installed(&event, installed_data);
}
static void release(void) {
  struct Event event = {7, 0};
  waiting = 0;
  installed(&event, installed_data);
}
static void replace_during_dispatch(void *event, void *data) {
  (void)event; assert(data == &moves);
  /* The current wrapper is destroyed here; dispatch must not access it again. */
  gdk_event_handler_set(receive, &moves, dispose);
  motion(1);
}
int main(void) {
  int i, cycle;
  /* Old implementation: initially ordinary motion; tooltip enables hints. */
  native_set(receive, &moves, 0);
  for (i = 0; i < 12; ++i) motion(0);
  assert(moves == 12);
  for (i = 0; i < 12; ++i) motion(1);
  assert(moves == 13); /* eleven samples disappear */
  release();
  for (i = 0; i < 12; ++i) motion(1);
  assert(moves == 14); /* a new gesture still fails */
  release();
  gdk_event_handler_set(receive, &moves, dispose);
  moves = releases = 0;
  for (cycle = 0; cycle < 1000; ++cycle) {
    for (i = 0; i < 12; ++i) motion(cycle % 2);
    release();
    assert(moves == (cycle + 1) * 12);
    assert(!waiting);
  }
  assert(releases == 1000 && acknowledged == 6000);
  gdk_event_handler_set(replace_during_dispatch, &moves, dispose);
  motion(1);
  assert(moves == 12001 && !waiting);
  gdk_event_handler_set(0, 0, 0);
  assert(destroyed == 3 && allocations == frees);
  return 0;
}
