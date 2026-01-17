#include "ast.h"
#include <stdlib.h>

ASTNode *new_node(ASTKind k) {
    ASTNode *n = calloc(1, sizeof(ASTNode));
    n->kind = k;
    return n;
}

ASTNode *ast_driver(char *name, ASTNode *body) {
    ASTNode *n = new_node(AST_DRIVER);
    n->driver.name = name;
    n->driver.body = body;
    return n;
}

ASTNode *ast_function(char *name, char *ret, ASTNode *body) {
    ASTNode *n = new_node(AST_FUNCTION);
    n->function.name = name;
    n->function.ret_type = ret;
    n->function.body = body;
    return n;
}

ASTNode *ast_block(ASTNode *stmts) {
    ASTNode *n = new_node(AST_BLOCK);
    n->block.statements = stmts;
    return n;
}

ASTNode *ast_return(ASTNode *value) {
    ASTNode *n = new_node(AST_RETURN);
    n->ret.value = value;
    return n;
}

ASTNode *ast_integer(unsigned long long v) {
    ASTNode *n = new_node(AST_INTEGER);
    n->integer = v;
    return n;
}

ASTNode *ast_bool(int v) {
    ASTNode *n = new_node(AST_BOOL);
    n->boolean = v;
    return n;
}
