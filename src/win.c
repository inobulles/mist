#include "win.h"
#include "log.h"

#include <EGL/egl.h>
#include <glad/gles2.h>

// clang-format off
static GLfloat quad[] = {
	-.5,  .5, 0, 0, 1, // 0, 0, 1,
	-.5, -.5, 0, 0, 0, // 0, 0, 1,
	 .5, -.5, 0, 1, 0, // 0, 0, 1,
	 .5,  .5, 0, 1, 1, // 0, 0, 1,
};

static GLbyte indices[] = {
	0, 1, 2,
	0, 2, 3,
};
// clang-format on

void win_create(win_t* win) {
	// Create VAO.

	glGenVertexArrays(1, &win->vao);
	glBindVertexArray(win->vao);

	// Create VBO.

	glGenBuffers(1, &win->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, win->vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof *quad * 5, 0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof *quad * 5, 0);
	glEnableVertexAttribArray(1);

	// glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 8, 0);
	// glEnableVertexAttribArray(2);

	// Create IBO.

	glGenBuffers(1, &win->ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, win->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);
}

void win_destroy(win_t* win) {
	glDeleteTextures(1, &win->tex);
	glDeleteVertexArrays(1, &win->vao);
	glDeleteBuffers(1, &win->vbo);
	glDeleteBuffers(1, &win->ibo);
}

void win_render(win_t* win) {
	glBindVertexArray(win->vao);
	glDrawElements(GL_TRIANGLES, sizeof indices / sizeof *indices, GL_UNSIGNED_BYTE, NULL);
}
