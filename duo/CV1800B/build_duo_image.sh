#!/bin/bash
# Build custom MilkV Duo (CV1800B) SD image in Lima VM
# The project directory is mounted read-only inside Lima, so we copy the SDK
# to the VM's local disk (/home/carlton.guest) and build there.
#
# Usage:
#   ./duo/CV1800B/build_duo_image.sh              # on macOS: auto-runs inside Lima VM
#   limactl shell duo-builder -- /Users/carlton/Documents/code/self/AIStatusHub/duo/CV1800B/build_duo_image.sh
# Or run directly inside the VM.

# Project paths (used both on macOS and inside Lima VM)
HOST_PROJ_DIR="/Users/carlton/Documents/code/self/AIStatusHub"
HOST_SDK_DIR="${HOST_PROJ_DIR}/duo/CV1800B/duo-buildroot-sdk"
HOST_INSTALL_DIR="${HOST_SDK_DIR}/install/soc_cv1800b_milkv_duo_sd"
LOCAL_BASE="/home/carlton.guest"
LOCAL_SDK_DIR="${LOCAL_BASE}/duo-buildroot-sdk"
LOCAL_INSTALL_DIR="${LOCAL_SDK_DIR}/install/soc_cv1800b_milkv_duo_sd"

# On macOS, run the build inside the Lima VM, then use limactl copy to pull
# outputs back. Direct cp through the virtiofs/sshfs mount is unreliable for
# large files, so we avoid it.
if [ "$(uname -s)" = "Darwin" ]; then
    LIMA_VM="duo-builder"
    SCRIPT_IN_LIMA="${HOST_PROJ_DIR}/duo/CV1800B/build_duo_image.sh"

    if ! command -v limactl >/dev/null 2>&1; then
        echo "错误: 本脚本需要在 Lima VM 内运行，但当前系统未安装 limactl。" >&2
        echo "请先安装 Lima 并创建 VM: scripts/setup-macos-duo-env.sh" >&2
        exit 1
    fi

    if ! limactl list --format '{{.Name}}' 2>/dev/null | grep -qx "${LIMA_VM}"; then
        echo "错误: Lima VM '${LIMA_VM}' 不存在。" >&2
        echo "请先执行: scripts/setup-macos-duo-env.sh" >&2
        exit 1
    fi

    echo "在 macOS 上检测到，自动进入 Lima VM '${LIMA_VM}' 执行构建..."
    if ! limactl shell "${LIMA_VM}" -- env SKIP_HOST_COPY=1 "${SCRIPT_IN_LIMA}" "$@"; then
        echo "错误: Lima VM 内构建失败。" >&2
        exit 1
    fi

    echo "构建完成，开始用 limactl copy 复制输出到 macOS..."
    mkdir -p "${HOST_INSTALL_DIR}"
    limactl copy "${LIMA_VM}:${LOCAL_INSTALL_DIR}/milkv-duo-sd.img" "${HOST_INSTALL_DIR}/milkv-duo-sd.img"
    limactl copy "${LIMA_VM}:${LOCAL_INSTALL_DIR}/upgrade.zip" "${HOST_INSTALL_DIR}/upgrade.zip"
    limactl copy "${LIMA_VM}:${LOCAL_INSTALL_DIR}/fip.bin" "${HOST_INSTALL_DIR}/fip.bin"
    echo "输出文件已复制到: ${HOST_INSTALL_DIR}"
    exit 0
fi

set -e

# Paths inside Lima VM
LOCAL_LOG_DIR="${LOCAL_BASE}/duo-build-logs"
LOCAL_BUILD_LOG="${LOCAL_LOG_DIR}/build_duo_image.log"
LOCAL_RESULT_LOG="${LOCAL_LOG_DIR}/build_duo_image_result.log"
HOST_BUILD_LOG="${HOST_PROJ_DIR}/duo/CV1800B/build_duo_image.log"
HOST_RESULT_LOG="${HOST_PROJ_DIR}/duo/CV1800B/build_duo_image_result.log"

