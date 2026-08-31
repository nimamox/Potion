/* Match WebReaderViewer's native window-name setup so AwesomeWM delivers the
 * Oasis page keys directly to Mesquite/WebKit. */
extern void *win_mgr_utils_new_name(int type, const char *name);
extern void win_mgr_utils_add_is_wisper_touch_supported(void *name, int enabled);

void *win_mgr_utils_new_application_name(void) {
  void *name = win_mgr_utils_new_name(0, "application");
  if (name) win_mgr_utils_add_is_wisper_touch_supported(name, 1);
  return name;
}
