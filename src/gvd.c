#include "desktop.h"
#include "log.h"

#include <aqua/gv_agent.h>
#include <aqua/mist.h>

#include <arpa/inet.h>
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wait.h>

#include <android_native_app_glue.h>

static void extract(AAssetManager* mgr, char const* asset_path, char* out_path) {
	AAsset* const asset = AAssetManager_open(mgr, asset_path, AASSET_MODE_STREAMING);

	if (asset == NULL) {
		LOGE("Failed to open asset '%s'.", asset_path);
		return;
	}

	char path[256];
	snprintf(path, sizeof path, "/data/data/com.inobulles.mist/files/%s", asset_path);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);

	if (fd < 0) {
		LOGE("Failed to open destination %s: %s.", path, strerror(errno));
		AAsset_close(asset);
		return;
	}

	char buf[4096];
	int n;

	while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0) {
		if (write(fd, buf, n) != n) {
			LOGE("Write error: %s.", strerror(errno));
			close(fd);
			AAsset_close(asset);
			return;
		}
	}

	close(fd);
	AAsset_close(asset);

	if (out_path != NULL) {
		strncpy(out_path, path, sizeof path);
	}

	LOGI("Extracted '%s' to '%s'.", asset_path, path);
}

static int get_default_ifname(char* out_ifname, size_t len) {
	struct ifaddrs *ifaddr = NULL, *ifa;
	int found = 0;

	if (getifaddrs(&ifaddr) == -1) {
		LOGE("getifaddrs() failed");
		return -1;
	}

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr) {
			continue;
		}

		// Skip loopback, down, or non-AF_INET interfaces.

		if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) {
			continue;
		}

		if (ifa->ifa_addr->sa_family != AF_INET) {
			continue;
		}

		// Prefer wlan* (Wi-Fi).

		if (strncmp(ifa->ifa_name, "wlan", 4) == 0) {
			strncpy(out_ifname, ifa->ifa_name, len - 1);
			out_ifname[len - 1] = '\0';
			found = 1;
			break;
		}

		// Fallback: take first non-loopback active interface.

		if (!found) {
			strncpy(out_ifname, ifa->ifa_name, len - 1);
			out_ifname[len - 1] = '\0';
			found = 1;
		}
	}

	freeifaddrs(ifaddr);

	if (!found) {
		LOGE("No suitable interface found");
		return -1;
	}

	return 0;
}

// TODO I stole this from an SO answer as a quick hack (TODO attribution).
// But this is very much a stopgap; in fine, we should really have a way for Umber to output to more than just stdout/err.

static int pfd[2];
static pthread_t thr;

static void* thread_func(void* _) {
	ssize_t rdsz;
	char buf[128];
	while ((rdsz = read(pfd[0], buf, sizeof buf - 1)) > 0) {
		if (buf[rdsz - 1] == '\n') {
			--rdsz;
		}
		buf[rdsz] = 0; /* add null-terminator */
		__android_log_write(ANDROID_LOG_DEBUG, "mist-log", buf);
	}
	return 0;
}

int start_logger(void) {
	/* make stdout line-buffered and stderr unbuffered */
	setvbuf(stdout, 0, _IOLBF, 0);
	setvbuf(stderr, 0, _IONBF, 0);

	/* create the pipe and redirect stdout and stderr */
	pipe(pfd);
	dup2(pfd[1], 1);
	dup2(pfd[1], 2);

	/* spawn the logging thread */
	if (pthread_create(&thr, 0, thread_func, 0) == -1) {
		return -1;
	}
	pthread_detach(thr);
	return 0;
}