mkdir -p "${LOCAL_LOG_DIR}"
: > "${LOCAL_BUILD_LOG}"
: > "${LOCAL_RESULT_LOG}"

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $1" | tee -a "${LOCAL_RESULT_LOG}" "${LOCAL_BUILD_LOG}"
}

exec > >(tee -a "${LOCAL_BUILD_LOG}")
exec 2>&1

copy_logs_back() {
    mkdir -p "$(dirname "${HOST_BUILD_LOG}")"
    cp -f "${LOCAL_BUILD_LOG}" "${HOST_BUILD_LOG}" 2>/dev/null || true
    cp -f "${LOCAL_RESULT_LOG}" "${HOST_RESULT_LOG}" 2>/dev/null || true
}
trap copy_logs_back EXIT

# Compute MD5 of a file (uses md5sum in Lima, falls back to md5 on macOS).
file_md5() {
    local f="$1"
    if command -v md5sum >/dev/null 2>&1; then
        md5sum "$f" | awk '{print $1}'
    elif command -v md5 >/dev/null 2>&1; then
        md5 "$f" | awk '{print $NF}'
    else
        echo "unknown"
    fi
}

# Copy a file from VM local disk to host project directory and verify checksum.
# Falls back to limactl copy if the direct write through the mount is unreliable.
copy_file_with_verify() {
    local src="$1"
    local dst="$2"
    local src_md5 dst_md5

    if [ ! -f "$src" ]; then
        return 1
    fi

    mkdir -p "$(dirname "$dst")" 2>/dev/null || true
    if cp -f -v "$src" "$dst" 2>/dev/null; then
        src_md5=$(file_md5 "$src")
        dst_md5=$(file_md5 "$dst")
        if [ "$src_md5" = "$dst_md5" ] && [ "$src_md5" != "unknown" ]; then
            return 0
        fi
        log "警告: 直接复制后校验和不匹配，尝试 limactl copy..."
    fi

    if command -v limactl >/dev/null 2>&1; then
        limactl copy "duo-builder:${src}" "$dst" 2>/dev/null || true
        src_md5=$(file_md5 "$src")
        dst_md5=$(file_md5 "$dst")
        if [ "$src_md5" = "$dst_md5" ] && [ "$src_md5" != "unknown" ]; then
            return 0
        fi
    fi

    return 1
}

log "========================================="
log "开始编译定制 Duo Linux 镜像"
log "宿主机 SDK 目录: ${HOST_SDK_DIR}"
log "VM 本地编译目录: ${LOCAL_SDK_DIR}"
log "========================================="

# 1. Verify host SDK path exists
if [ ! -d "${HOST_SDK_DIR}" ]; then
    log "错误: 宿主机 SDK 目录不存在: ${HOST_SDK_DIR}"
    exit 1
fi

# 2. Copy SDK to VM local disk (read-only virtiofs workaround)
#    Skip copy if local SDK already exists and is at the right commit,
#    to allow resuming a failed build.
HOST_SUBMODULE_COMMIT="$(cd "${HOST_PROJ_DIR}" && git ls-tree HEAD duo/CV1800B/duo-buildroot-sdk 2>/dev/null | awk '{print $3}')"
SKIP_COPY=0
if [ -d "${LOCAL_SDK_DIR}/.git" ]; then
    LOCAL_COMMIT="$(cd "${LOCAL_SDK_DIR}" && git rev-parse HEAD 2>/dev/null || echo 'unknown')"
    if [ "${LOCAL_COMMIT}" = "${HOST_SUBMODULE_COMMIT}" ]; then
        SKIP_COPY=1
        log "VM 本地 SDK 已存在且提交匹配，跳过复制以继续上次构建"
    fi
fi

