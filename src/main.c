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
    printf("===========================================================\n\n");
    printf("USAGE:\n");
    printf("    %s [OPTIONS] <file.unq>\n\n", prog_name);
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
    printf("    %s --verbose -h\n\n", prog_name);
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

int main(int argc, char **argv) {
    CLIConfig cfg = parse_args(argc, argv);
    
    if (cfg.show_help || !cfg.filepath) {
        print_usage(argv[0]);
        return cfg.show_help ? 0 : 1;
    }
    
    if (cfg.verbose) {
        printf("SuperLang compiler v0.1\n");
        printf("Input file: %s\n", cfg.filepath);
    }
    
    long source_size = 0;
    char *source = read_file(cfg.filepath, &source_size);
    if (!source) {
        return 1;
    }
    
    if (cfg.verbose) {
        printf("Source size: %ld bytes\n\n", source_size);
    }
    
    int result_code = 0;
    
    /* Initialize all cleanup pointers to NULL early */
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
        printf("==============\n\n");
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
        printf("===========\n\n");
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
        printf("======================\n\n");
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
    
    /* Stage 5: Synthesize Closure IR */
    if (cfg.verbose) printf("[5] Synthesizing closure operator...\n");
    closure = synthesize(trans.graph);
    
    if (cfg.verbose) {
        printf("    ✓ %zu rules\n", closure->rule_count);
    }
    
    if (cfg.dump_ir) {
        printf("\n=== CLOSURE IR ===\n");
        closure_dump(closure);
        printf("==================\n\n");
    }
    
    /* Stage 6: Load input facts */
    if (cfg.verbose) printf("[6] Loading %d input facts...\n", parse.program->input_count);
    config = config_new();
    for (int i = 0; i < parse.program->input_count; i++) {
        const InputFact *f = &parse.program->inputs[i];
        switch (f->arg_count) {
            case 1: config_add_fact(config, f->predicate, 1, f->args[0]); break;
            case 2: config_add_fact(config, f->predicate, 2, f->args[0], f->args[1]); break;
            case 3: config_add_fact(config, f->predicate, 3, f->args[0], f->args[1], f->args[2]); break;
            default: 
                if (cfg.verbose) {
                    printf("    (skipping fact with %d args — not supported)\n", f->arg_count);
                }
                break;
        }
    }
    
    if (cfg.verbose) {
        printf("    ✓ Loaded\n");
    }
    
    /* Stage 7: Saturation */
    if (cfg.verbose) printf("[7] Saturating...\n");
    saturate(config, closure);
    
    /* Stage 8: Queries */
    printf("\n=== QUERY RESULTS ===\n");
    for (int i = 0; i < parse.program->query_count; i++) {
        const Query *q = &parse.program->queries[i];
        int answer = 0;
        
        switch (q->arg_count) {
            case 1:
                answer = config_has_fact(config, q->predicate, 1, q->args[0]);
                break;
            case 2:
                answer = config_has_fact(config, q->predicate, 2, q->args[0], q->args[1]);
                break;
            case 3:
                answer = config_has_fact(config, q->predicate, 3, q->args[0], q->args[1], q->args[2]);
                break;
        }
        
        printf("    ?- %s(", q->predicate);
        for (int j = 0; j < q->arg_count; j++) {
            if (j > 0) printf(", ");
            printf("%s", q->args[j]);
        }
        printf(")\n");
        printf("       → %s\n\n", answer ? "TRUE" : "FALSE");
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
