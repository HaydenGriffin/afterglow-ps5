FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates clang-18 lld-18 llvm-18 libclang-rt-18-dev clang-format-18 clang-tidy-18 \
    ninja-build make python3 python3-pil curl wget unzip git pkg-config \
    libegl-dev libgl-dev libgl1-mesa-dri ccache && rm -rf /var/lib/apt/lists/*
# PS5 links x86-64 compiler builtins even when the build host is ARM64.
# Extract data only; all executed compiler tools use the host architecture.
RUN curl -fsSL https://archive.ubuntu.com/ubuntu/pool/universe/l/llvm-toolchain-18/libclang-rt-18-dev_18.1.3-1ubuntu1_amd64.deb -o /tmp/compiler-rt.deb \
    && echo '82d5d27d12c0abdf4cefbafa6044e7544a4cbbbec9c04784f5ce326979842b2b  /tmp/compiler-rt.deb' | sha256sum -c - \
    && dpkg-deb -x /tmp/compiler-rt.deb /tmp/compiler-rt \
    && cp /tmp/compiler-rt/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a /usr/lib/llvm-18/lib/clang/18/lib/linux/ \
    && rm -rf /tmp/compiler-rt /tmp/compiler-rt.deb
WORKDIR /work
ENV BUILD_JOBS=2 HOST_CXX=clang++-18 HOST_CC=clang-18
CMD ["make", "app"]
