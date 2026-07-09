#!/bin/bash

echo "======================================"
echo "SuperLang Valgrind Test Suite"
echo "======================================"
echo ""

PASS=0
FAIL=0

run_valgrind() {
    local file=$1
    local description=$2
    
    echo "Valgrind: $description"
    echo "File: $file"
    
    output=$(valgrind --leak-check=full ./superlang "$file" 2>&1)
    
    # Главный критерий: ERROR SUMMARY должен быть 0
    if ! echo "$output" | grep -q "ERROR SUMMARY: 0 errors"; then
        echo "✗ FAIL: Memory errors detected"
        echo "$output" | grep -A 5 "ERROR SUMMARY"
        FAIL=$((FAIL + 1))
        return 1
    fi
    
    echo "✓ No memory errors"
    
    # Проверка утечек — сначала ищем "All heap blocks were freed"
    if echo "$output" | grep -q "All heap blocks were freed"; then
        echo "✓ No memory leaks (perfect)"
        PASS=$((PASS + 1))
        echo ""
        return 0
    fi
    
    # Если "All heap blocks" нет — проверяем definitely lost
    if echo "$output" | grep -q "definitely lost: 0 bytes"; then
        echo "✓ No definite memory leaks"
        PASS=$((PASS + 1))
    else
        echo "✗ FAIL: Memory leaks detected"
        echo "$output" | grep -A 3 "LEAK SUMMARY"
        FAIL=$((FAIL + 1))
        return 1
    fi
    
    echo ""
    return 0
}

# Запускаем Valgrind на всех тестах
for f in tests/test*.unq experiments/test_builtin.unq; do
    if [ -f "$f" ]; then
        run_valgrind "$f" "$(basename "$f")"
    fi
done

echo "======================================"
echo "Summary: $PASS passed, $FAIL failed"
echo "======================================"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
