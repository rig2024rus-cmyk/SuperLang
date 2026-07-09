#!/bin/bash

echo "======================================"
echo "SuperLang Semantic Test Suite"
echo "======================================"
echo ""

PASS=0
FAIL=0

run_test() {
    local file=$1
    local description=$2
    local expected_exit=$3
    
    echo "Testing: $description"
    echo "File: $file"
    
    output=$(./superlang "$file" 2>&1)
    exit_code=$?
    
    if [ $exit_code -ne $expected_exit ]; then
        echo "✗ FAIL: Exit code $exit_code (expected $expected_exit)"
        FAIL=$((FAIL + 1))
        return 1
    fi
    
    echo "✓ Exit code correct ($exit_code)"
    
    # Выводим результат для ручной проверки
    echo "--- Output ---"
    echo "$output" | grep -A 20 "QUERY RESULTS" || echo "$output"
    echo "--------------"
    echo ""
    
    PASS=$((PASS + 1))
    return 0
}

# Test 1: Ancestor (рекурсия)
run_test "tests/test1_ancestor.unq" "Recursive ancestor relationship" 0

# Test 2: Connected components
run_test "tests/test2_connected.unq" "Transitive closure (connected)" 0

# Test 3: Depends (negation)
run_test "tests/test3_depends.unq" "Stratified negation (depends/blocked)" 0

# Test 4: Ready (negation + task scheduling)
run_test "tests/test4_ready.unq" "Task readiness with negation" 0

# Test 5: Total price (aggregates)
run_test "tests/test5_total_price.unq" "Aggregate sum with grouping" 0

# Test 6: Negative cycle (should be rejected)
run_test "tests/test6_negative_cycle.unq" "Reject negative cycle" 1

# Test 7: Self negation (should be rejected)
run_test "tests/test7_self_negation.unq" "Reject self-negation" 1

# Test 8: Arithmetic
run_test "tests/test8_arith.unq" "Arithmetic expressions" 0

# Test 9: Typo (should be rejected)
run_test "tests/test9_typo.unq" "Reject undefined predicate" 1

# Test 10: Built-in math functions
run_test "experiments/test_builtin.unq" "Built-in math (sqrt, pow, etc.)" 0

echo "======================================"
echo "Summary: $PASS passed, $FAIL failed"
echo "======================================"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
