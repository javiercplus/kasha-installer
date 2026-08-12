/*
 * ui_css.c
 * Custom GTK CSS
 */
#include "neko_installer.h"

void load_custom_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    const gchar *css_data =
    "progressbar trough { min-height: 20px; }"
    "progressbar progress { min-height: 20px; background-color: #33d17a; }"
    /* Console scrolled window: dark background, no borders, clean */
    "#console_scroll {"
    "  background-color: #1e1e1e;"
    "  border: none;"
    "  box-shadow: none;"
    "}"
    "#console_scroll scrollbar {"
    "  background-color: #1e1e1e;"
    "  border: none;"
    "}"
    "#console_scroll scrollbar slider {"
    "  background-color: #444444;"
    "  border-radius: 4px;"
    "  min-width: 8px;"
    "}"
    "#console_scroll scrollbar trough {"
    "  background-color: #1e1e1e;"
    "  border: none;"
    "}"
    /* Console text view itself: terminal colors, monospace */
    "#console_view {"
    "  background-color: #1e1e1e;"
    "  color: #e0e0e0;"
    "  font-family: \"DejaVu Sans Mono\", \"Noto Sans Mono\", monospace;"
    "  font-size: 10pt;"
    "  border: none;"
    "}"
    "#console_view text {"
    "  background-color: #1e1e1e;"
    "  color: #e0e0e0;"
    "}"
    "#console_view border {"
    "  background-color: #1e1e1e;"
    "  border: none;"
    "}"
    /* Desktop selection cards */
    ".desktop-row {"
    "  padding: 10px 14px;"
    "  border-radius: 8px;"
    "  background-color: alpha(@theme_fg_color, 0.04);"
    "  transition: background-color 150ms ease;"
    "}"
    ".desktop-row:checked {"
    "  background-color: alpha(@theme_selected_bg_color, 0.18);"
    "}"
    ".desktop-row:hover {"
    "  background-color: alpha(@theme_fg_color, 0.08);"
    "}"
    ".desktop-name {"
    "  font-weight: bold;"
    "}"
    ".desktop-desc {"
    "  color: alpha(@theme_fg_color, 0.65);"
    "  font-size: 9pt;"
    "}";

    GError *error = NULL;
    gtk_css_provider_load_from_data(provider, css_data, -1, &error);
    if (error) {
        g_printerr("Error loading CSS: %s\n", error->message);
        g_error_free(error);
    } else {
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                  GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}