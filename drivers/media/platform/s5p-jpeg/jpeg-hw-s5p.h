/* linux/drivers/media/platform/s5p-jpeg/jpeg-hw.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef JPEG_HW_S5P_H_
#define JPEG_HW_S5P_H_

#include <linux/io.h>
#include <linux/videodev2.h>

#include "jpeg-regs.h"

#define S5P_JPEG_MIN_WIDTH		32
#define S5P_JPEG_MIN_HEIGHT		32
#define S5P_JPEG_MAX_WIDTH		8192
#define S5P_JPEG_MAX_HEIGHT		8192
#define S5P_JPEG_RAW_IN_565		0
#define S5P_JPEG_RAW_IN_422		1
#define S5P_JPEG_RAW_OUT_422		0
#define S5P_JPEG_RAW_OUT_420		1

void jpeg_reset(void __iomem *regs);
void jpeg_poweron(void __iomem *regs);
void jpeg_input_raw_mode(void __iomem *regs, unsigned long mode);
void jpeg_input_raw_y16(void __iomem *regs, bool y16);
void jpeg_proc_mode(void __iomem *regs, unsigned long mode);
void jpeg_subsampling_mode(void __iomem *regs, unsigned int mode);
unsigned int jpeg_get_subsampling_mode(void __iomem *regs);
void jpeg_dri(void __iomem *regs, unsigned int dri);
void jpeg_qtbl(void __iomem *regs, unsigned int t, unsigned int n);
void jpeg_htbl_ac(void __iomem *regs, unsigned int t);
void jpeg_htbl_dc(void __iomem *regs, unsigned int t);
void jpeg_y(void __iomem *regs, unsigned int y);
void jpeg_x(void __iomem *regs, unsigned int x);
void jpeg_rst_int_enable(void __iomem *regs, bool enable);
void jpeg_data_num_int_enable(void __iomem *regs, bool enable);
void jpeg_final_mcu_num_int_enable(void __iomem *regs, bool enbl);
void jpeg_timer_enable(void __iomem *regs, unsigned long val);
void jpeg_timer_disable(void __iomem *regs);
int jpeg_timer_stat(void __iomem *regs);
void jpeg_clear_timer_stat(void __iomem *regs);
void jpeg_enc_stream_int(void __iomem *regs, unsigned long size);
int jpeg_enc_stream_stat(void __iomem *regs);
void jpeg_clear_enc_stream_stat(void __iomem *regs);
void jpeg_outform_raw(void __iomem *regs, unsigned long format);
void jpeg_jpgadr(void __iomem *regs, unsigned long addr);
void jpeg_imgadr(void __iomem *regs, unsigned long addr);
void jpeg_coef(void __iomem *regs, unsigned int i,
			     unsigned int j, unsigned int coef);
void jpeg_start(void __iomem *regs);
int jpeg_result_stat_ok(void __iomem *regs);
int jpeg_stream_stat_ok(void __iomem *regs);
void jpeg_clear_int(void __iomem *regs);
unsigned int jpeg_compressed_size(void __iomem *regs);

#endif /* JPEG_HW_S5P_H_ */
