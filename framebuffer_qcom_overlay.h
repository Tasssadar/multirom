/*
 *
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_MSM_ION_H
#define _LINUX_MSM_ION_H

#include <linux/ion.h>

#define ION_HEAP_TYPE_MSM_START (ION_HEAP_TYPE_CUSTOM + 1)
#define ION_HEAP_TYPE_IOMMU (ION_HEAP_TYPE_MSM_START)
#define ION_HEAP_TYPE_CP    (ION_HEAP_TYPE_IOMMU + 1)

/**
 * These are the only ids that should be used for Ion heap ids.
 * The ids listed are the order in which allocation will be attempted
 * if specified. Don't swap the order of heap ids unless you know what
 * you are doing!
 * Id's are spaced by purpose to allow new Id's to be inserted in-between (for
 * possible fallbacks)
 */

enum ion_heap_ids {
    INVALID_HEAP_ID = -1,
    ION_CP_MM_HEAP_ID = 8,
    ION_CP_MFC_HEAP_ID = 12,
    ION_CP_WB_HEAP_ID = 16, /* 8660 only */
    ION_CAMERA_HEAP_ID = 20, /* 8660 only */
    ION_ADSP_HEAP_ID = 22,
    ION_SF_HEAP_ID = 24,
    ION_IOMMU_HEAP_ID = 25,
    ION_QSECOM_HEAP_ID = 27,
    ION_AUDIO_HEAP_ID = 28,

    ION_MM_FIRMWARE_HEAP_ID = 29,
    ION_SYSTEM_HEAP_ID = 30,

    ION_HEAP_ID_RESERVED = 31 /** Bit reserved for ION_SECURE flag */
};

enum ion_fixed_position {
    NOT_FIXED,
    FIXED_LOW,
    FIXED_MIDDLE,
    FIXED_HIGH,
};

enum cp_mem_usage {
    VIDEO_BITSTREAM = 0x1,
    VIDEO_PIXEL = 0x2,
    VIDEO_NONPIXEL = 0x3,
    MAX_USAGE = 0x4,
    UNKNOWN = 0x7FFFFFFF,
};

#define ION_HEAP_CP_MASK        (1 << ION_HEAP_TYPE_CP)

/**
 * Flag to use when allocating to indicate that a heap is secure.
 */
#define ION_SECURE (1 << ION_HEAP_ID_RESERVED)

/**
 * Flag for clients to force contiguous memort allocation
 *
 * Use of this flag is carefully monitored!
 */
#define ION_FORCE_CONTIGUOUS (1 << 30)

/**
 * Macro should be used with ion_heap_ids defined above.
 */
#define ION_HEAP(bit) (1 << (bit))

#define ION_ADSP_HEAP_NAME  "adsp"
#define ION_VMALLOC_HEAP_NAME   "vmalloc"
#define ION_AUDIO_HEAP_NAME "audio"
#define ION_SF_HEAP_NAME    "sf"
#define ION_MM_HEAP_NAME    "mm"
#define ION_CAMERA_HEAP_NAME    "camera_preview"
#define ION_IOMMU_HEAP_NAME "iommu"
#define ION_MFC_HEAP_NAME   "mfc"
#define ION_WB_HEAP_NAME    "wb"
#define ION_MM_FIRMWARE_HEAP_NAME   "mm_fw"
#define ION_QSECOM_HEAP_NAME    "qsecom"
#define ION_FMEM_HEAP_NAME  "fmem"

#define ION_SET_CACHED(__cache)     (__cache | ION_FLAG_CACHED)
#define ION_SET_UNCACHED(__cache)   (__cache & ~ION_FLAG_CACHED)

#define ION_IS_CACHED(__flags)  ((__flags) & ION_FLAG_CACHED)

#ifdef __KERNEL__

/*
 * This flag allows clients when mapping into the IOMMU to specify to
 * defer un-mapping from the IOMMU until the buffer memory is freed.
 */
#define ION_IOMMU_UNMAP_DELAYED 1

