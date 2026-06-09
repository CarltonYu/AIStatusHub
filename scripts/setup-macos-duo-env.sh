#!/bin/bash
set -e

echo "=== AIStatusHub macOS Duo Build Environment Setup ==="

# 1. Install Lima if not present
if ! command -v limactl &> /dev/null; then
    echo "[1/5] Installing Lima via Homebrew..."
    brew install lima
else
    echo "[1/5] Lima already installed: $(limactl --version)"
fi

# 2. Start Ubuntu 22.04 VM
VM_NAME="duo-builder"
if ! limactl list "$VM_NAME" --format json &> /dev/null; then
    echo "[2/5] Creating Lima VM '$VM_NAME' with Ubuntu LTS..."
    limactl start --name="$VM_NAME" template://ubuntu-lts
else
    echo "[2/5] Lima VM '$VM_NAME' already exists."
    echo "        To recreate: limactl delete $VM_NAME && limactl start --name=$VM_NAME template://ubuntu-lts"
fi

# 3. Ensure VM is running
if ! limactl list "$VM_NAME" --format json | grep -q '"status":"Running"'; then
    echo "[3/5] Starting VM '$VM_NAME'..."
    limactl start "$VM_NAME"
else
    echo "[3/5] VM '$VM_NAME' is already running."
fi

# 4. Install dependencies inside VM
echo "[4/5] Installing build dependencies inside VM..."
limactl shell "$VM_NAME" << 'EOF'
set -e
sudo apt-get update
sudo apt-get install -y \
  build-essential bison flex libssl-dev bc \
  python3 python3-pip \
  git wget curl unzip \
  libncurses5-dev libncursesw5-dev \
  device-tree-compiler \
  cpio rsync file texinfo \
  gawk chrpath diffstat \
  zstd lz4 lzma \
  cmake expect mtools genext2fs parted
EOF

# 5. Download host-tools (cross compiler)
SDK_DIR="duo/CV1800B/duo-buildroot-sdk"
HOST_TOOLS_TGZ="host-tools.tar.gz"
HOST_TOOLS_URL="https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz"

echo "[5/5] Checking host-tools cross compiler..."
if [ -d "$SDK_DIR/host-tools" ]; then
    echo "        host-tools already exists."
else
    echo "        Downloading host-tools..."
    (
        cd "$SDK_DIR"
        if [ ! -f "$HOST_TOOLS_TGZ" ]; then
            wget "$HOST_TOOLS_URL"
        fi
        tar xzf "$HOST_TOOLS_TGZ"
    )
fi

echo ""
echo "=== Setup complete! ==="
echo ""
echo "Next steps:"
echo "  1. Enter VM:   limactl shell $VM_NAME"
echo "  2. Go to SDK:  cd $(pwd)/$SDK_DIR"
echo "  3. Setup env:  source build/envsetup_milkv.sh"
echo "  4. Config:     defconfig cv1800b_milkv_duo_sd"
echo "  5. Build:      build_all"
echo ""
echo "See docs/Duo_Buildroot_Guide.md for full details."
