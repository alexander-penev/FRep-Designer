#ifndef FUNCTION_H
#define FUNCTION_H
#include <ginac/ginac.h>
#include <glm/glm.hpp>


#include <string>
#include <vector>


using namespace GiNaC;
using namespace std;

class Function
{
public:
	Function(string function);
	
	string getRaw() const;
	
	string getNormal() const;
	
	void setPosition(glm::vec3 pos);
	glm::vec3 getPosition() const;
	
private:
	string function;
	string normal;
	glm::vec3 position;
};

typedef vector<Function> FunctionArray;

#endif // FUNCTION_H
