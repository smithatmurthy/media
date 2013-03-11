/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *          Younghwan Joo <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_ISP_H_
#define FIMC_ISP_H_

#include <asm/sizes.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/videobuf2-core.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/s5p_fimc.h>

#include "fimc-core.h"

/* TODO: revisit these constraints */
#define FIMC_ISP_SINK_WIDTH_MIN		(16 + 8)
#define FIMC_ISP_SINK_HEIGHT_MIN	(12 + 8)
#define FIMC_ISP_SOURCE_WIDTH_MIN	8
#define FIMC_ISP_SOURC_HEIGHT_MIN	8
/* FIXME: below are random numbers... */
#define FIMC_ISP_SINK_WIDTH_MAX		(4000 - 16)
#define FIMC_ISP_SINK_HEIGHT_MAX	(4000 + 12)
#define FIMC_ISP_SOURCE_WIDTH_MAX	4000
#define FIMC_ISP_SOURC_HEIGHT_MAX	4000

#define FIMC_ISP_NUM_FORMATS		3
#define FIMC_IS_REQ_BUFS_MIN		2

#define FIMC_IS_SD_PAD_SINK		0
#define FIMC_IS_SD_PAD_SRC_FIFO		1
#define FIMC_IS_SD_PAD_SRC_DMA		2
#define FIMC_IS_SD_PADS_NUM		3
#define FIMC_IS_MAX_PLANES		1

/**
 * struct fimc_isp_frame - source/target frame properties
 * @f_width: full pixel width
 * @f_height: full pixel height
 * @rect: crop/composition rectangle
 */
struct fimc_isp_frame {
	u16 f_width;
	u16 f_height;
	struct v4l2_rect rect;
};

/**
 * struct fimc_isp_buffer - video buffer structure
 * @vb: vb2 buffer
 * @list: list head for the buffers queue
 * @paddr: precalculated physical address
 */
struct fimc_isp_buffer {
	struct vb2_buffer vb;
	struct list_head list;
	dma_addr_t paddr;
};

struct fimc_isp_ctrls {
	struct v4l2_ctrl_handler handler;
	/* Internal mode selection */
	struct v4l2_ctrl *scenario;
	/* Frame rate */
	struct v4l2_ctrl *fps;
	/* Touch AF position */
	struct v4l2_ctrl *af_position_x;
	struct v4l2_ctrl *af_position_y;
	/* Auto white balance */
	struct v4l2_ctrl *auto_wb;
	/* ISO sensitivity */
	struct v4l2_ctrl *auto_iso;
	struct v4l2_ctrl *iso;

	struct v4l2_ctrl *contrast;
	struct v4l2_ctrl *saturation;
	struct v4l2_ctrl *sharpness;
	/* Auto/manual exposure */
	struct v4l2_ctrl *auto_exp;
	/* Manual exposure value */
	struct v4l2_ctrl *exposure;
	/* Adjust - brightness */
	struct v4l2_ctrl *brightness;
	/* Adjust - hue */
	struct v4l2_ctrl *hue;
	/* Exposure metering mode */
	struct v4l2_ctrl *exp_metering;
	/* AFC */
	struct v4l2_ctrl *afc;
	/* AE/AWB lock/unlock */
	struct v4l2_ctrl *aewb_lock;
	/* AF */
	struct v4l2_ctrl *focus_mode;
	/* AF status */
	struct v4l2_ctrl *af_status;
};

/**
 * struct fimc_isp - fimc isp structure
 * @pdev: pointer to FIMC-LITE platform device
 * @variant: variant information for this IP
 * @v4l2_dev: pointer to top the level v4l2_device
 * @vfd: video device node
 * @fh: v4l2 file handle
 * @alloc_ctx: videobuf2 memory allocator context
 * @subdev: FIMC-LITE subdev
 * @vd_pad: media (sink) pad for the capture video node
 * @subdev_pads: the subdev media pads
 * @ctrl_handler: v4l2 control handler
 * @test_pattern: test pattern controls
 * @index: FIMC-LITE platform device index
 * @pipeline: video capture pipeline data structure
 * @slock: spinlock protecting this data structure and the hw registers
 * @video_lock: mutex serializing video device and the subdev operations
 * @clock: FIMC-LITE gate clock
 * @regs: memory mapped io registers
 * @irq_queue: interrupt handler waitqueue
 * @fmt: pointer to color format description structure
 * @payload: image size in bytes (w x h x bpp)
 * @inp_frame: camera input frame structure
 * @out_frame: DMA output frame structure
 * @out_path: output data path (DMA or FIFO)
 * @source_subdev_grp_id: source subdev group id
 * @cac_margin_x: horizontal CAC margin in pixels
 * @cac_margin_y: vertical CAC margin in pixels
 * @state: driver state flags
 * @pending_buf_q: pending buffers queue head
 * @active_buf_q: the queue head of buffers scheduled in hardware
 * @capture_vb_queue: vb2 buffers queue for ISP capture video node
 * @active_buf_count: number of video buffers scheduled in hardware
 * @frame_count: the captured frames counter
 * @reqbufs_count: the number of buffers requested with REQBUFS ioctl
 * @ref_count: driver's private reference counter
 */
struct fimc_isp {
	struct platform_device		*pdev;
	struct fimc_is_variant		*variant;
	struct v4l2_device		*v4l2_dev;
	struct video_device		vfd;
	struct v4l2_fh			fh;
	struct vb2_alloc_ctx		*alloc_ctx;
	struct v4l2_subdev		subdev;
	struct media_pad		vd_pad;
	struct media_pad		subdev_pads[FIMC_IS_SD_PADS_NUM];
	struct v4l2_mbus_framefmt	subdev_fmt;
	struct v4l2_ctrl		*test_pattern;
	struct fimc_pipeline		pipeline;
	struct fimc_isp_ctrls		ctrls;

	u32				index;
	struct mutex			video_lock;
	struct mutex			subdev_lock;
	spinlock_t			slock;

	wait_queue_head_t		irq_queue;

	const struct fimc_fmt		*video_capture_format;
	unsigned long			payload[FIMC_IS_MAX_PLANES];
	struct fimc_isp_frame		inp_frame;
	struct fimc_isp_frame		out_frame;
	enum fimc_datapath		out_path;
	unsigned int			source_subdev_grp_id;

	unsigned int			cac_margin_x;
	unsigned int 			cac_margin_y;

	unsigned long			state;
	struct list_head		pending_buf_q;
	struct list_head		active_buf_q;
	struct vb2_queue		capture_vb_queue;
	unsigned int			frame_count;
	unsigned int			reqbufs_count;
	int				ref_count;
};

#define ctrl_to_fimc_isp(_ctrl) \
	container_of(ctrl->handler, struct fimc_isp, ctrls.handler)

struct fimc_is;

int fimc_isp_subdev_create(struct fimc_isp *isp);
void fimc_isp_subdev_destroy(struct fimc_isp *isp);
void fimc_isp_irq_handler(struct fimc_is *is);
int fimc_is_create_controls(struct fimc_isp *isp);
int fimc_is_delete_controls(struct fimc_isp *isp);
const struct fimc_fmt *fimc_isp_find_format(const u32 *pixelformat,
					const u32 *mbus_code, int index);
#endif /* FIMC_ISP_H_ */
