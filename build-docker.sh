#!/bin/bash
# Build script for Security Camera Image (Windows/Mac/Linux compatible)
# Usage: ./build-docker.sh [--native]

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="airport-camera-builder"
OUTPUT_DIR="$PROJECT_DIR/output-images"
USE_NATIVE=false

# Parse arguments
for arg in "$@"; do
    case $arg in
        --native)
            USE_NATIVE=true
            ;;
    esac
done

echo "📷 Building Security Camera Image for Raspberry Pi 4"
echo "====================================================="
echo ""
echo "Project Directory: $PROJECT_DIR"
echo "Output Directory:  $OUTPUT_DIR"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if we should use native build
if [ "$USE_NATIVE" = true ]; then
    echo "🔨 Using native build (no Docker)..."
    echo ""
    # Check if native build script exists
    if [ -f "$PROJECT_DIR/build-native.sh" ]; then
        cd "$PROJECT_DIR"
        chmod +x build-native.sh
        bash build-native.sh
        exit 0
    else
        echo "❌ Native build script not found. Run without --native to use Docker."
        exit 1
    fi
fi

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    echo "❌ Docker is not running. Please start Docker Desktop."
    echo ""
    echo "💡 Tip: Run './build-docker.sh --native' to build without Docker"
    exit 1
fi

# Try to fix Docker networking by adding DNS servers
DOCKER_CONF="$HOME/.docker/daemon.json"
NEEDS_DNS_FIX=false

if [ -f "$DOCKER_CONF" ]; then
    # Check if DNS is already configured
    if ! grep -q '"dns"' "$DOCKER_CONF" 2>/dev/null; then
        NEEDS_DNS_FIX=true
    fi
else
    NEEDS_DNS_FIX=true
fi

if [ "$NEEDS_DNS_FIX" = true ]; then
    echo "🔧 Configuring Docker DNS for better network access..."
    mkdir -p "$HOME/.docker"
    cat > "$DOCKER_CONF" << 'EOF'
{
    "dns": ["8.8.8.8", "8.8.4.4", "1.1.1.1"]
}
EOF
    echo ""
    echo "⚠️  Docker DNS updated. Please restart Docker Desktop and try again:"
    echo "   1. Close Docker Desktop completely"
    echo "   2. Open Docker Desktop"
    echo "   3. Run: ./build-docker.sh"
    echo ""
    echo "💡 Alternatively, use native build:"
    echo "   ./build-docker.sh --native"
    exit 1
fi

# Build the Docker image
echo "🔨 Building Docker image..."
docker build -t "$IMAGE_NAME" -f "$PROJECT_DIR/Dockerfile" "$PROJECT_DIR"

# Run the build
echo "🚀 Running build in container..."
docker run --rm -v "$PROJECT_DIR:/build" -w "/build" "$IMAGE_NAME"

# Extract output
echo "📦 Extracting build output..."
docker run --rm -v "$PROJECT_DIR:/build" -w "/build" "$IMAGE_NAME" bash -c "
    cp -r /build/buildroot/output/images/* /build/output-images/ 2>/dev/null || true
    echo 'Output files copied to: $OUTPUT_DIR'
"

# Copy files using cp command directly
cp -f "$PROJECT_DIR/buildroot/output/images/"* "$OUTPUT_DIR/" 2>/dev/null || true

echo ""
echo "✅ Build complete!"
echo ""
echo "Output files in: $OUTPUT_DIR"
ls -la "$OUTPUT_DIR" 2>/dev/null || echo "(No output files found - check for errors above)"
echo ""
echo "📖 See SECURITY-CAMERA-BUILD-REPORT.md for deployment instructions"
