%{
#include "parser.tab.h"
#include <string.h>
%}

%x COMMENT

%%
"driver"        return DRIVER;
"struct"        return STRUCT;
"fn"            return FN;
"init"          return INIT;
"requires"      return REQUIRES;
"exports"       return EXPORTS;
"return"        return RETURN;
"if"            return IF;
"else"          return ELSE;
"unsafe"        return UNSAFE;

"true"          { yylval.boolean = 1; return BOOL; }
"false"         { yylval.boolean = 0; return BOOL; }

"u8"|"u16"|"u32"|"u64"|"i8"|"i16"|"i32"|"i64"|"bool"|"usize"
                { yylval.string = strdup(yytext); return TYPE; }

[0-9]+          { yylval.integer = strtoull(yytext, 0, 10); return INTEGER; }

[a-zA-Z_][a-zA-Z0-9_]*
                { yylval.string = strdup(yytext); return IDENT; }

"{"             return '{';
"}"             return '}';
"("             return '(';
")"             return ')';
":"             return ':';
";"             return ';';
","             return ',';
"->"            return ARROW;
"="             return '=';

"//".*          ;
"/*"            BEGIN(COMMENT);
<COMMENT>"*/"   BEGIN(INITIAL);
<COMMENT>.|\n   ;

[ \t\r\n]+      ;
.               { printf("Unknown character: %c\n", yytext[0]); }

%%
