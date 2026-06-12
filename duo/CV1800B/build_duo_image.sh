#!/bin/bash
# Build custom MilkV Duo (CV1800B) SD image in Lima VM
# The project directory is mounted read-only inside Lima, so we copy the SDK
# to the VM's local disk (/home/carlton.guest) and build there.
#
# Usage:
#   limactl shell duo-builder -- /Users/carlton/Documents/code/self/AIStatusHub/duo/CV1800B/build_duo_image.sh
# Or run directly inside the VM.

set -e

# Project paths (inside Lima VM)
HOST_PROJ_DIR="/Users/carlton/Documents/code/self/AIStatusHub"
HOST_SDK_DIR="${HOST_PROJ_DIR}/duo/CV1800B/duo-buildroot-sdk"
LOCAL_BASE="/home/carlton.guest"
LOCAL_SDK_DIR="${LOCAL_BASE}/duo-buildroot-sdk"
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
HOST_INSTALL_DIR="${HOST_SDK_DIR}/install/soc_cv1800b_milkv_duo_sd"

log "========================================="
log "编译打包完成，检查输出文件:"
ls -lh "${INSTALL_DIR}" 2>/dev/null || true

IMG_FILE=""
for f in "${INSTALL_DIR}"/*.img; do
    [ -e "$f" ] && IMG_FILE="$f" && break
done

if [ -n "${IMG_FILE}" ]; then
    log "SD 镜像: ${IMG_FILE}"
    # Try to copy important outputs back to host project directory.
    # This may fail if the project directory is mounted read-only inside Lima.
    if mkdir -p "${HOST_INSTALL_DIR}" 2>/dev/null; then
        cp -v "${INSTALL_DIR}"/*.img "${HOST_INSTALL_DIR}/" 2>/dev/null || true
        cp -v "${INSTALL_DIR}"/*.zip "${HOST_INSTALL_DIR}/" 2>/dev/null || true
        cp -v "${INSTALL_DIR}/fip.bin" "${HOST_INSTALL_DIR}/" 2>/dev/null || true
        cp -v "${INSTALL_DIR}/rootfs.tar.xz" "${HOST_INSTALL_DIR}/" 2>/dev/null || true
        cp -r "${INSTALL_DIR}/rawimages" "${HOST_INSTALL_DIR}/" 2>/dev/null || true
        log "输出文件已复制到: ${HOST_INSTALL_DIR}"
        HOST_IMG="${HOST_INSTALL_DIR}/$(basename "${IMG_FILE}")"
    else
        log "宿主机目录在 VM 内只读，无法直接复制。"
        log "请在 macOS 宿主机上手动复制:"
        log "  limactl copy duo-builder:${IMG_FILE} ${HOST_INSTALL_DIR}/$(basename "${IMG_FILE}")"
        HOST_IMG="${HOST_INSTALL_DIR}/$(basename "${IMG_FILE}")"
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
