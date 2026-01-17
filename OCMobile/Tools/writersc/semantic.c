#include "ast.h"
#include <stdio.h>
#include <string.h>

void validate_driver(ASTNode *d) {
    int has_init = 0;
    ASTNode *fn = d->driver.body;

    while (fn) {
        if (fn->kind == AST_FUNCTION &&
            strcmp(fn->function.name, "init") == 0) {
            has_init = 1;
        }
        fn = fn->next;
    }

    if (!has_init) {
        fprintf(stderr, "Driver missing init() function\n");
    }
}
