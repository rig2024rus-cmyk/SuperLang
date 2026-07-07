#!/bin/bash

set -e

SUPERLANG="./superlang"
TESTS_DIR="examples"

echo "====================================="
echo "SuperLang Regression Test Suite"
echo "====================================="
echo ""

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

pass_count=0
fail_count=0

run_test() {
    local test_name="$1"
    local test_file="$2"
    local expected_exit="$3"
    
    echo "Running: $test_name"
    
    if [ ! -f "$test_file" ]; then
        echo -e "${RED}✗ FAIL: Test file not found: $test_file${NC}"
        fail_count=$((fail_count + 1))
        return
    fi
    
    # Run the test
    set +e
    output=$($SUPERLANG "$test_file" 2>&1)
    actual_exit=$?
    set -e
    
    if [ "$actual_exit" -eq "$expected_exit" ]; then
        echo -e "${GREEN}✓ PASS: $test_name (exit code: $actual_exit)${NC}"
        pass_count=$((pass_count + 1))
    else
        echo -e "${RED}✗ FAIL: $test_name (expected exit $expected_exit, got $actual_exit)${NC}"
        echo "$output"
        fail_count=$((fail_count + 1))
    fi
    echo ""
}

# Positive tests (should succeed with exit code 0)
run_test "Test 1: Ancestor (transitive closure)" "$TESTS_DIR/test1_ancestor.unq" 0
run_test "Test 2: Connected (symmetric closure)" "$TESTS_DIR/test2_connected.unq" 0
run_test "Test 3: Depends (task dependencies)" "$TESTS_DIR/test3_depends.unq" 0
run_test "Test 4: Ready (stratified negation)" "$TESTS_DIR/task_ready.unq" 0
run_test "Test 5: Total Price (aggregates)" "$TESTS_DIR/total_price.unq" 0
run_test "Test 8: Arithmetic (basic)" "$TESTS_DIR/test_arith.unq" 0

# Negative tests (should fail with exit code 1)
run_test "Test 6: Negative Cycle (validator rejection)" "$TESTS_DIR/test6_negative_cycle.unq" 1
run_test "Test 7: Self-Negation (validator rejection)" "$TESTS_DIR/test7_self_negation.unq" 1
run_test "TypeChecker: Typo Test (type checker rejection)" "$TESTS_DIR/typo_test.unq" 1

# Summary
echo "====================================="
echo "Test Summary"
echo "====================================="
echo -e "${GREEN}Passed: $pass_count${NC}"
echo -e "${RED}Failed: $fail_count${NC}"
echo ""

if [ "$fail_count" -eq 0 ]; then
    echo -e "${GREEN}✓ ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}✗ SOME TESTS FAILED${NC}"
    exit 1
fi
