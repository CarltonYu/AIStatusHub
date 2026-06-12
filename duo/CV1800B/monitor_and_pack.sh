#!/bin/bash
set -e

BUILD_PID=79957
BR_LOG="/Users/carlton/Documents/code/self/AIStatusHub.git/duo/CV1800B/duo-buildroot-sdk/build/br.log"
SDK_DIR="/Users/carlton/Documents/code/self/AIStatusHub.git/duo/CV1800B/duo-buildroot-sdk"
RESULT_LOG="/Users/carlton/Documents/code/self/AIStatusHub.git/duo/CV1800B/pack_result.log"

: > "$RESULT_LOG"
log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $1" | tee -a "$RESULT_LOG"
}

log "开始监控构建进程 PID=$BUILD_PID"

# 等待构建进程结束
while kill -0 "$BUILD_PID" 2>/dev/null; do
    sleep 30
done

log "构建进程已结束，检查构建结果..."

# 检查是否成功
if tail -n 50 "$BR_LOG" | grep -q "BUILD DONE"; then
    log "构建成功，准备打包 SD 镜像..."
    
    # 执行 pack_sd_image
    if limactl shell duo-builder -- bash -lc "set -e; cd '$SDK_DIR'; source build/envsetup_milkv.sh list >/dev/null; check_board milkv-duo-sd >/dev/null; build_info >/dev/null; pack_sd_image" >> "$RESULT_LOG" 2>&1; then
        log "SD 镜像打包完成"
    else
        log "SD 镜像打包失败，退出码: $?"
        exit 1
    fi
    
    # 配置核验
    log "开始配置核验..."
    {
        echo "--- 生成文件 ---"
        ls -lh "$SDK_DIR/install/soc_cv1800b_milkv_duo_sd/"*.img "$SDK_DIR/install/soc_cv1800b_milkv_duo_sd/"*.zip 2>/dev/null || true
        
        echo "--- kernel .config 核验 ---"
        grep -E "CONFIG_FB_TFT(=y|_ST7789V=|_GC9A01=)" "$SDK_DIR/buildroot-2021.05/output/milkv-duo-sd_musl_riscv64/build/linux-custom/.config" 2>/dev/null || true
        
        echo "--- rootfs 关键文件 ---"
        ls -lh "$SDK_DIR/buildroot-2021.05/output/milkv-duo-sd_musl_riscv64/images/" 2>/dev/null || true
        
        echo "--- 完成 ---"
    } >> "$RESULT_LOG" 2>&1
    
    log "所有步骤完成"
else
    log "构建可能失败或未正常完成，请在 br.log 中检查错误"
    tail -n 100 "$BR_LOG" >> "$RESULT_LOG" 2>&1
    exit 1
fi
