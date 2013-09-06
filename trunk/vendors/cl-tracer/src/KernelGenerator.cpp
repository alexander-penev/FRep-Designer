#include "KernelGenerator.h"

#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sstream>

KernelGenerator::KernelGenerator()
{
	std::ifstream t("./kernel.cl");

	if(!t.good())
	{
		std::cout << "not found";
	}
	baseSource = std::string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
}


string KernelGenerator::generate(FunctionArray &functions)
{
	string source;
	stringstream traceAllString;
	traceAllString << "\nbool traceAll(float4 ray, float3* normal)\n"
					  "{\n"
					  "\tbool hit = false;\n";
	

	int i = 0;
	for(FunctionArray::const_iterator it = functions.begin(); it != functions.end(); ++it, ++i)
	{
		source.append(generateFunctionSource(*it, i));
		traceAllString << "\thit |= function" << i <<"(ray, normal);\n";
	}
	
	traceAllString << "\t return hit;\n}\n";
	traceAllString << "#define FUNCTION_COUNT " << functions.size() << "\n"; 
	source.append(traceAllString.str());
	return source.append(baseSource);
	
}

string KernelGenerator::generateFunctionSource(const Function &function, int index)
{
	stringstream stream;
	glm::vec3 pos = function.getPosition();
	stream << "\nbool function" << index <<"(float4 ray, float3* normal)\n"
		   << "{\n "

			  "\tfloat value;\n"
			  "\tfloat x = ray.x + " << -pos.x << ";\n"
			  "\tfloat y = ray.y + " << -pos.y << " ;\n"
			  "\tfloat z = ray.z + " << -pos.z << ";\n"
			  "\tvalue = "<<function.getRaw() << ";\n";
    stream << "\t if(value >= 0) {\n";
	stream << generateNormalCalculationCode(function);	   
	stream << "\t}\n";
	stream << "\treturn value > 0;" <<
			  "\n}\n\n";
	return stream.str();	
}


string KernelGenerator::generateNormalCalculationCode(const Function &function)
{
	stringstream s;
	s << "*normal = (float3)(" << function.getNormal() << ");\n";
	cout << s.str() << endl;
	
	return s.str();
}

