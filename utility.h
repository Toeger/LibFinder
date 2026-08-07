#ifndef UTILITY_H
#define UTILITY_H

#include <filesystem>
#include <string>
#include <vector>

std::string get_output_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
std::string get_error_from_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");
std::pair<int, std::string> run_command(const char *command, std::vector<std::string> argv = {}, std::filesystem::path working_directory = ".");

template <class F, class... Args>
concept Callback_Function = requires(F &&f) {
	{ f(std::declval<Args>()...) } -> std::convertible_to<bool>;
} or requires(F &&f) {
	{ f(std::declval<Args>()...) } -> std::same_as<void>;
};

template <class... Args>
bool call_callback_function(Callback_Function<Args...> auto &&callback, Args... args) {
	if constexpr (std::is_void_v<decltype(callback(std::forward<Args>(args)...))>) {
		callback(std::forward<Args>(args)...);
		return true;
	} else {
		return callback(std::forward<Args>(args)...);
	}
}

#endif // UTILITY_H