/**
 * struct ion_cp_heap_pdata - defines a content protection heap in the given
 * platform
 * @permission_type:    Memory ID used to identify the memory to TZ
 * @align:      Alignment requirement for the memory
 * @secure_base:    Base address for securing the heap.
 *          Note: This might be different from actual base address
 *          of this heap in the case of a shared heap.
 * @secure_size:    Memory size for securing the heap.
 *          Note: This might be different from actual size
 *          of this heap in the case of a shared heap.
 * @reusable        Flag indicating whether this heap is reusable of not.
 *          (see FMEM)
 * @mem_is_fmem     Flag indicating whether this memory is coming from fmem
 *          or not.
 * @fixed_position  If nonzero, position in the fixed area.
 * @virt_addr:      Virtual address used when using fmem.
 * @iommu_map_all:  Indicates whether we should map whole heap into IOMMU.
 * @iommu_2x_map_domain: Indicates the domain to use for overmapping.
 * @request_region: function to be called when the number of allocations
 *          goes from 0 -> 1
 * @release_region: function to be called when the number of allocations
 *          goes from 1 -> 0
 * @setup_region:   function to be called upon ion registration
 * @memory_type:Memory type used for the heap
 * @no_nonsecure_alloc: don't allow non-secure allocations from this heap
 *
 */
struct ion_cp_heap_pdata {
    enum ion_permission_type permission_type;
    unsigned int align;
    ion_phys_addr_t secure_base; /* Base addr used when heap is shared */
    size_t secure_size; /* Size used for securing heap when heap is shared*/
    int reusable;
    int mem_is_fmem;
    int is_cma;
    enum ion_fixed_position fixed_position;
    int iommu_map_all;
    int iommu_2x_map_domain;
    ion_virt_addr_t *virt_addr;
    int (*request_region)(void *);
    int (*release_region)(void *);
    void *(*setup_region)(void);
    enum ion_memory_types memory_type;
    int no_nonsecure_alloc;
};

/**
 * struct ion_co_heap_pdata - defines a carveout heap in the given platform
 * @adjacent_mem_id:    Id of heap that this heap must be adjacent to.
 * @align:      Alignment requirement for the memory
 * @mem_is_fmem     Flag indicating whether this memory is coming from fmem
 *          or not.
 * @fixed_position  If nonzero, position in the fixed area.
 * @request_region: function to be called when the number of allocations
 *          goes from 0 -> 1
 * @release_region: function to be called when the number of allocations
 *          goes from 1 -> 0
 * @setup_region:   function to be called upon ion registration
 * @memory_type:Memory type used for the heap
 *
 */
struct ion_co_heap_pdata {
    int adjacent_mem_id;
    unsigned int align;
    int mem_is_fmem;
    enum ion_fixed_position fixed_position;
    int (*request_region)(void *);
    int (*release_region)(void *);
    void *(*setup_region)(void);
    enum ion_memory_types memory_type;
};

#ifdef CONFIG_ION
/**
 * msm_ion_secure_heap - secure a heap. Wrapper around ion_secure_heap.
 *
  * @heap_id - heap id to secure.
 *
 * Secure a heap
 * Returns 0 on success
 */
int msm_ion_secure_heap(int heap_id);

/**
 * msm_ion_unsecure_heap - unsecure a heap. Wrapper around ion_unsecure_heap.
 *
  * @heap_id - heap id to secure.
 *
 * Un-secure a heap
 * Returns 0 on success
 */
int msm_ion_unsecure_heap(int heap_id);

/**
 * msm_ion_secure_heap_2_0 - secure a heap using 2.0 APIs
 *  Wrapper around ion_secure_heap.
 *
 * @heap_id - heap id to secure.
 * @usage - usage hint to TZ
 *
 * Secure a heap
 * Returns 0 on success
 */
int msm_ion_secure_heap_2_0(int heap_id, enum cp_mem_usage usage);

/**
 * msm_ion_unsecure_heap - unsecure a heap secured with 3.0 APIs.
 * Wrapper around ion_unsecure_heap.
 *
 * @heap_id - heap id to secure.
 * @usage - usage hint to TZ
 *
 * Un-secure a heap
 * Returns 0 on success
 */
int msm_ion_unsecure_heap_2_0(int heap_id, enum cp_mem_usage usage);
#else
static inline int msm_ion_secure_heap(int heap_id)
{
    return -ENODEV;

}

static inline int msm_ion_unsecure_heap(int heap_id)
{
    return -ENODEV;
}

static inline int msm_ion_secure_heap_2_0(int heap_id, enum cp_mem_usage usage)
{
    return -ENODEV;
}

static inline int msm_ion_unsecure_heap_2_0(int heap_id,
                    enum cp_mem_usage usage)
{
    return -ENODEV;
}
#endif /* CONFIG_ION */

#endif /* __KERNEL */

