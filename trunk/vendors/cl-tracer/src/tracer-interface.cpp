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


extern "C" {



extern void init()
{
	OpenCL::init();
}

extern void destroy()
{
	OpenCL::destroy();
}

// not sure if we need this but in case the gui is multithreaded
static bool locked = false;

extern void getImage(unsigned char* image, int width, int height)
{
	if(locked)
		return;

	locked = true;
/*
	srand (time(NULL));
	uchar4* it = (uchar4*)image;

	for(int h = 0; h < height; h++)
	{
		for(int w = 0; w < width; w++, it++)
		{
			it->r = rand() % 255;
		}
	}

	cerr << "Size: " <<  width << 'x' << height << endl;
*/
	Camera cam;
	cam.position = vec3(1, 1, -40);
	cam.target = vec3(0,0,0);
	
	

	FunctionArray functions;
	Function shpere = Function("5*5 - (x)*(x) - (y)*(y)- (z*z)");
	shpere.setPosition(glm::vec3(0, 0, 30));
//	functions.push_back(Function("-(pow((6.5 - sqrt(x*x + y*y)), 2) + z*z - 8)"));
	functions.push_back(Function("5*5 - (x)*(x)*sin(x) - (y)*(y)/3- (z*z)"));
	functions.push_back(shpere);
	
	Tracer tracer;
	tracer.setCamera(cam.getMatrix());
	tracer.setFunctions(functions);
	tracer.resize(width, height);
	tracer.refreshSettings();
	tracer.trace(image);

	locked = false;
}

};
