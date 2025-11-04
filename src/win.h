#pragma once

#include <glad/gles2.h>

#include <stdbool.h>

typedef struct {
	bool created;
	uint32_t id;

	uint32_t x_res;
	uint32_t y_res;
	void* fb_data;

	GLuint tex;
	GLsizei index_count;
	GLuint vao;
	GLuint vbo;
	GLuint ibo;
} win_t;

void win_create(win_t* win);
void win_destroy(win_t* win);
void win_update_tex(win_t* win, uint32_t x_res, uint32_t y_res, void const* fb_data);
void win_render(win_t* win, GLuint uniform);