/* struct ion_flush_data - data passed to ion for flushing caches
 *
 * @handle: handle with data to flush
 * @fd:     fd to flush
 * @vaddr:  userspace virtual address mapped with mmap
 * @offset: offset into the handle to flush
 * @length: length of handle to flush
 *
 * Performs cache operations on the handle. If p is the start address
 * of the handle, p + offset through p + offset + length will have
 * the cache operations performed
 */
struct ion_flush_data {
    struct ion_handle *handle;
    int fd;
    void *vaddr;
    unsigned int offset;
    unsigned int length;
};

/* struct ion_flag_data - information about flags for this buffer
 *
 * @handle: handle to get flags from
 * @flags:  flags of this handle
 *
 * Takes handle as an input and outputs the flags from the handle
 * in the flag field.
 */
struct ion_flag_data {
    struct ion_handle *handle;
    unsigned long flags;
};

#define ION_IOC_MSM_MAGIC 'M'

/**
 * DOC: ION_IOC_CLEAN_CACHES - clean the caches
 *
 * Clean the caches of the handle specified.
 */
#define ION_IOC_CLEAN_CACHES    _IOWR(ION_IOC_MSM_MAGIC, 0, \
                        struct ion_flush_data)
/**
 * DOC: ION_IOC_INV_CACHES - invalidate the caches
 *
 * Invalidate the caches of the handle specified.
 */
#define ION_IOC_INV_CACHES  _IOWR(ION_IOC_MSM_MAGIC, 1, \
                        struct ion_flush_data)
/**
 * DOC: ION_IOC_CLEAN_INV_CACHES - clean and invalidate the caches
 *
 * Clean and invalidate the caches of the handle specified.
 */
#define ION_IOC_CLEAN_INV_CACHES    _IOWR(ION_IOC_MSM_MAGIC, 2, \
                        struct ion_flush_data)

/**
 * DOC: ION_IOC_GET_FLAGS - get the flags of the handle
 *
 * Gets the flags of the current handle which indicate cachability,
 * secure state etc.
 */
#define ION_IOC_GET_FLAGS       _IOWR(ION_IOC_MSM_MAGIC, 3, \
                        struct ion_flag_data)

#endif


/* include/linux/msm_mdp.h
 *
 * Copyright (C) 2007 Google Incorporated
 * Copyright (c) 2012-2013 The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _MSM_MDP_H_
#define _MSM_MDP_H_

#include <linux/types.h>
#include <linux/fb.h>

#define MSMFB_IOCTL_MAGIC 'm'
#define MSMFB_GRP_DISP          _IOW(MSMFB_IOCTL_MAGIC, 1, unsigned int)
#define MSMFB_BLIT              _IOW(MSMFB_IOCTL_MAGIC, 2, unsigned int)
#define MSMFB_SUSPEND_SW_REFRESHER _IOW(MSMFB_IOCTL_MAGIC, 128, unsigned int)
#define MSMFB_RESUME_SW_REFRESHER _IOW(MSMFB_IOCTL_MAGIC, 129, unsigned int)
#define MSMFB_CURSOR _IOW(MSMFB_IOCTL_MAGIC, 130, struct fb_cursor)
#define MSMFB_SET_LUT _IOW(MSMFB_IOCTL_MAGIC, 131, struct fb_cmap)
#define MSMFB_HISTOGRAM _IOWR(MSMFB_IOCTL_MAGIC, 132, struct mdp_histogram_data)
/* new ioctls's for set/get ccs matrix */
#define MSMFB_GET_CCS_MATRIX  _IOWR(MSMFB_IOCTL_MAGIC, 133, struct mdp_ccs)
#define MSMFB_SET_CCS_MATRIX  _IOW(MSMFB_IOCTL_MAGIC, 134, struct mdp_ccs)
#define MSMFB_OVERLAY_SET       _IOWR(MSMFB_IOCTL_MAGIC, 135, \
                        struct mdp_overlay)
#define MSMFB_OVERLAY_UNSET     _IOW(MSMFB_IOCTL_MAGIC, 136, unsigned int)

#define MSMFB_OVERLAY_PLAY      _IOW(MSMFB_IOCTL_MAGIC, 137, \
                        struct msmfb_overlay_data)
#define MSMFB_OVERLAY_QUEUE MSMFB_OVERLAY_PLAY

#define MSMFB_GET_PAGE_PROTECTION _IOR(MSMFB_IOCTL_MAGIC, 138, \
                    struct mdp_page_protection)
