#ifndef KERNELGENERATOR_H
#define KERNELGENERATOR_H

#include "Function.h"

class KernelGenerator
{
public:
	KernelGenerator();
	
	string generate(FunctionArray& functions);
	
private:
	string baseSource;
	
	string generateFunctionSource(const Function &function, int index);
	string generateNormalCalculationCode(const Function &function);
};

#endif // KERNELGENERATOR_H
