#ifndef SUPERLANG_LEXER_H
#define SUPERLANG_LEXER_H

#include "tokens.h"

typedef struct {
    const char *source;
    size_t pos;
    size_t length;
    int line;
    int column;
} Lexer;

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
    char *error_message;
    int error_line;
    int error_column;
} TokenList;

/* Initialize lexer with source code */
Lexer lexer_new(const char *source);

/* Tokenize entire source, returns token list */
TokenList lexer_tokenize(const char *source);

/* Free token list */
void token_list_free(TokenList *list);

/* Dump tokens for debugging */
void token_list_dump(const TokenList *list);

#endif
