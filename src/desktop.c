#include "desktop.h"
#include "log.h"
#include "matrix.h"
#include "shader.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

// TODO I think a window could have 

#define MULTILINE(...) #__VA_ARGS__
#pragma clang diagnostic ignored "-Wunknown-escape-sequence"

// clang-format off
static char const* const WIN_SHADER_VERT_SRC = MULTILINE(
\#version 310 es\n
precision highp float;

layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 tex_coord;
// layout(location = 2) in vec3 normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec3 world_pos;
out vec3 world_normal;

void main() {
	vec3 normal = vec3(0.0, 0.0, 1.0);

	vec4 world_pos_4 = model * vec4(pos, 1.0);
	world_pos = world_pos_4.xyz;
	world_normal = mat3(model) * normal;

	gl_Position = proj * view * world_pos_4;
}
);

static char const* const WIN_SHADER_FRAG_SRC = MULTILINE(
\#version 310 es\n
precision highp float;

in vec3 world_pos;
in vec3 world_normal;

uniform sampler2D env;
uniform vec3 camera_pos;

out vec4 frag_colour;

const float PI = 3.14159265359;

vec2 dir_to_equirect(vec3 dir) {
	float lon = atan(dir.z, dir.x) + PI / 2.0;
	float lat = asin(clamp(dir.y, -1.0, 1.0));
	return vec2(lon / (2.0 * PI) + 0.5, lat / PI + 0.5);
}

void main() {
	vec3 N = normalize(world_normal);
	vec3 V = normalize(world_pos - camera_pos);

	/* TODO Fresnel? */

	vec3 R = refract(V, N, 1.0 / 1.757 /* sapphire */);

	vec2 uv = dir_to_equirect(normalize(R));
	vec3 colour = texture(env, uv).rgb;

	frag_colour = vec4(colour, 1.0);
}
);
// clang-format on

int desktop_create(desktop_t* d, XrSession sesh, size_t view_count, XrViewConfigurationView* views, mist_env_t* env) {
	// TODO Maybe the desktop should be responsible for the environment too?

	d->sesh = sesh;
	d->env = env;
	d->win_count = 0;
	d->wins = NULL;
	d->view_count = view_count;

	// Create swapchains.

	d->swapchains = calloc(view_count, sizeof *d->swapchains);
	assert(d->swapchains != NULL);

	for (size_t i = 0; i < view_count; i++) {
		XrViewConfigurationView* const view = &views[i];
		swapchain_t* const swapchain = &d->swapchains[i];

		swapchain->x_res = view->recommendedImageRectWidth;
		swapchain->y_res = view->recommendedImageRectHeight;

		XrSwapchainCreateInfo const create_info = {
			.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
			.createFlags = 0,
			.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
			.format = GL_RGBA8, // TODO
			.sampleCount = view->recommendedSwapchainSampleCount,
			.width = swapchain->x_res,
			.height = swapchain->y_res,
			.faceCount = 1,
			.arraySize = 1,
			.mipCount = 1,
		};

		if (xrCreateSwapchain(sesh, &create_info, &swapchain->swapchain) != XR_SUCCESS) {
			LOGE("Failed to create swapchain.");
			goto err;
		}

		// Get images and give them all FBOs.

		uint32_t image_count = 0;

		if (xrEnumerateSwapchainImages(swapchain->swapchain, 0, &image_count, NULL) != XR_SUCCESS) {
			LOGE("Failed to enumerate swapchain images.");
			goto err;
		}

		XrSwapchainImageOpenGLESKHR* const images = calloc(image_count, sizeof *images);
		assert(images != NULL);

		for (size_t j = 0; j < image_count; j++) {
			images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
		}

		if (
			xrEnumerateSwapchainImages(
				swapchain->swapchain,
				image_count,
				&image_count,
				(XrSwapchainImageBaseHeader*) images
			) != XR_SUCCESS
		) {
			LOGE("Failed to enumerate swapchain images.");
			free(images);
			goto err;
		}

		swapchain->image_count = image_count;
		swapchain->fbos = calloc(image_count, sizeof *swapchain->fbos);
		assert(swapchain->fbos != NULL);

		glGenFramebuffers(image_count, swapchain->fbos);

		for (size_t j = 0; j < image_count; j++) {
			// XXX When we move on to using multiview, we should use glFramebufferTextureMultiviewOVR here.

			glBindFramebuffer(GL_FRAMEBUFFER, swapchain->fbos[j]);
			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint) images[j].image, 0);

			if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
				LOGE("Failed to complete framebuffers.");
				free(images);
				goto err;
			}
		}

		free(images);
	}

	// Create shader.

	d->win_shader = create_shader(WIN_SHADER_VERT_SRC, WIN_SHADER_FRAG_SRC);

	if (d->win_shader == 0) {
		LOGE("Failed to create window shader.");
		goto err;
	}

	d->win_model_uniform = glGetUniformLocation(d->win_shader, "model");
	d->win_view_uniform = glGetUniformLocation(d->win_shader, "view");
	d->win_proj_uniform = glGetUniformLocation(d->win_shader, "proj");

	d->win_camera_pos_uniform = glGetUniformLocation(d->win_shader, "camera_pos");
	d->win_env_sampler_uniform = glGetUniformLocation(d->win_shader, "env");

	// TODO Create dummy window.

	d->win_count = 1;
	d->wins = calloc(1, sizeof *d->wins);
	assert(d->wins != NULL);

	win_t* const win = &d->wins[0];
	win_create(win);

	return 0;

