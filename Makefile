CC = cc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude -g

SRCS = src/graph.c src/synthesizer.c src/runtime.c src/semantic_validator.c \
       src/lexer.c src/parser.c src/ast_to_graph.c src/type_checker.c src/main.c

OBJS = $(SRCS:.c=.o)

superlang: $(OBJS)
	$(CC) $(CFLAGS) -o superlang $(OBJS) -lm

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) superlang

test-all: superlang
	@echo "====================================="
	@echo "SuperLang Regression Test Suite"
	@echo "====================================="
	@if [ ! -d "tests" ] || [ -z "$$(ls -A tests/*.unq 2>/dev/null)" ]; then \
		echo "⚠️  No test files found in tests/"; \
		echo "    Create .unq files in tests/ directory to run regression tests"; \
		exit 0; \
	fi
	@passed=0; failed=0; total=0; \
	for test in tests/*.unq; do \
		[ -f "$$test" ] || continue; \
		total=$$((total + 1)); \
		name=$$(basename "$$test" .unq); \
		printf "\nRunning: $$name\n"; \
		output=$$(./superlang "$$test" 2>&1); \
		code=$$?; \
		if [ "$$code" = "0" ]; then \
			echo "✓ PASS: $$name (exit code: 0)"; \
			passed=$$((passed + 1)); \
		elif [ "$$code" = "1" ] && echo "$$name" | grep -q -iE "reject|negative|cycle|self|typo"; then \
			echo "✓ PASS: $$name (exit code: 1, expected rejection)"; \
			passed=$$((passed + 1)); \
		else \
			echo "✗ FAIL: $$name (exit code: $$code)"; \
			echo "$$output" | head -5; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "====================================="; \
	echo "Test Summary"; \
	echo "====================================="; \
	echo "Total:  $$total"; \
	echo "Passed: $$passed"; \
	echo "Failed: $$failed"; \
	echo ""; \
	if [ "$$failed" = "0" ]; then \
		echo "✓ ALL TESTS PASSED"; \
	else \
		echo "✗ SOME TESTS FAILED"; \
		exit 1; \
	fi

valgrind: superlang
	@echo "====================================="
	@echo "SuperLang Memory Safety Check"
	@echo "====================================="
	@if ! command -v valgrind >/dev/null 2>&1; then \
		echo "❌ Valgrind not installed. Install with:"; \
		echo "   sudo apt install valgrind    # Debian/Ubuntu"; \
		echo "   sudo dnf install valgrind    # Fedora"; \
		exit 1; \
	fi
	@passed=0; failed=0; \
	for file in examples/hello_world.unq examples/family_tree.unq examples/aggregates_demo.unq tests/test4_ready.unq tests/test1_ancestor.unq; do \
		if [ ! -f "$$file" ]; then \
			echo "⚠️  Skipping $$file (not found)"; \
			continue; \
		fi; \
		echo ""; \
		echo "→ Checking $$file"; \
		if valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 --quiet ./superlang "$$file" >/dev/null 2>&1; then \
			echo "  ✓ CLEAN: no leaks, no errors"; \
			passed=$$((passed + 1)); \
		else \
			echo "  ✗ FAILED: memory issues detected"; \
			valgrind --leak-check=full --show-leak-kinds=all ./superlang "$$file" 2>&1 | tail -20; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo ""; \
	echo "====================================="; \
	echo "Valgrind Summary"; \
	echo "====================================="; \
	echo "Passed: $$passed"; \
	echo "Failed: $$failed"; \
	echo ""; \
	if [ "$$failed" = "0" ]; then \
		echo "✓ ALL MEMORY CHECKS PASSED"; \
	else \
		echo "✗ SOME MEMORY CHECKS FAILED"; \
		exit 1; \
	fi

.PHONY: clean test-all valgrind