static void* vr_vdev_conn_listener_thread(void* arg) {
	start_logger();

	LOGI("%s: Create server UDS.", __func__);
	int const server_fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (server_fd < 0) {
		LOGE("socket(AF_UNIX): %s", strerror(errno));
		return NULL;
	}

	LOGI("%s: Bind.", __func__);
	char const* const spec = "aquabsd.black.vr";

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0'; // Abstract UDS.
	strncpy(addr.sun_path + 1, spec, sizeof addr.sun_path - 1);

	if (bind(server_fd, (struct sockaddr*) &addr, sizeof addr) < 0) {
		LOGE("bind: %s", strerror(errno));
		close(server_fd);
		return NULL;
	}

	LOGI("%s: Listen.", __func__);

	if (listen(server_fd, 1) < 0) {
		LOGE("listen: %s", strerror(errno));
		close(server_fd);
		return NULL;
	}

	LOGI("%s: Waiting for connection to accept.", __func__);
	int const client_fd = accept(server_fd, NULL, NULL);

	if (client_fd < 0) {
		LOGE("accept: %s", strerror(errno));
		return NULL;
	}

	LOGI("%s: Waiting for message.", __func__);

	uint64_t vdev_id = 0;
	char control[CMSG_SPACE(sizeof(int))] = {0};

	struct iovec io = {
		.iov_base = &vdev_id,
		.iov_len = sizeof vdev_id
	};

	struct msghdr msg = {
		.msg_iov = &io,
		.msg_iovlen = 1,
		.msg_control = control,
		.msg_controllen = sizeof control,
	};

	if (recvmsg(client_fd, &msg, 0) < 0) {
		LOGE("recvmsg: %s", strerror(errno));
		return NULL;
	}

	int received_fd = -1;
	struct cmsghdr* const cmsg = CMSG_FIRSTHDR(&msg);

	if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
		memcpy(&received_fd, CMSG_DATA(cmsg), sizeof received_fd);
	}

	close(client_fd);
	close(server_fd);

	// TODO If we are to set this shit, it must be somehow before the app loads, because otherwise we can't set UMBER_LVL in libraries that use constructors.

	setenv("GV_NODES_PATH", "/data/data/com.inobulles.mist/files/tmp/gv.nodes", true);
	setenv("GV_LOCK_PATH", "/data/data/com.inobulles.mist/files/tmp/gv.lock", true);
	setenv("GV_HOST_ID_PATH", "/data/data/com.inobulles.mist/files/tmp/gv.host_id", true);
	setenv("UMBER_LVL", "*=v,aqua.gvd.elp=i", true);
	setenv("UMBER_LINEBUF", "true", true);
	setenv("VDRIVER_PATH", "/data/data/com.inobulles.mist/files/lib/vdriver", true);

	gv_agent_t* const agent = gv_agent_create(received_fd, "aquabsd.black.vr", vdev_id);

	if (agent == NULL) {
		LOGE("Failed to create GrapeVine KOS agent.");
		goto err_gv_agent_create;
	}

	vdriver_t* const vdriver = gv_agent_get_vdriver(agent);
	mist_ops_t* const mist_ops = dlsym(vdriver->lib, "MIST_OPS");

	if (mist_ops == NULL) {
		LOGE("Could not find MIST_OPS on loaded aquabsd.black.vr VDRIVER.");
		goto err_get_mist_ops;
	}

	mist_ops->send_win = desktop_send_win;
	mist_ops->destroy_win = desktop_destroy_win;
	mist_ops->set = true;

	gv_agent_loop(agent);

err_get_mist_ops:

	gv_agent_destroy(agent);

err_gv_agent_create:

	close(received_fd);
	return NULL;
}

typedef struct {
	int fd;
	char const* stream_name;
} log_thread_args_t;

