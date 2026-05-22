/*
 * country_data.h
 * Country → Locale/Timezone mapping for automatic system configuration
 */
#ifndef COUNTRY_DATA_H
#define COUNTRY_DATA_H

typedef struct {
    const char *name;           // Country name (English)
    const char *locale;         // Default locale (e.g. "en_US.UTF-8")
    const char *tz_area;        // Timezone area (e.g. "America")
    const char *tz_city;        // Timezone city (e.g. "New_York")
    const char *console_kmap;   // Console keymap for /etc/rc.conf (e.g. "es", "us")
    const char *x11_layout;     // X11/Wayland layout (e.g. "es", "gb", "latam")
} CountryInfo;

const CountryInfo* get_country_list(int *count);
const CountryInfo* find_country_by_name(const char *name);

// --- Keyboard Layout Data ---
typedef struct {
    const char *code;        // Variant code (e.g. "dvorak", "nodeadkeys")
    const char *name;        // Human-readable name (e.g. "Dvorak")
} KbdVariant;

typedef struct {
    const char *layout;      // X11 layout code (e.g. "us", "es", "de")
    const char *name;        // Display name (e.g. "English (US)")
    const char *console_kmap;// Console keymap for /etc/rc.conf
    const KbdVariant *variants; // NULL-terminated array of variants
    int variant_count;
} KbdLayout;

const KbdLayout* get_keyboard_layouts(int *count);
const KbdLayout* find_keyboard_layout(const char *layout_code);

#endif
