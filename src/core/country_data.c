/*
 * country_data.c
 * Country → Locale/Timezone/Keymap mapping database
 * Sorted alphabetically by country name
 */
#include "country_data.h"
#include <string.h>
#include <stddef.h>

static const CountryInfo country_list[] = {
    { "Afghanistan",        "ps_AF.UTF-8",  "Asia",      "Kabul",              "us",       "us"    },
    { "Albania",            "sq_AL.UTF-8",  "Europe",    "Tirane",             "us",       "us"    },
    { "Algeria",            "ar_DZ.UTF-8",  "Africa",    "Algiers",            "us",       "us"    },
    { "Andorra",            "ca_AD.UTF-8",  "Europe",    "Andorra",            "es",       "es"    },
    { "Angola",             "pt_AO.UTF-8",  "Africa",    "Luanda",             "pt",       "pt"    },
    { "Argentina",          "es_AR.UTF-8",  "America",   "Argentina/Buenos_Aires", "la-latin1", "latam" },
    { "Armenia",            "hy_AM.UTF-8",  "Asia",      "Yerevan",            "us",       "us"    },
    { "Australia",          "en_AU.UTF-8",  "Australia", "Sydney",             "us",       "us"    },
    { "Austria",            "de_AT.UTF-8",  "Europe",    "Vienna",             "de",       "de"    },
    { "Azerbaijan",         "az_AZ.UTF-8",  "Asia",      "Baku",               "us",       "us"    },
    { "Bahrain",            "ar_BH.UTF-8",  "Asia",      "Bahrain",            "us",       "us"    },
    { "Bangladesh",         "bn_BD.UTF-8",  "Asia",      "Dhaka",              "us",       "us"    },
    { "Belarus",            "be_BY.UTF-8",  "Europe",    "Minsk",              "by",       "by"    },
    { "Belgium",            "fr_BE.UTF-8",  "Europe",    "Brussels",           "fr",       "fr"    },
    { "Bolivia",            "es_BO.UTF-8",  "America",   "La_Paz",             "la-latin1", "latam" },
    { "Bosnia",             "bs_BA.UTF-8",  "Europe",    "Sarajevo",           "ba",       "ba"    },
    { "Brazil",             "pt_BR.UTF-8",  "America",   "Sao_Paulo",          "br-abnt2", "br"    },
    { "Bulgaria",           "bg_BG.UTF-8",  "Europe",    "Sofia",              "bg",       "bg"    },
    { "Cambodia",           "km_KH.UTF-8",  "Asia",      "Phnom_Penh",         "us",       "us"    },
    { "Cameroon",           "fr_CM.UTF-8",  "Africa",    "Douala",             "fr",       "fr"    },
    { "Canada",             "en_CA.UTF-8",  "America",   "Toronto",            "us",       "us"    },
    { "Chile",              "es_CL.UTF-8",  "America",   "Santiago",           "la-latin1", "latam" },
    { "China",              "zh_CN.UTF-8",  "Asia",      "Shanghai",           "us",       "us"    },
    { "Colombia",           "es_CO.UTF-8",  "America",   "Bogota",             "la-latin1", "latam" },
    { "Congo (DRC)",        "fr_CD.UTF-8",  "Africa",    "Kinshasa",           "fr",       "fr"    },
    { "Costa Rica",         "es_CR.UTF-8",  "America",   "Costa_Rica",         "la-latin1", "latam" },
    { "Croatia",            "hr_HR.UTF-8",  "Europe",    "Zagreb",             "hr",       "hr"    },
    { "Cuba",               "es_CU.UTF-8",  "America",   "Havana",             "la-latin1", "latam" },
    { "Cyprus",             "el_CY.UTF-8",  "Asia",      "Nicosia",            "gr",       "gr"    },
    { "Czech Republic",     "cs_CZ.UTF-8",  "Europe",    "Prague",             "cz",       "cz"    },
    { "Denmark",            "da_DK.UTF-8",  "Europe",    "Copenhagen",         "dk",       "dk"    },
    { "Dominican Republic", "es_DO.UTF-8",  "America",   "Santo_Domingo",      "la-latin1", "latam" },
    { "Ecuador",            "es_EC.UTF-8",  "America",   "Guayaquil",          "la-latin1", "latam" },
    { "Egypt",              "ar_EG.UTF-8",  "Africa",    "Cairo",              "us",       "us"    },
    { "El Salvador",        "es_SV.UTF-8",  "America",   "El_Salvador",        "la-latin1", "latam" },
    { "Estonia",            "et_EE.UTF-8",  "Europe",    "Tallinn",            "ee",       "ee"    },
    { "Ethiopia",           "am_ET.UTF-8",  "Africa",    "Addis_Ababa",        "us",       "us"    },
    { "Finland",            "fi_FI.UTF-8",  "Europe",    "Helsinki",           "fi",       "fi"    },
    { "France",             "fr_FR.UTF-8",  "Europe",    "Paris",              "fr",       "fr"    },
    { "Georgia",            "ka_GE.UTF-8",  "Asia",      "Tbilisi",            "us",       "us"    },
    { "Germany",            "de_DE.UTF-8",  "Europe",    "Berlin",             "de",       "de"    },
    { "Ghana",              "en_GH.UTF-8",  "Africa",    "Accra",              "us",       "us"    },
    { "Greece",             "el_GR.UTF-8",  "Europe",    "Athens",             "gr",       "gr"    },
    { "Guatemala",          "es_GT.UTF-8",  "America",   "Guatemala",          "la-latin1", "latam" },
    { "Haiti",              "fr_HT.UTF-8",  "America",   "Port-au-Prince",     "fr",       "fr"    },
    { "Honduras",           "es_HN.UTF-8",  "America",   "Tegucigalpa",        "la-latin1", "latam" },
    { "Hong Kong",          "zh_HK.UTF-8",  "Asia",      "Hong_Kong",          "us",       "us"    },
    { "Hungary",            "hu_HU.UTF-8",  "Europe",    "Budapest",           "hu",       "hu"    },
    { "Iceland",            "is_IS.UTF-8",  "Atlantic",  "Reykjavik",          "is",       "is"    },
    { "India",              "hi_IN.UTF-8",  "Asia",      "Kolkata",            "us",       "us"    },
    { "Indonesia",          "id_ID.UTF-8",  "Asia",      "Jakarta",            "us",       "us"    },
    { "Iran",               "fa_IR.UTF-8",  "Asia",      "Tehran",             "us",       "us"    },
    { "Iraq",               "ar_IQ.UTF-8",  "Asia",      "Baghdad",            "us",       "us"    },
    { "Ireland",            "en_IE.UTF-8",  "Europe",    "Dublin",             "uk",       "gb"    },
    { "Israel",             "he_IL.UTF-8",  "Asia",      "Jerusalem",          "il",       "il"    },
    { "Italy",              "it_IT.UTF-8",  "Europe",    "Rome",               "it",       "it"    },
    { "Jamaica",            "en_JM.UTF-8",  "America",   "Jamaica",            "us",       "us"    },
    { "Japan",              "ja_JP.UTF-8",  "Asia",      "Tokyo",              "jp",       "jp"    },
    { "Jordan",             "ar_JO.UTF-8",  "Asia",      "Amman",              "us",       "us"    },
    { "Kazakhstan",         "kk_KZ.UTF-8",  "Asia",      "Almaty",             "kz",       "kz"    },
    { "Kenya",              "sw_KE.UTF-8",  "Africa",    "Nairobi",            "us",       "us"    },
    { "Kuwait",             "ar_KW.UTF-8",  "Asia",      "Kuwait",             "us",       "us"    },
    { "Latvia",             "lv_LV.UTF-8",  "Europe",    "Riga",               "lv",       "lv"    },
    { "Lebanon",            "ar_LB.UTF-8",  "Asia",      "Beirut",             "us",       "us"    },
    { "Libya",              "ar_LY.UTF-8",  "Africa",    "Tripoli",            "us",       "us"    },
    { "Lithuania",          "lt_LT.UTF-8",  "Europe",    "Vilnius",            "lt",       "lt"    },
    { "Luxembourg",         "fr_LU.UTF-8",  "Europe",    "Luxembourg",         "fr",       "fr"    },
    { "Malaysia",           "ms_MY.UTF-8",  "Asia",      "Kuala_Lumpur",       "us",       "us"    },
    { "Mexico",             "es_MX.UTF-8",  "America",   "Mexico_City",        "la-latin1", "latam" },
    { "Moldova",            "ro_MD.UTF-8",  "Europe",    "Chisinau",           "ro",       "ro"    },
    { "Mongolia",           "mn_MN.UTF-8",  "Asia",      "Ulaanbaatar",        "us",       "us"    },
    { "Montenegro",         "sr_ME.UTF-8",  "Europe",    "Podgorica",          "rs",       "rs"    },
    { "Morocco",            "ar_MA.UTF-8",  "Africa",    "Casablanca",         "us",       "us"    },
    { "Mozambique",         "pt_MZ.UTF-8",  "Africa",    "Maputo",             "pt",       "pt"    },
    { "Myanmar",            "my_MM.UTF-8",  "Asia",      "Yangon",             "us",       "us"    },
    { "Nepal",              "ne_NP.UTF-8",  "Asia",      "Kathmandu",          "us",       "us"    },
    { "Netherlands",        "nl_NL.UTF-8",  "Europe",    "Amsterdam",          "nl",       "nl"    },
    { "New Zealand",        "en_NZ.UTF-8",  "Pacific",   "Auckland",           "us",       "us"    },
    { "Nicaragua",          "es_NI.UTF-8",  "America",   "Managua",            "la-latin1", "latam" },
    { "Nigeria",            "en_NG.UTF-8",  "Africa",    "Lagos",              "us",       "us"    },
    { "North Macedonia",    "mk_MK.UTF-8",  "Europe",    "Skopje",             "mk",       "mk"    },
    { "Norway",             "nb_NO.UTF-8",  "Europe",    "Oslo",               "no",       "no"    },
    { "Oman",               "ar_OM.UTF-8",  "Asia",      "Muscat",             "us",       "us"    },
    { "Pakistan",           "ur_PK.UTF-8",  "Asia",      "Karachi",            "pk",       "pk"    },
    { "Panama",             "es_PA.UTF-8",  "America",   "Panama",             "la-latin1", "latam" },
    { "Paraguay",           "es_PY.UTF-8",  "America",   "Asuncion",           "la-latin1", "latam" },
    { "Peru",               "es_PE.UTF-8",  "America",   "Lima",               "la-latin1", "latam" },
    { "Philippines",        "en_PH.UTF-8",  "Asia",      "Manila",             "us",       "us"    },
    { "Poland",             "pl_PL.UTF-8",  "Europe",    "Warsaw",             "pl",       "pl"    },
    { "Portugal",           "pt_PT.UTF-8",  "Europe",    "Lisbon",             "pt",       "pt"    },
    { "Puerto Rico",        "es_PR.UTF-8",  "America",   "Puerto_Rico",        "la-latin1", "latam" },
    { "Qatar",              "ar_QA.UTF-8",  "Asia",      "Qatar",              "us",       "us"    },
    { "Romania",            "ro_RO.UTF-8",  "Europe",    "Bucharest",          "ro",       "ro"    },
    { "Russia",             "ru_RU.UTF-8",  "Europe",    "Moscow",             "ru",       "ru"    },
    { "Saudi Arabia",       "ar_SA.UTF-8",  "Asia",      "Riyadh",             "us",       "us"    },
    { "Senegal",            "fr_SN.UTF-8",  "Africa",    "Dakar",              "fr",       "fr"    },
    { "Serbia",             "sr_RS.UTF-8",  "Europe",    "Belgrade",           "rs",       "rs"    },
    { "Singapore",          "en_SG.UTF-8",  "Asia",      "Singapore",          "us",       "us"    },
    { "Slovakia",           "sk_SK.UTF-8",  "Europe",    "Bratislava",         "sk",       "sk"    },
    { "Slovenia",           "sl_SI.UTF-8",  "Europe",    "Ljubljana",          "si",       "si"    },
    { "South Africa",       "en_ZA.UTF-8",  "Africa",    "Johannesburg",       "us",       "us"    },
    { "South Korea",        "ko_KR.UTF-8",  "Asia",      "Seoul",              "kr",       "kr"    },
    { "Spain",              "es_ES.UTF-8",  "Europe",    "Madrid",             "es",       "es"    },
    { "Sri Lanka",          "si_LK.UTF-8",  "Asia",      "Colombo",            "us",       "us"    },
    { "Sudan",              "ar_SD.UTF-8",  "Africa",    "Khartoum",           "us",       "us"    },
    { "Sweden",             "sv_SE.UTF-8",  "Europe",    "Stockholm",          "se",       "se"    },
    { "Switzerland",        "de_CH.UTF-8",  "Europe",    "Zurich",             "ch",       "ch"    },
    { "Syria",              "ar_SY.UTF-8",  "Asia",      "Damascus",           "us",       "us"    },
    { "Taiwan",             "zh_TW.UTF-8",  "Asia",      "Taipei",             "us",       "us"    },
    { "Tanzania",           "sw_TZ.UTF-8",  "Africa",    "Dar_es_Salaam",      "us",       "us"    },
    { "Thailand",           "th_TH.UTF-8",  "Asia",      "Bangkok",            "th",       "th"    },
    { "Trinidad & Tobago",  "en_TT.UTF-8",  "America",   "Port_of_Spain",      "us",       "us"    },
    { "Tunisia",            "ar_TN.UTF-8",  "Africa",    "Tunis",              "us",       "us"    },
    { "Turkey",             "tr_TR.UTF-8",  "Europe",    "Istanbul",           "tr",       "tr"    },
    { "Uganda",             "en_UG.UTF-8",  "Africa",    "Kampala",            "us",       "us"    },
    { "Ukraine",            "uk_UA.UTF-8",  "Europe",    "Kiev",               "ua",       "ua"    },
    { "United Arab Emirates","ar_AE.UTF-8", "Asia",      "Dubai",              "us",       "us"    },
    { "United Kingdom",     "en_GB.UTF-8",  "Europe",    "London",             "uk",       "gb"    },
    { "United States",      "en_US.UTF-8",  "America",   "New_York",           "us",       "us"    },
    { "Uruguay",            "es_UY.UTF-8",  "America",   "Montevideo",         "la-latin1", "latam" },
    { "Uzbekistan",         "uz_UZ.UTF-8",  "Asia",      "Tashkent",           "us",       "us"    },
    { "Venezuela",          "es_VE.UTF-8",  "America",   "Caracas",            "la-latin1", "latam" },
    { "Vietnam",            "vi_VN.UTF-8",  "Asia",      "Ho_Chi_Minh",        "us",       "us"    },
    { "Yemen",              "ar_YE.UTF-8",  "Asia",      "Aden",               "us",       "us"    },
    { "Zambia",             "en_ZM.UTF-8",  "Africa",    "Lusaka",             "us",       "us"    },
    { "Zimbabwe",           "en_ZW.UTF-8",  "Africa",    "Harare",             "us",       "us"    },
};

