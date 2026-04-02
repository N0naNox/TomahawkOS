#!/bin/bash
# =============================================================================
#  TomahawkOS — WSL Development Environment Setup
# =============================================================================
#
#  Run once on a fresh WSL (Ubuntu 22.04 / 24.04) installation to get
#  everything needed to build and run TomahawkOS.
#
#  What this installs / builds:
#    1.  System packages  (build tools, QEMU, mtools, gnu-efi, dosfstools …)
#    2.  x86_64-linux-gnu cross toolchain  (for the UEFI bootloader)
#    3.  x86_64-elf bare-metal cross compiler (GCC 13.2.0 + binutils 2.41)
#         → installed to /usr/local/cross  so 'x86_64-elf-gcc' is on PATH
#    4.  OVMF.fd  (UEFI firmware blob for QEMU)
#    5.  Shell PATH/profile update
#
#  Usage:
#    chmod +x dependencies.sh
#    ./dependencies.sh          # builds cross-compiler (~20-40 min)
#    ./dependencies.sh --skip-cross  # skip cross-compiler if already built
#
# =============================================================================

set -euo pipefail

# --------------------------------------------------------------------------- #
#  Colour helpers
# --------------------------------------------------------------------------- #
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()    { echo -e "${CYAN}[setup]${NC} $*"; }
success() { echo -e "${GREEN}[setup]${NC} $*"; }
warn()    { echo -e "${YELLOW}[setup]${NC} $*"; }
die()     { echo -e "${RED}[setup] FATAL:${NC} $*" >&2; exit 1; }
section() { echo -e "\n${BOLD}${CYAN}==============================${NC}"; \
            echo -e "${BOLD}${CYAN}  $*${NC}"; \
            echo -e "${BOLD}${CYAN}==============================${NC}"; }

# --------------------------------------------------------------------------- #
#  Parse flags
# --------------------------------------------------------------------------- #
SKIP_CROSS=false
for arg in "$@"; do
    case "$arg" in
        --skip-cross) SKIP_CROSS=true ;;
        -h|--help)
            echo "Usage: $0 [--skip-cross]"
            echo "  --skip-cross   Skip building the x86_64-elf GCC cross-compiler"
            exit 0
            ;;
        *) die "Unknown argument: $arg" ;;
    esac
done

# --------------------------------------------------------------------------- #
#  Guard: must run inside WSL / Linux
# --------------------------------------------------------------------------- #
if [[ "$(uname -s)" != "Linux" ]]; then
    die "This script must be run inside WSL (Linux). Current OS: $(uname -s)"
fi

# --------------------------------------------------------------------------- #
#  Cross-compiler version pins  (must match kernel/makefile LDFLAGS paths)
# --------------------------------------------------------------------------- #
BINUTILS_VERSION="2.41"
GCC_VERSION="13.2.0"
TARGET="x86_64-elf"
PREFIX="/usr/local/cross"
BUILD_JOBS="$(nproc)"
CROSS_SRC_DIR="${HOME}/cross-src"  # scratch space, deleted afterwards

# --------------------------------------------------------------------------- #
#  STEP 1 — System packages
# --------------------------------------------------------------------------- #
section "STEP 1 — Installing system packages"

info "Updating apt cache..."
sudo apt-get update -qq

PACKAGES=(
    # Core build infrastructure
    build-essential
    make
    nasm          # assembler (.asm sources in kernel & user)
    curl
    wget
    git
    python3       # used in create_fat32_test.sh

    # Cross-compiler build dependencies
    libgmp-dev
    libmpfr-dev
    libmpc-dev
    libisl-dev
    flex
    bison
    texinfo

    # x86_64-linux-gnu toolchain (bootloader)
    gcc-x86-64-linux-gnu
    binutils-x86-64-linux-gnu

    # mingw objcopy (bootloader PE32+ conversion)
    binutils-mingw-w64-x86-64

    # gnu-efi (headers + crt/lds for the UEFI bootloader)
    gnu-efi

    # Disk image tools
    mtools        # mformat / mmd / mcopy  (produces kernel.img)
    dosfstools    # mkfs.fat               (create_fat32_test.sh)

    # initrd
    cpio

    # QEMU
    qemu-system-x86
    qemu-utils

    # OVMF (UEFI firmware for QEMU)
    ovmf

    # Loop-mount support used in create_fat32_test.sh
    mount
)

# libcloog-isl-dev is needed on older Ubuntu but absent on 22.04+; install if available
if apt-cache show libcloog-isl-dev &>/dev/null; then
    PACKAGES+=(libcloog-isl-dev)
fi

info "Installing: ${PACKAGES[*]}"
sudo apt-get install -y --no-install-recommends "${PACKAGES[@]}"
success "System packages installed."

