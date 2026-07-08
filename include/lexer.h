#ifndef SUPERLANG_LEXER_H
#define SUPERLANG_LEXER_H

#include "tokens.h"
#include <stddef.h>

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
    char *error_message;
    int error_line;
    int error_column;
} TokenList;

typedef struct {
    const char *source;
    size_t pos;
    size_t length;
    int line;
    int column;
} Lexer;

TokenList lexer_tokenize(const char *source);
void token_list_free(TokenList *list);
void token_list_dump(const TokenList *list);

#endif