static const int country_count = sizeof(country_list) / sizeof(country_list[0]);

const CountryInfo* get_country_list(int *count) {
    if (count) *count = country_count;
    return country_list;
}

const CountryInfo* find_country_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < country_count; i++) {
        if (strcmp(country_list[i].name, name) == 0) {
            return &country_list[i];
        }
    }
    return NULL;
}

// =====================================================================
// Keyboard Layout Database
// =====================================================================

// Variant arrays (NULL-terminated)
static const KbdVariant none_variants[]  = { {NULL, NULL} };

static const KbdVariant us_variants[] = {
    { "",           "Default"                 },
    { "dvorak",     "Dvorak"                  },
    { "colemak",    "Colemak"                 },
    { "altgr-intl", "Intl (AltGr dead keys)"  },
    { NULL, NULL }
};

static const KbdVariant gb_variants[] = {
    { "",       "Default"              },
    { "extd",   "Extended (Win keys)"  },
    { "intl",   "Intl (dead keys)"     },
    { "dvorak", "Dvorak"               },
    { NULL, NULL }
};

static const KbdVariant es_variants[] = {
    { "",          "Default"     },
    { "cat",       "Catalan"     },
    { "ast",       "Asturian"    },
    { "deadtilde", "Dead tilde"  },
    { "dvorak",    "Dvorak"      },
    { NULL, NULL }
};

