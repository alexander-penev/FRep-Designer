#include <iostream>
#include <ginac/ginac.h>
#include "opencl.h"
#include <glm/gtx/rotate_vector.hpp>
#include "KernelGenerator.h"
#include "Tracer.h"
#include "Camera.h"

#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

using namespace std;
using namespace GiNaC;
using namespace glm;


struct uchar4{
	unsigned char r,g,b,a;
};


int main(void)
{
	OpenCL::init();

	Camera cam;
	cam.position = vec3(50, 50, -40);
	cam.target = vec3(0,0,0);

	uint width = 800;
	uint height = 700;
	uchar4 blurredImage[width*height];

	FunctionArray functions;
	Function shpere = Function("5*5 - (x)*(x) - (y)*(y)- (z*z)");
	shpere.setPosition(glm::vec3(0, 0,30));
	//functions.push_back(Function("-(pow((6.5 - sqrt(x*x + y*y)), 2) + z*z - 8)"));
//	functions.push_back(Function("5*5 - (x)*(x)*sin(x) - (y)*(y)/3- (z*z)"));
	functions.push_back(shpere);

	Tracer tracer;
	tracer.setCamera(cam.getMatrix());
	tracer.setFunctions(functions);
	tracer.resize(width, height);
	tracer.refreshSettings();
	tracer.trace((unsigned char*)blurredImage);


	std::ofstream ofs("out.ppm", std::ios::out | std::ios::binary);
	ofs << "P6\n" << width << " " << height << "\n255\n";
	for (unsigned i = 0; i < width * height; ++i) {
		ofs << (unsigned char)blurredImage[i].r <<
		(unsigned char)blurredImage[i].g<<
		(unsigned char) blurredImage[i].b;
	}


	ofs.close();

	OpenCL::destroy();

	return 0;
}

