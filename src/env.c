#include "env.h"

#include "log.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int mist_env_render(mist_env_t* env, XrSpace space, XrCompositionLayerEquirect2KHR* layer) {
	// Configure composition layer.
	// TODO Can this just be done on init?

	layer->next = NULL;
	layer->subImage.imageArrayIndex = 0;
	layer->type = XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR;
	layer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;

	layer->space = space;
	layer->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	layer->radius = 0;
	layer->centralHorizontalAngle = 2 * M_PI;
	layer->upperVerticalAngle = M_PI_2;
	layer->lowerVerticalAngle = -M_PI_2;

	layer->subImage.swapchain = env->equirect_swapchain;

	// clang-format off
	layer->pose = (XrPosef) {{0, 0, 0, 1}, {0, 0, 0}};

	layer->subImage.imageRect = (XrRect2Di) {
		.offset = {0, 0},
		.extent = {(int32_t) env->equirect_x_res, (int32_t) env->equirect_y_res},
	};
	// clang-format on

	// Acquire swapchain.

	uint32_t image_index = 0;
	XrSwapchainImageAcquireInfo acquire_info = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};

	if (xrAcquireSwapchainImage(env->equirect_swapchain, &acquire_info, &image_index) != XR_SUCCESS) {
		LOGE("Failed to acquire swapchain.");
		return -1;
	}

	XrSwapchainImageWaitInfo wait_info = {
		.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
		.timeout = XR_INFINITE_DURATION,
	};

	int rv = 0;

	if (xrWaitSwapchainImage(env->equirect_swapchain, &wait_info) != XR_SUCCESS) {
		LOGE("Failed to wait for swapchain.");
		rv = -1;
	}

	XrSwapchainImageReleaseInfo release_info = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

	if (xrReleaseSwapchainImage(env->equirect_swapchain, &release_info) != XR_SUCCESS) {
		LOGE("Failed to release swapchain.");
	}

	return rv;
}

int mist_env_create(mist_env_t* env, XrSession session, AAssetManager* mgr, char const* name) {
	int rv = -1;

	// Open asset for equirectangular map.

	char asset_path[256];
	snprintf(asset_path, sizeof asset_path, "envs/%s/equirectangle.png", name);
	AAsset* const asset = AAssetManager_open(mgr, asset_path, AASSET_MODE_STREAMING);

	if (asset == NULL) {
		LOGE("Could not load asset %s.", asset_path);
		goto err_load_asset;
	}

	// Read asset contents.

	off_t const asset_size = AAsset_getLength(asset);
	void* const asset_buf = malloc(asset_size);
	assert(asset_buf != NULL);

	if (AAsset_read(asset, asset_buf, asset_size) < 0) {
		LOGE("Could not read %s.", name);
		goto err_read_asset;
	}

	// Decode image from memory.
	// TODO Explicit casts necessary in C? clangd is annoying me because it thinks this is C++.

	stbi_set_flip_vertically_on_load(true);

	int channels;
	void* const buf = stbi_load_from_memory((stbi_uc const*) asset_buf, (int) asset_size, (int*) &env->equirect_x_res, (int*) &env->equirect_y_res, &channels, STBI_rgb_alpha);

	if (buf == NULL) {
		LOGE("Failed to decode image %s.", name);
		goto err_decode;
	}

	// Create OpenGL texture.
	// TODO Are mipmaps necessary in this case? At what stage is the mipmap even chosen? My guess is that nothing will happen as when we render to a framebuffer it has no knowledge of how far away the pixels are being rendered by the OpenXR compositor.
	// TODO Don't know if I have to keep this around or if I can just free this.

	glGenTextures(1, &env->equirect_tex);
	glBindTexture(GL_TEXTURE_2D, env->equirect_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, env->equirect_y_res, env->equirect_y_res, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	glGenerateMipmap(GL_TEXTURE_2D);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// Create swapchain.

	XrSwapchainCreateInfo const swapchain_create = {
		.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
		.createFlags = 0,
		.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
		.format = GL_RGBA8,
		.sampleCount = 1,
		.width = env->equirect_x_res,
		.height = env->equirect_y_res,
		.faceCount = 1,
		.arraySize = 1,
		.mipCount = 1,
	};

	if (xrCreateSwapchain(session, &swapchain_create, &env->equirect_swapchain) != XR_SUCCESS) {
		LOGE("Failed to create swapchain for %s.", name);
		goto err_create_swapchain;
	}

	// Get swapchain images.

	if (xrEnumerateSwapchainImages(env->equirect_swapchain, 0, &env->equirect_swapchain_image_count, NULL) != XR_SUCCESS) {
		LOGE("Failed to enumerate swapchain images for %s.", name);
		goto err_enum_images;
	}

	XrSwapchainImageOpenGLESKHR* const images = calloc(env->equirect_swapchain_image_count, sizeof *images);
	assert(images != NULL);

	for (size_t i = 0; i < env->equirect_swapchain_image_count; i++) {
		images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
	}

	if (xrEnumerateSwapchainImages(env->equirect_swapchain, env->equirect_swapchain_image_count, &env->equirect_swapchain_image_count, (XrSwapchainImageBaseHeader*) images) != XR_SUCCESS) {
		LOGE("Failed to enumerate swapchain images for %s.", name);
		goto err_enum_images2;
	}

	// Create framebuffers for each swapchain image.

	env->equirect_fbos = malloc(env->equirect_swapchain_image_count * sizeof *env->equirect_fbos);
	assert(env->equirect_fbos != NULL);
	glGenFramebuffers(env->equirect_swapchain_image_count, env->equirect_fbos);

	for (size_t i = 0; i < env->equirect_swapchain_image_count; i++) {
		// glBindFramebuffer(GL_FRAMEBUFFER, env->equirect_fbos[i]);
		// glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint) images[i].image, 0);

		// if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		// 	LOGE("Framebuffer not complete!");
		// }

		// Also render to each image FBO already, because this will not change much anyway.

		glClearColor(0.0, 1.0, 1.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		glBindTexture(GL_TEXTURE_2D, (GLuint) images[i].image);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, env->equirect_x_res, env->equirect_y_res, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	}

	// Cleanup.

	rv = 0;

	if (rv != 0) {
		glDeleteFramebuffers(env->equirect_swapchain_image_count, env->equirect_fbos);
	}

err_enum_images2:

	free(images);

err_enum_images:

	if (rv != 0) {
		xrDestroySwapchain(env->equirect_swapchain);
	}

err_create_swapchain:

	if (rv != 0) {
		glDeleteTextures(1, &env->equirect_tex);
	}

	free(buf);

err_decode:
err_read_asset:

	free(asset_buf);
	AAsset_close(asset);

err_load_asset:

	return rv;
}
