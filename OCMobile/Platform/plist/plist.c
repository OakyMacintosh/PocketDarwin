#include "plist.h"
#include <string.h>
#include <ctype.h>

/* ---------- helpers ---------- */

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

static int starts_with(const char *p, const char *s) {
    return strncmp(p, s, strlen(s)) == 0;
}

static const char *find_tag_end(const char *p) {
    while (*p && *p != '>')
        p++;
    return (*p == '>') ? p + 1 : NULL;
}

static const char *extract_text(
    const char *p,
    const char *end_tag,
    const char **out_text,
    size_t *out_len
) {
    const char *start = p;
    const char *end = strstr(p, end_tag);
    if (!end)
        return NULL;

    *out_text = start;
    *out_len = (size_t)(end - start);
    return end + strlen(end_tag);
}

/* ---------- core ---------- */

int plist_parse_xml(
    const char *buf,
    size_t size,
    plist_dict_t *dict
) {
    const char *p = buf;
    const char *end = buf + size;

    if (size >= 8 && memcmp(buf, "bplist00", 8) == 0)
        return -1; /* binary plist: hard no */

    memset(dict, 0, sizeof(*dict));

    p = skip_ws(p);

    /* skip XML header if present */
    if (starts_with(p, "<?xml"))
        p = strstr(p, "?>") + 2;

    p = skip_ws(p);

    if (!starts_with(p, "<plist"))
        return -1;

    p = strstr(p, ">") + 1;
    p = skip_ws(p);

    if (!starts_with(p, "<dict>"))
        return -1;

    p += 6;

    while (p < end) {
        const char *key_text;
        size_t key_len;

        p = skip_ws(p);

        if (starts_with(p, "</dict>"))
            break;

        if (!starts_with(p, "<key>"))
            return -1;

        p += 5;

        p = extract_text(p, "</key>", &key_text, &key_len);
        if (!p)
            return -1;

        /* store key pointer directly */
        plist_entry_t *e = &dict->entries[dict->count];
        e->key = key_text;

        /* null-terminate in-place */
        ((char *)key_text)[key_len] = '\0';

        p = skip_ws(p);

        if (starts_with(p, "<string>")) {
            const char *txt;
            size_t len;

            p += 8;
            p = extract_text(p, "</string>", &txt, &len);
            if (!p)
                return -1;

            ((char *)txt)[len] = '\0';

            e->type = PLIST_STRING;
            e->value.string = txt;
        }
        else if (starts_with(p, "<integer>")) {
            const char *txt;
            size_t len;

            p += 9;
            p = extract_text(p, "</integer>", &txt, &len);
            if (!p)
                return -1;

            char tmp[32];
            if (len >= sizeof(tmp))
                return -1;

            memcpy(tmp, txt, len);
            tmp[len] = '\0';

            e->type = PLIST_INTEGER;
            e->value.integer = strtol(tmp, NULL, 10);
        }
        else if (starts_with(p, "<true/>")) {
            e->type = PLIST_BOOL;
            e->value.boolean = 1;
            p += 7;
        }
        else if (starts_with(p, "<false/>")) {
            e->type = PLIST_BOOL;
            e->value.boolean = 0;
            p += 8;
        }
        else {
            return -1;
        }

        dict->count++;
    }

    return 0;
}

const plist_entry_t *plist_get(
    const plist_dict_t *dict,
    const char *key
) {
    for (size_t i = 0; i < dict->count; i++) {
        if (strcmp(dict->entries[i].key, key) == 0)
            return &dict->entries[i];
    }
    return NULL;
}
