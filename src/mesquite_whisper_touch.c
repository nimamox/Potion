/* Match WebReaderViewer's native window-name setup so AwesomeWM delivers the
 * Oasis page keys directly to Mesquite/WebKit. */
extern void *win_mgr_utils_new_name(int type, const char *name);
extern void win_mgr_utils_add_is_wisper_touch_supported(void *name, int enabled);

void *win_mgr_utils_new_application_name(void) {
  void *name = win_mgr_utils_new_name(0, "application");
  if (name) win_mgr_utils_add_is_wisper_touch_supported(name, 1);
  return name;
}

/* GTK 2 tooltips add GDK_POINTER_MOTION_HINT_MASK to Mesquite's shared
 * WebView window on first hover. Mesquite does not acknowledge those hints,
 * so GDK drops subsequent motion until button state changes. Complete the
 * GDK delivery protocol before dispatching each motion to the original handler.
 * This is event-driven; it neither polls nor synthesizes selection/input.
 *
 * Only public opaque GDK APIs and the stable GdkEventType prefix are needed.
 * Resolve against Mesquite's system libraries, not the daemon's newer libc.
 */
#include <dlfcn.h>
#include <stddef.h>

typedef void (*PotionEventHandler)(void *, void *);
typedef void (*PotionDestroyNotify)(void *);
typedef void (*PotionSetHandler)(PotionEventHandler, void *, PotionDestroyNotify);
extern void *g_malloc(size_t size);
extern void g_free(void *memory);
extern void gdk_event_request_motions(const void *event);

struct PotionEventDispatch {
  PotionEventHandler handler;
  void *data;
  PotionDestroyNotify notify;
};

static void potion_dispatch_event(void *event, void *data) {
  struct PotionEventDispatch *dispatch = data;
  /* GDK_MOTION_NOTIFY == 3. The API itself checks is_hint; ordinary motion
   * is a no-op. Acknowledge before calling user code, which may replace the
   * handler (and destroy dispatch) or enter a nested event loop. */
  if (event && *(const int *)event == 3)
    gdk_event_request_motions(event);
  dispatch->handler(event, dispatch->data);
}

static void potion_destroy_dispatch(void *data) {
  struct PotionEventDispatch *dispatch = data;
  if (dispatch->notify) dispatch->notify(dispatch->data);
  g_free(dispatch);
}

void gdk_event_handler_set(PotionEventHandler handler, void *data,
                           PotionDestroyNotify notify) {
  PotionSetHandler next = (PotionSetHandler)dlsym(RTLD_NEXT, "gdk_event_handler_set");
  struct PotionEventDispatch *dispatch;
  if (!handler) {
    next(handler, data, notify);
    return;
  }
  dispatch = g_malloc(sizeof(*dispatch));
  dispatch->handler = handler;
  dispatch->data = data;
  dispatch->notify = notify;
  next(potion_dispatch_event, dispatch, potion_destroy_dispatch);
}
