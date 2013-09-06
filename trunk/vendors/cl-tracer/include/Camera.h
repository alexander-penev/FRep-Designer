#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>



class Camera
{
public:
	Camera();
	
	glm::vec4 *getMatrix();
	
	glm::vec3 position;
	glm::vec3 target;

private:
	glm::vec4 matrix[4];
};

#endif // CAMERA_H
