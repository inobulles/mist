#pragma once

#include <glad/gles2.h>

typedef struct {
	GLuint tex;
	GLsizei index_count;
	GLuint vao;
	GLuint vbo;
	GLuint ibo;
} platform_t;

// TODO The idea is that we'd have a "glass" platform below the user.
// The loaded texture would be a roughness map which would select between refracting/reflecting the blurred equirectangular map or the unblurred one.

void platform_create(platform_t* p);
void platform_render(platform_t* p, GLuint uniform);
