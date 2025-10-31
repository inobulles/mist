#pragma once

#include <glad/gles2.h>

typedef struct {
	GLuint tex;
	GLuint vao;
	GLuint vbo;
	GLuint ibo;
} win_t;

void win_create(win_t* win);
void win_destroy(win_t* win);
void win_render(win_t* win);
