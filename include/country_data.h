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
} CountryInfo;

const CountryInfo* get_country_list(int *count);
const CountryInfo* find_country_by_name(const char *name);

#endif
