// SPDX-License-Identifier: MIT
#include "status-item.hpp"
#include <FL/Fl.H>
#include <gio/gio.h>
#include <cstring>
#include <utility>

namespace artcraft {
namespace {
constexpr const char *path = "/StatusNotifierItem";
constexpr const char *interface = "org.kde.StatusNotifierItem";
constexpr const char *xml = R"XML(<node><interface name="org.kde.StatusNotifierItem">
<property name="Category" type="s" access="read"/>
<property name="Id" type="s" access="read"/>
<property name="Title" type="s" access="read"/>
<property name="Status" type="s" access="read"/>
<property name="IconName" type="s" access="read"/>
<property name="IconPixmap" type="a(iiay)" access="read"/>
<property name="WindowId" type="u" access="read"/>
<property name="ItemIsMenu" type="b" access="read"/>
<property name="Menu" type="o" access="read"/>
<method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
</interface></node>)XML";
}
struct StatusItem::Impl {
  std::string id, title, menu;
  std::function<void()> activate;
  GDBusConnection *bus = nullptr;
  GDBusNodeInfo *info = nullptr;
  guint registration = 0, watch = 0;
  bool present = false;
  static void activate_later(void *data) {
    auto &self = *static_cast<Impl *>(data);
    self.activate();
  }
  static void method(GDBusConnection *, const gchar *, const gchar *,
                     const gchar *, const gchar *name, GVariant *,
                     GDBusMethodInvocation *call, gpointer data) {
    if (!strcmp(name, "Activate") || !strcmp(name, "SecondaryActivate"))
      Fl::add_timeout(0, activate_later, data);
    g_dbus_method_invocation_return_value(call, nullptr);
  }
  static GVariant *property(GDBusConnection *, const gchar *, const gchar *,
      const gchar *, const gchar *name, GError **, gpointer data) {
    auto &self = *static_cast<Impl *>(data);
    if (!strcmp(name, "Category")) return g_variant_new_string("ApplicationStatus");
    if (!strcmp(name, "Id")) return g_variant_new_string(self.id.c_str());
    if (!strcmp(name, "Title")) return g_variant_new_string(self.title.c_str());
    if (!strcmp(name, "Status")) return g_variant_new_string("Active");
    if (!strcmp(name, "IconName")) return g_variant_new_string("applications-graphics");
    if (!strcmp(name, "WindowId")) return g_variant_new_uint32(0);
    if (!strcmp(name, "ItemIsMenu")) return g_variant_new_boolean(false);
    if (!strcmp(name, "Menu")) return g_variant_new_object_path(self.menu.c_str());
    if (!strcmp(name, "IconPixmap")) {
      // Embedded ARGB mark: does not require a host icon theme.
      unsigned char pixels[24 * 24 * 4]{};
      for (int y = 3; y < 21; ++y)
        for (int x = 3; x < 21; ++x)
          if (x == 3 || x == 20 || y == 3 || y == 20 || x == y) {
            auto p = pixels + (y * 24 + x) * 4;
            p[0] = 255; p[1] = 107; p[2] = 171; p[3] = 255;
          }
      GVariantBuilder icons;
      g_variant_builder_init(&icons, G_VARIANT_TYPE("a(iiay)"));
      g_variant_builder_add(&icons, "(ii@ay)", 24, 24,
        g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, pixels, sizeof pixels, 1));
      return g_variant_builder_end(&icons);
    }
    return nullptr;
  }
  Impl(std::string app_id, std::string app_title, std::string menu_path,
       std::function<void()> callback)
    : id(std::move(app_id)), title(std::move(app_title)), menu(std::move(menu_path)),
      activate(std::move(callback)) {
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return;
    info = g_dbus_node_info_new_for_xml(xml, nullptr);
    static const GDBusInterfaceVTable vtable = {method, property, nullptr, {nullptr}};
    registration = g_dbus_connection_register_object(bus, path, info->interfaces[0],
      &vtable, this, nullptr, nullptr);
    if (!registration) return;
    watch = g_bus_watch_name_on_connection(bus, "org.kde.StatusNotifierWatcher",
      G_BUS_NAME_WATCHER_FLAGS_NONE,
      [](GDBusConnection *, const gchar *, const gchar *, gpointer data) {
        auto &self = *static_cast<Impl *>(data);
        self.accepted = std::make_shared<bool>(false);
        // Hide-to-tray remains disabled until the watcher accepts registration.
        g_dbus_connection_call(self.bus, "org.kde.StatusNotifierWatcher",
          "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
          "RegisterStatusNotifierItem", g_variant_new("(s)",
            g_dbus_connection_get_unique_name(self.bus)), nullptr,
          G_DBUS_CALL_FLAGS_NONE, 2000, self.cancel,
          [](GObject *source, GAsyncResult *result, gpointer context) {
            GError *error = nullptr;
            auto reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            // A cancelled operation can finish after the owner is destroyed.
            auto token = static_cast<std::weak_ptr<bool> *>(context);
            if (auto present = token->lock()) *present = reply != nullptr;
            delete token;
            if (reply) g_variant_unref(reply);
            if (error) g_error_free(error);
          }, new std::weak_ptr<bool>(self.accepted));
        self.present = true;
      },
      [](GDBusConnection *, const gchar *, gpointer data) {
        auto &self = *static_cast<Impl *>(data);
        self.present = false;
        self.accepted = std::make_shared<bool>(false);
        // Keep background windows reachable when the tray manager disappears.
        Fl::add_timeout(0, activate_later, &self);
      }, this, nullptr);
  }
  std::shared_ptr<bool> accepted = std::make_shared<bool>(false);
  GCancellable *cancel = g_cancellable_new();
  ~Impl() {
    Fl::remove_timeout(activate_later, this);
    if (watch) g_bus_unwatch_name(watch);
    g_cancellable_cancel(cancel);
    accepted.reset();
    g_object_unref(cancel);
    if (bus) {
      if (registration) g_dbus_connection_unregister_object(bus, registration);
      g_object_unref(bus);
    }
    if (info) g_dbus_node_info_unref(info);
  }
};
StatusItem::StatusItem(std::string id, std::string title, std::string menu,
                       std::function<void()> activate)
  : impl_(std::make_unique<Impl>(std::move(id), std::move(title), std::move(menu),
                                  std::move(activate))) {}
StatusItem::~StatusItem() = default;
bool StatusItem::available() const { return impl_->present && *impl_->accepted; }
} // namespace artcraft
