/*
 * gifdec.h —— LVGL 8.3.11 GIF 解码器头文件的**修改版**（本项目补丁）
 * 改动: gd_GIF 增加 data_owned 字段（标记 ->data 是否为堆拷贝，关闭时需释放）。
 * 详见同目录 gifdec.c 与 README.md。
 */
#ifndef GIFDEC_H
#define GIFDEC_H

#include <stdint.h>
#include <stddef.h>
#include "../../../misc/lv_fs.h"

#if LV_USE_GIF

typedef struct gd_Palette {
    int size;
    uint8_t colors[0x100 * 3];
} gd_Palette;

typedef struct gd_GCE {
    uint16_t delay;
    uint8_t tindex;
    uint8_t disposal;
    int input;
    int transparency;
} gd_GCE;



typedef struct gd_GIF {
    lv_fs_file_t fd;
    const char * data;
    uint8_t is_file;
    uint8_t data_owned;   /* 1 if ->data was heap_caps_malloc'd (copied out of flash) */
    uint32_t f_rw_p;
    int32_t anim_start;
    uint16_t width, height;
    uint16_t depth;
    int32_t loop_count;
    gd_GCE gce;
    gd_Palette *palette;
    gd_Palette lct, gct;
    void (*plain_text)(
        struct gd_GIF *gif, uint16_t tx, uint16_t ty,
        uint16_t tw, uint16_t th, uint8_t cw, uint8_t ch,
        uint8_t fg, uint8_t bg
    );
    void (*comment)(struct gd_GIF *gif);
    void (*application)(struct gd_GIF *gif, char id[8], char auth[3]);
    uint16_t fx, fy, fw, fh;
    uint8_t bgindex;
    uint8_t *canvas, *frame;
} gd_GIF;

gd_GIF * gd_open_gif_file(const char *fname);

gd_GIF * gd_open_gif_data(const void *data, size_t data_size);

void gd_render_frame(gd_GIF *gif, uint8_t *buffer);

int gd_get_frame(gd_GIF *gif);
void gd_rewind(gd_GIF *gif);
void gd_close_gif(gd_GIF *gif);

#endif /*LV_USE_GIF*/

#endif /* GIFDEC_H */