static const KbdVariant latam_variants[] = {
    { "",          "Default"     },
    { "deadtilde", "Dead tilde"  },
    { "dvorak",    "Dvorak"      },
    { "suntype6",  "Sun Type 6"  },
    { NULL, NULL }
};

static const KbdVariant de_variants[] = {
    { "",           "Default"       },
    { "nodeadkeys", "No dead keys"  },
    { "dvorak",     "Dvorak"        },
    { "neo",        "Neo"           },
    { NULL, NULL }
};

static const KbdVariant fr_variants[] = {
    { "",           "Default"               },
    { "nodeadkeys", "No dead keys"          },
    { "oss",        "OSS (Sun dead keys)"   },
    { "latin9",     "Latin9"                },
    { "bepo",       "Bepo (ergonomic)"      },
    { "dvorak",     "Dvorak"                },
    { NULL, NULL }
};

static const KbdVariant it_variants[] = {
    { "",           "Default"       },
    { "nodeadkeys", "No dead keys"  },
    { NULL, NULL }
};

static const KbdVariant pt_variants[] = {
    { "",           "Default"       },
    { "nodeadkeys", "No dead keys"  },
    { "nativo",     "Nativo"        },
    { NULL, NULL }
};

static const KbdVariant br_variants[] = {
    { "",           "Default"       },
    { "nodeadkeys", "No dead keys"  },
    { "dvorak",     "Dvorak"        },
    { NULL, NULL }
};