#define MSMFB_SET_PAGE_PROTECTION _IOW(MSMFB_IOCTL_MAGIC, 139, \
                    struct mdp_page_protection)
#define MSMFB_OVERLAY_GET      _IOR(MSMFB_IOCTL_MAGIC, 140, \
                        struct mdp_overlay)
#define MSMFB_OVERLAY_PLAY_ENABLE     _IOW(MSMFB_IOCTL_MAGIC, 141, unsigned int)
#define MSMFB_OVERLAY_BLT       _IOWR(MSMFB_IOCTL_MAGIC, 142, \
                        struct msmfb_overlay_blt)
#define MSMFB_OVERLAY_BLT_OFFSET     _IOW(MSMFB_IOCTL_MAGIC, 143, unsigned int)
#define MSMFB_HISTOGRAM_START   _IOR(MSMFB_IOCTL_MAGIC, 144, \
                        struct mdp_histogram_start_req)
#define MSMFB_HISTOGRAM_STOP    _IOR(MSMFB_IOCTL_MAGIC, 145, unsigned int)
#define MSMFB_NOTIFY_UPDATE _IOW(MSMFB_IOCTL_MAGIC, 146, unsigned int)

#define MSMFB_OVERLAY_3D       _IOWR(MSMFB_IOCTL_MAGIC, 147, \
                        struct msmfb_overlay_3d)

#define MSMFB_MIXER_INFO       _IOWR(MSMFB_IOCTL_MAGIC, 148, \
                        struct msmfb_mixer_info_req)
#define MSMFB_OVERLAY_PLAY_WAIT _IOWR(MSMFB_IOCTL_MAGIC, 149, \
                        struct msmfb_overlay_data)
#define MSMFB_WRITEBACK_INIT _IO(MSMFB_IOCTL_MAGIC, 150)
#define MSMFB_WRITEBACK_START _IO(MSMFB_IOCTL_MAGIC, 151)
#define MSMFB_WRITEBACK_STOP _IO(MSMFB_IOCTL_MAGIC, 152)
#define MSMFB_WRITEBACK_QUEUE_BUFFER _IOW(MSMFB_IOCTL_MAGIC, 153, \
                        struct msmfb_data)
#define MSMFB_WRITEBACK_DEQUEUE_BUFFER _IOW(MSMFB_IOCTL_MAGIC, 154, \
                        struct msmfb_data)
#define MSMFB_WRITEBACK_TERMINATE _IO(MSMFB_IOCTL_MAGIC, 155)
#define MSMFB_MDP_PP _IOWR(MSMFB_IOCTL_MAGIC, 156, struct msmfb_mdp_pp)
#define MSMFB_OVERLAY_VSYNC_CTRL _IOW(MSMFB_IOCTL_MAGIC, 160, unsigned int)
#define MSMFB_VSYNC_CTRL  _IOW(MSMFB_IOCTL_MAGIC, 161, unsigned int)
#define MSMFB_BUFFER_SYNC  _IOW(MSMFB_IOCTL_MAGIC, 162, struct mdp_buf_sync)
#define MSMFB_DISPLAY_COMMIT      _IOW(MSMFB_IOCTL_MAGIC, 164, \
                        struct mdp_display_commit)
#define MSMFB_WRITEBACK_SET_MIRRORING_HINT _IOW(MSMFB_IOCTL_MAGIC, 165, \
                        unsigned int)
#define MSMFB_METADATA_GET  _IOW(MSMFB_IOCTL_MAGIC, 166, struct msmfb_metadata)

#define FB_TYPE_3D_PANEL 0x10101010
#define MDP_IMGTYPE2_START 0x10000
#define MSMFB_DRIVER_VERSION    0xF9E8D701

enum {
    NOTIFY_UPDATE_START,
    NOTIFY_UPDATE_STOP,
};

enum {
    MDP_RGB_565,      /* RGB 565 planer */
    MDP_XRGB_8888,    /* RGB 888 padded */
    MDP_Y_CBCR_H2V2,  /* Y and CbCr, pseudo planer w/ Cb is in MSB */
    MDP_Y_CBCR_H2V2_ADRENO,
    MDP_ARGB_8888,    /* ARGB 888 */
    MDP_RGB_888,      /* RGB 888 planer */
    MDP_Y_CRCB_H2V2,  /* Y and CrCb, pseudo planer w/ Cr is in MSB */

