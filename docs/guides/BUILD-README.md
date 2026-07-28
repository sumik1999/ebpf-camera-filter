# Build Instructions

## Problem

The `build-docker.sh` script fails because the Docker VM cannot reach Ubuntu package mirrors (`ports.ubuntu.com`). This is a common Docker Desktop on macOS networking issue.

## Solution 1: Fix Docker DNS (Recommended)

```bash
# Run the DNS fix script
./scripts/build/fix-docker-dns.sh

# Then follow the prompts:
# 1. Completely quit Docker Desktop (right-click whale → Quit)
# 2. Wait 5 seconds
# 3. Reopen Docker Desktop
# 4. Wait for it to fully start (whale icon stops animating)
# 5. Try the build again
./scripts/build/build-docker.sh
```

## Solution 2: Build Natively (No Docker)

If Docker continues to fail, build directly on macOS:

```bash
# Install required tools (one-time setup)
brew install gcc gpatch bison flex wget pkgconf libelf
brew install bash

# Run the native build script
bash scripts/build/build-native.sh
```

**Note:** Native build on macOS requires significant GNU tool compatibility workarounds. This may take 30-60 minutes.

## Solution 3: Use Lima (Linux VM on macOS)

Lima creates a lightweight Linux VM that can run Docker natively:

```bash
brew install lima
limactl start --name buildroot
limactl shell buildroot
# Inside the Lima VM:
git clone <your-repo>
cd airport-ebpf-mac
./scripts/build/build-docker.sh  # Now Docker works natively!
```

## Solution 4: Buildroot Without Docker

You can also build buildroot directly on macOS (see `scripts/build/build-native.sh` for the script).

## Troubleshooting

### Docker DNS still doesn't work
- Check if you're behind a corporate proxy
- Configure Docker proxy settings in Docker Desktop → Settings → Resources → Proxies
- Try: `docker info 2>&1 | grep -A5 'DNS'`

### Native build fails
- Ensure Homebrew is installed: `brew doctor`
- Check that `gcc --version` shows Homebrew GCC (not Apple clang)
- The build needs: `make`, `gcc`, `g++`, `gpatch`, `bison`, `flex`, `wget`, `pkgconf`, `libelf`
- Check the full build log: `cat /tmp/buildroot-build.log`

### Still stuck?
- Check Docker Desktop logs: Docker → Troubleshoot → Check for Issues
- Try restarting your Mac
- Verify internet connectivity from Docker: `docker run --rm alpine ping -c 3 google.com`
