#ifndef TRACER_H
#define TRACER_H

#ifdef __APPLE__
#include <OpenCL/cl.h>
#include <OpenCL/cl_gl.h>
#else
#include <CL/cl.h>
#include <CL/cl_gl.h>
#endif

#include <glm/glm.hpp>
#include "Function.h"
#include "KernelGenerator.h"
#include "opencl.h"
#include "cl-errors.h"

class Tracer
{
public:
	Tracer();

	int trace(unsigned char *image);

	void resize(uint width, uint height);
	void setFunctions(FunctionArray &functions);
	void refreshSettings();
	
	void setCamera(glm::vec4 *camera);
private:
	std::string getProgramBuildInfoLog(cl_program& prog);
	cl_program createProgramFromSource(string str);
	void generateRays();

	cl_program prog;
	cl_kernel kernel;

	uint width, height;
	cl_mem rayMem;
	cl_mem kernelOutputMem;
	cl_mem clCameraMatrix;
	KernelGenerator kernelGenerator;
	glm::vec4* rays;
	glm::vec4* camera;
};

#endif // TRACER_H
