#include "platform.h"
#include "win.h"

void platform_create(platform_t* p) {
	glGenVertexArrays(1, &p->vao);
	glBindVertexArray(p->vao);

	glGenBuffers(1, &p->vbo);
	glGenBuffers(1, &p->ibo);

	win_t win = {
		.vao = p->vao,
		.vbo = p->vbo,
		.ibo = p->ibo,
	};

	gen_pane(&win, 5, 5);
	p->index_count = win.index_count;

	// TODO Read texture.
}

void platform_render(platform_t* p, GLuint uniform) {
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUniform1i(uniform, 1);

	glBindVertexArray(p->vao);
	glDrawElements(GL_TRIANGLES, p->index_count, GL_UNSIGNED_BYTE, NULL);
}
