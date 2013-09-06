#include "Function.h"
#include <sstream>

Function::Function(string function)
{
	this->function = function;
	symbol x("x"), y("y"), z("z");
	ex f(function, lst(x, y, z));
	stringstream normalStream;
	normalStream << csrc << f.diff(x) << ", " << f.diff(y) << ", " << f.diff(z) << dflt;
	normal = normalStream.str();
}

string Function::getRaw() const
{
	return function;
}

string Function::getNormal() const
{
	return normal;
}

void Function::setPosition(glm::vec3 pos)
{
	position = pos;
}

glm::vec3 Function::getPosition() const
{
	return position;
}
