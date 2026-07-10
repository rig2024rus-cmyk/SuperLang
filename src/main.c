#include "lexer.h"
#include "parser.h"
#include "ast_to_graph.h"
#include "semantic_validator.h"
#include "synthesizer.h"
#include "runtime.h"
#include "type_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

typedef struct {
    const char *filepath;
    int dump_tokens;
    int dump_ast;
    int dump_graph;
    int dump_ir;
    int verbose;
    int show_help;
} CLIConfig;

static CLIConfig parse_args(int argc, char **argv) {
    CLIConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--dump-tokens") == 0) {
            cfg.dump_tokens = 1;
        } else if (strcmp(arg, "--dump-ast") == 0) {
            cfg.dump_ast = 1;
        } else if (strcmp(arg, "--dump-graph") == 0) {
            cfg.dump_graph = 1;
        } else if (strcmp(arg, "--dump-ir") == 0) {
            cfg.dump_ir = 1;
        } else if (strcmp(arg, "--verbose") == 0 || strcmp(arg, "-v") == 0) {
            cfg.verbose = 1;
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            cfg.show_help = 1;
        } else if (arg[0] == '-') {
            fprintf(stderr, "Unknown flag: %s\n", arg);
            cfg.show_help = 1;
        } else {
            if (cfg.filepath != NULL) {
                fprintf(stderr, "Multiple input files not supported: %s, %s\n",
                        cfg.filepath, arg);
                cfg.show_help = 1;
            } else {
                cfg.filepath = arg;
            }
        }
    }
    return cfg;
}

static void print_usage(const char *prog_name) {
    printf("SuperLang — Declarative language for observable properties\n");
    printf("===========================================================\n");
    printf("USAGE:\n");
    printf("    %s [OPTIONS] <file.unq>\n", prog_name);
    printf("OPTIONS:\n");
    printf("    --dump-tokens    Print tokens from lexer\n");
    printf("    --dump-ast       Print parsed AST\n");
    printf("    --dump-graph     Print semantic dependency graph\n");
    printf("    --dump-ir        Print synthesized Closure IR\n");
    printf("    --verbose, -v    Enable verbose output\n");
    printf("    --help, -h       Show this help\n");
    printf("\nEXAMPLE:\n");
    printf("    %s examples/task_ready.unq\n", prog_name);
    printf("    %s --dump-ast --dump-graph examples/task_ready.unq\n", prog_name);
    printf("    %s --verbose -h\n", prog_name);
}

static char *read_file(const char *path, long *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open file '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "Cannot seek in file '%s'\n", path);
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "Cannot get size of file '%s'\n", path);
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fprintf(stderr, "Out of memory reading file '%s'\n", path);
        fclose(f);
        return NULL;
    }
    size_t read_count = fread(buffer, 1, (size_t)size, f);
    fclose(f);
    if ((long)read_count != size) {
        fprintf(stderr, "Short read on file '%s' (expected %ld, got %zu)\n",
                path, size, read_count);
        free(buffer);
        return NULL;
    }
    buffer[size] = '\0';
    if (out_size) *out_size = size;
    return buffer;
}

/* ====================================================================== */
/* Pattern Matching Engine for Queries (v0.7 - !X prefix)                */
/* ====================================================================== */

static int is_query_var(const char* name) {
    return name && name[0] == '!';
}

typedef struct {
    const char *target_pred;
    int target_arity;
    int *arg_to_var;
    int var_count;
    const char **query_args;
    
    char ***rows;
    int row_count;
    int row_cap;
    int ground_match;
} MatchCtx;

static void match_visitor(const char *pred, int arity, const char **args, void *user_data) {
    MatchCtx *ctx = (MatchCtx *)user_data;
    
    if (strcmp(pred, ctx->target_pred) != 0 || arity != ctx->target_arity) return;
    
    char **bindings = calloc(ctx->var_count > 0 ? ctx->var_count : 1, sizeof(char*));
    int match = 1;
    
    for (int i = 0; i < ctx->target_arity; i++) {
        int var_idx = ctx->arg_to_var[i];
        if (var_idx == -1) {
            if (strcmp(ctx->query_args[i], args[i]) != 0) {
                match = 0; break;
            }
        } else {
            if (bindings[var_idx] == NULL) {
                bindings[var_idx] = (char *)args[i];
            } else {
                if (strcmp(bindings[var_idx], args[i]) != 0) {
                    match = 0; break;
                }
            }
        }
    }
    
    if (match) {
        if (ctx->var_count == 0) {
            ctx->ground_match = 1;
        } else {
            if (ctx->row_count >= ctx->row_cap) {
                ctx->row_cap = ctx->row_cap == 0 ? 8 : ctx->row_cap * 2;
                ctx->rows = realloc(ctx->rows, sizeof(char**) * ctx->row_cap);
            }
            char **row = malloc(sizeof(char*) * ctx->var_count);
            for (int i = 0; i < ctx->var_count; i++) {
                row[i] = strdup(bindings[i] ? bindings[i] : "");
            }
            ctx->rows[ctx->row_count++] = row;
        }
    }
    
    free(bindings);
}

