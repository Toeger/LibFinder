#include <iostream>
#include <boost/filesystem.hpp>

int main(){
	boost::filesystem::path libdir("/usr/lib");
	for (auto &file : boost::filesystem::directory_iterator(libdir)){
		if (file.path().extension() != ".so")
			continue;
		std::cout << file << '\n';
		system(("nm " + file.path().string()).c_str());
	}
}
