#include "shader.h"
#include "log.h"

#include <EGL/egl.h>
#include <glad/gles2.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compile_shader(GLuint shader, char const* src) {
	int rv = -1;

	glShaderSource(shader, 1, &src, 0);
	glCompileShader(shader);

	GLint log_len;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);

	char* const log_buf = malloc(log_len); // 'log_len' includes null character.
	glGetShaderInfoLog(shader, log_len, NULL, log_buf);

	if (log_len) {
		LOGW("%s failed to compile: %s.", src, log_buf);
		goto err;
	}

	rv = 0;

err:

	free(log_buf);
	return rv;
}

GLuint create_shader(char const* vert_src, char const* frag_src) {
	GLuint const vert = glCreateShader(GL_VERTEX_SHADER);
	GLuint const frag = glCreateShader(GL_FRAGMENT_SHADER);

	if (compile_shader(vert, vert_src) < 0) {
		goto error;
	}

	if (compile_shader(frag, frag_src) < 0) {
		goto error;
	}

	GLuint const program = glCreateProgram();

	glAttachShader(program, vert);
	glAttachShader(program, frag);

	glLinkProgram(program);
	return program;

error:

	glDeleteShader(vert);
	glDeleteShader(frag);

	return 0;
}
