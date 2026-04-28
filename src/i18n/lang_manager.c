/*
 * lang_manager.c
 * Language manager - loads all language modules
 */
#include "lang.h"
#include <string.h>

extern const LangInfo *lang_en_module(void);
extern const LangInfo *lang_es_module(void);
extern const LangInfo *lang_ja_module(void);

static const LangInfo *all_langs[3];

void lang_init(void) {
    all_langs[0] = lang_en_module();
    all_langs[1] = lang_es_module();
    all_langs[2] = lang_ja_module();
}

const LangInfo* get_lang(const char *code) {
    if (code == NULL) return all_langs[0];
    
    for (int i = 0; i < 3; i++) {
        if (all_langs[i] && strcmp(all_langs[i]->code, code) == 0) {
            return all_langs[i];
        }
    }
    return all_langs[0]; // default to English
}

const char** get_available_langs(void) {
    static const char *codes[] = { "en", "es", "ja" };
    return codes;
}

int get_lang_count(void) {
    return 3;
}