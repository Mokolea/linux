/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2009-2025 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 *
 **************************************************************************/

#ifndef VMWGFX_KMS_H_
#define VMWGFX_KMS_H_

#include "vmwgfx_cursor_plane.h"
#include "vmwgfx_drv.h"

#include <drm/drm_encoder.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_probe_helper.h>

/**
 * struct vmw_du_update_plane - Closure structure for vmw_du_helper_plane_update
 * @plane: Plane which is being updated.
 * @old_state: Old state of plane.
 * @dev_priv: Device private.
 * @du: Display unit on which to update the plane.
 * @vfb: Framebuffer which is blitted to display unit.
 * @out_fence: Out fence for resource finish.
 * @mutex: The mutex used to protect resource reservation.
 * @cpu_blit: True if need cpu blit.
 * @intr: Whether to perform waits interruptible if possible.
 *
 * This structure loosely represent the set of operations needed to perform a
 * plane update on a display unit. Implementer will define that functionality
 * according to the function callbacks for this structure. In brief it involves
 * surface/buffer object validation, populate FIFO commands and command
 * submission to the device.
 */
struct vmw_du_update_plane {
	/**
	 * @calc_fifo_size: Calculate fifo size.
	 *
	 * Determine fifo size for the commands needed for update. The number of
	 * damage clips on display unit @num_hits will be passed to allocate
	 * sufficient fifo space.
	 *
	 * Return: Fifo size needed
	 */
	uint32_t (*calc_fifo_size)(struct vmw_du_update_plane *update,
				   uint32_t num_hits);

	/**
	 * @post_prepare: Populate fifo for resource preparation.
	 *
	 * Some surface resource or buffer object need some extra cmd submission
	 * like update GB image for proxy surface and define a GMRFB for screen
	 * object. That should be done here as this callback will be
	 * called after FIFO allocation with the address of command buufer.
	 *
	 * This callback is optional.
	 *
	 * Return: Size of commands populated to command buffer.
	 */
	uint32_t (*post_prepare)(struct vmw_du_update_plane *update, void *cmd);

	/**
	 * @pre_clip: Populate fifo before clip.
	 *
	 * This is where pre clip related command should be populated like
	 * surface copy/DMA, etc.
	 *
	 * This callback is optional.
	 *
	 * Return: Size of commands populated to command buffer.
	 */
	uint32_t (*pre_clip)(struct vmw_du_update_plane *update, void *cmd,
			     uint32_t num_hits);

	/**
	 * @clip: Populate fifo for clip.
	 *
	 * This is where to populate clips for surface copy/dma or blit commands
	 * if needed. This will be called times have damage in display unit,
	 * which is one if doing full update. @clip is the damage in destination
	 * coordinates which is crtc/DU and @src_x, @src_y is damage clip src in
	 * framebuffer coordinate.
	 *
	 * This callback is optional.
	 *
	 * Return: Size of commands populated to command buffer.
	 */
	uint32_t (*clip)(struct vmw_du_update_plane *update, void *cmd,
			 struct drm_rect *clip, uint32_t src_x, uint32_t src_y);

	/**
	 * @post_clip: Populate fifo after clip.
	 *
	 * This is where to populate display unit update commands or blit
	 * commands.
	 *
	 * Return: Size of commands populated to command buffer.
	 */
	uint32_t (*post_clip)(struct vmw_du_update_plane *update, void *cmd,
				    struct drm_rect *bb);

	struct drm_plane *plane;
	struct drm_plane_state *old_state;
	struct vmw_private *dev_priv;
	struct vmw_display_unit *du;
	struct vmw_framebuffer *vfb;
	struct vmw_fence_obj **out_fence;
	struct mutex *mutex;
	bool intr;
};

/**
 * struct vmw_du_update_plane_surface - closure structure for surface
 * @base: base closure structure.
 * @cmd_start: FIFO command start address (used by SOU only).
 */
struct vmw_du_update_plane_surface {
	struct vmw_du_update_plane base;
	/* This member is to handle special case SOU surface update */
	void *cmd_start;
};

/**
 * struct vmw_du_update_plane_buffer - Closure structure for buffer object
 * @base: Base closure structure.
 * @fb_left: x1 for fb damage bounding box.
 * @fb_top: y1 for fb damage bounding box.
 */
struct vmw_du_update_plane_buffer {
	struct vmw_du_update_plane base;
	int fb_left, fb_top;
};