    /* This one is defined in CM (caf?), but not AOSP. This changes IDs of
     * all following types. Bloody hell.
     */
//    MDP_YCBYCR_H2V1,  /* YCbYCr interleave */

    MDP_YCRYCB_H2V1,  /* YCrYCb interleave */
    MDP_Y_CRCB_H2V1,  /* Y and CrCb, pseduo planer w/ Cr is in MSB */
    MDP_Y_CBCR_H2V1,   /* Y and CrCb, pseduo planer w/ Cr is in MSB */
    MDP_Y_CRCB_H1V2,
    MDP_Y_CBCR_H1V2,
    MDP_RGBA_8888,    /* ARGB 888 */
    MDP_BGRA_8888,    /* ABGR 888 */
    MDP_RGBX_8888,    /* RGBX 888 */
    MDP_Y_CRCB_H2V2_TILE,  /* Y and CrCb, pseudo planer tile */
    MDP_Y_CBCR_H2V2_TILE,  /* Y and CbCr, pseudo planer tile */
    MDP_Y_CR_CB_H2V2,  /* Y, Cr and Cb, planar */
    MDP_Y_CR_CB_GH2V2,  /* Y, Cr and Cb, planar aligned to Android YV12 */
    MDP_Y_CB_CR_H2V2,  /* Y, Cb and Cr, planar */
    MDP_Y_CRCB_H1V1,  /* Y and CrCb, pseduo planer w/ Cr is in MSB */
    MDP_Y_CBCR_H1V1,  /* Y and CbCr, pseduo planer w/ Cb is in MSB */
    MDP_YCRCB_H1V1,   /* YCrCb interleave */
    MDP_YCBCR_H1V1,   /* YCbCr interleave */
    MDP_BGR_565,      /* BGR 565 planer */
    MDP_IMGTYPE_LIMIT,
    MDP_RGB_BORDERFILL, /* border fill pipe */
    MDP_FB_FORMAT = MDP_IMGTYPE2_START,    /* framebuffer format */
    MDP_IMGTYPE_LIMIT2 /* Non valid image type after this enum */
};

enum {
    PMEM_IMG,
    FB_IMG,
};

enum {
    HSIC_HUE = 0,
    HSIC_SAT,
    HSIC_INT,
    HSIC_CON,
    NUM_HSIC_PARAM,
};

#define MDSS_MDP_ROT_ONLY       0x80
#define MDSS_MDP_RIGHT_MIXER        0x100

/* mdp_blit_req flag values */
#define MDP_ROT_NOP 0
#define MDP_FLIP_LR 0x1
#define MDP_FLIP_UD 0x2
#define MDP_ROT_90 0x4
#define MDP_ROT_180 (MDP_FLIP_UD|MDP_FLIP_LR)
#define MDP_ROT_270 (MDP_ROT_90|MDP_FLIP_UD|MDP_FLIP_LR)
#define MDP_DITHER 0x8
#define MDP_BLUR 0x10
#define MDP_BLEND_FG_PREMULT 0x20000
#define MDP_DEINTERLACE 0x80000000
#define MDP_SHARPENING  0x40000000
#define MDP_NO_DMA_BARRIER_START    0x20000000
#define MDP_NO_DMA_BARRIER_END      0x10000000
#define MDP_NO_BLIT         0x08000000
#define MDP_BLIT_WITH_DMA_BARRIERS  0x000
#define MDP_BLIT_WITH_NO_DMA_BARRIERS    \
    (MDP_NO_DMA_BARRIER_START | MDP_NO_DMA_BARRIER_END)
#define MDP_BLIT_SRC_GEM                0x04000000
#define MDP_BLIT_DST_GEM                0x02000000
#define MDP_BLIT_NON_CACHED     0x01000000
#define MDP_OV_PIPE_SHARE       0x00800000
#define MDP_DEINTERLACE_ODD     0x00400000
#define MDP_OV_PLAY_NOWAIT      0x00200000
#define MDP_SOURCE_ROTATED_90       0x00100000
#define MDP_OVERLAY_PP_CFG_EN       0x00080000
#define MDP_BACKEND_COMPOSITION     0x00040000
#define MDP_BORDERFILL_SUPPORTED    0x00010000
#define MDP_SECURE_OVERLAY_SESSION      0x00008000
#define MDP_MEMORY_ID_TYPE_FB       0x00001000

#define MDP_TRANSP_NOP 0xffffffff
#define MDP_ALPHA_NOP 0xff