err:

	desktop_destroy(d);
	return -1;
}

void desktop_destroy(desktop_t* d) {
	// Destroy shader.

	if (d->win_shader == 0) {
		glDeleteShader(d->win_shader);
	}

	// Destroy swapchains.

	for (size_t i = 0; i < d->view_count; i++) {
		swapchain_t* const swapchain = &d->swapchains[i];

		if (swapchain->swapchain == NULL) {
			continue;
		}

		glDeleteFramebuffers(swapchain->image_count, swapchain->fbos);
		xrDestroySwapchain(d->swapchains[i].swapchain);
	}

	free(d->swapchains);

	// Destroy windows.

	for (size_t i = 0; i < d->win_count; i++) {
		win_destroy(&d->wins[i]);
	}

	free(d->wins);
}

int desktop_render(
	desktop_t* d,
	XrSpace space,
	XrViewConfigurationType view_config,
	XrTime predicted_display_time,
	XrCompositionLayerProjection* layer,
	XrCompositionLayerProjectionView** layer_views
) {
	// Get view information.

	XrViewState view_state = {XR_TYPE_VIEW_STATE};

	XrViewLocateInfo view_locate_info = {
		.type = XR_TYPE_VIEW_LOCATE_INFO,
		.viewConfigurationType = view_config,
		.displayTime = predicted_display_time,
		.space = space,
	};

	XrView* const views = calloc(d->view_count, sizeof *views);
	assert(views != NULL);

	for (size_t i = 0; i < d->view_count; i++) {
		views[i].type = XR_TYPE_VIEW;
	}

	uint32_t view_count;

	if (xrLocateViews(d->sesh, &view_locate_info, &view_state, d->view_count, &view_count, views) != XR_SUCCESS) {
		LOGE("Failed to locate views.");
		free(views);
		return -1;
	}

	*layer_views = calloc(d->view_count, sizeof **layer_views);
	assert(*layer_views != NULL);

	// Render for each view.

	for (size_t i = 0; i < d->view_count; i++) {
		swapchain_t* const swapchain = &d->swapchains[i];
		XrView* const view = &views[i];

		// Acquire swapchain image.

		uint32_t img_i = 0;
		XrSwapchainImageAcquireInfo acquire_info = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};

		if (xrAcquireSwapchainImage(swapchain->swapchain, &acquire_info, &img_i) != XR_SUCCESS) {
			LOGE("Failed to acquire swapchain.");
			continue; // TODO I guess we fail all rendering in this situation?
		}

		XrSwapchainImageWaitInfo wait_info = {
			.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
			.timeout = XR_INFINITE_DURATION,
		};

		if (xrWaitSwapchainImage(swapchain->swapchain, &wait_info) != XR_SUCCESS) {
			LOGE("Failed to wait for swapchain.");
			goto release; // TODO Ditto as above.
		}

		// Set up matrices.

		matrix_t proj_matrix;
		matrix_identity(proj_matrix);
		matrix_perspective(proj_matrix, view->fov, 0.1, 500);

		matrix_t model_matrix;
		matrix_identity(model_matrix);

		matrix_translate(model_matrix, (float[3]) {0, 0, -2});

		matrix_t view_matrix;
		matrix_identity(view_matrix);

		matrix_rotate_quat(view_matrix, (float*) &view->pose.orientation);

		matrix_translate(view_matrix, (float[3]) {
													-view->pose.position.x,
													-view->pose.position.y,
													-view->pose.position.z,
												});

		// Actually render.

		glBindFramebuffer(GL_FRAMEBUFFER, swapchain->fbos[img_i]);

		glUseProgram(d->win_shader); // TODO Does this only apply to bound framebuffer? Or global state? Test this out.

		glUniformMatrix4fv(d->win_model_uniform, 1, false, (void*) &model_matrix);
		glUniformMatrix4fv(d->win_view_uniform, 1, false, (void*) &view_matrix);
		glUniformMatrix4fv(d->win_proj_uniform, 1, false, (void*) &proj_matrix);
		glUniform3fv(d->win_camera_pos_uniform, 1, (float*) &view->pose.position);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, d->env->equirect_tex);
		glUniform1i(d->win_env_sampler_uniform, 0);

		glViewport(0, 0, swapchain->x_res, swapchain->y_res);

		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);

		for (size_t j = 0; j < d->win_count; j++) {
			win_render(&d->wins[j]);
		}

		// Populate relevant layer view.

		XrCompositionLayerProjectionView* const layer_view = &(*layer_views)[i];

		layer_view->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		layer_view->next = NULL;

		layer_view->fov = view->fov;
		layer_view->pose = view->pose;

		layer_view->subImage.swapchain = swapchain->swapchain;
		layer_view->subImage.imageArrayIndex = 0;

		layer_view->subImage.imageRect = (XrRect2Di) {
			.offset = {               0,                0},
			.extent = {swapchain->x_res, swapchain->y_res},
		};

release:;

		XrSwapchainImageReleaseInfo release_info = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

		if (xrReleaseSwapchainImage(swapchain->swapchain, &release_info) != XR_SUCCESS) {
			LOGE("Failed to release swapchain.");
			// TODO Probably should cancel all rendering here.
		}
	}

	free(views);

	// Fill in composition layer.

	layer->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
	layer->next = NULL;

	layer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
	layer->space = space;

	layer->viewCount = d->view_count;
	layer->views = *layer_views;

	return 0;
}
