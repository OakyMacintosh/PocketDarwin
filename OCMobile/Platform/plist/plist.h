#ifndef PLIST_H
#define PLIST_H

#include <stddef.h>

typedef enum {
    PLIST_STRING,
    PLIST_INTEGER,
    PLIST_BOOL
} plist_type_t;

typedef struct {
    const char *key;

    plist_type_t type;
    union {
        const char *string;
        long integer;
        int boolean;
    } value;
} plist_entry_t;

typedef struct {
    plist_entry_t *entries;
    size_t count;
    size_t capacity;
} plist_dict_t;

int plist_parse_xml(
    const char *buffer,
    size_t size,
    plist_dict_t *out_dict
);

const plist_entry_t *plist_get(
    const plist_dict_t *dict,
    const char *key
);

#endif
