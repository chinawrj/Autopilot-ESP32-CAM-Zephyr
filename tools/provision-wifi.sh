#!/bin/bash
# tools/provision-wifi.sh — Inject WiFi credentials into build configuration
#
# Reads credentials from (in priority order):
#   1. Environment variables: ESP_WIFI_SSID / ESP_WIFI_PASSWORD
#   2. Config file: ~/.esp-wifi-credentials
#
# Writes: wifi-credentials.conf (gitignored overlay)

set -euo pipefail

CRED_FILE="$HOME/.esp-wifi-credentials"
OVERLAY_FILE="wifi-credentials.conf"

# Priority 1: Environment variables
if [[ -n "${ESP_WIFI_SSID:-}" && -n "${ESP_WIFI_PASSWORD:-}" ]]; then
    SSID="$ESP_WIFI_SSID"
    PASS="$ESP_WIFI_PASSWORD"
    echo "📡 Using WiFi credentials from environment variables"

# Priority 2: Config file
elif [[ -f "$CRED_FILE" ]]; then
    SSID=$(grep -E "^ssid\s*=" "$CRED_FILE" | sed 's/^ssid\s*=\s*//')
    PASS=$(grep -E "^password\s*=" "$CRED_FILE" | sed 's/^password\s*=\s*//')
    echo "📡 Using WiFi credentials from $CRED_FILE"
else
    echo "❌ No WiFi credentials found!"
    echo "   Option 1: export ESP_WIFI_SSID=... ESP_WIFI_PASSWORD=..."
    echo "   Option 2: Create ~/.esp-wifi-credentials with format:"
    echo "     [wifi]"
    echo "     ssid = YOUR_SSID"
    echo "     password = YOUR_PASSWORD"
    exit 1
fi

if [[ -z "$SSID" || -z "$PASS" ]]; then
    echo "❌ SSID or password is empty!"
    exit 1
fi

# Write overlay conf (gitignored)
cat > "$OVERLAY_FILE" << EOF
CONFIG_WIFI_SSID="$SSID"
CONFIG_WIFI_PASSWORD="$PASS"
EOF

echo "✅ WiFi credentials injected into $OVERLAY_FILE"
echo "   SSID: $SSID"
echo "   Password: ****$(echo "$PASS" | tail -c 4)"
echo ""
echo "   Build with: west build -- -DOVERLAY_CONFIG=$OVERLAY_FILE"
