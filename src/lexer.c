#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char *word;
    TokenType type;
} Keyword;

static const Keyword keywords[] = {
    {"entity", TOK_ENTITY},
    {"relation", TOK_RELATION},
    {"observe", TOK_OBSERVE},
    {"derive", TOK_DERIVE},
    {"where", TOK_WHERE},
    {"from", TOK_FROM},
    {"input", TOK_INPUT},
    {"query", TOK_QUERY},
    {"not", TOK_NOT},
    {"and", TOK_AND},
    {"sum", TOK_SUM},
    {"count", TOK_COUNT},
    {"min", TOK_MIN},
    {"max", TOK_MAX},
    {NULL, TOK_EOF}
};

const char *token_type_str(TokenType t) {
    switch (t) {
        case TOK_ENTITY: return "ENTITY";
        case TOK_RELATION: return "RELATION";
        case TOK_OBSERVE: return "OBSERVE";
        case TOK_DERIVE: return "DERIVE";
        case TOK_WHERE: return "WHERE";
        case TOK_FROM: return "FROM";
        case TOK_INPUT: return "INPUT";
        case TOK_QUERY: return "QUERY";
        case TOK_NOT: return "NOT";
        case TOK_AND: return "AND";
        case TOK_SUM: return "SUM";
        case TOK_COUNT: return "COUNT";
        case TOK_MIN: return "MIN";
        case TOK_MAX: return "MAX";
        case TOK_IDENT: return "IDENT";
        case TOK_STRING: return "STRING";
        case TOK_NUMBER: return "NUMBER";
        case TOK_LPAREN: return "LPAREN";
        case TOK_RPAREN: return "RPAREN";
        case TOK_LBRACE: return "LBRACE";
        case TOK_RBRACE: return "RBRACE";
        case TOK_COMMA: return "COMMA";
        case TOK_EQUALS: return "EQUALS";
        case TOK_NOT_EQUALS: return "NOT_EQUALS";
        case TOK_LESS: return "LESS";
        case TOK_GREATER: return "GREATER";
        case TOK_LESS_EQ: return "LESS_EQ";
        case TOK_GREATER_EQ: return "GREATER_EQ";
        case TOK_ASSIGN: return "ASSIGN";
        case TOK_PLUS: return "PLUS";
        case TOK_MINUS: return "MINUS";
        case TOK_STAR: return "STAR";
        case TOK_SLASH: return "SLASH";
        case TOK_EOF: return "EOF";
    }
    return "?";
}

void token_free(Token *t) {
    if (t && t->value) {
        free(t->value);
        t->value = NULL;
    }
}

Lexer lexer_new(const char *source) {
    Lexer l;
    l.source = source;
    l.pos = 0;
    l.length = strlen(source);
    l.line = 1;
    l.column = 1;
    return l;
}

static char lexer_peek(Lexer *l, int offset) {
    size_t idx = l->pos + offset;
    if (idx < l->length) return l->source[idx];
    return '\0';
}

static char lexer_advance(Lexer *l) {
    char ch = l->source[l->pos];
    l->pos++;
    if (ch == '\n') {
        l->line++;
        l->column = 1;
    } else {
        l->column++;
    }
    return ch;
}

static void lexer_skip_whitespace(Lexer *l) {
    while (l->pos < l->length) {
        char ch = lexer_peek(l, 0);
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            lexer_advance(l);
        } else {
            break;
        }
    }
}

static void lexer_skip_comment(Lexer *l) {
    while (l->pos < l->length && lexer_peek(l, 0) != '\n') {
        lexer_advance(l);
    }
}

static char *lexer_read_identifier(Lexer *l) {
    size_t start = l->pos;
    while (l->pos < l->length) {
        char ch = lexer_peek(l, 0);
        if (isalnum(ch) || ch == '_') {
            lexer_advance(l);
        } else {
            break;
        }
    }
    size_t len = l->pos - start;
    char *word = malloc(len + 1);
    memcpy(word, l->source + start, len);
    word[len] = '\0';
    return word;
}

static char *lexer_read_number(Lexer *l) {
    size_t start = l->pos;
    while (l->pos < l->length) {
        char ch = lexer_peek(l, 0);
        if (isdigit(ch) || ch == '.') {
            lexer_advance(l);
        } else {
            break;
        }
    }
    size_t len = l->pos - start;
    char *num = malloc(len + 1);
    memcpy(num, l->source + start, len);
    num[len] = '\0';
    return num;
}

static char *lexer_read_string(Lexer *l) {
    lexer_advance(l);
    size_t start = l->pos;
    while (l->pos < l->length && lexer_peek(l, 0) != '"') {
        lexer_advance(l);
    }
    size_t len = l->pos - start;
    char *str = malloc(len + 1);
    memcpy(str, l->source + start, len);
    str[len] = '\0';
    if (l->pos < l->length) {
        lexer_advance(l);
    }
    return str;
}

static TokenType lookup_keyword(const char *word) {
    for (int i = 0; keywords[i].word != NULL; i++) {
        if (strcmp(word, keywords[i].word) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENT;
}

static void token_list_add(TokenList *list, TokenType type, char *value, int line, int column) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity == 0 ? 32 : list->capacity * 2;
        list->tokens = realloc(list->tokens, sizeof(Token) * list->capacity);
    }
    Token *t = &list->tokens[list->count++];
    t->type = type;
    t->value = value;
    t->line = line;
    t->column = column;
}

