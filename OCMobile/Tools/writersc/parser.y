%{
#include "ast.h"
#include <stdio.h>

extern int yylex();
void yyerror(const char *s);
ASTNode *root;
%}

%union {
    char *string;
    unsigned long long integer;
    int boolean;
    ASTNode *node;
}

%token DRIVER STRUCT FN INIT REQUIRES EXPORTS RETURN IF ELSE UNSAFE
%token TYPE IDENT INTEGER BOOL
%token ARROW

%type <node> program driver fn_decl block stmt expr

%%

program:
    driver { root = $1; }
;

driver:
    DRIVER IDENT '{' driver_body '}' {
        $$ = ast_driver($2, $4);
    }
;

driver_body:
    fn_decl
    | driver_body fn_decl
;

fn_decl:
    FN IDENT '(' ')' ARROW TYPE block {
        $$ = ast_function($2, $6, $7);
    }
;

block:
    '{' stmt_list '}' {
        $$ = ast_block($2);
    }
;

stmt_list:
    stmt
    | stmt_list stmt
;

stmt:
    RETURN expr ';' {
        $$ = ast_return($2);
    }
;

expr:
    INTEGER {
        $$ = ast_integer($1);
    }
    | BOOL {
        $$ = ast_bool($1);
    }
;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Parser error: %s\n", s);
}
