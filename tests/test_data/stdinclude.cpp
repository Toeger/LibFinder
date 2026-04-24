//#include <iostream>
#include <sstream>

void f() {
	//std::cout << "Hello world" << std::endl;
	//std::cout << (std::stringstream{} << 42).str();
	std::stringstream ss{};
	ss << 42;
}
