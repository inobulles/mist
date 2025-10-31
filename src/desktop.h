#pragma once

#include "env.h"
#include "win.h"

#include <jni.h>

#include <EGL/egl.h>
#include <glad/gles2.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

typedef struct {
	XrSwapchain swapchain;

	uint32_t x_res;
	uint32_t y_res;

	size_t image_count;
	GLuint* fbos;
} swapchain_t;

typedef struct {
	XrSession sesh;
	mist_env_t* env;

	size_t win_count;
	win_t* wins;

	// TODO A swapchain for each view. Actually make this a list because we could have 1 or 2.
	// TODO Do we need a depth swapchain even?

	size_t view_count;
	swapchain_t* swapchains;

	GLuint win_shader;
	GLuint win_model_uniform;
	GLuint win_view_uniform;
	GLuint win_proj_uniform;
	GLuint win_camera_pos_uniform;
	GLuint win_env_sampler_uniform;
} desktop_t;

#if defined(__cplusplus)
extern "C" {
#endif

int desktop_create(desktop_t* d, XrSession sesh, size_t view_count, XrViewConfigurationView* views, mist_env_t* env);
void desktop_destroy(desktop_t* d);

int desktop_render(
	desktop_t* d,
	XrSpace space,
	XrViewConfigurationType view_config,
	XrTime predicted_display_time,
	XrCompositionLayerProjection* layer,
	XrCompositionLayerProjectionView** layer_views
);

#if defined(__cplusplus)
}
#endif
