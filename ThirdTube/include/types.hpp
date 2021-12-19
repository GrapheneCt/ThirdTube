#pragma once
#include <string>

struct Result_with_string
{
	std::string string = "[Success] ";
	std::string error_description = "";
	unsigned int code = 0;
};
