#include <jni.h>
#include <string.h>
#include <cassert>

#include <EGL/egl.h>
#include <GLES/gl.h>

#include <android/log.h>
#include <android_native_app_glue.h>
#include <vector>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "native-activity", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "native-activity", __VA_ARGS__))

/**
 * Our saved state data.
 */
struct saved_state {
	float angle;
	int32_t x;
	int32_t y;
};

/**
 * Shared state for our app.
 */
struct engine {
	struct android_app* app;

	int animating;
	EGLDisplay display;
	EGLSurface surface;
	EGLContext context;
	int32_t width;
	int32_t height;
	struct saved_state state;
};

/**
 * Initialize an EGL context for the current display.
 */
static int engine_init_display(struct engine* engine) {
	// initialize OpenGL ES and EGL

	/*
	 * Here specify the attributes of the desired configuration.
	 * Below, we select an EGLConfig with at least 8 bits per color
	 * component compatible with on-screen windows
	 */
	const EGLint attrs[] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_BLUE_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_RED_SIZE, 8,
			EGL_NONE
	};
	EGLint w, h, format;
	EGLint numConfigs;
	EGLConfig config;
	EGLSurface surface;
	EGLContext context;

	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	eglInitialize(display, 0, 0);

	/* Here, the application chooses the configuration it desires.
	 * find the best match if possible, otherwise use the very first one
	 */
	eglChooseConfig(display, attrs, nullptr,0, &numConfigs);
	auto supportedConfigs = new EGLConfig[numConfigs];
	assert(supportedConfigs);
	eglChooseConfig(display, attrs, supportedConfigs, numConfigs, &numConfigs);
	assert(numConfigs);
	auto i = 0;
	for (; i < numConfigs; i++) {
		auto& cfg = supportedConfigs[i];
		EGLint r, g, b, d;
		if (eglGetConfigAttrib(display, cfg, EGL_RED_SIZE, &r)   &&
			eglGetConfigAttrib(display, cfg, EGL_GREEN_SIZE, &g) &&
			eglGetConfigAttrib(display, cfg, EGL_BLUE_SIZE, &b)  &&
			eglGetConfigAttrib(display, cfg, EGL_DEPTH_SIZE, &d) &&
			r == 8 && g == 8 && b == 8 && d == 0 ) {

			config = supportedConfigs[i];
			break;
		}
	}
	if (i == numConfigs) {
		config = supportedConfigs[0];
	}

	/* EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
	 * guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
	 * As soon as we picked a EGLConfig, we can safely reconfigure the
	 * ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID. */
	eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
	surface = eglCreateWindowSurface(display, config, engine->app->window, NULL);
	context = eglCreateContext(display, config, NULL, NULL);

	if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
		LOGW("Unable to eglMakeCurrent");
		return -1;
	}

	eglQuerySurface(display, surface, EGL_WIDTH, &w);
	eglQuerySurface(display, surface, EGL_HEIGHT, &h);

	engine->display = display;
	engine->context = context;
	engine->surface = surface;
	engine->width = w;
	engine->height = h;
	engine->state.angle = 0;

	// Check openGL on the system
	auto opengl_info = {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS};
	for (auto name : opengl_info) {
		auto info = glGetString(name);
		LOGI("OpenGL Info: %s", info);
	}
	// Initialize GL state.
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	glEnable(GL_CULL_FACE);
	glShadeModel(GL_SMOOTH);
	glDisable(GL_DEPTH_TEST);

	return 0;
}

/**
 * Just the current frame in the display.
 */
static void engine_draw_frame(struct engine* engine) {
	if (engine->display == NULL) {
		// No display.
		return;
	}

	// Just fill the screen with a color.
	glClearColor(((float)engine->state.x)/engine->width, engine->state.angle,
				 ((float)engine->state.y)/engine->height, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(engine->display, engine->surface);
}

/**
 * Tear down the EGL context currently associated with the display.
 */
static void engine_term_display(struct engine* engine) {
	if (engine->display != EGL_NO_DISPLAY) {
		eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (engine->context != EGL_NO_CONTEXT) {
			eglDestroyContext(engine->display, engine->context);
		}
		if (engine->surface != EGL_NO_SURFACE) {
			eglDestroySurface(engine->display, engine->surface);
		}
		eglTerminate(engine->display);
	}
	engine->animating = 0;
	engine->display = EGL_NO_DISPLAY;
	engine->context = EGL_NO_CONTEXT;
	engine->surface = EGL_NO_SURFACE;
}

/**
 * Process the next input event.
 */
static int32_t engine_handle_input(struct android_app* app, AInputEvent* event) {
	struct engine* engine = (struct engine*)app->userData;
	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
		engine->animating = 1;
		engine->state.x = AMotionEvent_getX(event, 0);
		engine->state.y = AMotionEvent_getY(event, 0);
		return 1;
	}
	return 0;
}

