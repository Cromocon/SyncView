#include "ui/theme.h"

#include "core/logger.h"
#include "syncview-resources.h"

#include <string.h>

#define RESOURCE_PREFIX "/com/syncview/SyncView/"

static GtkCssProvider *provider;
static GdkDisplay *themed_display;
static SyncviewThemeChoice choice = SYNCVIEW_THEME_AUTO;
static gboolean dark_applied;
static guint parse_errors;
static gboolean resources_registered;

static void
on_parsing_error(GtkCssProvider *source, GtkCssSection *section, GError *error, gpointer user_data)
{
    (void)source;
    (void)user_data;

    char *where = section ? gtk_css_section_to_string(section) : g_strdup("?");
    log_ui("CSS: errore di analisi in %s: %s", where, error->message);
    g_free(where);
    parse_errors++;
}

/* Il sistema usa il tema scuro? GTK >= 4.20 espone la preferenza del sistema; prima si ripiega sulle impostazioni note. */
static gboolean
system_prefers_dark(GdkDisplay *display)
{
    GtkSettings *settings = gtk_settings_get_for_display(display);

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-interface-color-scheme")) {
        int scheme = 0;  /* 0 non supportato, 1 predefinito, 2 scuro, 3 chiaro */

        g_object_get(settings, "gtk-interface-color-scheme", &scheme, NULL);
        if (scheme == 2) {
            return TRUE;
        }
        if (scheme == 3) {
            return FALSE;
        }
    }

    gboolean prefer_dark = FALSE;
    char *theme_name = NULL;

    g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark, "gtk-theme-name", &theme_name, NULL);
    if (!prefer_dark && theme_name) {
        char *lower = g_ascii_strdown(theme_name, -1);

        prefer_dark = strstr(lower, "dark") != NULL;
        g_free(lower);
    }
    g_free(theme_name);
    return prefer_dark;
}

static gboolean
wants_dark(GdkDisplay *display)
{
    const char *forced = g_getenv("SYNCVIEW_THEME");

    if (choice == SYNCVIEW_THEME_DARK) {
        return TRUE;
    }
    if (choice == SYNCVIEW_THEME_LIGHT) {
        return FALSE;
    }
    if (forced && strcmp(forced, "dark") == 0) {
        return TRUE;
    }
    if (forced && strcmp(forced, "light") == 0) {
        return FALSE;
    }
    return system_prefers_dark(display);
}

static void
apply(void)
{
    gboolean dark = wants_dark(themed_display);
    const char *name = dark ? RESOURCE_PREFIX "syncview-dark.css" : RESOURCE_PREFIX "syncview-light.css";
    GBytes *bytes = g_resources_lookup_data(name, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);

    if (!bytes) {
        log_ui("Tema: risorsa %s non trovata", name);
        return;
    }

    gsize size = 0;
    const char *data = g_bytes_get_data(bytes, &size);
    char *text = g_strndup(data, size);

    gtk_css_provider_load_from_string(provider, text);
    g_free(text);
    g_bytes_unref(bytes);

    dark_applied = dark;
    log_ui("Tema applicato: %s", dark ? "scuro" : "chiaro");
}

static void
on_settings_changed(GObject *settings, GParamSpec *pspec, gpointer user_data)
{
    (void)settings;
    (void)pspec;
    (void)user_data;

    if (choice == SYNCVIEW_THEME_AUTO && !g_getenv("SYNCVIEW_THEME")) {
        apply();
    }
}

void
syncview_theme_init(GdkDisplay *display)
{
    if (provider || !display) {
        return;
    }

    if (!resources_registered) {
        g_resources_register(syncview_get_resource());
        resources_registered = TRUE;
    }

    themed_display = display;
    provider = gtk_css_provider_new();
    g_signal_connect(provider, "parsing-error", G_CALLBACK(on_parsing_error), NULL);
    apply();
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkSettings *settings = gtk_settings_get_for_display(display);

    g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme", G_CALLBACK(on_settings_changed), NULL);
    g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(on_settings_changed), NULL);
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "gtk-interface-color-scheme")) {
        g_signal_connect(settings, "notify::gtk-interface-color-scheme", G_CALLBACK(on_settings_changed), NULL);
    }
}

void
syncview_theme_set_choice(SyncviewThemeChoice new_choice)
{
    choice = new_choice;
    if (provider) {
        apply();
    }
}

gboolean
syncview_theme_is_dark(void)
{
    return dark_applied;
}

guint
syncview_theme_parse_errors(void)
{
    return parse_errors;
}
