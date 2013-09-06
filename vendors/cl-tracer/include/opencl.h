#ifndef OPENCL_H
#define OPENCL_H



#include <CL/cl.h>
#include <CL/cl_gl.h>
#include <stdio.h>
#include <string.h>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <vector>
#include <iostream>
#include <cassert>
#include <streambuf>
#include <string>
#include <glm/glm.hpp>
#include "Function.h"

namespace OpenCL{

struct int2{int x,y;};

extern cl_command_queue command_queue;
extern cl_int error;
extern cl_platform_id platform;
extern cl_device_id device;
extern cl_uint platforms, devices;
extern cl_context context;

int init();
int destroy();
int blur(float* image, uint width, uint height);

std::string getProgramBuildInfoLog();

}

#endif // OPENCL_H
