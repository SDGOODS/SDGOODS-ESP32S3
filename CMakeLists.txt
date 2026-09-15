# 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
# 谷仓次元屏（谷仓电子徽章）固件 —— ESP32-S3 + ST77916 QSPI + LVGL
# Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
#
# 工程名决定固件产物名：编译后得到 build*/SDGOODS_EBADGE.bin（烧到 0x10000）。
# EBADGE = Electronic Badge（电子徽章）—— 与产品名「谷仓次元屏（谷仓电子徽章）」一致。
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SDGOODS_EBADGE)