/**
 * struct vmw_kms_dirty - closure structure for the vmw_kms_helper_dirty
 * function.
 *
 * @fifo_commit: Callback that is called once for each display unit after
 * all clip rects. This function must commit the fifo space reserved by the
 * helper. Set up by the caller.
 * @clip: Callback that is called for each cliprect on each display unit.
 * Set up by the caller.
 * @fifo_reserve_size: Fifo size that the helper should try to allocat for
 * each display unit. Set up by the caller.
 * @dev_priv: Pointer to the device private. Set up by the helper.
 * @unit: The current display unit. Set up by the helper before a call to @clip.
 * @cmd: The allocated fifo space. Set up by the helper before the first @clip
 * call.
 * @crtc: The crtc for which to build dirty commands.
 * @num_hits: Number of clip rect commands for this display unit.
 * Cleared by the helper before the first @clip call. Updated by the @clip
 * callback.
 * @fb_x: Clip rect left side in framebuffer coordinates.
 * @fb_y: Clip rect right side in framebuffer coordinates.
 * @unit_x1: Clip rect left side in crtc coordinates.
 * @unit_y1: Clip rect top side in crtc coordinates.
 * @unit_x2: Clip rect right side in crtc coordinates.
 * @unit_y2: Clip rect bottom side in crtc coordinates.
 *
 * The clip rect coordinates are updated by the helper for each @clip call.
 * Note that this may be derived from if more info needs to be passed between
 * helper caller and helper callbacks.
 */
struct vmw_kms_dirty {
	void (*fifo_commit)(struct vmw_kms_dirty *);
	void (*clip)(struct vmw_kms_dirty *);
	size_t fifo_reserve_size;
	struct vmw_private *dev_priv;
	struct vmw_display_unit *unit;
	void *cmd;
	struct drm_crtc *crtc;
	u32 num_hits;
	s32 fb_x;
	s32 fb_y;
	s32 unit_x1;
	s32 unit_y1;
	s32 unit_x2;
	s32 unit_y2;
};

#define vmw_framebuffer_to_vfb(x) \
	container_of(x, struct vmw_framebuffer, base)
#define vmw_framebuffer_to_vfbs(x) \
	container_of(x, struct vmw_framebuffer_surface, base.base)
#define vmw_framebuffer_to_vfbd(x) \
	container_of(x, struct vmw_framebuffer_bo, base.base)

/**
 * Base class for framebuffers
 *
 * @pin is called the when ever a crtc uses this framebuffer
 * @unpin is called
 */
struct vmw_framebuffer {
	struct drm_framebuffer base;
	bool bo;
};

struct vmw_framebuffer_surface {
	struct vmw_framebuffer base;
	struct vmw_user_object uo;
};

struct vmw_framebuffer_bo {
	struct vmw_framebuffer base;
	struct vmw_bo *buffer;
};


static const uint32_t __maybe_unused vmw_primary_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB1555,
};


#define vmw_crtc_state_to_vcs(x) container_of(x, struct vmw_crtc_state, base)
#define vmw_plane_state_to_vps(x) container_of(x, struct vmw_plane_state, base)
#define vmw_connector_state_to_vcs(x) \
		container_of(x, struct vmw_connector_state, base)

/**
 * Derived class for crtc state object
 *
 * @base DRM crtc object
 */
struct vmw_crtc_state {
	struct drm_crtc_state base;
};


/**
 * Derived class for plane state object
 *
 * @base DRM plane object
 * @surf Display surface for STDU
 * @bo display bo for SOU
 * @content_fb_type Used by STDU.
 * @bo_size Size of the bo, used by Screen Object Display Unit
 * @pinned pin count for STDU display surface
 */
struct vmw_plane_state {
	struct drm_plane_state base;
	struct vmw_user_object uo;

	int content_fb_type;
	unsigned long bo_size;

	int pinned;

	/* For CPU Blit */
	unsigned int cpp;

	struct vmw_cursor_plane_state cursor;
};


/**
 * Derived class for connector state object
 *
 * @base DRM connector object
 * @is_implicit connector property
 *
 */
struct vmw_connector_state {
	struct drm_connector_state base;

	/**
	 * @gui_x:
	 *
	 * vmwgfx connector property representing the x position of this display
	 * unit (connector is synonymous to display unit) in overall topology.
	 * This is what the device expect as xRoot while creating screen.
	 */
	int gui_x;

	/**
	 * @gui_y:
	 *
	 * vmwgfx connector property representing the y position of this display
	 * unit (connector is synonymous to display unit) in overall topology.
	 * This is what the device expect as yRoot while creating screen.
	 */
	int gui_y;
};


