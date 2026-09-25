# 自动生成编译版本号头文件 (格式 MMDDHHMM, 例如 09110532)
# 由 main/CMakeLists.txt 在每次编译时调用，确保刷机后能看到最新编译时间。
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED BIN_DIR)
    set(BIN_DIR ${CMAKE_CURRENT_BINARY_DIR})
endif()

# %m=月份 %d=日 %H=时(24h) %M=分
string(TIMESTAMP BUILD_VERSION "%m%d%H%M")

set(_out "${BIN_DIR}/build_version.h")
file(WRITE "${_out}"
"#pragma once\n"
"/* 本文件由 gen_build_version.cmake 自动生成，请勿手动编辑。\n"
"   版本号为编译时间 MMDDHHMM，每次编译自动刷新。 */\n"
"#define BUILD_VERSION_STR \"${BUILD_VERSION}\"\n"
"\n"
"/* ---- SDGOODS 品牌信息：固件的「身份证」 ----\n"
"   任何需要品牌文案的地方（启动日志、关于页、串口 dump）都从这里取，\n"
"   不要另写一份字面量 —— 改一次就全改（含中文，改动后记得重跑 tools/gen_fonts.py）。 */\n"
"#define SDGOODS_PROGRAM      \"谷仓共创计划\"\n"
"#define SDGOODS_BRAND        \"SDGOODS\"\n"
"#define SDGOODS_PLATFORM     \"谷仓 SDGOODS 开放平台\"\n"
"#define SDGOODS_PRODUCT      \"谷仓次元屏（谷仓电子徽章）\"\n"
"#define SDGOODS_VENDOR       \"深圳希德创新网络有限公司 (SDGOODS)\"\n"
"#define SDGOODS_HOMEPAGE     \"https://sdgoods.ai\"\n"
"/* 联系邮箱：对外统一入口（商业授权、报 bug、合作）。\n"
"   只用于开机串口横幅打印 —— 只拿到一颗烧好的芯片也能找到源头。\n"
"   关于页按产品口径只显示官网 SDGOODS_HOMEPAGE，不展示邮箱。\n"
"   改这里就全改，不要在源码里另写别的邮箱字面量。 */\n"
"#define SDGOODS_CONTACT_EMAIL \"karl@sdgoods.ai\"\n"
"#define SDGOODS_LICENSE_TAG  \"个人免费 · 商用需授权\"\n"
"/* 英文界面的许可标签（品牌名与公司名不翻译，见 sdgoods_i18n.h 的说明）。
   注意长度：这句单行要 327px，而圆屏在它所在的高度只剩约 291px 宽，
   所以关于页是限宽折行显示的（见 ui_about.c 的 ABT_LICENSE_W）。
   改文案后跑 tools/font_metrics.py 量一下宽度。 */\n"
"#define SDGOODS_LICENSE_TAG_EN \"Free for personal use · Commercial needs license\"\n"
)

message(STATUS "build_version.h -> ${BUILD_VERSION}")
