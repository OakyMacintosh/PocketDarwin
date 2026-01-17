#include "ast.h"
#include <stdio.h>

extern int yyparse();
extern ASTNode *root;

int main(int argc, char **argv) {
    if (yyparse() != 0) {
        return 1;
    }

    printf("Parsed WriteSc driver successfully.\n");
    // semantic validation here
    // codegen here

    return 0;
}