static const KbdVariant jp_variants[] = {
    { "",     "Default" },
    { "kana", "Kana"    },
    { NULL, NULL }
};

static const KbdVariant ru_variants[] = {
    { "",         "Default"   },
    { "phonetic", "Phonetic"  },
    { NULL, NULL }
};

static const KbdLayout keyboard_layouts[] = {
    { "us",     "English (US)",              "us",         us_variants,    4 },
    { "gb",     "English (UK)",              "uk",         gb_variants,    4 },
    { "es",     "Spanish",                   "es",         es_variants,    5 },
    { "latam",  "Spanish (Latin American)",  "la-latin1",  latam_variants, 4 },
    { "de",     "German",                    "de",         de_variants,    4 },
    { "fr",     "French",                    "fr",         fr_variants,    6 },
    { "it",     "Italian",                   "it",         it_variants,    2 },
    { "pt",     "Portuguese",                "pt",         pt_variants,    3 },
    { "br",     "Portuguese (Brazil)",       "br-abnt2",   br_variants,    3 },
    { "jp",     "Japanese",                  "jp",         jp_variants,    2 },
    { "kr",     "Korean",                    "kr",         none_variants,  0 },
    { "ru",     "Russian",                   "ru",         ru_variants,    2 },
    { "se",     "Swedish",                   "se",         none_variants,  0 },
    { "no",     "Norwegian",                 "no",         none_variants,  0 },
    { "dk",     "Danish",                    "dk",         none_variants,  0 },
    { "fi",     "Finnish",                   "fi",         none_variants,  0 },
    { "nl",     "Dutch",                     "nl",         none_variants,  0 },
    { "pl",     "Polish",                    "pl",         none_variants,  0 },
    { "cz",     "Czech",                     "cz",         none_variants,  0 },
    { "hu",     "Hungarian",                 "hu",         none_variants,  0 },
    { "tr",     "Turkish",                   "tr",         none_variants,  0 },
    { "gr",     "Greek",                     "gr",         none_variants,  0 },
    { "il",     "Hebrew",                    "il",         none_variants,  0 },
    { "th",     "Thai",                      "th",         none_variants,  0 },
    { "ua",     "Ukrainian",                 "ua",         none_variants,  0 },
    { "by",     "Belarusian",                "by",         none_variants,  0 },
    { "hr",     "Croatian",                  "hr",         none_variants,  0 },
    { "rs",     "Serbian",                   "rs",         none_variants,  0 },
    { "si",     "Slovenian",                 "si",         none_variants,  0 },
    { "sk",     "Slovak",                    "sk",         none_variants,  0 },
    { "ba",     "Bosnian",                   "ba",         none_variants,  0 },
    { "mk",     "Macedonian",                "mk",         none_variants,  0 },
    { "bg",     "Bulgarian",                 "bg",         none_variants,  0 },
    { "ro",     "Romanian",                  "ro",         none_variants,  0 },
    { "lt",     "Lithuanian",                "lt",         none_variants,  0 },
    { "lv",     "Latvian",                   "lv",         none_variants,  0 },
    { "ee",     "Estonian",                  "ee",         none_variants,  0 },
    { "pk",     "Urdu (Pakistan)",           "pk",         none_variants,  0 },
    { "kz",     "Kazakh",                    "kz",         none_variants,  0 },
    { "ch",     "Swiss German",              "ch",         none_variants,  0 },
};

static const int kbd_layout_count = sizeof(keyboard_layouts) / sizeof(keyboard_layouts[0]);

const KbdLayout* get_keyboard_layouts(int *count) {
    if (count) *count = kbd_layout_count;
    return keyboard_layouts;
}

const KbdLayout* find_keyboard_layout(const char *layout_code) {
    if (!layout_code) return &keyboard_layouts[0]; /* default US */
    for (int i = 0; i < kbd_layout_count; i++) {
        if (strcmp(keyboard_layouts[i].layout, layout_code) == 0) {
            return &keyboard_layouts[i];
        }
    }
    return &keyboard_layouts[0]; /* fallback US */
}
