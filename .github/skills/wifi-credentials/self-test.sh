#!/bin/bash
# self-test for wifi-credentials
# Run: bash .github/skills/wifi-credentials/self-test.sh

PASS=0; FAIL=0

tp() { echo "SELF_TEST_PASS: $1"; PASS=$((PASS+1)); }
tf() { echo "SELF_TEST_FAIL: $1"; FAIL=$((FAIL+1)); }

# Credential file exists
[ -f "$HOME/.esp-wifi-credentials" ] && tp "cred_file_exists" || tf "cred_file_exists"

# .gitignore contains wifi-credentials.conf
grep -q "wifi-credentials.conf\|sdkconfig" .gitignore 2>/dev/null && tp "gitignore_sdkconfig" || tf "gitignore_sdkconfig"

# No hardcoded passwords in source code
if ! grep -rn 'NETGEAR\|password.*=.*"[a-zA-Z0-9]' src/ 2>/dev/null | grep -v Kconfig | grep -v "CONFIG_" | grep -q .; then
    tp "no_hardcoded_password"
else
    tf "no_hardcoded_password"
fi

echo ""; echo "Results: $PASS passed, $FAIL failed"
exit $FAIL