/**
 * Base class display unit.
 *
 * Since the SVGA hw doesn't have a concept of a crtc, encoder or connector
 * so the display unit is all of them at the same time. This is true for both
 * legacy multimon and screen objects.
 */
struct vmw_display_unit {
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_plane primary;
	struct vmw_cursor_plane cursor;

	unsigned unit;

	/*
	 * Prefered mode tracking.
	 */
	unsigned pref_width;
	unsigned pref_height;
	bool pref_active;

	/*
	 * Gui positioning
	 */
	int gui_x;
	int gui_y;
	bool is_implicit;
	int set_gui_x;
	int set_gui_y;

	struct {
		struct work_struct crc_generator_work;
		struct hrtimer timer;
		ktime_t period_ns;

		/* protects concurrent access to the vblank handler */
		atomic_t atomic_lock;
		/* protected by @atomic_lock */
		bool crc_enabled;
		struct vmw_surface *surface;

		/* protects concurrent access to the crc worker */
		spinlock_t crc_state_lock;
		/* protected by @crc_state_lock */
		bool crc_pending;
		u64 frame_start;
		u64 frame_end;
	} vkms;
};

#define vmw_crtc_to_du(x) \
	container_of(x, struct vmw_display_unit, crtc)
#define vmw_connector_to_du(x) \
	container_of(x, struct vmw_display_unit, connector)


/*
 * Shared display unit functions - vmwgfx_kms.c
 */
void vmw_du_init(struct vmw_display_unit *du);
void vmw_du_cleanup(struct vmw_display_unit *du);
int vmw_du_crtc_gamma_set(struct drm_crtc *crtc,
			   u16 *r, u16 *g, u16 *b,
			   uint32_t size,
			   struct drm_modeset_acquire_ctx *ctx);
int vmw_du_connector_set_property(struct drm_connector *connector,
				  struct drm_property *property,
				  uint64_t val);
int vmw_du_connector_atomic_set_property(struct drm_connector *connector,
					 struct drm_connector_state *state,
					 struct drm_property *property,
					 uint64_t val);
int
vmw_du_connector_atomic_get_property(struct drm_connector *connector,
				     const struct drm_connector_state *state,
				     struct drm_property *property,
				     uint64_t *val);
int vmw_du_connector_dpms(struct drm_connector *connector, int mode);
void vmw_du_connector_save(struct drm_connector *connector);
void vmw_du_connector_restore(struct drm_connector *connector);
enum drm_connector_status
vmw_du_connector_detect(struct drm_connector *connector, bool force);
int vmw_kms_helper_dirty(struct vmw_private *dev_priv,
			 struct vmw_framebuffer *framebuffer,
			 const struct drm_clip_rect *clips,
			 const struct drm_vmw_rect *vclips,
			 s32 dest_x, s32 dest_y,
			 int num_clips,
			 int increment,
			 struct vmw_kms_dirty *dirty);
enum drm_mode_status vmw_connector_mode_valid(struct drm_connector *connector,
					      const struct drm_display_mode *mode);
int vmw_connector_get_modes(struct drm_connector *connector);

void vmw_kms_helper_validation_finish(struct vmw_private *dev_priv,
				      struct drm_file *file_priv,
				      struct vmw_validation_context *ctx,
				      struct vmw_fence_obj **out_fence,
				      struct drm_vmw_fence_rep __user *
				      user_fence_rep);
int vmw_kms_readback(struct vmw_private *dev_priv,
		     struct drm_file *file_priv,
		     struct vmw_framebuffer *vfb,
		     struct drm_vmw_fence_rep __user *user_fence_rep,
		     struct drm_vmw_rect *vclips,
		     uint32_t num_clips);
struct vmw_framebuffer *
vmw_kms_new_framebuffer(struct vmw_private *dev_priv,
			struct vmw_user_object *uo,
			const struct drm_format_info *info,
			const struct drm_mode_fb_cmd2 *mode_cmd);
void vmw_guess_mode_timing(struct drm_display_mode *mode);
void vmw_kms_update_implicit_fb(struct vmw_private *dev_priv);
void vmw_kms_create_implicit_placement_property(struct vmw_private *dev_priv);

/* Universal Plane Helpers */
void vmw_du_primary_plane_destroy(struct drm_plane *plane);

/* Atomic Helpers */
int vmw_du_primary_plane_atomic_check(struct drm_plane *plane,
				      struct drm_atomic_state *state);
void vmw_du_plane_cleanup_fb(struct drm_plane *plane,
			     struct drm_plane_state *old_state);