#define MDP_FB_PAGE_PROTECTION_NONCACHED         (0)
#define MDP_FB_PAGE_PROTECTION_WRITECOMBINE      (1)
#define MDP_FB_PAGE_PROTECTION_WRITETHROUGHCACHE (2)
#define MDP_FB_PAGE_PROTECTION_WRITEBACKCACHE    (3)
#define MDP_FB_PAGE_PROTECTION_WRITEBACKWACACHE  (4)
/* Sentinel: Don't use! */
#define MDP_FB_PAGE_PROTECTION_INVALID           (5)
/* Count of the number of MDP_FB_PAGE_PROTECTION_... values. */
#define MDP_NUM_FB_PAGE_PROTECTION_VALUES        (5)

struct mdp_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct mdp_img {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t offset;
    int memory_id;      /* the file descriptor */
    uint32_t priv;
};

/*
 * {3x3} + {3} ccs matrix
 */

#define MDP_CCS_RGB2YUV     0
#define MDP_CCS_YUV2RGB     1

#define MDP_CCS_SIZE    9
#define MDP_BV_SIZE 3

struct mdp_ccs {
    int direction;          /* MDP_CCS_RGB2YUV or YUV2RGB */
    uint16_t ccs[MDP_CCS_SIZE]; /* 3x3 color coefficients */
    uint16_t bv[MDP_BV_SIZE];   /* 1x3 bias vector */
};

struct mdp_csc {
    int id;
    uint32_t csc_mv[9];
    uint32_t csc_pre_bv[3];
    uint32_t csc_post_bv[3];
    uint32_t csc_pre_lv[6];
    uint32_t csc_post_lv[6];
};

/* The version of the mdp_blit_req structure so that
 * user applications can selectively decide which functionality
 * to include
 */

#define MDP_BLIT_REQ_VERSION 2

struct mdp_blit_req {
    struct mdp_img src;
    struct mdp_img dst;
    struct mdp_rect src_rect;
    struct mdp_rect dst_rect;
    uint32_t alpha;
    uint32_t transp_mask;
    uint32_t flags;
    int sharpening_strength;  /* -127 <--> 127, default 64 */
};

struct mdp_blit_req_list {
    uint32_t count;
    struct mdp_blit_req req[];
};

#define MSMFB_DATA_VERSION 2

struct msmfb_data {
    uint32_t offset;
    int memory_id;
    int id;
    uint32_t flags;
    uint32_t priv;
    uint32_t iova;
};

#define MSMFB_NEW_REQUEST -1

struct msmfb_overlay_data {
    uint32_t id;
    struct msmfb_data data;
    uint32_t version_key;
    struct msmfb_data plane1_data;
    struct msmfb_data plane2_data;
    struct msmfb_data dst_data;
};

struct msmfb_img {
    uint32_t width;
    uint32_t height;
    uint32_t format;
};

#define MSMFB_WRITEBACK_DEQUEUE_BLOCKING 0x1
struct msmfb_writeback_data {
    struct msmfb_data buf_info;
    struct msmfb_img img;
};

#define MDP_PP_OPS_ENABLE 0x1
#define MDP_PP_OPS_READ 0x2
#define MDP_PP_OPS_WRITE 0x4
#define MDP_PP_OPS_DISABLE 0x8

struct mdp_qseed_cfg {
    uint32_t table_num;
    uint32_t ops;
    uint32_t len;
    uint32_t *data;
};

struct mdp_qseed_cfg_data {
    uint32_t block;
    struct mdp_qseed_cfg qseed_data;
};

struct mdp_sharp_cfg {
    uint32_t flags;
    uint32_t strength;
    uint32_t edge_thr;
    uint32_t smooth_thr;
    uint32_t noise_thr;
};

#define MDP_OVERLAY_PP_CSC_CFG      0x1
#define MDP_OVERLAY_PP_QSEED_CFG    0x2
#define MDP_OVERLAY_PP_PA_CFG    0x4
#define MDP_OVERLAY_PP_IGC_CFG    0x8
#define MDP_OVERLAY_PP_SHARP_CFG    0x10

#define MDP_CSC_FLAG_ENABLE 0x1
#define MDP_CSC_FLAG_YUV_IN 0x2
#define MDP_CSC_FLAG_YUV_OUT    0x4

struct mdp_csc_cfg {
    /* flags for enable CSC, toggling RGB,YUV input/output */
    uint32_t flags;
    uint32_t csc_mv[9];
    uint32_t csc_pre_bv[3];
    uint32_t csc_post_bv[3];
    uint32_t csc_pre_lv[6];
    uint32_t csc_post_lv[6];
};

