#!/usr/bin/env bash
# count_lines.sh - count lines of code in src and include, ignoring build/ and external/

total=0

for dir in src include; do
    if [ -d "$dir" ]; then
        lines=$(find "$dir" -type f \( -name "*.c" -o -name "*.h" -o -name "*.s" \) \
                ! -path "*/build/*" ! -path "*/external/*" \
                -exec wc -l {} + | awk '{sum += $1} END {print sum}')
        echo "$dir: $lines lines"
        total=$((total + lines))
    fi
done

echo "Total lines of code: $total"