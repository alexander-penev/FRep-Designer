#include "Tracer.h"

using namespace std;
using namespace glm;
using namespace OpenCL;

Tracer::Tracer()
{
	rays = 0;
	width = 0;
	height = 0;
}

int Tracer::trace(unsigned char* image)
{
	size_t bufSize = width*height*4;
	size_t workSize[] = {width, height};

	// Perform the operation
	cl_check_error(clEnqueueNDRangeKernel(command_queue, kernel, 2, NULL, workSize, 0, 0, NULL, NULL));
	// Read the result back into buf2
	cl_check_error(clEnqueueReadBuffer(command_queue, kernelOutputMem, CL_FALSE, 0, bufSize, image, 0, NULL, NULL));
	// Await completion of all the above
	cl_check_error(clFinish(command_queue));

	return CL_SUCCESS;
}

void Tracer::resize(uint width, uint height)
{
	if(width != this->width || height != this->height)
	{
		this->width = width;
		this->height = height;
		delete [] rays;
		rays = new vec4[width*height];
		generateRays();
		rayMem = clCreateBuffer(context, CL_MEM_READ_ONLY, width*height*4*sizeof(float), NULL, &error); cl_check_error(error);
		kernelOutputMem = clCreateBuffer(context, CL_MEM_WRITE_ONLY, width*height*4, NULL, &error); cl_check_error(error);
		
	}
}

void Tracer::setCamera(vec4* camera)
{
	this->camera = camera;
	clCameraMatrix = clCreateBuffer(context, CL_MEM_READ_ONLY, 16*sizeof(float), NULL, &error); cl_check_error(error);
}

void Tracer::setFunctions(FunctionArray& functions)
{
	prog = createProgramFromSource(kernelGenerator.generate(functions).c_str());
	// get a handle and map parameters for the kernel
	kernel = clCreateKernel(prog, "trace", &error); cl_check_error(error);
}

void Tracer::refreshSettings()
{
	cl_check_error(clSetKernelArg(kernel, 0, sizeof(rayMem), &rayMem));
	cl_check_error(clSetKernelArg(kernel, 1, sizeof(kernelOutputMem), &kernelOutputMem));
	cl_check_error(clSetKernelArg(kernel, 2, sizeof(clCameraMatrix), &clCameraMatrix));
	// Send rays to the input buffer
	cl_check_error(clEnqueueWriteBuffer(command_queue, rayMem, CL_FALSE, 0, width*height*4*sizeof(float), rays, 0, NULL, NULL));
	cl_check_error(clEnqueueWriteBuffer(command_queue, clCameraMatrix, CL_FALSE, 0, 16*sizeof(float), camera, 0, NULL, NULL));
}

string Tracer::getProgramBuildInfoLog(cl_program& prog)
{
	// Determine the size of the log
	size_t log_size;
	cl_check_error(clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size));

	// Allocate memory for the log
	char *log = (char *) malloc(log_size);

	// Get the log

	cl_check_error(clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL));

	string res(log);
	free(log);

	return res;
}

cl_program Tracer::createProgramFromSource(string str)
{
	const char *src = str.c_str();
	size_t srcsize = str.size();

	const char *srcptr[]={src};
	// Submit the source code of the rot13 kernel to OpenCL
	cl_program prog=clCreateProgramWithSource(context, 1, srcptr, &srcsize, &error);
	cl_check_error(error);
	// and compile it (after this we could extract the compiled version)
	cl_check_error(clBuildProgram(prog, 0, NULL, "", NULL, NULL));

	string log = getProgramBuildInfoLog(prog);
	cout << log;

	return prog;
}

void Tracer::generateRays()
{
	vec4* pixel = rays;
	float invWidth = 1 / (float)(width);
	float invHeight = 1 / (float)(height);
	float fov = 45;
	float aspectratio = width / (float)(height);
	float angle = tan(M_PI * 0.5 * fov / (float)(180));

	// Trace rays
	for (unsigned y = 0; y < height; ++y) {
		for (unsigned x = 0; x < width; ++x, ++pixel) {
			float xx = (2 * ((x + 0.5) * invWidth) - 1) * angle * aspectratio;
			float yy = (1 - 2 * ((y + 0.5) * invHeight)) * angle;
			*pixel = vec4(normalize(vec3(xx * 1, yy * 1, 1.f)), 0.f);
			normalize(pixel);
		}
	}
}
