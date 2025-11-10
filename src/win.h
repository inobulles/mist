#pragma once

#include <glad/gles2.h>

#include <stdbool.h>

typedef struct {
	bool created;
	bool destroyed;

	uint32_t id;

	uint32_t x_res;
	uint32_t y_res;
	void* fb_data;

	GLuint tex;
	GLsizei index_count;
	GLuint vao;
	GLuint vbo;
	GLuint ibo;

	float rot;
	float target_rot;

	float height;
	float target_height;
} win_t;

void gen_pane(win_t* win, float width, float height);

void win_create(win_t* win);
void win_destroy(win_t* win);
void win_render(win_t* win, GLuint uniform);
