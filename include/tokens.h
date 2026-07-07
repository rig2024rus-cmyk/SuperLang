#ifndef SUPERLANG_TOKENS_H
#define SUPERLANG_TOKENS_H

#include <stddef.h>

typedef enum {
    /* Keywords */
    TOK_ENTITY,
    TOK_RELATION,
    TOK_OBSERVE,
    TOK_DERIVE,
    TOK_WHERE,
    TOK_FROM,
    TOK_INPUT,
    TOK_QUERY,
    TOK_NOT,
    TOK_AND,
    TOK_SUM,
    TOK_COUNT,
    TOK_MIN,
    TOK_MAX,
    
    /* Literals and identifiers */
    TOK_IDENT,
    TOK_STRING,
    TOK_NUMBER,
    
    /* Delimiters */
    TOK_LPAREN,    /* ( */
    TOK_RPAREN,    /* ) */
    TOK_LBRACE,    /* { */
    TOK_RBRACE,    /* } */
    TOK_COMMA,     /* , */
    
    /* Operators */
    TOK_EQUALS,     /* == */
    TOK_NOT_EQUALS, /* != */
    TOK_LESS,       /* < */
    TOK_GREATER,    /* > */
    TOK_LESS_EQ,    /* <= */
    TOK_GREATER_EQ, /* >= */
    TOK_ASSIGN,     /* = (single equals for aggregates/arithmetic) */
    
    /* Arithmetic operators */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
    TOK_SLASH,      /* / */
    
    /* Special */
    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int line;
    int column;
} Token;

const char *token_type_str(TokenType t);
void token_free(Token *t);

#endif