void vmw_du_plane_reset(struct drm_plane *plane);
struct drm_plane_state *vmw_du_plane_duplicate_state(struct drm_plane *plane);
void vmw_du_plane_destroy_state(struct drm_plane *plane,
				struct drm_plane_state *state);
void vmw_du_plane_unpin_surf(struct vmw_plane_state *vps);

int vmw_du_crtc_atomic_check(struct drm_crtc *crtc,
			     struct drm_atomic_state *state);
void vmw_du_crtc_atomic_begin(struct drm_crtc *crtc,
			      struct drm_atomic_state *state);
void vmw_du_crtc_reset(struct drm_crtc *crtc);
struct drm_crtc_state *vmw_du_crtc_duplicate_state(struct drm_crtc *crtc);
void vmw_du_crtc_destroy_state(struct drm_crtc *crtc,
				struct drm_crtc_state *state);
void vmw_du_connector_reset(struct drm_connector *connector);
struct drm_connector_state *
vmw_du_connector_duplicate_state(struct drm_connector *connector);

void vmw_du_connector_destroy_state(struct drm_connector *connector,
				    struct drm_connector_state *state);

/*
 * Legacy display unit functions - vmwgfx_ldu.c
 */
int vmw_kms_ldu_init_display(struct vmw_private *dev_priv);
int vmw_kms_ldu_close_display(struct vmw_private *dev_priv);
int vmw_kms_update_proxy(struct vmw_resource *res,
			 const struct drm_clip_rect *clips,
			 unsigned num_clips,
			 int increment);

/*
 * Screen Objects display functions - vmwgfx_scrn.c
 */
int vmw_kms_sou_init_display(struct vmw_private *dev_priv);
int vmw_kms_sou_do_surface_dirty(struct vmw_private *dev_priv,
				 struct vmw_framebuffer *framebuffer,
				 struct drm_clip_rect *clips,
				 struct drm_vmw_rect *vclips,
				 struct vmw_resource *srf,
				 s32 dest_x,
				 s32 dest_y,
				 unsigned num_clips, int inc,
				 struct vmw_fence_obj **out_fence,
				 struct drm_crtc *crtc);
int vmw_kms_sou_do_bo_dirty(struct vmw_private *dev_priv,
			    struct vmw_framebuffer *framebuffer,
			    struct drm_clip_rect *clips,
			    struct drm_vmw_rect *vclips,
			    unsigned int num_clips, int increment,
			    bool interruptible,
			    struct vmw_fence_obj **out_fence,
			    struct drm_crtc *crtc);
int vmw_kms_sou_readback(struct vmw_private *dev_priv,
			 struct drm_file *file_priv,
			 struct vmw_framebuffer *vfb,
			 struct drm_vmw_fence_rep __user *user_fence_rep,
			 struct drm_vmw_rect *vclips,
			 uint32_t num_clips,
			 struct drm_crtc *crtc);

/*
 * Screen Target Display Unit functions - vmwgfx_stdu.c
 */
int vmw_kms_stdu_init_display(struct vmw_private *dev_priv);
int vmw_kms_stdu_surface_dirty(struct vmw_private *dev_priv,
			       struct vmw_framebuffer *framebuffer,
			       struct drm_clip_rect *clips,
			       struct drm_vmw_rect *vclips,
			       struct vmw_resource *srf,
			       s32 dest_x,
			       s32 dest_y,
			       unsigned num_clips, int inc,
			       struct vmw_fence_obj **out_fence,
			       struct drm_crtc *crtc);
int vmw_kms_stdu_readback(struct vmw_private *dev_priv,
			  struct drm_file *file_priv,
			  struct vmw_framebuffer *vfb,
			  struct drm_vmw_fence_rep __user *user_fence_rep,
			  struct drm_clip_rect *clips,
			  struct drm_vmw_rect *vclips,
			  uint32_t num_clips,
			  int increment,
			  struct drm_crtc *crtc);

int vmw_du_helper_plane_update(struct vmw_du_update_plane *update);

/**
 * vmw_du_translate_to_crtc - Translate a rect from framebuffer to crtc
 * @state: Plane state.
 * @r: Rectangle to translate.
 */
static inline void vmw_du_translate_to_crtc(struct drm_plane_state *state,
					    struct drm_rect *r)
{
	int translate_crtc_x = -((state->src_x >> 16) - state->crtc_x);
	int translate_crtc_y = -((state->src_y >> 16) - state->crtc_y);

	drm_rect_translate(r, translate_crtc_x, translate_crtc_y);
}

#endif