# --------------------------------------------------------------------------- #
#  STEP 2 — x86_64-elf bare-metal cross compiler
# --------------------------------------------------------------------------- #
if $SKIP_CROSS; then
    warn "--skip-cross specified — skipping cross-compiler build."
    warn "Make sure '${PREFIX}/bin' is on your PATH and x86_64-elf-gcc ${GCC_VERSION} is present."
else
    section "STEP 2 — Building x86_64-elf cross compiler (GCC ${GCC_VERSION})"
    info "This will take 20-40 minutes depending on your CPU (using ${BUILD_JOBS} jobs)."

    # Check whether the right version is already built to avoid rebuilding
    EXISTING_GCC="${PREFIX}/bin/${TARGET}-gcc"
    if [[ -x "$EXISTING_GCC" ]]; then
        EXISTING_VER=$("$EXISTING_GCC" --version 2>/dev/null | head -1 | grep -oP '\d+\.\d+\.\d+' | head -1 || true)
        if [[ "$EXISTING_VER" == "$GCC_VERSION" ]]; then
            success "x86_64-elf-gcc ${GCC_VERSION} already at ${PREFIX} — skipping build."
            SKIP_CROSS=true
        else
            warn "Found ${TARGET}-gcc ${EXISTING_VER} at ${PREFIX} — rebuilding to ${GCC_VERSION}."
        fi
    fi
fi

if ! $SKIP_CROSS; then
    mkdir -p "$CROSS_SRC_DIR"

    # ---- 2a. binutils ----
    BINUTILS_TARBALL="binutils-${BINUTILS_VERSION}.tar.xz"
    BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/${BINUTILS_TARBALL}"

    info "Downloading binutils ${BINUTILS_VERSION}..."
    wget -q --show-progress -P "$CROSS_SRC_DIR" "$BINUTILS_URL"

    info "Extracting binutils..."
    tar -xf "${CROSS_SRC_DIR}/${BINUTILS_TARBALL}" -C "$CROSS_SRC_DIR"

    info "Configuring binutils..."
    mkdir -p "${CROSS_SRC_DIR}/build-binutils"
    cd "${CROSS_SRC_DIR}/build-binutils"
    "../binutils-${BINUTILS_VERSION}/configure" \
        --target="$TARGET"          \
        --prefix="$PREFIX"          \
        --with-sysroot              \
        --disable-nls               \
        --disable-werror            \
        --quiet

    info "Building binutils (${BUILD_JOBS} jobs)..."
    make -j"${BUILD_JOBS}" --quiet
    sudo make install --quiet
    success "binutils ${BINUTILS_VERSION} installed to ${PREFIX}."

    # ---- 2b. GCC (C only, no libc) ----
    GCC_TARBALL="gcc-${GCC_VERSION}.tar.xz"
    GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/${GCC_TARBALL}"

    info "Downloading GCC ${GCC_VERSION}..."
    wget -q --show-progress -P "$CROSS_SRC_DIR" "$GCC_URL"

    info "Extracting GCC..."
    tar -xf "${CROSS_SRC_DIR}/${GCC_TARBALL}" -C "$CROSS_SRC_DIR"

    info "Configuring GCC..."
    mkdir -p "${CROSS_SRC_DIR}/build-gcc"
    cd "${CROSS_SRC_DIR}/build-gcc"
    "../gcc-${GCC_VERSION}/configure"  \
        --target="$TARGET"             \
        --prefix="$PREFIX"             \
        --disable-nls                  \
        --enable-languages=c           \
        --without-headers              \
        --quiet

    info "Building GCC all-gcc (${BUILD_JOBS} jobs)..."
    make -j"${BUILD_JOBS}" all-gcc --quiet
    info "Building libgcc..."
    make -j"${BUILD_JOBS}" all-target-libgcc --quiet
    sudo make install-gcc --quiet
    sudo make install-target-libgcc --quiet
    success "GCC ${GCC_VERSION} (${TARGET}) installed to ${PREFIX}."

    info "Cleaning up build sources (${CROSS_SRC_DIR})..."
    rm -rf "$CROSS_SRC_DIR"

    cd -   # return to wherever we were
fi

# --------------------------------------------------------------------------- #
#  STEP 3 — OVMF.fd
# --------------------------------------------------------------------------- #
section "STEP 3 — OVMF (UEFI firmware)"

OVMF_DEST="$(pwd)/OVMF.fd"  # run/rerun scripts expect it beside the workspace

# Try the standard Ubuntu OVMF package location first
OVMF_PKG_PATH=""
for candidate in \
    /usr/share/OVMF/OVMF_CODE.fd \
    /usr/share/ovmf/OVMF.fd \
    /usr/share/qemu/OVMF.fd \
    /usr/share/edk2/ovmf/OVMF_CODE.fd
