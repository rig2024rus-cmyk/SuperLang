#ifndef SUPERLANG_PARSER_H
#define SUPERLANG_PARSER_H

#include "ast_nodes.h"
#include "lexer.h"

typedef struct {
    Program *program;
    int is_valid;
    char *error_message;
    int error_line;
    int error_column;
} ParseResult;

/* Parse token list into AST */
ParseResult parser_parse(const TokenList *tokens);

/* Free parse result */
void parse_result_free(ParseResult *result);

#endif