static void set_error(TokenList *list, const char *msg, int line, int column) {
    if (list->error_message) free(list->error_message);
    list->error_message = strdup(msg);
    list->error_line = line;
    list->error_column = column;
}

TokenList lexer_tokenize(const char *source) {
    TokenList list;
    list.tokens = NULL;
    list.count = 0;
    list.capacity = 0;
    list.error_message = NULL;
    list.error_line = 0;
    list.error_column = 0;
    
    Lexer l = lexer_new(source);
    
    while (l.pos < l.length) {
        lexer_skip_whitespace(&l);
        if (l.pos >= l.length) break;
        
        char ch = lexer_peek(&l, 0);
        int start_line = l.line;
        int start_col = l.column;
        
        /* Comments */
        if (ch == '#') {
            lexer_skip_comment(&l);
            continue;
        }
        
        /* Identifiers / keywords */
        if (isalpha(ch) || ch == '_') {
            char *word = lexer_read_identifier(&l);
            TokenType type = lookup_keyword(word);
            token_list_add(&list, type, word, start_line, start_col);
            continue;
        }
        
        /* Numbers */
        if (isdigit(ch)) {
            char *num = lexer_read_number(&l);
            token_list_add(&list, TOK_NUMBER, num, start_line, start_col);
            continue;
        }
        
        /* Strings */
        if (ch == '"') {
            char *str = lexer_read_string(&l);
            token_list_add(&list, TOK_STRING, str, start_line, start_col);
            continue;
        }
        
        /* Two-character operators (must be checked BEFORE single-char) */
        if (ch == '=' && lexer_peek(&l, 1) == '=') {
            lexer_advance(&l); lexer_advance(&l);
            token_list_add(&list, TOK_EQUALS, strdup("=="), start_line, start_col);
            continue;
        }
        if (ch == '!' && lexer_peek(&l, 1) == '=') {
            lexer_advance(&l); lexer_advance(&l);
            token_list_add(&list, TOK_NOT_EQUALS, strdup("!="), start_line, start_col);
            continue;
        }
        if (ch == '<' && lexer_peek(&l, 1) == '=') {
            lexer_advance(&l); lexer_advance(&l);
            token_list_add(&list, TOK_LESS_EQ, strdup("<="), start_line, start_col);
            continue;
        }
        if (ch == '>' && lexer_peek(&l, 1) == '=') {
            lexer_advance(&l); lexer_advance(&l);
            token_list_add(&list, TOK_GREATER_EQ, strdup(">="), start_line, start_col);
            continue;
        }
        
        /* Single-character tokens */
        if (ch == '(') { lexer_advance(&l); token_list_add(&list, TOK_LPAREN, strdup("("), start_line, start_col); continue; }
        if (ch == ')') { lexer_advance(&l); token_list_add(&list, TOK_RPAREN, strdup(")"), start_line, start_col); continue; }
        if (ch == '{') { lexer_advance(&l); token_list_add(&list, TOK_LBRACE, strdup("{"), start_line, start_col); continue; }
        if (ch == '}') { lexer_advance(&l); token_list_add(&list, TOK_RBRACE, strdup("}"), start_line, start_col); continue; }
        if (ch == ',') { lexer_advance(&l); token_list_add(&list, TOK_COMMA, strdup(","), start_line, start_col); continue; }
        if (ch == '<') { lexer_advance(&l); token_list_add(&list, TOK_LESS, strdup("<"), start_line, start_col); continue; }
        if (ch == '>') { lexer_advance(&l); token_list_add(&list, TOK_GREATER, strdup(">"), start_line, start_col); continue; }
        if (ch == '=') { lexer_advance(&l); token_list_add(&list, TOK_ASSIGN, strdup("="), start_line, start_col); continue; }
        if (ch == '+') { lexer_advance(&l); token_list_add(&list, TOK_PLUS, strdup("+"), start_line, start_col); continue; }
        if (ch == '-') { lexer_advance(&l); token_list_add(&list, TOK_MINUS, strdup("-"), start_line, start_col); continue; }
        if (ch == '*') { lexer_advance(&l); token_list_add(&list, TOK_STAR, strdup("*"), start_line, start_col); continue; }
        if (ch == '/') { lexer_advance(&l); token_list_add(&list, TOK_SLASH, strdup("/"), start_line, start_col); continue; }
        
        /* Unknown character */
        lexer_advance(&l);
        char msg[128];
        snprintf(msg, sizeof(msg), "Unexpected character '%c'", ch);
        set_error(&list, msg, start_line, start_col);
        break;
    }
    
    /* EOF token */
    token_list_add(&list, TOK_EOF, strdup(""), l.line, l.column);
    
    return list;
}

void token_list_free(TokenList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        token_free(&list->tokens[i]);
    }
    free(list->tokens);
    if (list->error_message) free(list->error_message);
    list->tokens = NULL;
    list->count = 0;
    list->capacity = 0;
    list->error_message = NULL;
}

void token_list_dump(const TokenList *list) {
    printf("Tokens: %zu\n", list->count);
    for (size_t i = 0; i < list->count; i++) {
        Token *t = &list->tokens[i];
        printf("  %-12s '%s' @ %d:%d\n",
               token_type_str(t->type), t->value, t->line, t->column);
    }
    if (list->error_message) {
        printf("  ERROR at %d:%d: %s\n",
               list->error_line, list->error_column, list->error_message);
    }
}
