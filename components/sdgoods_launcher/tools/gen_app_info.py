#!/usr/bin/env python3
"""生成 sdgoods_app_info.h，把工程根 version.txt 透传为 SDGOODS_APP_VERSION。

用法: gen_app_info.py <repo_root> <generated_include_dir>
  - 读取 <repo_root>/version.txt（不存在则默认 1.0.0）
  - 直接透传为 SDGOODS_APP_VERSION（不做末位 +1）

为什么不再 +1：ESP-IDF 的 project.cmake 会在 project() 时读取工程根
version.txt 注入二进制里的 esp_app_desc.version（长按菜单弹框即读此字段）。
若本脚本再 +1，关于页(SDGOODS_APP_VERSION) 就会与弹框(esp_app_desc.version)
结构性差 1。让 version.txt 成为「唯一真相源」：两份显示都读它，永远一致。
发版需要新版本号时，由开发者手动改 version.txt。

该脚本由 sdgoods_launcher 组件的 CMake 以 always-run custom target 驱动，
保证头文件随源码重编（sdgoods_cc.c 依赖此头）。
"""
import os
import sys


def main():
    if len(sys.argv) < 3:
        print("usage: gen_app_info.py <repo_root> <out_dir>", file=sys.stderr)
        sys.exit(2)

    repo = sys.argv[1]
    out_dir = sys.argv[2]
    ver_file = os.path.join(repo, "version.txt")

    ver = "1.0.0"
    if os.path.isfile(ver_file):
        try:
            with open(ver_file, "r", encoding="utf-8") as f:
                v = f.read().strip()
                if v:
                    ver = v
        except OSError:
            pass

    new = ver  # 透传，不再 +1（见文件头说明）

    os.makedirs(out_dir, exist_ok=True)
    hdr = os.path.join(out_dir, "sdgoods_app_info.h")
    try:
        with open(hdr, "w", encoding="utf-8") as f:
            f.write("/* 自动生成，勿手改。由 tools/gen_app_info.py 于每次编译时生成。 */\n")
            f.write("#pragma once\n")
            f.write('#define SDGOODS_APP_VERSION "%s"\n' % new)
            f.write("#define SDGOODS_APP_VERSION_BUILD_TIME (__DATE__ \" \" __TIME__)\n")
        print("app version -> %s" % new)
    except OSError as e:
        print("error: cannot write %s: %s" % (hdr, e), file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
