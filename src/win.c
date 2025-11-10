#include "win.h"
#include "log.h"

#include <EGL/egl.h>
#include <glad/gles2.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>

void gen_pane(win_t* win, float width, float height) {
	// This is sort of a cursed function, I know.

	float const radius = 0.05;

#define M_TAU (2 * M_PI)
#define CORNER_RESOLUTION 16

	assert(CORNER_RESOLUTION >= 2);
	// assert(radius * 2 < width);
	// assert(radius * 2 < height);

	// Vertex buffer: 4 for the centre quad, then $CORNER_RESOLUTION vertices for each of the 4 corners.

	size_t const vertex_count = 4 + 4 * CORNER_RESOLUTION;
	GLfloat buf[vertex_count][8];

	assert(vertex_count <= 256); // Because we're using GLbyte for the indices.

	// Index buffer: 2 tris for the centre quad, 2 tris for each of the 4 edges, and 1 tri for $CORNER_RESOLUTION - 1 for each of the 4 corners.

	size_t const tri_count = 2 + 4 * 2 + 4 * (CORNER_RESOLUTION - 1);
	GLbyte indices[tri_count][3];

	float const centre_width = width - 2 * radius;
	float const centre_height = height - 2 * radius;

	// Add the centre quad.
	// It's normals will be facing us.

	buf[0][0] = centre_width / 2, buf[0][1] = centre_height / 2;
	buf[1][0] = -centre_width / 2, buf[1][1] = centre_height / 2;
	buf[2][0] = -centre_width / 2, buf[2][1] = -centre_height / 2;
	buf[3][0] = centre_width / 2, buf[3][1] = -centre_height / 2;

	for (size_t i = 0; i < 4; i++) {
		buf[i][5] = 0, buf[i][6] = 0, buf[i][7] = 1;
	}

	indices[0][0] = 0, indices[0][1] = 1, indices[0][2] = 2;
	indices[1][0] = 0, indices[1][1] = 2, indices[1][2] = 3;

	// Add corners to edge triangles already.
	// We'll add the ends of the triangle fans for the corners later.

	indices[2][0] = 0, indices[2][1] = 1, indices[3][0] = 1; // Top edge.
	indices[4][0] = 1, indices[4][1] = 2, indices[5][0] = 2; // Left edge.
	indices[6][0] = 2, indices[6][1] = 3, indices[7][0] = 2; // Bottom edge.
	indices[8][0] = 3, indices[8][1] = 0, indices[9][0] = 3; // Right edge.

	// Go through all angles and add vertex data for each vertex.

	size_t buf_off = 4;
	size_t tri_off = 2 + 4 * 2; // Reserve space for edge triangles.

	for (size_t i = 0; i < CORNER_RESOLUTION; i++) {
		float const theta = (float) i / (CORNER_RESOLUTION - 1) * M_TAU / 4;

		float const nx = cos(theta);
		float const ny = sin(theta);

		float const dist_x = centre_width / 2 + radius * nx;
		float const dist_y = centre_height / 2 + radius * ny;

		// Top right.

		buf[buf_off][0] = dist_x, buf[buf_off][1] = dist_y;
		buf[buf_off][5] = nx, buf[buf_off][6] = -ny, buf[buf_off][7] = 0;

		if (i != 0) { // Corner triangle fan.
			indices[tri_off][0] = 0;
			indices[tri_off][1] = buf_off - 4;
			indices[tri_off][2] = buf_off;

			tri_off++;
		}

		if (i == 0) {                               // Edge tris.
			indices[8][2] = indices[9][1] = buf_off; // Right edge.
		}

		if (i == CORNER_RESOLUTION - 1) {           // Edge tris.
			indices[2][2] = indices[3][1] = buf_off; // Top edge.
		}

		buf_off++;

		// Top left.

		buf[buf_off][0] = -dist_x, buf[buf_off][1] = dist_y;
		buf[buf_off][5] = -nx, buf[buf_off][6] = -ny, buf[buf_off][7] = 0;

		if (i != 0) { // Corner triangle fan.
			indices[tri_off][0] = 1;
			indices[tri_off][1] = buf_off - 4;
			indices[tri_off][2] = buf_off;

			tri_off++;
		}

		if (i == 0) {                               // Edge tris.
			indices[4][2] = indices[5][1] = buf_off; // Left edge.
		}

		if (i == CORNER_RESOLUTION - 1) { // Edge tris.
			indices[3][2] = buf_off;       // Top edge.
		}

		buf_off++;

		// Bottom left.

		buf[buf_off][0] = -dist_x, buf[buf_off][1] = -dist_y;
		buf[buf_off][5] = -nx, buf[buf_off][6] = ny, buf[buf_off][7] = 0;

		if (i != 0) { // Corner triangle fan.
			indices[tri_off][0] = 2;
			indices[tri_off][1] = buf_off - 4;
			indices[tri_off][2] = buf_off;

			tri_off++;
		}

		if (i == 0) {               // Edge tris.
			indices[5][2] = buf_off; // Left edge.
		}

		if (i == CORNER_RESOLUTION - 1) { // Edge tris.
			indices[7][1] = buf_off;       // Bottom edge.
		}

		buf_off++;

		// Bottom right.

		buf[buf_off][0] = dist_x, buf[buf_off][1] = -dist_y;
		buf[buf_off][5] = nx, buf[buf_off][6] = ny, buf[buf_off][7] = 0;

		if (i != 0) { // Corner triangle fan.
			indices[tri_off][0] = 3;
			indices[tri_off][1] = buf_off - 4;
			indices[tri_off][2] = buf_off;

			tri_off++;
		}

		if (i == 0) {               // Edge tris.
			indices[9][2] = buf_off; // Right edge.
		}

		if (i == CORNER_RESOLUTION - 1) {           // Edge tris.
			indices[7][2] = indices[6][2] = buf_off; // Bottom edge.
		}

		buf_off++;
	}

	// Everything is at the same Z coord.

	for (size_t i = 0; i < vertex_count; i++) {
		buf[i][2] = 0;
	}

	// Generate texture coordinates from vertex positions.

	for (size_t i = 0; i < vertex_count; i++) {
		float const x = buf[i][0];
		float const y = buf[i][1];

		buf[i][3] = x / (width - radius * 2) + .5;
		buf[i][4] = 1 - (y / (height - radius * 2) + .5);
	}

	// Update GL buffers (because we allocated everything on the stack so gotta do this now).

	glBindBuffer(GL_ARRAY_BUFFER, win->vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof buf, buf, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, win->ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof indices, indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof *buf, 0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof *buf, (void*) 12);
	glEnableVertexAttribArray(1);

	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof *buf, (void*) 20);
	glEnableVertexAttribArray(2);

	win->index_count = tri_count * 3;

