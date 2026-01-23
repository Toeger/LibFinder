#include "utility.h"

#include <memory>
#include <mutex>

static std::mutex popen_mutex;

std::string get_output_from_command(std::string_view command) {
	std::unique_ptr<FILE, decltype([](FILE *f) { pclose(f); })> fp{nullptr};
	{
		std::lock_guard<std::mutex> popen_lock(popen_mutex); //unfortunately popen doesn't seem to be thread safe
		fp.reset(popen(command.data(), "r"));
	}
	if (!fp) {
		return {};
	}
	std::string buffer;
	const int buffersize = 1024;
	for (;;) {
		buffer.resize(buffer.size() + buffersize);
		std::size_t read = fread(&buffer[buffer.size() - buffersize], sizeof *buffer.data(), buffersize, fp.get());
		if (read < buffersize) {
			buffer.resize(buffer.size() - buffersize + read);
			break;
		}
	}
	return buffer;
}
