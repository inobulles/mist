#pragma once

#include <math.h>
#include <string.h>

#include <openxr/openxr.h>

typedef float matrix_t[4][4];

static inline void matrix_identity(matrix_t x) {
	memset(x, 0, sizeof(matrix_t)); // C doesn't like me getting the size of array parameters (even though their size is explicitly defined)

	x[0][0] = 1.0;
	x[1][1] = 1.0;
	x[2][2] = 1.0;
	x[3][3] = 1.0;
}

static inline void matrix_copy(matrix_t dest, matrix_t src) {
	memcpy(dest, src, sizeof(matrix_t)); // same as with matrix_identity
}

static inline void matrix_multiply(matrix_t out, matrix_t x, matrix_t y) {
	matrix_t res;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
#define L(index) (x[(index)][j] * y[i][(index)])
			res[i][j] = L(0) + L(1) + L(2) + L(3);
#undef L
		}
	}

	matrix_copy(out, res);
}

static inline void matrix_scale(matrix_t matrix, float vector[3]) {
	for (int i = 0; i < 3; i++) {    // each scaling vector dimension
		for (int j = 0; j < 4; j++) { // each matrix dimension
			matrix[i][j] *= vector[i];
		}
	}
}

static inline void matrix_translate(matrix_t matrix, float vector[3]) {
	float x = vector[0];
	float y = vector[1];
	float z = vector[2];

	for (int i = 0; i < 4; i++) {
		matrix[3][i] += matrix[0][i] * x + matrix[1][i] * y + matrix[2][i] * z;
	}
}

static inline void matrix_rotate(matrix_t matrix, float angle, float vector[3]) {
	float x = vector[0];
	float y = vector[1];
	float z = vector[2];

	float magnitude = sqrt(x * x + y * y + z * z);

	x /= -magnitude;
	y /= -magnitude;
	z /= -magnitude;

	float s = sin(angle);
	float c = cos(angle); // TODO possible optimization
	float one_minus_c = 1 - c;

	float xx = x * x, yy = y * y, zz = z * z;
	float xy = x * y, yz = y * z, zx = z * x;
	float xs = x * s, ys = y * s, zs = z * s;

	matrix_t rotation_matrix = {}; // no need for this to be the identity matrix

	rotation_matrix[0][0] = (one_minus_c * xx) + c;
	rotation_matrix[0][1] = (one_minus_c * xy) - zs;
	rotation_matrix[0][2] = (one_minus_c * zx) + ys;

	rotation_matrix[1][0] = (one_minus_c * xy) + zs;
	rotation_matrix[1][1] = (one_minus_c * yy) + c;
	rotation_matrix[1][2] = (one_minus_c * yz) - xs;

	rotation_matrix[2][0] = (one_minus_c * zx) - ys;
	rotation_matrix[2][1] = (one_minus_c * yz) + xs;
	rotation_matrix[2][2] = (one_minus_c * zz) + c;

	rotation_matrix[3][3] = 1;
	matrix_multiply(matrix, matrix, rotation_matrix);
}

static inline void matrix_rotate_quat(matrix_t matrix, float q[4]) {
	float x = q[0];
	float y = q[1];
	float z = q[2];
	float w = q[3];

	float xx = x * x;
	float yy = y * y;
	float zz = z * z;
	float xy = x * y;
	float xz = x * z;
	float yz = y * z;
	float wx = w * x;
	float wy = w * y;
	float wz = w * z;

	matrix_t rotation_matrix = {};

	rotation_matrix[0][0] = 1.0f - 2.0f * (yy + zz);
	rotation_matrix[0][1] = 2.0f * (xy - wz);
	rotation_matrix[0][2] = 2.0f * (xz + wy);

	rotation_matrix[1][0] = 2.0f * (xy + wz);
	rotation_matrix[1][1] = 1.0f - 2.0f * (xx + zz);
	rotation_matrix[1][2] = 2.0f * (yz - wx);

	rotation_matrix[2][0] = 2.0f * (xz - wy);
	rotation_matrix[2][1] = 2.0f * (yz + wx);
	rotation_matrix[2][2] = 1.0f - 2.0f * (xx + yy);

	rotation_matrix[3][3] = 1.0f;

	matrix_multiply(matrix, matrix, rotation_matrix);
}

static inline void matrix_rotate_2d(matrix_t matrix, float vector[2]) {
	float x = vector[0];
	float y = vector[1];

	matrix_rotate(matrix, x, (float[]) {0, 1, 0});
	matrix_rotate(matrix, -y, (float[]) {cos(x), 0, sin(x)});
}

static inline void matrix_frustum(matrix_t matrix, float tan_left, float tan_right, float tan_up, float tan_down, float near, float far) {
	float const dx = tan_right - tan_left;
	float const dy = tan_up - tan_down;

	memset(matrix, 0, sizeof(matrix_t));

	matrix[0][0] = 2.0f / dx;
	matrix[2][0] = (tan_right + tan_left) / dx;

	matrix[1][1] = 2.0f / dy;
	matrix[2][1] = (tan_up + tan_down) / dy;

	matrix[2][2] = -(far + near) / (far - near);
	matrix[3][2] = -(2 * far * near) / (far - near);

	matrix[2][3] = -1.0f;
}

static inline void matrix_perspective(matrix_t matrix, XrFovf fov, float near, float far) {
	float const tan_left = tanf(fov.angleLeft);
	float const tan_right = tanf(fov.angleRight);
	float const tan_down = tanf(fov.angleDown);
	float const tan_up = tanf(fov.angleUp);

	matrix_frustum(matrix, tan_left, tan_right, tan_up, tan_down, near, far);
}

static inline void matrix_transform(matrix_t out, float pos[3], float rot[2], float scale[3]) {
	matrix_identity(out);
	matrix_translate(out, pos);
	matrix_scale(out, scale);
	matrix_rotate_2d(out, rot);
}