/**
 * Process the next main command.
 */
static void engine_handle_cmd(struct android_app* app, int32_t cmd) {
	struct engine* engine = (struct engine*)app->userData;
	switch (cmd) {
		case APP_CMD_SAVE_STATE:
			// The system has asked us to save our current state.  Do so.
			engine->app->savedState = malloc(sizeof(struct saved_state));
			*((struct saved_state*)engine->app->savedState) = engine->state;
			engine->app->savedStateSize = sizeof(struct saved_state);
			break;
		case APP_CMD_INIT_WINDOW:
			// The window is being shown, get it ready.
			if (engine->app->window != NULL) {
				engine_init_display(engine);
				engine_draw_frame(engine);
			}
			break;
		case APP_CMD_TERM_WINDOW:
			// The window is being hidden or closed, clean it up.
			engine_term_display(engine);
			break;
		case APP_CMD_GAINED_FOCUS:
			break;
		case APP_CMD_LOST_FOCUS:
			// Also stop animating.
			engine->animating = 0;
			engine_draw_frame(engine);
			break;
	}
}

/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 */
void android_main(struct android_app* app) {
	// TODO Clean up the following code.

	struct engine engine;

	memset(&engine, 0, sizeof(engine));
	app->userData = &engine;
	// app->onAppCmd = engine_handle_cmd;
	app->onInputEvent = engine_handle_input;
	engine.app = app;

	// Initialize OpenXR loader.

	PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;

	if (
		xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*) &xrInitializeLoaderKHR) != XR_SUCCESS ||
		xrInitializeLoaderKHR == nullptr
	) {
		LOGE("Getting xrInitializeLoaderKHR failed.");
		return;
	}

	XrLoaderInitInfoAndroidKHR const loader_init_info = {
		.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
		.applicationVM = app->activity->vm,
		.applicationContext = app->activity->clazz,
	};

	if (xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*) &loader_init_info) != XR_SUCCESS) {
		LOGE("xrInitializeLoaderKHR failed.");
		return;
	}

	// Enumerate API layers runtime has made available.
	// No need to check any as we're not using anything special at the moment.

	uint32_t api_layer_count;

	if (xrEnumerateApiLayerProperties(0, &api_layer_count, nullptr) != XR_SUCCESS) {
		LOGE("Failed to enumerate API layers.");
		return;
	}

	auto api_layers = std::vector<XrApiLayerProperties>(api_layer_count, {XR_TYPE_API_LAYER_PROPERTIES});

	if (xrEnumerateApiLayerProperties(api_layer_count, &api_layer_count, api_layers.data()) != XR_SUCCESS) {
		LOGE("Failed to enumerate API layers.");
		return;
	}

	for (auto &api_layer : api_layers) {
		LOGI("OpenXR runtime supports '%s' API layer v%lu.", api_layer.layerName, api_layer.specVersion);
	}

	// Enumerate extensions runtime has made available.

	uint32_t ext_count;

	if (xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr) != XR_SUCCESS) {
		LOGE("Failed to enumerate extensions.");
		return;
	}

	LOGI("extension count: %d", ext_count);

	auto exts = std::vector<XrExtensionProperties>(ext_count, {XR_TYPE_EXTENSION_PROPERTIES});

	if (xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, exts.data()) != XR_SUCCESS) {
		LOGE("Failed to enumerate extensions.");
		return;
	}

	auto required_exts = std::vector{
		XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
		XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
	};

	for (auto &ext : exts) {
		LOGI("OpenXR runtime supports '%s' extension v%u.", ext.extensionName, ext.extensionVersion);
	}

	for (auto &required : required_exts) {
		bool found = false;

		for (auto &ext : exts) {
			if (strcmp(required, ext.extensionName) == 0) {
				found = true;
			}
		}

		if (!found) {
			LOGE("OpenXR runtime missing support for '%s' extension.", required);
			return;
		}
	}

	// Actually create instance.

	XrApplicationInfo const app_info = {
		.applicationName = "Mist",
		.applicationVersion = 1,
		.engineName = "OpenXR Engine",
		.engineVersion = 1,
		.apiVersion = XR_MAKE_VERSION(1, 0, 0), // Oculus Quest only supports OpenXR 1.0.0.
	};

	XrInstanceCreateInfo const info = {
		.type = XR_TYPE_INSTANCE_CREATE_INFO,
		.applicationInfo = app_info,
		.enabledExtensionCount = static_cast<uint32_t>(required_exts.size()),
		.enabledExtensionNames = required_exts.data(),
	};

	XrInstance inst = XR_NULL_HANDLE;
	XrResult const res = xrCreateInstance(&info, &inst);

	if (res != XR_SUCCESS) {
		LOGE("xrCreateInstance failed: %d", res);
		return;
	}

	// Get the instance properties.

	XrInstanceProperties instance_props = {XR_TYPE_INSTANCE_PROPERTIES};

	if (xrGetInstanceProperties(inst, &instance_props) != XR_SUCCESS) {
		return;
	}

	LOGI("OpenXR runtime: %s %d.%d.%d",
		instance_props.runtimeName,
		XR_VERSION_MAJOR(instance_props.runtimeVersion),
		XR_VERSION_MINOR(instance_props.runtimeVersion),
		XR_VERSION_PATCH(instance_props.runtimeVersion)
	);

	// TODO Set up XR_EXT_debug_utils.
	// See the OpenXR-Tutorials/Common/OpenXRDebugUtils.h file.

	// Get system info.

	XrSystemGetInfo const sys_get_info = {
		.type = XR_TYPE_SYSTEM_GET_INFO,
		.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
	};

	XrSystemId sys_id;

	if (xrGetSystem(inst, &sys_get_info, &sys_id) != XR_SUCCESS) {
		return;
	}

	XrSystemProperties sys_props = {XR_TYPE_SYSTEM_PROPERTIES};

	if (xrGetSystemProperties(inst, sys_id, &sys_props) != XR_SUCCESS) {
		return;
	}

	LOGI("System name: %s (vendor 0x%x)", sys_props.systemName, sys_props.vendorId);

	// Get OpenXR's OpenGL ES graphics API requirements.

	PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR;

	if (
		xrGetInstanceProcAddr(inst, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*) &xrGetOpenGLESGraphicsRequirementsKHR) != XR_SUCCESS ||
		xrGetOpenGLESGraphicsRequirementsKHR == nullptr
	) {
		LOGE("Failed to get xrGetOpenGLESGraphicsRequirementsKHR.");
		return;
	}

	XrGraphicsRequirementsOpenGLESKHR graphics_requirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};

	if (xrGetOpenGLESGraphicsRequirementsKHR(inst, sys_id, &graphics_requirements) != XR_SUCCESS) {
		LOGE("Failed to call xrGetOpenGLESGraphicsRequirementsKHR.");
		return;
	}

	LOGI("OpenXR OpenGL ES graphics API requirements: min %u.%u.%u, max %u.%u.%u",
		XR_VERSION_MAJOR(graphics_requirements.minApiVersionSupported),
		XR_VERSION_MINOR(graphics_requirements.minApiVersionSupported),
		XR_VERSION_PATCH(graphics_requirements.minApiVersionSupported),
		XR_VERSION_MAJOR(graphics_requirements.maxApiVersionSupported),
		XR_VERSION_MINOR(graphics_requirements.maxApiVersionSupported),
		XR_VERSION_PATCH(graphics_requirements.maxApiVersionSupported)
	);

	// Initialize EGL.

	EGLDisplay const display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	if (display == EGL_NO_DISPLAY) {
		LOGE("Couldn't get EGL display.");
		return;
	}

	EGLint egl_major, egl_minor;

	if (eglInitialize(display, &egl_major, &egl_minor) != EGL_TRUE) {
		LOGE("Couldn't initialize EGL.");
		return;
	}

	LOGI("Initialized EGL %d.%d\n", egl_major, egl_minor);

	// Find a suitable config.
	// Note that we don't really want to use multisampling here, as that would be completely wasted by composition and timewarping.
	// This is also why we don't use eglChooseConfig; on Android, if "force 4x MSAA" is set, only configs with 4x MSAA will be shown to us.
	// Some of this code comes from OpenXR's gfxwrapper_opengl.

	EGLint config_count = 0;

	if (eglGetConfigs(display, nullptr, 0, &config_count) != EGL_TRUE) {
		LOGE("Couldn't get EGL configs.");
		return;
	}

	auto configs = std::vector<EGLConfig>(config_count);

	if (eglGetConfigs(display, configs.data(), config_count, &config_count) != EGL_TRUE) {
		LOGE("Couldn't get EGL configs.");
		return;
	}

	EGLConfig selected_config = nullptr;

	for (auto config : configs) {
		EGLint value = 0;
		eglGetConfigAttrib(display, config, EGL_RENDERABLE_TYPE, &value);

		if ((value & EGL_OPENGL_ES3_BIT) != EGL_OPENGL_ES3_BIT) {
			continue;
		}

		// Without EGL_KHR_surfaceless_context, the config needs to support both pbuffers and window surfaces.

		eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE, &value);

		if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) != (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) {
			continue;
		}

		selected_config = config;
	}

	if (selected_config == nullptr) {
		LOGE("Couldn't find a suitable EGL config.");
		return;
	}

	// Create an EGL context.

	EGLint const context_attrs[] = {
		EGL_CONTEXT_CLIENT_VERSION, XR_VERSION_MAJOR(graphics_requirements.maxApiVersionSupported),
		// EGL_CONTEXT_PRIORITY_LEVEL_IMG, EGL_CONTEXT_PRIORITY_LOW_IMG,
		EGL_NONE,
	};

	EGLContext const context = eglCreateContext(display, selected_config, EGL_NO_CONTEXT, context_attrs);

	if (context == EGL_NO_CONTEXT) {
		LOGE("Couldn't create EGL context.");
		return;
	}

	// Create an EGL surface.

	EGLint const surf_attrs[] = {
		EGL_WIDTH, 16,
		EGL_HEIGHT, 16,
		EGL_NONE,
	};

	EGLSurface const tiny_surf = eglCreatePbufferSurface(display, selected_config, surf_attrs);

	if (tiny_surf == EGL_NO_SURFACE) {
		LOGE("Couldn't create tiny EGL surface.");
		return;
	}

	// Create an OpenXR session.

	XrGraphicsBindingOpenGLESAndroidKHR const graphics_binding = {
		.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
		.display = display,
		.config = selected_config,
		.context = context,
	};

	XrSessionCreateInfo const session_create_info = {
		.type = XR_TYPE_SESSION_CREATE_INFO,
		.next = &graphics_binding,
		.createFlags = 0,
		.systemId = sys_id,
	};

	XrSession session;

	if (xrCreateSession(inst, &session_create_info, &session) != XR_SUCCESS) {
		return;
	}

	// TODO Clean up the following code.

	if (app->savedState != NULL) {
		// We are starting with a previous saved state; restore from it.
		engine.state = *(struct saved_state*)app->savedState;
	}

	// loop waiting for stuff to do.

	while (1) {
		// Read all pending events.
		int ident;
		int events;
		struct android_poll_source* source;

		// If not animating, we will block forever waiting for events.
		// If animating, we loop until all events are read, then continue
		// to draw the next frame of animation.
		while ((ident=ALooper_pollOnce(engine.animating ? 0 : -1, NULL, &events,
									  (void**)&source)) >= 0) {

			// Process this event.
			if (source != NULL) {
				source->process(app, source);
			}

			// Check if we are exiting.
			if (app->destroyRequested != 0) {
				engine_term_display(&engine);
				return;
			}
		}

		if (engine.animating) {
			// Done with events; draw next animation frame.
			engine.state.angle += .01f;
			if (engine.state.angle > 1) {
				engine.state.angle = 0;
			}

			// Drawing is throttled to the screen update rate, so there
			// is no need to do timing here.
			engine_draw_frame(&engine);
		}
	}

	// Cleanup.

	xrDestroySession(session);
	xrDestroyInstance(inst);
}
