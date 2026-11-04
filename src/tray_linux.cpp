#include "desktop_support.hpp"
#ifdef CYSYNCK_APPINDICATOR
#include <libayatana-appindicator/app-indicator.h>
#endif

namespace cysynck::desktop {
struct Tray::Impl {
    std::deque<TrayAction> actions;
    std::string error = "Built without Ayatana AppIndicator support";
#ifdef CYSYNCK_APPINDICATOR
    AppIndicator *indicator = nullptr;
    GtkWidget *menu = nullptr;
    std::array<GtkWidget *, 4> items{};
    struct Binding {
        Impl *owner;
        TrayAction action;
    };
    std::array<Binding, 4> bindings{};
    TrayLabels last;
    static void activate(GtkMenuItem *, gpointer context) {
        auto binding = static_cast<Binding *>(context);
        if (binding->owner->actions.size() < 32)
            binding->owner->actions.push_back(binding->action);
    }
    explicit Impl(const TrayLabels &labels) {
        if (!gtk_init_check(nullptr, nullptr)) {
            error = "No graphical GTK session is available";
            return;
        }
        indicator =
            app_indicator_new("cysynck", "folder-sync", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
        if (!indicator) {
            error = "Cannot create an application indicator";
            return;
        }
        menu = gtk_menu_new();
        g_object_ref_sink(menu);
        for (size_t i = 0; i < items.size(); ++i) {
            items[i] = gtk_menu_item_new_with_label("");
            bindings[i] = {this, static_cast<TrayAction>(i + 1)};
            g_signal_connect(items[i], "activate", G_CALLBACK(activate), &bindings[i]);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), items[i]);
        }
        update(labels);
        gtk_widget_show_all(menu);
        app_indicator_set_menu(indicator, GTK_MENU(menu));
        app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
        error = "No StatusNotifier host is connected; the window stays accessible";
    }
    void update(const TrayLabels &labels) {
        if (!indicator)
            return;
        if (last == labels)
            return;
        last = labels;
        std::array<std::string, 4> values{labels.show, labels.open_web, labels.backup_all,
                                          labels.quit};
        for (size_t i = 0; i < items.size(); ++i)
            gtk_menu_item_set_label(GTK_MENU_ITEM(items[i]), values[i].c_str());
        app_indicator_set_title(indicator, labels.tooltip.c_str());
    }
    bool connected() const {
        if (!indicator)
            return false;
        gboolean connected = FALSE;
        g_object_get(indicator, "connected", &connected, nullptr);
        return connected;
    }
    ~Impl() {
        if (indicator) {
            app_indicator_set_status(indicator, APP_INDICATOR_STATUS_PASSIVE);
            g_object_unref(indicator);
        }
        if (menu) {
            gtk_widget_destroy(menu);
            g_object_unref(menu);
        }
    }
#else
    explicit Impl(const TrayLabels &) {}
    void update(const TrayLabels &) {}
    bool connected() const { return false; }
#endif
};
Tray::Tray(const TrayLabels &labels) : impl_(std::make_unique<Impl>(labels)) {}
Tray::~Tray() = default;
bool Tray::available() const { return impl_->connected(); }
std::string Tray::reason() const { return impl_->error; }
void Tray::update(const TrayLabels &labels) { impl_->update(labels); }
void Tray::pump() {
#ifdef CYSYNCK_APPINDICATOR
    for (int count = 0; count < 32 && gtk_events_pending(); ++count)
        gtk_main_iteration_do(FALSE);
#endif
}
bool Tray::next(TrayAction &action) {
    if (impl_->actions.empty())
        return false;
    action = impl_->actions.front();
    impl_->actions.pop_front();
    return true;
}
} // namespace cysynck::desktop
