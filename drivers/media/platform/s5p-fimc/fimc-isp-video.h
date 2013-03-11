/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_ISP_VIDEO__
#define FIMC_ISP_VIDEO__

#include <media/videobuf2-core.h>
#include "fimc-isp.h"

int fimc_isp_video_device_register(struct fimc_isp *isp,
				struct v4l2_device *v4l2_dev);
void fimc_isp_video_device_unregister(struct fimc_isp *isp);

static inline void fimc_is_active_queue_add(struct fimc_isp *isp,
					 struct fimc_isp_buffer *buf)
{
	list_add_tail(&buf->list, &isp->active_buf_q);
}

static inline struct fimc_isp_buffer *fimc_is_active_queue_pop(
					struct fimc_isp *isp)
{
	struct fimc_isp_buffer *buf = list_entry(isp->active_buf_q.next,
					      struct fimc_isp_buffer, list);
	list_del(&buf->list);
	return buf;
}

static inline void fimc_is_pending_queue_add(struct fimc_isp *isp,
					struct fimc_isp_buffer *buf)
{
	list_add_tail(&buf->list, &isp->pending_buf_q);
}

static inline struct fimc_isp_buffer *fimc_is_pending_queue_pop(
					struct fimc_isp *isp)
{
	struct fimc_isp_buffer *buf = list_entry(isp->pending_buf_q.next,
					      struct fimc_isp_buffer, list);
	list_del(&buf->list);
	return buf;
}
#endif /* FIMC_ISP_VIDEO__ */
