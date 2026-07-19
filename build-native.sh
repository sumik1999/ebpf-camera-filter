#!/bin/bash
# Native build script for Security Camera Image (macOS / Linux)
# Uses buildroot directly without Docker
# Usage: ./build-native.sh

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="$PROJECT_DIR/output-images"
BUILDROOT_DIR="$PROJECT_DIR/buildroot"

# Homebrew paths
HB="/opt/homebrew"
HB_GNUBIN="$HB/opt/coreutils/libexec/gnubin"
HB_FIND_GNUBIN="$HB/opt/findutils/libexec/gnubin"
HB_UTIL_LINUX="$HB/opt/util-linux/bin"
HB_GPATCH="$HB/opt/gpatch/libexec/gnubin"
HB_BASH="$HB/Cellar/bash/5.3.15/bin/bash"

# Check if Homebrew bash is installed
if [ ! -f "$HB_BASH" ]; then
    echo "❌ Homebrew bash not found. Install with:"
    echo "   brew install bash"
    exit 1
fi

# Setup PATH with GNU tools
export PATH="$HB/bin:$HB_GNUBIN:$HB_FIND_GNUBIN:$HB_UTIL_LINUX:$HB_GPATCH:$PATH"

# Override SHELL to use Homebrew bash for buildroot scripts
export SHELL="$HB_BASH"

# Override install command
export INSTALL="$HB/opt/coreutils/bin/ginstall"

# macOS requires these for some tools
export LDFLAGS="-L$HB/opt/libelf/lib $LDFLAGS"
export CPPFLAGS="-I$HB/opt/libelf/include $CPPFLAGS"

echo "📷 Building Security Camera Image for Raspberry Pi 4 (Native)"
echo "============================================================="
echo ""
echo "Project Directory: $PROJECT_DIR"
echo "Output Directory:  $OUTPUT_DIR"
echo "Bash version:      $($HB_BASH --version | head -1)"
echo ""

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Check required tools
MISSING_TOOLS=()
for tool in make gpatch gcc g++ gperf bison flex bc wget git ginstall gfind flock; do
    if ! command -v "$tool" &>/dev/null; then
        MISSING_TOOLS+=("$tool")
    fi
done

if [ ${#MISSING_TOOLS[@]} -gt 0 ]; then
    echo "❌ Missing tools: ${MISSING_TOOLS[*]}"
    echo ""
    echo "Install missing tools with:"
    echo "  brew install coreutils findutils gcc gpatch bison flex wget util-linux"
    echo "  brew install bash"
    exit 1
fi

echo "✅ All build tools found"
echo "   gcc: $(gcc --version | head -1)"
echo "   patch: $(patch --version | head -1)"
echo "   find: $(gfind --version 2>&1 | head -1)"
echo "   coreutils install: $(ginstall --version 2>&1 | head -1)"
echo ""

# Clean up legacy configuration
echo "🔧 Checking configuration..."
cd "$BUILDROOT_DIR"
perl -pi -e 's/^BR2_LEGACY=y$/# BR2_LEGACY is not set/' .config
perl -pi -e 's/^BR2_KERNEL_HEADERS_5_4=y$/# BR2_KERNEL_HEADERS_5_4 is not set/' .config
perl -pi -e 's/^BR2_KERNEL_HEADERS_4_12=y$/# BR2_KERNEL_HEADERS_4_12 is not set/' .config
perl -pi -e 's/^BR2_KERNEL_HEADERS_3_2=y$/# BR2_KERNEL_HEADERS_3_2 is not set/' .config
make olddefconfig > /dev/null 2>&1
cd "$PROJECT_DIR"

# Check for existing output
if [ -d "$BUILDROOT_DIR/output/images" ] && [ "$(ls -A "$BUILDROOT_DIR/output/images" 2>/dev/null)" ]; then
    echo "📦 Found existing build output, copying..."
    cp -f "$BUILDROOT_DIR/output/images/"* "$OUTPUT_DIR/" 2>/dev/null || true
    echo "   Output files copied to: $OUTPUT_DIR"
    echo ""
    echo "✅ Done!"
    echo ""
    echo "Output files in: $OUTPUT_DIR"
    ls -la "$OUTPUT_DIR"
    echo ""
    echo "📖 See SECURITY-CAMERA-BUILD-REPORT.md for deployment instructions"
    exit 0
fi

# Build with buildroot
CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc)
echo "🔨 Building with buildroot (this may take 15-30 minutes)..."
echo "   Using $CORES CPU cores"
echo ""

cd "$BUILDROOT_DIR"

# Run make with SHELL overridden to use Homebrew bash
# Use env to ensure all GNU tools are available in PATH
env SHELL="$HB_BASH" \
    INSTALL="$HB/opt/coreutils/bin/ginstall" \
    PATH="$HB/bin:$HB_GNUBIN:$HB_FIND_GNUBIN:$HB_UTIL_LINUX:$HB_GPATCH:$PATH" \
    LDFLAGS="-L$HB/opt/libelf/lib" \
    CPPFLAGS="-I$HB/opt/libelf/include" \
    make -j"$CORES" 2>&1 | tee /tmp/buildroot-build.log

BUILD_EXIT=$?

cd "$PROJECT_DIR"

# Copy output files
echo ""
echo "📦 Copying build output..."
if [ -d "$BUILDROOT_DIR/output/images" ] && [ "$(ls -A "$BUILDROOT_DIR/output/images" 2>/dev/null)" ]; then
    echo "   Output files found:"
    ls -la "$BUILDROOT_DIR/output/images/"
    cp -f "$BUILDROOT_DIR/output/images/"* "$OUTPUT_DIR/" 2>/dev/null || true
    echo "   Output files copied to: $OUTPUT_DIR"
else
    echo "⚠️  No output/images directory found after build"
    echo "   Check /tmp/buildroot-build.log for errors"
    if [ -f /tmp/buildroot-build.log ]; then
        echo ""
        echo "=== Last 50 lines of build log ==="
        tail -50 /tmp/buildroot-build.log
    fi
    exit 1
fi

echo ""
echo "✅ Build complete!"
echo ""
echo "Output files in: $OUTPUT_DIR"
ls -la "$OUTPUT_DIR"
echo ""
echo "📖 See SECURITY-CAMERA-BUILD-REPORT.md for deployment instructions"