int main(int argc, char **argv) {
    CLIConfig cfg = parse_args(argc, argv);
    if (cfg.show_help || !cfg.filepath) {
        print_usage(argv[0]);
        return cfg.show_help ? 0 : 1;
    }
    
    if (cfg.verbose) {
        printf("SuperLang compiler v0.9\n");
        printf("Input file: %s\n", cfg.filepath);
    }
    
    long source_size = 0;
    char *source = read_file(cfg.filepath, &source_size);
    if (!source) {
        return 1;
    }
    
    if (cfg.verbose) {
        printf("Source size: %ld bytes\n", source_size);
    }
    
    int result_code = 0;
    
    ParseResult parse;
    parse.program = NULL;
    parse.error_message = NULL;
    
    TranslationResult trans;
    trans.graph = NULL;
    trans.error_message = NULL;
    
    ValidationResult struct_val;
    struct_val.error_message = NULL;
    
    ValidationResult sem_val;
    sem_val.error_message = NULL;
    
    ClosureIR *closure = NULL;
    Config *config = NULL;
    
    /* Stage 1: Lexer */
    if (cfg.verbose) printf("[1] Lexing...\n");
    TokenList tokens = lexer_tokenize(source);
    free(source);
    
    if (tokens.error_message) {
        fprintf(stderr, "✗ Lexer FAILED at %d:%d: %s\n",
                tokens.error_line, tokens.error_column, tokens.error_message);
        token_list_free(&tokens);
        return 1;
    }
    if (cfg.verbose) {
        printf("    ✓ %zu tokens\n", tokens.count);
    }
    if (cfg.dump_tokens) {
        printf("\n=== TOKENS ===\n");
        token_list_dump(&tokens);
        printf("==============\n");
    }
    
    /* Stage 2: Parser */
    if (cfg.verbose) printf("[2] Parsing...\n");
    parse = parser_parse(&tokens);
    token_list_free(&tokens);
    
    if (!parse.is_valid) {
        fprintf(stderr, "✗ Parser FAILED at %d:%d: %s\n",
                parse.error_line, parse.error_column, parse.error_message);
        parse_result_free(&parse);
        return 1;
    }
    if (cfg.verbose) {
        printf("    ✓ AST built\n");
        printf("        entities: %d\n", parse.program->entity_count);
        printf("        relations: %d\n", parse.program->relation_count);
        printf("        observes: %d\n", parse.program->observe_count);
        printf("        derives: %d\n", parse.program->derive_count);
        printf("        inputs: %d\n", parse.program->input_count);
        printf("        queries: %d\n", parse.program->query_count);
    }
    if (cfg.dump_ast) {
        printf("\n=== AST ===\n");
        program_dump(parse.program);
        printf("===========\n");
    }
    
    /* Stage 2.5: Type checking */
    if (cfg.verbose) printf("[2.5] Type checking...\n");
    TypeCheckResult type_check = typecheck_program(parse.program);
    if (cfg.dump_ast || type_check.count > 0) {
        typecheck_result_dump(&type_check);
        printf("\n");
    }
    if (type_check.count > 0) {
        fprintf(stderr, "\n✗ Compilation stopped: type/existence/safety errors above.\n");
        fprintf(stderr, "  (Use --dump-ast to inspect parsed structure)\n");
        typecheck_result_free(&type_check);
        parse_result_free(&parse);
        return 1;
    }
    typecheck_result_free(&type_check);
    
    /* Stage 3: AST → Semantic Graph */
    if (cfg.verbose) printf("[3] Translating to semantic graph...\n");
    trans = ast_to_graph_translate(parse.program);
    if (!trans.is_valid) {
        fprintf(stderr, "✗ Translation FAILED: %s\n", trans.error_message);
        translation_result_free(&trans);
        parse_result_free(&parse);
        return 1;
    }
    if (cfg.verbose) {
        printf("    ✓ %zu nodes, %zu edges\n",
               trans.graph->node_count, trans.graph->edge_count);
    }
    if (cfg.dump_graph) {
        printf("\n=== SEMANTIC GRAPH ===\n");
        graph_dump(trans.graph);
        printf("======================\n");
    }
    
    /* Stage 4: Validation */
    if (cfg.verbose) printf("[4] Validating...\n");
    struct_val = graph_validate_structure(trans.graph);
    sem_val = graph_validate_semantics(trans.graph);
    
    int validation_failed = 0;
    if (!struct_val.is_valid || !sem_val.is_valid) {
        fprintf(stderr, "✗ Validation FAILED\n");
        fprintf(stderr, "    Structural: "); validation_result_dump(&struct_val);
        fprintf(stderr, "    Semantic:   "); validation_result_dump(&sem_val);
        validation_failed = 1;
    } else {
        if (cfg.verbose) {
            printf("    ✓ Validation PASSED\n");
        }
    }
    
    if (validation_failed) {
        result_code = 1;
        goto cleanup;
    }
    
    /* Stage 4.5: Assign evaluation strata now that we know the graph is
     * stratifiable — reuses the SCC condensation graph_validate_semantics
     * already computed rather than a second, independent notion of stratum. */
    graph_compute_strata(trans.graph);
    
    /* Stage 5: Synthesize Closure IR */
    if (cfg.verbose) printf("[5] Synthesizing closure operator...\n");
    closure = synthesize(trans.graph);
    if (cfg.verbose) {
        printf("    ✓ %zu rules\n", closure->rule_count);
    }
    if (cfg.dump_ir) {
        printf("\n=== CLOSURE IR ===\n");
        closure_dump(closure);
        printf("==================\n");
    }
    
    /* Stage 6: Load input facts */
    if (cfg.verbose) printf("[6] Loading %d input facts...\n", parse.program->input_count);
    config = config_new();
    for (int i = 0; i < parse.program->input_count; i++) {
        const InputFact *f = &parse.program->inputs[i];
        add_fact_direct(config, f->predicate, f->arg_count, f->args);
    }
    if (cfg.verbose) {
        printf("    ✓ Loaded\n");
    }
    
    /* Stage 7: Saturation */
    if (cfg.verbose) printf("[7] Saturating...\n");
    saturate(config, closure);
    
    /* Stage 8: Queries (Pattern Matching - v0.7) */
    printf("\n=== QUERY RESULTS ===\n");
    for (int i = 0; i < parse.program->query_count; i++) {
        const Query *q = &parse.program->queries[i];
        
        int var_count = 0;
        int *arg_to_var = malloc(sizeof(int) * q->arg_count);
        char **var_names = NULL;
        
        for (int j = 0; j < q->arg_count; j++) {
            if (is_query_var(q->args[j])) {
                int found = -1;
                for (int k = 0; k < var_count; k++) {
                    if (strcmp(var_names[k], q->args[j]) == 0) {
                        found = k; break;
                    }
                }
                if (found == -1) {
                    var_names = realloc(var_names, sizeof(char*) * (var_count + 1));
                    var_names[var_count] = strdup(q->args[j]);
                    arg_to_var[j] = var_count;
                    var_count++;
                } else {
                    arg_to_var[j] = found;
                }
            } else {
                arg_to_var[j] = -1;
            }
        }
        
        MatchCtx ctx = {0};
        ctx.target_pred = q->predicate;
        ctx.target_arity = q->arg_count;
        ctx.arg_to_var = arg_to_var;
        ctx.var_count = var_count;
        ctx.query_args = (const char **)q->args;
        
        config_visit_facts(config, match_visitor, &ctx);
        
        printf("    ?- %s(", q->predicate);
        for (int j = 0; j < q->arg_count; j++) {
            if (j > 0) printf(", ");
            printf("%s", q->args[j]);
        }
        printf(")\n");
        
        if (var_count == 0) {
            printf("       → %s\n", ctx.ground_match ? "TRUE" : "FALSE");
        } else {
            if (ctx.row_count == 0) {
                printf("       → []\n");
            } else {
                printf("       → [");
                for (int r = 0; r < ctx.row_count; r++) {
                    if (r > 0) printf(", ");
                    if (var_count == 1) {
                        printf("%s", ctx.rows[r][0]);
                    } else {
                        printf("(");
                        for (int v = 0; v < var_count; v++) {
                            if (v > 0) printf(", ");
                            printf("%s", ctx.rows[r][v]);
                        }
                        printf(")");
                    }
                }
                printf("]\n");
            }
        }
        
        for (int r = 0; r < ctx.row_count; r++) {
            for (int v = 0; v < var_count; v++) free(ctx.rows[r][v]);
            free(ctx.rows[r]);
        }
        free(ctx.rows);
        for (int j = 0; j < var_count; j++) free(var_names[j]);
        free(var_names);
        free(arg_to_var);
    }
    printf("=====================\n");
    
cleanup:
    if (config) config_free(config);
    if (closure) closure_free(closure);
    validation_result_free(&struct_val);
    validation_result_free(&sem_val);
    translation_result_free(&trans);
    parse_result_free(&parse);
    return result_code;
}
