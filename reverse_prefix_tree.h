#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace Reverse_prefix_tree {
	struct Writer {
		std::size_t add(std::string_view str);
		void write(std::filesystem::path filepath) const;
		std::string test_read(std::size_t index) const;

		private:
		std::size_t child(std::size_t node_index, char c);

		struct Node {
			char letter;
			std::size_t parent;
			std::vector<std::size_t> children;
		};

		std::vector<Node> nodes{{}};
		std::size_t max_jump = 0;
	};

	struct Reader {
		Reader(std::filesystem::path filepath);
		std::string get(std::size_t index);

		private:
		struct Node {
			char letter;
			std::size_t jump;
		};

		Node next(std::size_t index);
		Node next();

		std::fstream file;
		std::uint8_t jump_size;
		std::size_t element_count;
	};
} // namespace Reverse_prefix_tree