#if 0
	// Print to Python lists which we can use for easy debugging.

	printf("buf = [\n\t");

	for (size_t i = 0; i < vertex_count; i++) {
		for (size_t j = 0; j < sizeof *buf / sizeof **buf; j++) {
			printf("%.3f, ", buf[i][j]);
		}

		printf("\n\t");
	}

	printf("]\n");

	printf("indices = [\n\t");

	for (size_t i = 0; i < tri_count; i++) {
		for (size_t j = 0; j < sizeof *indices / sizeof **indices; j++) {
			printf("%d, ", indices[i][j]);
		}

		printf("\n\t");
	}

	printf("]\n");
#endif
}

void win_create(win_t* win) {
	win->rot = 0;
	win->height = 0;
	win->target_height = 1;

	// Create VAO, VBO, and IBO.

	glGenVertexArrays(1, &win->vao);
	glBindVertexArray(win->vao);

	glGenBuffers(1, &win->vbo);
	glGenBuffers(1, &win->ibo);

	// Create window texture.

	glGenTextures(1, &win->tex);
}

void win_destroy(win_t* win) {
	glDeleteTextures(1, &win->tex);
	glDeleteVertexArrays(1, &win->vao);
	glDeleteBuffers(1, &win->vbo);
	glDeleteBuffers(1, &win->ibo);

	free(win->fb_data);
}

void win_render(win_t* win, GLuint uniform) {
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, win->tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, win->x_res, win->y_res, 0, GL_RGBA, GL_UNSIGNED_BYTE, win->fb_data);
	glGenerateMipmap(GL_TEXTURE_2D); // TODO Necessary?
	gen_pane(win, (float) win->x_res / 300, (float) win->y_res / 300);

	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, (float[]) {0, 0, 0, 0});
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glUniform1i(uniform, 1);

	glBindVertexArray(win->vao);
	glDrawElements(GL_TRIANGLES, win->index_count, GL_UNSIGNED_BYTE, NULL);

	win->rot += (win->target_rot - win->rot) * 0.1;
	win->height += (win->target_height - win->height) * 0.1;
}