do
    if [[ -f "$candidate" ]]; then
        OVMF_PKG_PATH="$candidate"
        break
    fi
done

if [[ -f "$OVMF_DEST" ]]; then
    success "OVMF.fd already present at $(pwd)/OVMF.fd — skipping."
elif [[ -n "$OVMF_PKG_PATH" ]]; then
    cp "$OVMF_PKG_PATH" "$OVMF_DEST"
    success "Copied OVMF.fd from ${OVMF_PKG_PATH}."
else
    warn "Could not find OVMF in standard paths. Attempting direct download..."
    OVMF_URL="https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd"
    wget -q --show-progress -O "$OVMF_DEST" "$OVMF_URL" \
        && success "Downloaded OVMF.fd from edk2-nightly." \
        || { warn "Download failed. You will need to copy OVMF.fd manually to: $(pwd)/OVMF.fd"; }
fi

# --------------------------------------------------------------------------- #
#  STEP 4 — PATH configuration
# --------------------------------------------------------------------------- #
section "STEP 4 — Updating PATH"

PROFILE_LINE="export PATH=\"${PREFIX}/bin:\$PATH\""
PROFILE_FILES=("${HOME}/.bashrc" "${HOME}/.profile")

for profile in "${PROFILE_FILES[@]}"; do
    if ! grep -qF "${PREFIX}/bin" "$profile" 2>/dev/null; then
        echo "" >> "$profile"
        echo "# TomahawkOS cross-compiler (x86_64-elf)" >> "$profile"
        echo "$PROFILE_LINE" >> "$profile"
        info "Added cross-compiler to PATH in ${profile}"
    else
        info "${profile} already has ${PREFIX}/bin in PATH — skipping."
    fi
done

# Apply immediately for the rest of this session
export PATH="${PREFIX}/bin:${PATH}"

# --------------------------------------------------------------------------- #
#  STEP 5 — Smoke-test the toolchain
# --------------------------------------------------------------------------- #
section "STEP 5 — Toolchain verification"

TOOLS=(
    "x86_64-elf-gcc --version"
    "x86_64-elf-ld --version"
    "x86_64-elf-objcopy --version"
    "x86_64-linux-gnu-gcc --version"
    "x86_64-w64-mingw32-objcopy --version"
    "nasm --version"
    "qemu-system-x86_64 --version"
    "mformat --version"
    "mkfs.fat --version"
    "cpio --version"
    "python3 --version"
)

ALL_OK=true
for cmd in "${TOOLS[@]}"; do
    BIN="${cmd%% *}"
    if command -v "$BIN" &>/dev/null; then
        VER=$(eval "$cmd" 2>&1 | head -1)
        success "${BIN}: ${VER}"
    else
        warn "MISSING: ${BIN}"
        ALL_OK=false
    fi
done

# gnu-efi headers
if [[ -f /usr/include/efi/efi.h ]]; then
    success "gnu-efi headers: /usr/include/efi/efi.h ✓"
else
    warn "MISSING: /usr/include/efi/efi.h  (gnu-efi headers not found)"
    ALL_OK=false
fi

# gnu-efi crt/lds
if [[ -f /usr/lib/crt0-efi-x86_64.o ]]; then
    success "gnu-efi crt: /usr/lib/crt0-efi-x86_64.o ✓"
else
    warn "MISSING: /usr/lib/crt0-efi-x86_64.o  (gnu-efi runtime not found)"
    ALL_OK=false
fi

if [[ -f "$OVMF_DEST" ]]; then
    success "OVMF.fd: $(pwd)/OVMF.fd ✓"
else
    warn "MISSING: $(pwd)/OVMF.fd  — copy it manually before running QEMU"
fi

# --------------------------------------------------------------------------- #
#  Done
# --------------------------------------------------------------------------- #
echo ""
if $ALL_OK; then
    echo -e "${BOLD}${GREEN}============================================${NC}"
    echo -e "${BOLD}${GREEN}  All tools verified. Environment is ready!${NC}"
    echo -e "${BOLD}${GREEN}============================================${NC}"
    echo ""
    echo "  Build:       cd src && make"
    echo "  Build+run:   bash rerun"
    echo "  Run only:    bash run"
    echo ""
    echo "  NOTE: Restart your terminal (or run 'source ~/.bashrc') so"
    echo "        ${PREFIX}/bin appears on PATH in new shells."
else
    echo -e "${BOLD}${YELLOW}============================================${NC}"
    echo -e "${BOLD}${YELLOW}  Setup complete with warnings (see above).${NC}"
    echo -e "${BOLD}${YELLOW}============================================${NC}"
    echo ""
    echo "  Fix the missing items above, then try 'bash rerun'."
fi
