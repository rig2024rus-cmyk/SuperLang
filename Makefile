CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -g
SRC      = src/graph.c src/synthesizer.c src/runtime.c src/semantic_validator.c \
           src/lexer.c src/parser.c src/ast_to_graph.c src/type_checker.c src/main.c
OBJ      = $(SRC:.c=.o)
BIN      = superlang

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

# Positive test: stratified negation
test: $(BIN)
	@echo "====================================="
	@echo "Running: ./$(BIN) examples/task_ready.unq"
	@echo "====================================="
	./$(BIN) examples/task_ready.unq

# Negative test: TypeChecker should reject
test-errors: $(BIN)
	@echo "====================================="
	@echo "Expected: compilation FAILS with errors"
	@echo "====================================="
	./$(BIN) examples/typo_test.unq; \
	if [ $$? -eq 1 ]; then \
		echo ""; \
		echo "✓ TypeChecker correctly rejected the program"; \
	else \
		echo ""; \
		echo "✗ TypeChecker should have failed but didn't"; \
		exit 1; \
	fi

# Full regression suite (8 tests)
test-all: $(BIN)
	@chmod +x tests/run_tests.sh
	@./tests/run_tests.sh

# Debug mode
debug: $(BIN)
	./$(BIN) --dump-ast --dump-graph --dump-ir examples/task_ready.unq

# Memory safety
valgrind: $(BIN)
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 \
		./$(BIN) examples/task_ready.unq

# Verbose mode
quick: $(BIN)
	./$(BIN) --verbose examples/task_ready.unq

.PHONY: all clean test test-errors test-all debug valgrind quick