static void* log_reader_thread(void* arg) {
	log_thread_args_t* const args = (void*) arg;
	FILE* const fp = fdopen(args->fd, "r");

	if (!fp) {
		LOGE("fdopen() failed for %s", args->stream_name);
		return NULL;
	}

	char* line = NULL;
	size_t len = 0;
	ssize_t nread;

	while ((nread = getline(&line, &len, fp)) != -1) {
		// Remove trailing newline if present.

		if (nread > 0 && line[nread - 1] == '\n') {
			line[nread - 1] = '\0';
		}

		if (strcmp(args->stream_name, "stderr") == 0) {
			LOGE("%s", line);
		}

		else {
			LOGI("%s", line);
		}
	}

	LOGE("No more output on %s!", args->stream_name);

	free(line);
	fclose(fp);
	close(args->fd);

	return NULL;
}

void start_gvd(AAssetManager* mgr) {
	// TODO Create the directory tree structure intelligently by parsing the asset path.

	mkdir("/data/data/com.inobulles.mist/files/bin", 0755);
	mkdir("/data/data/com.inobulles.mist/files/lib", 0755);
	mkdir("/data/data/com.inobulles.mist/files/lib/vdriver", 0755);
	mkdir("/data/data/com.inobulles.mist/files/tmp", 0755);

	// Extract gvd binary and supporting stuff.

	char gvd_path[256];

	extract(mgr, "bin/gvd", gvd_path);
	extract(mgr, "lib/libumber.so", NULL);
	extract(mgr, "lib/libvdriver_loader.so", NULL);
	extract(mgr, "lib/vdriver/aquabsd.black.vr.vdriver", NULL);

	// Get default interface name.

	char ifname[256];

	if (get_default_ifname(ifname, sizeof ifname) < 0) {
		return;
	}

	// Set up logging pipes.

	int stdout_pipe[2];
	assert(pipe(stdout_pipe) == 0);

	int stderr_pipe[2];
	assert(pipe(stderr_pipe) == 0);

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);
	posix_spawn_file_actions_addclose(&actions, stdout_pipe[0]);
	posix_spawn_file_actions_addclose(&actions, stderr_pipe[0]);

	// Spawn gvd process.

	pid_t pid = 0;

	char* argv[] = {
		(char*) gvd_path,
		(char*) "-i",
		(char*) ifname,
		NULL,
	};

	char* const envp[] = {
		(char*) "LD_LIBRARY_PATH=/data/data/com.inobulles.mist/files/lib",
		(char*) "UMBER_LVL=*=v,aqua.gvd.elp=i",
		(char*) "UMBER_LINEBUF=true",
		(char*) "GV_NODES_PATH=/data/data/com.inobulles.mist/files/tmp/gv.nodes",
		(char*) "GV_LOCK_PATH=/data/data/com.inobulles.mist/files/tmp/gv.lock",
		(char*) "GV_HOST_ID_PATH=/data/data/com.inobulles.mist/files/tmp/gv.host_id",
		(char*) "VDRIVER_PATH=/data/data/com.inobulles.mist/files/lib/vdriver",
		NULL,
	};

	int res = posix_spawn(&pid, gvd_path, &actions, NULL, argv, envp);

	if (res != 0) {
		LOGE("posix_spawn failed: %s.", strerror(res));
		return;
	}

	close(stdout_pipe[1]);
	close(stderr_pipe[1]);

	LOGI("Spawned gvd (pid=%d).", pid);

	pthread_t stdout_thread, stderr_thread;

	static log_thread_args_t stdout_args = {0, "stdout"};
	static log_thread_args_t stderr_args = {0, "stderr"};

	stdout_args.fd = stdout_pipe[0];
	stderr_args.fd = stderr_pipe[0];

	pthread_create(&stdout_thread, NULL, log_reader_thread, (void*) &stdout_args);
	pthread_create(&stderr_thread, NULL, log_reader_thread, (void*) &stderr_args);

	posix_spawn_file_actions_destroy(&actions);

	// Don't close other end of pipes here because log reader threads are still using them!

	// Start thread for waiting for VDEV connections.

	pthread_t vr_vdev_conn_listener;
	pthread_create(&vr_vdev_conn_listener, NULL, vr_vdev_conn_listener_thread, NULL);
}
