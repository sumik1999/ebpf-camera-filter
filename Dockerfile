FROM debian:bullseye

# Prevent interactive prompts during build
ENV DEBIAN_FRONTEND=noninteractive
# Allow building as root (required for Docker builds)
ENV FORCE_UNSAFE_CONFIGURE=1

# Install ALL build dependencies for buildroot (with --fix-missing for transient network errors)
RUN apt-get update && \
    apt-get install -y --fix-missing --no-install-recommends \
    build-essential \
    git \
    bc \
    bison \
    flex \
    libssl-dev \
    make \
    libc6-dev \
    libncurses5-dev \
    wget \
    unzip \
    curl \
    ca-certificates \
    python3 \
    python3-dev \
    file \
    gperf \
    libelf-dev \
    texinfo \
    cpio \
    rsync \
    xz-utils \
    bzip2 \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /build

# Copy the entire project
COPY . .

# Ensure buildroot is configured
RUN cd buildroot && \
    make olddefconfig 2>/dev/null || true

# Build the image (parallel jobs)
RUN cd buildroot && \
    make -j$(nproc) && \
    echo "Build completed successfully!" && \
    ls -la output/images/

# Create a tarball of the output for easy extraction
RUN tar czf /build/build-output.tar.gz -C buildroot/output/images .

# Final command shows what was built
CMD ["ls", "-la", "/build/buildroot/output/images/"]
