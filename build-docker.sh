#!/bin/bash
# Build script for Security Camera Image (Windows/Mac/Linux compatible)
# Usage: ./build-docker.sh

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE_NAME="airport-camera-builder"
OUTPUT_DIR="$PROJECT_DIR/output-images"

echo "📷 Building Security Camera Image for Raspberry Pi 4"
echo "====================================================="
echo ""
echo "Project Directory: $PROJECT_DIR"
echo "Output Directory: $OUTPUT_DIR"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    echo "❌ Docker is not running. Please start Docker Desktop."
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
