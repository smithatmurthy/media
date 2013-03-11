/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "fimc-mdevice.h"
#include "fimc-core.h"
#include "fimc-is.h"

static int isp_video_capture_start_streaming(struct vb2_queue *q,
					unsigned int count)
{
	/* TODO: start ISP output DMA */
	return 0;
}

static int isp_video_capture_stop_streaming(struct vb2_queue *q)
{
	/* TODO: stop ISP output DMA */
	return 0;
}

static int isp_video_capture_queue_setup(struct vb2_queue *vq,
			const struct v4l2_format *pfmt,
			unsigned int *num_buffers, unsigned int *num_planes,
			unsigned int sizes[], void *allocators[])
{
	const struct v4l2_pix_format_mplane *pixm = NULL;
	struct fimc_isp *isp = vq->drv_priv;
	struct fimc_isp_frame *frame = &isp->out_frame;
	const struct fimc_fmt *fmt = isp->video_capture_format;
	unsigned long wh;
	int i;

	if (pfmt) {
		pixm = &pfmt->fmt.pix_mp;
		fmt = fimc_isp_find_format(&pixm->pixelformat, NULL, -1);
		wh = pixm->width * pixm->height;
	} else {
		wh = frame->f_width * frame->f_height;
	}

	if (fmt == NULL)
		return -EINVAL;

	*num_planes = fmt->memplanes;

	for (i = 0; i < fmt->memplanes; i++) {
		unsigned int size = (wh * fmt->depth[i]) / 8;
		if (pixm)
			sizes[i] = max(size, pixm->plane_fmt[i].sizeimage);
		else
			sizes[i] = size;
		allocators[i] = isp->alloc_ctx;
	}

	return 0;
}

static int isp_video_capture_buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct fimc_isp *isp = vq->drv_priv;
	int i;

	if (isp->video_capture_format == NULL)
		return -EINVAL;

	for (i = 0; i < isp->video_capture_format->memplanes; i++) {
		unsigned long size = isp->payload[i];

		if (vb2_plane_size(vb, i) < size) {
			v4l2_err(&isp->vfd,
				 "User buffer too small (%ld < %ld)\n",
				 vb2_plane_size(vb, i), size);
			return -EINVAL;
		}
		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static void isp_video_capture_buffer_queue(struct vb2_buffer *vb)
{
	/* TODO: */
}

static void isp_video_lock(struct vb2_queue *vq)
{
	struct fimc_isp *isp = vb2_get_drv_priv(vq);
	mutex_lock(&isp->video_lock);
}

static void isp_video_unlock(struct vb2_queue *vq)
{
	struct fimc_isp *isp = vb2_get_drv_priv(vq);
	mutex_unlock(&isp->video_lock);
}

static const struct vb2_ops isp_video_capture_qops = {
	.queue_setup	 = isp_video_capture_queue_setup,
	.buf_prepare	 = isp_video_capture_buffer_prepare,
	.buf_queue	 = isp_video_capture_buffer_queue,
	.wait_prepare	 = isp_video_unlock,
	.wait_finish	 = isp_video_lock,
	.start_streaming = isp_video_capture_start_streaming,
	.stop_streaming	 = isp_video_capture_stop_streaming,
};

static int isp_video_capture_open(struct file *file)
{
	struct fimc_isp *isp = video_drvdata(file);
	int ret = 0;

	if (mutex_lock_interruptible(&isp->video_lock))
		return -ERESTARTSYS;

	/* ret = pm_runtime_get_sync(&isp->pdev->dev); */
	if (ret < 0)
		goto done;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto done;

	/* TODO: prepare video pipeline */
done:
	mutex_unlock(&isp->video_lock);
	return ret;
}

static int isp_video_capture_close(struct file *file)
{
	struct fimc_isp *isp = video_drvdata(file);
	int ret = 0;

	mutex_lock(&isp->video_lock);

	if (isp->out_path == FIMC_IO_DMA) {
		/* TODO: stop capture, cleanup */
	}

	/* pm_runtime_put(&isp->pdev->dev); */

	if (isp->ref_count == 0)
		vb2_queue_release(&isp->capture_vb_queue);

	ret = v4l2_fh_release(file);

	mutex_unlock(&isp->video_lock);
	return ret;
}

static unsigned int isp_video_capture_poll(struct file *file,
				   struct poll_table_struct *wait)
{
	struct fimc_isp *isp = video_drvdata(file);
	int ret;

	mutex_lock(&isp->video_lock);
	ret = vb2_poll(&isp->capture_vb_queue, file, wait);
	mutex_unlock(&isp->video_lock);
	return ret;
}

static int isp_video_capture_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fimc_isp *isp = video_drvdata(file);
	int ret;

	if (mutex_lock_interruptible(&isp->video_lock))
		return -ERESTARTSYS;

	ret = vb2_mmap(&isp->capture_vb_queue, vma);
	mutex_unlock(&isp->video_lock);

	return ret;
}

static const struct v4l2_file_operations isp_video_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= isp_video_capture_open,
	.release	= isp_video_capture_close,
	.poll		= isp_video_capture_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= isp_video_capture_mmap,
};

/*
 * Video node ioctl operations
 */
static int fimc_isp_capture_querycap_capture(struct file *file, void *priv,
					     struct v4l2_capability *cap)
{

	strlcpy(cap->driver, FIMC_IS_DRV_NAME, sizeof(cap->driver));
	strlcpy(cap->card, FIMC_IS_DRV_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
				"platform:exynos4x12-isp");

