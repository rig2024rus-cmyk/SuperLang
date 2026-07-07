#ifndef SUPERLANG_AST_TO_GRAPH_H
#define SUPERLANG_AST_TO_GRAPH_H

#include "ast_nodes.h"
#include "graph.h"

typedef struct {
    Graph *graph;
    int is_valid;
    char *error_message;
    int error_line;
    int error_column;
} TranslationResult;

/* Translate AST program into Semantic Graph */
TranslationResult ast_to_graph_translate(const Program *program);

/* Free translation result (including the graph) */
void translation_result_free(TranslationResult *result);

#endif
