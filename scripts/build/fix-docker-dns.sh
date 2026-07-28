#!/bin/bash
# Fix Docker Desktop DNS on macOS
# This resolves the "Could not connect to ports.ubuntu.com" issue
# Usage: ./fix-docker-dns.sh

set -e

DOCKER_CONF="$HOME/.docker/daemon.json"

echo "🔧 Fixing Docker Desktop DNS settings..."
echo ""

mkdir -p "$HOME/.docker"

# Write DNS configuration
cat > "$DOCKER_CONF" << 'EOF'
{
    "dns": ["8.8.8.8", "8.8.4.4", "1.1.1.1"],
    "dns-search": []
}
EOF

echo "✅ DNS configuration written to: $DOCKER_CONF"
echo ""
echo "📋 Contents:"
cat "$DOCKER_CONF"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔄 NEXT STEPS:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "1. Completely quit Docker Desktop"
echo "   - Right-click Docker icon in menu bar → Quit"
echo "   - Or: killall com.docker.docker"
echo ""
echo "2. Wait 5 seconds"
echo ""
echo "3. Reopen Docker Desktop"
echo "   - Wait for the whale icon to stop animating"
echo "   - Check that 'Docker Desktop is running' appears"
echo ""
echo "4. Test the fix:"
echo "   docker info 2>&1 | grep -A5 'Server Version'"
echo ""
echo "5. Try the build again:"
echo "   ./scripts/build/build-docker.sh"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "💡 If it still fails, your network may be blocking"
echo "   port 80. Try these alternatives:"
echo ""
echo "   a) Build natively: bash scripts/build/build-native.sh"
echo "   b) Install Lima:  brew install lima"
echo "      Then use Lima to create a Linux VM for building"
echo "   c) Check if you're behind a corporate proxy"
echo ""