if [ "${SKIP_COPY}" -eq 0 ]; then
    log "正在将 SDK 复制到 VM 本地磁盘，请稍候..."
    rm -rf "${LOCAL_SDK_DIR}"
    mkdir -p "${LOCAL_SDK_DIR}"

    if command -v rsync >/dev/null 2>&1; then
        rsync -a --info=progress2 "${HOST_SDK_DIR}/" "${LOCAL_SDK_DIR}/"
    else
        cp -a "${HOST_SDK_DIR}/"* "${LOCAL_SDK_DIR}/"
    fi
    log "SDK 已复制到 ${LOCAL_SDK_DIR}"
else
    log "使用已存在的本地 SDK: ${LOCAL_SDK_DIR}"
fi

# 1a. Always sync board overlay files. The SKIP_COPY optimization above skips the
# full SDK copy when the submodule commit matches, but overlay files (e.g. extra
# binaries, init scripts) are often changed without bumping the submodule commit.
# If we don't re-sync them, the final rootfs will use stale overlays.
log "同步 overlay 文件到本地 SDK..."
OVERLAY_DIRS=(
    "device/common/br_overlay"
    "device/milkv-duo-sd/overlay"
)

# Remove stale files that have been renamed/removed in the host SDK but may
# still be present in the VM's cached buildroot target/rootfs. Buildroot's
# overlay application does not always delete removed files.
STALE_FILES=(
    "${LOCAL_SDK_DIR}/device/common/br_overlay/etc/init.d/S99gc9a01-face"
    "${BR_OUTPUT_LOCAL}/milkv-duo-sd_musl_riscv64/target/etc/init.d/S99gc9a01-face"
    "${BR_ROOTFS_LOCAL}/etc/init.d/S99gc9a01-face"
)
for p in "${STALE_FILES[@]}"; do
    [ -f "$p" ] && { rm -f "$p"; log "移除陈旧文件: $p"; }
done

for rel in "${OVERLAY_DIRS[@]}"; do
    host_dir="${HOST_SDK_DIR}/${rel}"
    local_dir="${LOCAL_SDK_DIR}/${rel}"
    if [ -d "${host_dir}" ]; then
        mkdir -p "${local_dir}"
        if command -v rsync >/dev/null 2>&1; then
            rsync -a --delete "${host_dir}/" "${local_dir}/"
        else
            rm -rf "${local_dir}"
            cp -a "${host_dir}" "${local_dir}"
        fi
        log "  已同步 ${rel}"
    fi
done

# 1b. Sync source files that affect the kernel / u-boot configuration. The
# SKIP_COPY optimization skips the full SDK copy when the submodule commit
# matches, so these files must be kept in sync explicitly. When they change,
# the corresponding build directory must be cleaned or the SDK will reuse stale
# artifacts and silently ignore the new defconfig / bootargs.
log "同步内核 / u-boot 关键源码文件..."
NEED_CLEAN_KERNEL=0
NEED_CLEAN_UBOOT=0

sync_source_file() {
    local rel="$1"
    local host_f="${HOST_SDK_DIR}/${rel}"
    local local_f="${LOCAL_SDK_DIR}/${rel}"
    if [ ! -f "${host_f}" ]; then
        log "  警告: 宿主机文件不存在: ${rel}"
        return 1
    fi
    mkdir -p "$(dirname "${local_f}")"
    if [ ! -f "${local_f}" ] || ! diff -q "${host_f}" "${local_f}" >/dev/null 2>&1; then
        cp -f "${host_f}" "${local_f}"
        log "  已同步 ${rel}"
        return 0
    fi
    return 1
}

if sync_source_file "build/boards/cv180x/cv1800b_milkv_duo_sd/linux/cvitek_cv1800b_milkv_duo_sd_defconfig"; then
    NEED_CLEAN_KERNEL=1
fi
if sync_source_file "u-boot-2021.10/include/configs/cv180x-asic.h"; then
    NEED_CLEAN_UBOOT=1
fi

cd "${LOCAL_SDK_DIR}"