	cap->device_caps = V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int fimc_isp_capture_enum_fmt_mplane(struct file *file, void *priv,
					    struct v4l2_fmtdesc *f)
{
	const struct fimc_fmt *fmt;

	if (f->index >= FIMC_ISP_NUM_FORMATS)
		return -EINVAL;

	fmt = fimc_isp_find_format(NULL, NULL, f->index);
	if (WARN_ON(fmt == NULL))
		return -EINVAL;

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int fimc_isp_capture_g_fmt_mplane(struct file *file, void *fh,
					 struct v4l2_format *f)
{
	/* TODO:  */
	return 0;
}

static int fimc_isp_capture_try_fmt(struct fimc_isp *isp,
				    struct v4l2_pix_format_mplane *pixm,
				    const struct fimc_fmt **ffmt)
{
	/* TODO: */
	return 0;
}

static int fimc_isp_capture_try_fmt_mplane(struct file *file, void *fh,
					   struct v4l2_format *f)
{
	struct fimc_isp *isp = video_drvdata(file);
	return fimc_isp_capture_try_fmt(isp, &f->fmt.pix_mp, NULL);
}

static int fimc_isp_capture_s_fmt_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	/* TODO: */
	return 0;
}

static int fimc_isp_pipeline_validate(struct fimc_isp *isp)
{
	/* TODO: */
	return 0;
}

static int fimc_isp_capture_streamon(struct file *file, void *priv,
				     enum v4l2_buf_type type)
{
	struct fimc_isp *isp = video_drvdata(file);
	struct v4l2_subdev *sensor = isp->pipeline.subdevs[IDX_SENSOR];
	struct fimc_pipeline *p = &isp->pipeline;
	int ret;

	/* TODO: check if the OTF interface is not running */

	ret = media_entity_pipeline_start(&sensor->entity, p->m_pipeline);
	if (ret < 0)
		return ret;

	ret = fimc_isp_pipeline_validate(isp);
	if (ret) {
		media_entity_pipeline_stop(&sensor->entity);
		return ret;
	}

	return vb2_streamon(&isp->capture_vb_queue, type);
}

static int fimc_isp_capture_streamoff(struct file *file, void *priv,
				      enum v4l2_buf_type type)
{
	struct fimc_isp *isp = video_drvdata(file);
	struct v4l2_subdev *sd = isp->pipeline.subdevs[IDX_SENSOR];
	int ret;

	ret = vb2_streamoff(&isp->capture_vb_queue, type);
	if (ret == 0)
		media_entity_pipeline_stop(&sd->entity);
	return ret;
}

static int fimc_isp_capture_reqbufs(struct file *file, void *priv,
				    struct v4l2_requestbuffers *reqbufs)
{
	struct fimc_isp *isp = video_drvdata(file);
	int ret;

	reqbufs->count = max_t(u32, FIMC_IS_REQ_BUFS_MIN, reqbufs->count);
	ret = vb2_reqbufs(&isp->capture_vb_queue, reqbufs);
	if (!ret < 0)
		isp->reqbufs_count = reqbufs->count;

	return ret;
}

static const struct v4l2_ioctl_ops isp_video_capture_ioctl_ops = {
	.vidioc_querycap		= fimc_isp_capture_querycap_capture,
	.vidioc_enum_fmt_vid_cap_mplane	= fimc_isp_capture_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_isp_capture_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_isp_capture_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_isp_capture_g_fmt_mplane,
	.vidioc_reqbufs			= fimc_isp_capture_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= fimc_isp_capture_streamon,
	.vidioc_streamoff		= fimc_isp_capture_streamoff,
};

int fimc_isp_video_device_register(struct fimc_isp *isp,
				   struct v4l2_device *v4l2_dev)
{
	struct vb2_queue *q = &isp->capture_vb_queue;
	struct video_device *vfd = &isp->vfd;
	int ret;

	mutex_init(&isp->video_lock);
	INIT_LIST_HEAD(&isp->pending_buf_q);
	INIT_LIST_HEAD(&isp->active_buf_q);

	memset(vfd, 0, sizeof(*vfd));
	snprintf(vfd->name, sizeof(vfd->name), "fimc-is-isp.capture");

	isp->video_capture_format = fimc_isp_find_format(NULL, NULL, 0);
	isp->out_path = FIMC_IO_DMA;
	isp->ref_count = 0;
	isp->reqbufs_count = 0;

	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP;
	q->ops = &isp_video_capture_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct flite_buffer);
	q->drv_priv = isp;
	q->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	ret = vb2_queue_init(q);
	if (ret < 0)
		return ret;

	vfd->queue = q;
	vfd->fops = &isp_video_capture_fops;
	vfd->ioctl_ops = &isp_video_capture_ioctl_ops;
	vfd->v4l2_dev = v4l2_dev;
	vfd->minor = -1;
	vfd->release = video_device_release_empty;
	vfd->lock = &isp->video_lock;

	isp->vd_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_init(&vfd->entity, 1, &isp->vd_pad, 0);
	if (ret < 0)
		return ret;

	video_set_drvdata(vfd, isp);

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		media_entity_cleanup(&vfd->entity);
		return ret;
	}

	v4l2_info(v4l2_dev, "Registered %s as /dev/%s\n",
		  vfd->name, video_device_node_name(vfd));

	return 0;
}

void fimc_isp_video_device_unregister(struct fimc_isp *isp)
{
	if (isp && video_is_registered(&isp->vfd)) {
		video_unregister_device(&isp->vfd);
		media_entity_cleanup(&isp->vfd.entity);
	}
}
