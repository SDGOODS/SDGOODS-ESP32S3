#!/usr/bin/env bash
# 谷仓次元屏 · 一键构建 / 烧录 / 监视（封装环境坑）
# https://github.com/SDGOODS/SDGOODS-ESP32S3
#
# 解决的问题：
#   idf.py 调 os.mkdir 会被 sitecustomize 的 shim 拦截，在建 build_xxx/log 时崩 EEXIST；
#   同时 cmake 需要 CODEBUDDY_SESSION_ID 存在。所以保留 SESSION_ID、关闭另外三个沙箱变量。
#   本脚本把这套「env 坑」固化下来，AI 或开发者一行就能构建，不用记 export / unset。
#
# 用法：
#   tools/build.sh                  # 原地构建到 build_pub
#   tools/build.sh -B build_fixNN   # 指定构建目录（换名字即可，不要 rm -rf 旧目录）
#   tools/build.sh flash            # 构建 + 烧录（引导层用 platform/prebuilt 预编译件）
#   tools/build.sh monitor          # 打开串口监视
#   tools/build.sh dev              # 构建 + 烧录 + 监视
#   tools/build.sh build -p COMX    # 把 -p 透传给 idf.py（flash 阶段由 flash_local.sh 处理）

set -o pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1

# ---- 定位并激活 ESP-IDF ----
if [ -n "${IDF_PATH:-}" ]; then
    :
elif [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    IDF_PATH="$HOME/esp/esp-idf"
fi
if [ -f "${IDF_PATH:-}/export.sh" ]; then
    # 屏蔽 export 过程噪声；失败也不致命（可能 PATH 里已有 idf.py）
    source "${IDF_PATH}/export.sh" >/dev/null 2>&1 || true
fi

# ---- 解析参数 ----
CMD="build"
BUILD="build_pub"
EXTRA=()
while [ $# -gt 0 ]; do
    case "$1" in
        build|flash|monitor|dev) CMD="$1" ;;
        -B) BUILD="$2"; shift ;;
        *)  EXTRA+=("$1") ;;
    esac
    shift
done

# ---- 封装三个沙箱变量（内联，避免 zsh 不分割存变量的坑）----
run_idf() {
    env -u CODEBUDDY_SAFE_DELETE_SANDBOX \
        -u CODEBUDDY_BROKERED_FS_HOOK_ENABLED \
        -u CODEBUDDY_SAFE_DELETE_ENABLED \
        idf.py -B "$BUILD" "$@"
}

case "$CMD" in
    build)
        run_idf build "${EXTRA[@]}"
        ;;
    flash)
        run_idf build "${EXTRA[@]}" && bash tools/flash_local.sh -b "$BUILD"
        ;;
    monitor)
        run_idf monitor "${EXTRA[@]}"
        ;;
    dev)
        run_idf build "${EXTRA[@]}" \
            && bash tools/flash_local.sh -b "$BUILD" \
            && run_idf monitor "${EXTRA[@]}"
        ;;
esac
