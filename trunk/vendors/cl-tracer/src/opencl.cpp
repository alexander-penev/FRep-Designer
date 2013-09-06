#include "opencl.h"
#include "functions.h"
#include "KernelGenerator.h"

#include <CL/cl.h>

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
#include "cl-errors.h"

using namespace std;
using namespace glm;

namespace OpenCL{

cl_int error;
cl_platform_id platform;
cl_device_id device;
cl_uint platforms, devices;
cl_context context;
cl_command_queue command_queue;

struct float3{
	float x,y,z;
};

int init()
{
	cl_uint platformCount;

	// Fetch the Platform and Device IDs; we only want one.
	cl_check_error(clGetPlatformIDs(1, &platform, &platformCount));
	cl_check_error(clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, &device, &devices));

	cl_context_properties properties[]={CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};

	// Note that nVidia's OpenCL requires the platform property
	context=clCreateContext(properties, 1, &device, NULL, NULL, &error); cl_check_error(error);
	command_queue = clCreateCommandQueue(context, device, 0, &error); cl_check_error(error);

	// ------------------ info --------------------------

	cl_platform_id* availablePlatforms = (cl_platform_id*) malloc(sizeof(cl_platform_id) * platformCount);
	cl_check_error(clGetPlatformIDs(platformCount, availablePlatforms, NULL));

	cl_device_id* availableDevices;

	char* value;
	size_t valueSize;
	cl_uint deviceCount;
	cl_uint maxComputeUnits;

	for (int i = 0; i < platformCount; i++) {

		// get all devices
		cl_check_error(clGetDeviceIDs(availablePlatforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &deviceCount));
		availableDevices = (cl_device_id*) malloc(sizeof(cl_device_id) * deviceCount);
		cl_check_error(clGetDeviceIDs(availablePlatforms[i], CL_DEVICE_TYPE_ALL, deviceCount, availableDevices, NULL));

		// for each device print critical attributes
		for (int j = 0; j < deviceCount; j++) {

			// print device name
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DEVICE_NAME, 0, NULL, &valueSize));
			value = (char*) malloc(valueSize);
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DEVICE_NAME, valueSize, value, NULL));
			printf("%d. Device: %s\n", j+1, value);
			free(value);

			// print hardware device version
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DEVICE_VERSION, 0, NULL, &valueSize));
			value = (char*) malloc(valueSize);
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DEVICE_VERSION, valueSize, value, NULL));
			printf(" %d.%d Hardware version: %s\n", j+1, 1, value);
			free(value);

			// print software driver version
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DRIVER_VERSION, 0, NULL, &valueSize));
			value = (char*) malloc(valueSize);
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DRIVER_VERSION, valueSize, value, NULL));
			printf(" %d.%d Software version: %s\n", j+1, 2, value);
			free(value);

			// print c version supported by compiler for device
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DEVICE_OPENCL_C_VERSION, 0, NULL, &valueSize));
			value = (char*) malloc(valueSize);
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DEVICE_OPENCL_C_VERSION, valueSize, value, NULL));
			printf(" %d.%d OpenCL C version: %s\n", j+1, 3, value);
			free(value);

			// print parallel compute units
			cl_check_error(clGetDeviceInfo(availableDevices[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(maxComputeUnits), &maxComputeUnits, NULL));
			printf(" %d.%d Parallel compute units: %d\n", j+1, 4, maxComputeUnits);

		}

		free(availableDevices);

	}
	free(availablePlatforms);

	return CL_SUCCESS;
}


int destroy()
{
	cl_check_error(clReleaseCommandQueue(command_queue));
	cl_check_error(clReleaseContext(context));

	return CL_SUCCESS;
}

} // namespace OpenCL{