struct mdp_csc_cfg_data {
    uint32_t block;
    struct mdp_csc_cfg csc_data;
};

struct mdp_pa_cfg {
    uint32_t flags;
    uint32_t hue_adj;
    uint32_t sat_adj;
    uint32_t val_adj;
    uint32_t cont_adj;
};

struct mdp_igc_lut_data {
    uint32_t block;
    uint32_t len, ops;
    uint32_t *c0_c1_data;
    uint32_t *c2_data;
};

struct mdp_overlay_pp_params {
    uint32_t config_ops;
    struct mdp_csc_cfg csc_cfg;
    struct mdp_qseed_cfg qseed_cfg[2];
    struct mdp_pa_cfg pa_cfg;
    struct mdp_igc_lut_data igc_cfg;
    struct mdp_sharp_cfg sharp_cfg;
};

enum {
    BLEND_OP_NOT_DEFINED = 0,
    BLEND_OP_OPAQUE,
    BLEND_OP_PREMULTIPLIED,
    BLEND_OP_COVERAGE,
    BLEND_OP_MAX,
};

struct mdp_overlay {
    struct msmfb_img src;
    struct mdp_rect src_rect;
    struct mdp_rect dst_rect;
    uint32_t z_order;   /* stage number */
    uint32_t is_fg;     /* control alpha & transp */
    uint32_t alpha;
    uint32_t transp_mask;
    uint32_t blend_op;
    uint32_t flags;
    uint32_t id;
    uint32_t user_data[8];
    struct mdp_overlay_pp_params overlay_pp_cfg;
};

struct msmfb_overlay_3d {
    uint32_t is_3d;
    uint32_t width;
    uint32_t height;
};


