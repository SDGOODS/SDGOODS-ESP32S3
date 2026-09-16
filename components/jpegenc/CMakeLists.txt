# 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
# 平台层 vendored 组件：JPEG 编码器（bitbank2/JPEGENC，Apache-2.0）
#
# 仅用于设备端截屏：把 RGB565 直接编码成 JPEG（见 components/sdgoods_board/src/sdgoods_jpeg.cpp）。
# 上游：https://github.com/bitbank2/JPEGENC  (Copyright 2021 BitBank Software, Inc.)
# 许可证：Apache-2.0（与本项目平台层一致）。

idf_component_register(
    SRCS "JPEGENC.cpp"
    INCLUDE_DIRS "."
)

# 这是一份第三方 vendored 代码，放宽告警（避免 -Werror 把无关告警当错误）：
# 主要是符号比较、未用变量、隐式 fallthrough 等，不影响正确性。
target_compile_options(${COMPONENT_TARGET} PRIVATE
    -Wno-error
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-implicit-fallthrough
    -Wno-unused-but-set-variable
)
