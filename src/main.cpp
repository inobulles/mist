#include <cassert>
#include <jni.h>
#include <string.h>

#include <EGL/egl.h>
#include <glad/gles2.h>

#include <android/log.h>
#include <android_native_app_glue.h>
#include <vector>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define LOGI(...) ((void) __android_log_print(ANDROID_LOG_INFO, "mist", __VA_ARGS__))
#define LOGW(...) ((void) __android_log_print(ANDROID_LOG_WARN, "mist", __VA_ARGS__))
#define LOGE(...) ((void) __android_log_print(ANDROID_LOG_ERROR, "mist", __VA_ARGS__))

typedef struct {
	struct android_app* app;
	XrSession session;
} state_t;

XrBool32 debug_utils_messenger_cb(
	XrDebugUtilsMessageSeverityFlagsEXT severity,
	XrDebugUtilsMessageTypeFlagsEXT type,
	XrDebugUtilsMessengerCallbackDataEXT const* data,
	void* user_data
) {
	std::string type_str = "";

	if ((type & XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) != 0) {
		type_str += "GEN,";
	}

	if ((type & XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0) {
		type_str += "VAL,";
	}

	if ((type & XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) != 0) {
		type_str += "PERF,";
	}

	if ((type & XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT) != 0) {
		type_str += "CONF,";
	}

	if (!type_str.empty() && type_str.back() == ',') {
		type_str.pop_back();
	}

	switch (severity) {
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		LOGI("[XR_DEBUG_UTILS_MESSAGE %s %s] %s", type_str.c_str(), data->messageId, data->message);
		break;
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		LOGW("[XR_DEBUG_UTILS_MESSAGE %s %s] %s", type_str.c_str(), data->messageId, data->message);
		break;
	case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		LOGE("[XR_DEBUG_UTILS_MESSAGE %s %s] %s", type_str.c_str(), data->messageId, data->message);
		break;
	}

	return XrBool32();
}

static void render(state_t* s) {
	(void) s;
}

void android_main(struct android_app* app) {
	state_t s = {};
	app->userData = &s;

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
	std::vector<char const*> selected_api_layers;

	if (xrEnumerateApiLayerProperties(api_layer_count, &api_layer_count, api_layers.data()) != XR_SUCCESS) {
		LOGE("Failed to enumerate API layers.");
		return;
	}

	auto wanted_api_layers = {
		// "XR_APILAYER_LUNARG_api_dump",
		"XR_APILAYER_KHRONOS_best_practices_validation",
		"XR_APILAYER_LUNARG_core_validation",
	};

	for (auto& api_layer : api_layers) {
		LOGI(
			"OpenXR runtime supports '%s' API layer %u.%u.%u.",
			api_layer.layerName,
			XR_VERSION_MAJOR(api_layer.specVersion),
			XR_VERSION_MINOR(api_layer.specVersion),
			XR_VERSION_PATCH(api_layer.specVersion)
		);

		for (auto& wanted : wanted_api_layers) {
			if (strcmp(api_layer.layerName, wanted) != 0) {
				continue;
			}

			LOGI("Selected '%s' OpenXR API layer.", api_layer.layerName);
			selected_api_layers.push_back(api_layer.layerName);

			break;
		}
	}

	// Enumerate extensions runtime has made available.

	uint32_t ext_count;

	if (xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr) != XR_SUCCESS) {
		LOGE("Failed to enumerate extensions.");
		return;
	}

	auto exts = std::vector<XrExtensionProperties>(ext_count, {XR_TYPE_EXTENSION_PROPERTIES});

	if (xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, exts.data()) != XR_SUCCESS) {
		LOGE("Failed to enumerate extensions.");
		return;
	}

	auto required_exts = std::vector {
		XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
		XR_EXT_DEBUG_UTILS_EXTENSION_NAME,
	};

	for (auto& ext : exts) {
		LOGI("OpenXR runtime supports '%s' extension v%u.", ext.extensionName, ext.extensionVersion);
	}

	for (auto& required : required_exts) {
		bool found = false;

		for (auto& ext : exts) {
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
		.enabledApiLayerCount = static_cast<uint32_t>(selected_api_layers.size()),
		.enabledApiLayerNames = selected_api_layers.data(),
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

	LOGI(
		"OpenXR runtime: %s %d.%d.%d",
		instance_props.runtimeName,
		XR_VERSION_MAJOR(instance_props.runtimeVersion),
		XR_VERSION_MINOR(instance_props.runtimeVersion),
		XR_VERSION_PATCH(instance_props.runtimeVersion)
	);

	// Set up XR_EXT_debug_utils.
	// See the OpenXR-Tutorials/Common/OpenXRDebugUtils.h file.

	XrDebugUtilsMessengerCreateInfoEXT const debug_utils_messenger_info = {
		.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverities =
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageTypes =
			XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
			XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT,
		.userCallback = (PFN_xrDebugUtilsMessengerCallbackEXT) debug_utils_messenger_cb,
		.userData = nullptr,
	};

	PFN_xrCreateDebugUtilsMessengerEXT xrCreateDebugUtilsMessengerEXT;

	if (xrGetInstanceProcAddr(inst, "xrCreateDebugUtilsMessengerEXT", (PFN_xrVoidFunction*) &xrCreateDebugUtilsMessengerEXT) != XR_SUCCESS) {
		LOGE("Couldn't get xrCreateDebugUtilsMessengerEXT.");
		return;
	}

	XrDebugUtilsMessengerEXT debug_utils_messenger;

	if (xrCreateDebugUtilsMessengerEXT(inst, &debug_utils_messenger_info, &debug_utils_messenger) != XR_SUCCESS) {
		LOGE("Couldn't create debug utils messenger.");
		return;
	}

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

	LOGI(
		"OpenXR OpenGL ES graphics API requirements: min %u.%u.%u, max %u.%u.%u",
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

	LOGI("Initialized EGL %d.%d.", egl_major, egl_minor);

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
		EGL_CONTEXT_CLIENT_VERSION,
		XR_VERSION_MAJOR(graphics_requirements.maxApiVersionSupported),
		// EGL_CONTEXT_PRIORITY_LEVEL_IMG, EGL_CONTEXT_PRIORITY_LOW_IMG,
		EGL_NONE,
	};

	EGLContext const context = eglCreateContext(display, selected_config, EGL_NO_CONTEXT, context_attrs);

	if (context == EGL_NO_CONTEXT) {
		LOGE("Couldn't create EGL context.");
		return;
	}

	// Create an EGL surface.

	EGLint const surf_attrs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
	EGLSurface const tiny_surf = eglCreatePbufferSurface(display, selected_config, surf_attrs);

	if (tiny_surf == EGL_NO_SURFACE) {
		LOGE("Couldn't create tiny EGL surface.");
		return;
	}

	// Make EGL surface and context current.

	if (eglMakeCurrent(display, tiny_surf, tiny_surf, context) != EGL_TRUE) {
		LOGE("Couldn't make EGL surface and context current.");
		return;
	}

	// Load GLES2 with Glad.

	int const version = gladLoadGLES2(eglGetProcAddress);

	if (version <= 0) {
		LOGE("Couldn't load GLES2 (with Glad): %d", version);
		return;
	}

	LOGI("Glad loaded OpenGL ES version %d.%d.", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

	LOGI("OpenGL ES vendor: %s.", glGetString(GL_VENDOR));
	LOGI("OpenGL ES version: %s.", glGetString(GL_VERSION));
	LOGI("OpenGL ES renderer: %s.", glGetString(GL_RENDERER));
	LOGI("OpenGL ES shading language version: %s.", glGetString(GL_SHADING_LANGUAGE_VERSION));

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

	if (xrCreateSession(inst, &session_create_info, &s.session) != XR_SUCCESS) {
		return;
	}

	// Get view configurations.

	uint32_t view_config_count = 0;

	if (xrEnumerateViewConfigurations(inst, sys_id, 0, &view_config_count, nullptr) != XR_SUCCESS) {
		LOGE("Couldn't enumerate view configurations.");
		return;
	}

	auto view_configs = std::vector<XrViewConfigurationType>(view_config_count);

	if (xrEnumerateViewConfigurations(inst, sys_id, view_config_count, &view_config_count, view_configs.data()) != XR_SUCCESS) {
		LOGE("Couldn't enumerate view configurations.");
		return;
	}

	XrViewConfigurationType view_config = XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM;

	for (auto& v : view_configs) {
		LOGI("OpenXR runtime supports %d view configuration type.", v);

		if (v == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
			view_config = v;
			break;
		}
	}

	if (view_config == XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM) {
		LOGW("Couldn't find primary stereo view configuration type.");
		return;
	}

	// Get view configuration views.

	uint32_t view_config_view_count = 0;

	if (xrEnumerateViewConfigurationViews(inst, sys_id, view_config, 0, &view_config_view_count, nullptr) != XR_SUCCESS) {
		LOGW("Couldn't get configuration views.");
		return;
	}

	auto view_config_views = std::vector<XrViewConfigurationView>(view_config_view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});

	if (xrEnumerateViewConfigurationViews(inst, sys_id, view_config, view_config_view_count, &view_config_view_count, view_config_views.data()) != XR_SUCCESS) {
		LOGW("Couldn't get configuration views.");
		return;
	}

	for (auto& v : view_config_views) {
		LOGI("For our primary stereo view configuration type, OpenXR runtime supports a (recommended) %dx%d resolution view.", v.recommendedImageRectWidth, v.recommendedImageRectHeight);
	}

	// Get swapchain formats.

	uint32_t format_count = 0;

	if (xrEnumerateSwapchainFormats(s.session, 0, &format_count, nullptr) != XR_SUCCESS) {
		LOGW("Couldn't get swapchain formats.");
		return;
	}

	auto formats = std::vector<int64_t>(format_count);

	if (xrEnumerateSwapchainFormats(s.session, format_count, &format_count, formats.data()) != XR_SUCCESS) {
		LOGW("Couldn't get swapchain formats.");
		return;
	}

	int64_t const format = formats[0]; // First one is our OpenXR runtime's preference.
	LOGI("Selecting OpenXR format 0x%lx (runtime preference).", format);

	// Create the swapchains (one for each view, and one for colour and depth).

	struct SwapchainInfo {
		XrSwapchain swapchain = XR_NULL_HANDLE;
		int64_t format = 0;
		std::vector<void*> image_views;
	};

	auto colour_swapchains = std::vector<SwapchainInfo>(view_config_views.size());
	auto depth_swapchains = std::vector<SwapchainInfo>(view_config_views.size());

	for (size_t i = 0; i < view_config_views.size(); i++) {
		auto view_config_view = view_config_views[i];

		// Colour swapchain.

		SwapchainInfo& colour_swapchain = colour_swapchains[i];
		colour_swapchain.format = GL_RGBA8; // TODO Get this from supported swapchain formats (see GetSupportedColorSwapchainFormats and m_graphicsAPI->SelectColorSwapchainFormat(formats)).

		XrSwapchainCreateInfo const colour_create_info = {
			.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
			.createFlags = 0,
			.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
			.format = colour_swapchain.format,
			.sampleCount = view_config_view.recommendedSwapchainSampleCount,
			.width = view_config_view.recommendedImageRectWidth,
			.height = view_config_view.recommendedImageRectHeight,
			.faceCount = 1,
			.arraySize = 1,
			.mipCount = 1,
		};

		assert(xrCreateSwapchain(s.session, &colour_create_info, &colour_swapchain.swapchain) == XR_SUCCESS);

		// Depth swapchain.

		SwapchainInfo& depth_swapchain = depth_swapchains[i];
		depth_swapchain.format = GL_DEPTH_COMPONENT24; // TODO Get this from supported swapchain formats (see GetSupportedColorSwapchainFormats and m_graphicsAPI->SelectColorSwapchainFormat(formats)).

		XrSwapchainCreateInfo const depth_create_info = {
			.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
			.createFlags = 0,
			.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
			.format = depth_swapchain.format,
			.sampleCount = view_config_view.recommendedSwapchainSampleCount,
			.width = view_config_view.recommendedImageRectWidth,
			.height = view_config_view.recommendedImageRectHeight,
			.faceCount = 1,
			.arraySize = 1,
			.mipCount = 1,
		};

		assert(xrCreateSwapchain(s.session, &depth_create_info, &depth_swapchain.swapchain) == XR_SUCCESS);

		// Get the colour swapchain images.

		uint32_t colour_image_count = 0;
		assert(xrEnumerateSwapchainImages(colour_swapchain.swapchain, 0, &colour_image_count, nullptr) == XR_SUCCESS);
		auto colour_images = std::vector<XrSwapchainImageOpenGLESKHR>(colour_image_count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
		assert(xrEnumerateSwapchainImages(colour_swapchain.swapchain, colour_image_count, &colour_image_count, (XrSwapchainImageBaseHeader*) colour_images.data()) == XR_SUCCESS);

		// Get the depth swapchain images.

		uint32_t depth_image_count = 0;
		assert(xrEnumerateSwapchainImages(depth_swapchain.swapchain, 0, &depth_image_count, nullptr) == XR_SUCCESS);
		auto depth_images = std::vector<XrSwapchainImageOpenGLESKHR>(depth_image_count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
		assert(xrEnumerateSwapchainImages(depth_swapchain.swapchain, depth_image_count, &depth_image_count, (XrSwapchainImageBaseHeader*) depth_images.data()) == XR_SUCCESS);

		// Create framebuffers for colour swapchain images.

		for (auto& image : colour_images) {
			GLuint framebuffer = 0;
			glGenFramebuffers(1, &framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

			// XXX When we move on to using multiview, we should use glFramebufferTextureMultiviewOVR here.

			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint) image.image, 0);
			assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
		}

		// Create framebuffers for depth swapchain images.

		for (auto& image : depth_images) {
			GLuint framebuffer = 0;
			glGenFramebuffers(1, &framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

			// XXX When we move on to using multiview, we should use glFramebufferTextureMultiviewOVR here.

			glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, (GLuint) image.image, 0);
			assert(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
		}
	}

	// Get environment blend modes and select one.

	uint32_t env_blend_mode_count = 0;

	if (xrEnumerateEnvironmentBlendModes(inst, sys_id, view_config, 0, &env_blend_mode_count, nullptr) != XR_SUCCESS) {
		LOGE("Failed to enumerate OpenXR environment blend modes.");
		return;
	}

	auto env_blend_modes = std::vector<XrEnvironmentBlendMode>(env_blend_mode_count);

	if (xrEnumerateEnvironmentBlendModes(inst, sys_id, view_config, env_blend_mode_count, &env_blend_mode_count, env_blend_modes.data()) != XR_SUCCESS) {
		LOGE("Failed to enumerate OpenXR environment blend modes.");
		return;
	}

	XrEnvironmentBlendMode env_blend_mode = XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM;

	for (auto& blend_mode : env_blend_modes) {
		if (blend_mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE || blend_mode == XR_ENVIRONMENT_BLEND_MODE_ADDITIVE) {
			env_blend_mode = blend_mode;
			break;
		}
	}

	if (env_blend_mode == XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM) {
		LOGE("No supported OpenXR environment blend mode.");
		return;
	}

	LOGI("Selecting OpenXR environment blend mode 0x%x (runtime preference).", env_blend_mode);

	// Create reference space.

	XrReferenceSpaceCreateInfo const ref_space_create = {
		.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
		.poseInReferenceSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
	};

	XrSpace ref_space;

	if (xrCreateReferenceSpace(s.session, &ref_space_create, &ref_space) != XR_SUCCESS) {
		LOGE("Failed to create OpenXR local reference space.");
		return;
	}

	// Main loop.

	LOGI("Starting main loop.");

	bool running = true;
	bool session_running = false;
	XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;

	(void) session_state;

	for (; running;) {
		// Read Android events.

		int events;
		struct android_poll_source* source = nullptr;

		while (ALooper_pollOnce(0, nullptr, &events, (void**) &source) >= 0) {
			if (source == nullptr) {
				break;
			}

			source->process(app, source);
		}

		// Read OpenXR events.

		for (
			XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
			xrPollEvent(inst, &event) == XR_SUCCESS;
			event = {XR_TYPE_EVENT_DATA_BUFFER}
		) {
			switch (event.type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
				XrEventDataEventsLost* const events_lost = reinterpret_cast<XrEventDataEventsLost*>(&event);
				LOGW("%d OpenXR events lost.", events_lost->lostEventCount);
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				XrEventDataInstanceLossPending* const inst_loss_pending = reinterpret_cast<XrEventDataInstanceLossPending*>(&event);
				LOGW("OpenXR instance loss pending at %ld", inst_loss_pending->lossTime);

				running = false;
				session_running = false;

				break;
			}
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				XrEventDataInteractionProfileChanged* const interaction_profile_change = reinterpret_cast<XrEventDataInteractionProfileChanged*>(&event);
				LOGI("Interaction profile changed for our OpenXR session.");

				if (interaction_profile_change->session != s.session) {
					LOGW("Interaction profile changed for unknown OpenXR session %p.", interaction_profile_change->session);
				}

				break;
			}
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				XrEventDataReferenceSpaceChangePending* const ref_space_change_pending = reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&event);
				LOGI("Reference space change pending for our OpenXR session.");

				if (ref_space_change_pending->session != s.session) {
					LOGW("Reference space change pending for unknown OpenXR session %p.", ref_space_change_pending->session);
				}

				break;
			}
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				XrEventDataSessionStateChanged* const session_state_change = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);

				if (session_state_change->session != s.session) {
					LOGW("State changed for unknown OpenXR session %p.", session_state_change->session);
					break;
				}

				switch (session_state_change->state) {
				case XR_SESSION_STATE_READY: {
					XrSessionBeginInfo const session_begin_info = {
						.type = XR_TYPE_SESSION_BEGIN_INFO,
						.primaryViewConfigurationType = view_config,
					};

					XrResult const res = xrBeginSession(s.session, &session_begin_info);

					if (res != XR_SUCCESS) {
						LOGW("xrBeginSession failed: %d.", res);
					}

					else {
						session_running = true;
					}

					break;
				}
				case XR_SESSION_STATE_STOPPING: {
					XrResult const res = xrEndSession(s.session);

					if (res != XR_SUCCESS) {
						LOGW("xrEndSession failed: %d.", res);
					}

					session_running = false;
					break;
				}
				case XR_SESSION_STATE_EXITING:
					session_running = false;
					running = false;
					break;
				case XR_SESSION_STATE_LOSS_PENDING:
					session_running = false;
					running = false;
					break;
				default:
					break;
				}

				session_state = session_state_change->state;
				break;
			}
			default:;
			}
		}

		// Render.

		if (session_running) {
			render(&s);
		}
	}

	// Cleanup.

	xrDestroySession(s.session);
	xrDestroyInstance(inst);
}