struct msmfb_overlay_blt {
    uint32_t enable;
    uint32_t offset;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

struct mdp_histogram {
    uint32_t frame_cnt;
    uint32_t bin_cnt;
    uint32_t *r;
    uint32_t *g;
    uint32_t *b;
};


/*

    mdp_block_type defines the identifiers for pipes in MDP 4.3 and up

    MDP_BLOCK_RESERVED is provided for backward compatibility and is
    deprecated. It corresponds to DMA_P. So MDP_BLOCK_DMA_P should be used
    instead.

    MDP_LOGICAL_BLOCK_DISP_0 identifies the display pipe which fb0 uses,
    same for others.

*/

enum {
    MDP_BLOCK_RESERVED = 0,
    MDP_BLOCK_OVERLAY_0,
    MDP_BLOCK_OVERLAY_1,
    MDP_BLOCK_VG_1,
    MDP_BLOCK_VG_2,
    MDP_BLOCK_RGB_1,
    MDP_BLOCK_RGB_2,
    MDP_BLOCK_DMA_P,
    MDP_BLOCK_DMA_S,
    MDP_BLOCK_DMA_E,
    MDP_BLOCK_OVERLAY_2,
    MDP_LOGICAL_BLOCK_DISP_0 = 0x1000,
    MDP_LOGICAL_BLOCK_DISP_1,
    MDP_LOGICAL_BLOCK_DISP_2,
    MDP_BLOCK_MAX,
};

/*
 * mdp_histogram_start_req is used to provide the parameters for
 * histogram start request
 */

struct mdp_histogram_start_req {
    uint32_t block;
    uint8_t frame_cnt;
    uint8_t bit_mask;
    uint8_t num_bins;
};

/*
 * mdp_histogram_data is used to return the histogram data, once
 * the histogram is done/stopped/cance
 */

struct mdp_histogram_data {
    uint32_t block;
    uint8_t bin_cnt;
    uint32_t *c0;
    uint32_t *c1;
    uint32_t *c2;
    uint32_t *extra_info;
};

struct mdp_pcc_coeff {
    uint32_t c, r, g, b, rr, gg, bb, rg, gb, rb, rgb_0, rgb_1;
};

struct mdp_pcc_cfg_data {
    uint32_t block;
    uint32_t ops;
    struct mdp_pcc_coeff r, g, b;
};

enum {
    mdp_lut_igc,
    mdp_lut_pgc,
    mdp_lut_hist,
    mdp_lut_max,
};

struct mdp_ar_gc_lut_data {
    uint32_t x_start;
    uint32_t slope;
    uint32_t offset;
};

struct mdp_pgc_lut_data {
    uint32_t block;
    uint32_t flags;
    uint8_t num_r_stages;
    uint8_t num_g_stages;
    uint8_t num_b_stages;
    struct mdp_ar_gc_lut_data *r_data;
    struct mdp_ar_gc_lut_data *g_data;
    struct mdp_ar_gc_lut_data *b_data;
};


struct mdp_hist_lut_data {
    uint32_t block;
    uint32_t ops;
    uint32_t len;
    uint32_t *data;
};

struct mdp_lut_cfg_data {
    uint32_t lut_type;
    union {
        struct mdp_igc_lut_data igc_lut_data;
        struct mdp_pgc_lut_data pgc_lut_data;
        struct mdp_hist_lut_data hist_lut_data;
    } data;
};

struct mdp_bl_scale_data {
    uint32_t min_lvl;
    uint32_t scale;
};

struct mdp_calib_config_data {
    uint32_t ops;
    uint32_t addr;
    uint32_t data;
};

struct mdp_pa_cfg_data {
    uint32_t block;
    struct mdp_pa_cfg pa_data;
};

enum {
    mdp_op_pcc_cfg,
    mdp_op_csc_cfg,
    mdp_op_lut_cfg,
    mdp_op_qseed_cfg,
    mdp_bl_scale_cfg,
    mdp_op_calib_cfg,
    mdp_op_pa_cfg,
    mdp_op_max,
};

struct msmfb_mdp_pp {
    uint32_t op;
    union {
        struct mdp_pcc_cfg_data pcc_cfg_data;
        struct mdp_csc_cfg_data csc_cfg_data;
        struct mdp_lut_cfg_data lut_cfg_data;
        struct mdp_qseed_cfg_data qseed_cfg_data;
        struct mdp_bl_scale_data bl_scale_data;
        struct mdp_calib_config_data calib_cfg;
        struct mdp_pa_cfg_data pa_cfg_data;
    } data;
};

enum {
    metadata_op_none,
    metadata_op_base_blend,
    metadata_op_frame_rate,
    metadata_op_max
};

struct mdp_blend_cfg {
    uint32_t is_premultiplied;
};

struct msmfb_metadata {
    uint32_t op;
    uint32_t flags;
    union {
        struct mdp_blend_cfg blend_cfg;
        uint32_t panel_frame_rate;
    } data;
};

#define MDP_MAX_FENCE_FD    10
#define MDP_BUF_SYNC_FLAG_WAIT  1

struct mdp_buf_sync {
    uint32_t flags;
    uint32_t acq_fen_fd_cnt;
    int *acq_fen_fd;
    int *rel_fen_fd;
    int *retire_fen_fd;
};

#define MDP_DISPLAY_COMMIT_OVERLAY 0x00000001

struct mdp_display_commit {
    uint32_t flags;
    uint32_t wait_for_finish;
    struct fb_var_screeninfo var;
};

struct mdp_page_protection {
    uint32_t page_protection;
};


struct mdp_mixer_info {
    int pndx;
    int pnum;
    int ptype;
    int mixer_num;
    int z_order;
};

#define MAX_PIPE_PER_MIXER  5

struct msmfb_mixer_info_req {
    int mixer_num;
    int cnt;
    struct mdp_mixer_info info[MAX_PIPE_PER_MIXER];
};

enum {
    DISPLAY_SUBSYSTEM_ID,
    ROTATOR_SUBSYSTEM_ID,
};

enum {
    MDP_WRITEBACK_MIRROR_OFF,
    MDP_WRITEBACK_MIRROR_ON,
    MDP_WRITEBACK_MIRROR_PAUSE,
    MDP_WRITEBACK_MIRROR_RESUME,
};

#ifdef __KERNEL__

/* get the framebuffer physical address information */
int get_fb_phys_info(unsigned long *start, unsigned long *len, int fb_num,
    int subsys_id);
struct fb_info *msm_fb_get_writeback_fb(void);
int msm_fb_writeback_init(struct fb_info *info);
int msm_fb_writeback_start(struct fb_info *info);
int msm_fb_writeback_queue_buffer(struct fb_info *info,
        struct msmfb_data *data);
int msm_fb_writeback_dequeue_buffer(struct fb_info *info,
        struct msmfb_data *data);
int msm_fb_writeback_stop(struct fb_info *info);
int msm_fb_writeback_terminate(struct fb_info *info);
#endif

#endif /*_MSM_MDP_H_*/
