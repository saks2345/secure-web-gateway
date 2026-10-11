
#!/bin/bash

PROXY="http://127.0.0.1:8080"
PASS=0
FAIL=0

check() {
    if [ "$2" -eq 0 ]; then
        echo "PASS: $1"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $1"
        FAIL=$((FAIL + 1))
    fi
}

echo "Testing Secure Web Gateway..."

# 1. Blocked HTTP must return 403
code=$(curl -s -o /dev/null -w "%{http_code}" \
    --max-time 10 -x "$PROXY" http://example.com/ 2>/dev/null)
[ "$code" = "403" ]
check "Blocked HTTP returns 403" $?

# 2. Allowed HTTP must return 200
code=$(curl -s -o /dev/null -w "%{http_code}" \
    --max-time 10 -x "$PROXY" http://example.org/ 2>/dev/null)
[ "$code" = "200" ]
check "Allowed HTTP succeeds" $?

# 3. Blocked HTTPS must be rejected with CONNECT 403
output=$(curl -v -o /dev/null --max-time 10 \
    -x "$PROXY" https://example.com/ 2>&1)
echo "$output" | grep -q "Received HTTP code 403 from proxy after CONNECT"
check "Blocked HTTPS CONNECT is rejected with 403" $?

# 4. Five concurrent HTTP requests must succeed
codes=$(for i in 1 2 3 4 5; do
    curl -s -o /dev/null -w "%{http_code}\n" \
        --max-time 15 -x "$PROXY" http://example.org/ &
done
wait
)

count=$(printf '%s\n' "$codes" | grep -c '^200$')
[ "$count" -eq 5 ]
check "Five concurrent HTTP requests succeed" $?

# 5. SQLite must contain request records
records=$(sqlite3 logs.db "SELECT COUNT(*) FROM requests;" 2>/dev/null)
if [[ "$records" =~ ^[0-9]+$ ]] && [ "$records" -gt 0 ]; then
    check "SQLite contains request records" 0
else
    check "SQLite contains request records" 1
fi

echo
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
