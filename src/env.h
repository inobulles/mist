#pragma once

#include <jni.h>

#include <EGL/egl.h>
#include <glad/gles2.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android_native_app_glue.h>

typedef struct {
	uint32_t equirect_x_res;
	uint32_t equirect_y_res;

	uint32_t blur_equirect_x_res;
	uint32_t blur_equirect_y_res;

	GLuint equirect_tex;
	GLuint blur_equirect_tex;

	XrSwapchain equirect_swapchain;
} mist_env_t;

#if defined(__cplusplus)
extern "C" {
#endif

int mist_env_create(mist_env_t* env, XrSession session, AAssetManager* mgr, char const* name);
int mist_env_render(mist_env_t* env, XrSpace space, XrCompositionLayerEquirect2KHR* layer);

#if defined(__cplusplus)
}
#endif