# 3. Verify submodule commit matches parent tracking
CURRENT_COMMIT="$(git rev-parse HEAD 2>/dev/null || echo 'unknown')"
log "父仓库追踪子模块提交: ${HOST_SUBMODULE_COMMIT}"
log "当前子模块提交: ${CURRENT_COMMIT}"
if [ -n "${HOST_SUBMODULE_COMMIT}" ] && [ "${CURRENT_COMMIT}" != "${HOST_SUBMODULE_COMMIT}" ]; then
    log "切换到父仓库追踪的提交..."
    git checkout -f "${HOST_SUBMODULE_COMMIT}"
fi
log "当前提交信息: $(git log -1 --pretty=%s)"

# 4. Check host-tools
if [ ! -d "${LOCAL_SDK_DIR}/host-tools/gcc" ]; then
    log "错误: host-tools 交叉编译器未找到"
    log "请在 macOS 宿主机上执行:"
    log "  cd ${HOST_SDK_DIR}"
    log "  wget https://sophon-file.sophon.cn/sophon-prod-s3/drive/23/03/07/16/host-tools.tar.gz"
    log "  tar xzf host-tools.tar.gz"
    exit 1
fi
log "host-tools 已就绪"

# 5. Redirect buildroot output to VM local disk to avoid virtiofs hard-link/setuid issues
BR_OUTPUT_LOCAL="/tmp/duo-buildroot-output"
BR_ROOTFS_LOCAL="/tmp/br-rootfs-local"

log "设置本地编译输出目录..."
mkdir -p "${BR_OUTPUT_LOCAL}"
mkdir -p "${BR_ROOTFS_LOCAL}"

rm -rf "${LOCAL_SDK_DIR}/buildroot-2021.05/output"
ln -sfn "${BR_OUTPUT_LOCAL}" "${LOCAL_SDK_DIR}/buildroot-2021.05/output"

# 6. Source SDK environment (pass 'list' to avoid interactive board selection)
log "加载 SDK 环境..."
source build/envsetup_milkv.sh list

# 7. Select board config
log "选择板级配置: milkv-duo-sd"
check_board milkv-duo-sd
build_info

# 7a. Clean stale build artifacts when kernel / u-boot source config changed.
# build_all reuses existing build directories, so a clean is required for the
# updated defconfig / bootargs to take effect.
if [ "${NEED_CLEAN_KERNEL}" -eq 1 ]; then
    log "内核 defconfig 已更新，执行 clean_kernel..."
    clean_kernel || { log "警告: clean_kernel 失败，继续尝试构建"; }
fi
if [ "${NEED_CLEAN_UBOOT}" -eq 1 ]; then
    log "u-boot 头文件已更新，执行 clean_uboot..."
    clean_uboot || { log "警告: clean_uboot 失败，继续尝试构建"; }
fi

# 8. Patch middleware sample build for no-camera / no-ION image
#    Camera/ISP/video samples reference symbols unavailable when ION is disabled.
SAMPLE_MAKEFILE="${LOCAL_SDK_DIR}/middleware/v2/sample/Makefile"
if [ -f "${SAMPLE_MAKEFILE}" ]; then
    log "打补丁：跳过 camera/ISP/video 相关 middleware 样例..."
    cat > "${SAMPLE_MAKEFILE}" <<'EOF'
all:
	@echo "##############################"
	@echo "#                            #"
	@echo "#   Compiling 'samples'...   #"
	@echo "#                            #"
	@echo "##############################"
	@cd common; make all || exit 1; cd ../;
	@for x in `find -L ./ -maxdepth 2 -mindepth 2 -name "Makefile" `; do \
		dir=`dirname $$x`; \
		case "$$dir" in \
			./venc|./vdec|./vdecvo|./vio|./sensor_test|./ir_auto|./fisheye|./cvg|./scene_auto|./tp2863_tp2803|./osd|./region|./overlay) \
				echo "Skipping camera/ISP/video sample: $$dir"; \
				continue ;; \
			esac; \
		if [ "$$x" = "./mipi_tx/Makefile" ] && [ "${CHIP_ARCH}" = "CV180X" ]; then \
			continue; \
		fi; \
		cd "$$dir"; \
		if [ $$? ]; then \
			$(MAKE) all || exit 1; \
			cd ../; \
		fi; \
	done

