#ifndef CLERRORS_H
#define CLERRORS_H

const char* cl_get_error(int error);

void cl_error(int error, const char* file, unsigned long int line);

#define cl_check_error(x) cl_error(x, __FILE__, __LINE__)

#endif // CLERRORS_H
