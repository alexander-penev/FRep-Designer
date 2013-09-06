#include "Camera.h"

using namespace glm;

Camera::Camera()
{
}

void print(const vec4& vec)
{
	printf("[%f, %f, %f, %f]\n", vec.x, vec.y, vec.z, vec.w);
}

void print(const vec3& vec)
{
	printf("[%f, %f, %f]\n", vec.x, vec.y, vec.z);
}

vec4 *Camera::getMatrix()
{
	vec3 z = normalize(target - position);
	vec3 x = normalize(vec3(z.y, -z.x, 0));
	vec3 y = cross(z, x);
	
	matrix[0] = vec4(x.x, y.x, z.x, 1);
	matrix[1] = vec4(x.y, y.y, z.y, 1);
	matrix[2] = vec4(x.z, y.z, z.z, 1);
	
//	matrix[0] = vec4(x, 1);
//	matrix[1] = vec4(y, 1);
//	matrix[2] = vec4(z, 1);

	matrix[3] = vec4(position, 1);

//	print(matrix[0]);
//	print(matrix[1]);
//	print(matrix[2]);
//	print(matrix[3]);

//	mat3 mat(vec3(x.x, y.x, z.x),
//		   vec3(x.y, y.y, z.y),
//		   vec3(x.z, y.z, z.z));

	mat3 mat(x, y, z);
	vec3 res = mat * vec3(0, 0, 1);
	print(res);
	
	return matrix;
	
}