clean:
	@for x in `find -L ./ -maxdepth 2 -mindepth 2 -name "Makefile" `; do \
		cd `dirname $$x`; \
		if [ $$? ]; then \
			$(MAKE) clean; \
			cd ../; \
		fi; \
	done
EOF
fi

# 9. Full build
log "开始全量编译 (build_all)，预计 30-120 分钟..."
if build_all; then
    log "build_all 完成"
else
    log "错误: build_all 失败"
    exit 1
fi

# 10. Pack SD image
log "开始打包 SD 镜像..."
if pack_sd_image; then
    log "SD 镜像打包完成"
else
    log "错误: pack_sd_image 失败"
    exit 1
fi

# 11. Copy output image back to host project directory
INSTALL_DIR="${LOCAL_SDK_DIR}/install/soc_cv1800b_milkv_duo_sd"

log "========================================="
log "编译打包完成，检查输出文件:"
ls -lh "${INSTALL_DIR}" 2>/dev/null || true

IMG_FILE=""
for f in "${INSTALL_DIR}"/*.img; do
    [ -e "$f" ] && IMG_FILE="$f" && break
done

if [ -n "${IMG_FILE}" ]; then
    log "SD 镜像: ${IMG_FILE}"

    if [ "${SKIP_HOST_COPY:-0}" = "1" ]; then
        # macOS wrapper will pull outputs with limactl copy after the VM build finishes.
        log "跳过宿主机复制（由 macOS 外层脚本通过 limactl copy 处理）。"
    else
        # Copy important outputs back to host project directory.
        # Direct cp through the virtiofs/sshfs mount can silently produce stale files,
        # so we verify checksums and fall back to limactl copy when necessary.
        mkdir -p "${HOST_INSTALL_DIR}" 2>/dev/null || true

        HOST_IMG="${HOST_INSTALL_DIR}/$(basename "${IMG_FILE}")"
        if copy_file_with_verify "${IMG_FILE}" "${HOST_IMG}"; then
            log "SD 镜像已复制并校验通过: ${HOST_IMG}"
        else
            log "警告: SD 镜像复制到宿主机可能未成功。"
            log "请在 macOS 宿主机上手动复制:"
            log "  limactl copy duo-builder:${IMG_FILE} ${HOST_IMG}"
        fi

        # Copy other outputs (best-effort; verify when possible).
        for f in "${INSTALL_DIR}"/*.zip "${INSTALL_DIR}/fip.bin" "${INSTALL_DIR}/rootfs.tar.xz"; do
            if [ -f "$f" ]; then
                copy_file_with_verify "$f" "${HOST_INSTALL_DIR}/$(basename "$f")" || true
            fi
        done
        # rawimages is a directory of intermediate files; skip if host dir is read-only.
        cp -r "${INSTALL_DIR}/rawimages" "${HOST_INSTALL_DIR}/" 2>/dev/null || true
        log "输出目录: ${HOST_INSTALL_DIR}"
    fi

    log "在 macOS 上可用以下命令烧录到 SD 卡:"
    log "  diskutil list"
    log "  diskutil unmountDisk /dev/diskX"
    log "  sudo dd if=${HOST_IMG} of=/dev/rdiskX bs=1m status=progress"
    log "  diskutil eject /dev/diskX"
else
    log "警告: 未找到生成的 .img 文件"
fi

log "详细日志: ${HOST_BUILD_LOG}"
log "结果摘要: ${HOST_RESULT_LOG}"
log "========================================="
