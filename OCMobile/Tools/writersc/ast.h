#ifndef AST_H
#define AST_H

typedef enum {
    AST_DRIVER,
    AST_FUNCTION,
    AST_BLOCK,
    AST_RETURN,
    AST_INTEGER,
    AST_BOOL
} ASTKind;

typedef struct ASTNode {
    ASTKind kind;
    union {
        struct {
            char *name;
            struct ASTNode *body;
        } driver;

        struct {
            char *name;
            char *ret_type;
            struct ASTNode *body;
        } function;

        struct {
            struct ASTNode *statements;
        } block;

        struct {
            struct ASTNode *value;
        } ret;

        unsigned long long integer;
        int boolean;
    };
    struct ASTNode *next;
} ASTNode;

ASTNode *ast_driver(char *name, ASTNode *body);
ASTNode *ast_function(char *name, char *ret, ASTNode *body);
ASTNode *ast_block(ASTNode *stmts);
ASTNode *ast_return(ASTNode *value);
ASTNode *ast_integer(unsigned long long v);
ASTNode *ast_bool(int v);

#endif
