#include "reverse_prefix_tree.h"
#include "asserts.h"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <string>

std::size_t Reverse_prefix_tree::Writer::add(std::string_view str) {
	std::size_t current_node = 0;
	for (auto letter : str) {
		current_node = child(current_node, letter);
	}
	return current_node;
}

void Reverse_prefix_tree::Writer::write(std::filesystem::path filepath) const {
	std::ofstream file{filepath, std::ios_base::out | std::ios::binary};
	std::uint8_t jump_bytes = 0;
	for (auto max_jump_copy = max_jump; max_jump_copy; max_jump_copy >>= 8) {
		jump_bytes++;
	}
	file << jump_bytes;
	auto write_jump = [jump_bytes, &file](std::size_t amount) {
		for (auto bytes = jump_bytes; bytes; bytes--) {
			std::uint8_t byte = amount & 0xFF;
			amount >>= 8;
			file.write(reinterpret_cast<const char *>(&byte), 1);
		}
		assume(amount == 0);
	};
	std::size_t index = std::size(nodes);
	for (auto &node : nodes | std::views::reverse) {
		file << node.letter;
		write_jump(--index - node.parent);
	}
}

std::string Reverse_prefix_tree::Writer::test_read(std::size_t index) const {
	std::string result;
	while (index) {
		result += nodes[index].letter;
		index = nodes[index].parent;
	}
	return {std::rbegin(result), std::rend(result)};
}

std::size_t Reverse_prefix_tree::Writer::child(std::size_t node_index, char c) {
	Node &node = nodes[node_index];
	for (auto child_index : node.children) {
		if (nodes[child_index].letter == c) {
			return child_index;
		}
	}
	auto new_index = nodes.size();
	node.children.push_back(new_index);
	nodes.push_back({.letter = c, .parent = node_index, .children = {}});
	max_jump = std::max(max_jump, new_index - node_index);
	return new_index;
}

Reverse_prefix_tree::Reader::Reader(std::filesystem::path filepath)
	: file{filepath, std::ios_base::binary | std::ios_base::in} {
	file >> jump_size;
	file.seekg(0, std::ios_base::end);
	element_count = file.tellg();
	//element_count -= 1; //unnecessary due to rounding below
	element_count /= (jump_size + 1);
}

std::string Reverse_prefix_tree::Reader::get(std::size_t index) {
	std::string retval;
	for (Node node = next(index); node.letter; node = next()) {
		retval.push_back(node.letter);
		if (node.jump != 1) {
			file.seekg((node.jump - 1) * (1 + jump_size), std::ios_base::cur);
		}
	}
	std::reverse(std::begin(retval), std::end(retval));
	return retval;
}

Reverse_prefix_tree::Reader::Node Reverse_prefix_tree::Reader::next() {
	Node node{};
	file >> node.letter;
	for (int i = 0; i < jump_size; i++) {
		std::uint8_t jump_byte;
		file >> jump_byte;
		node.jump <<= 8;
		node.jump |= jump_byte;
	}
	return node;
}

Reverse_prefix_tree::Reader::Node Reverse_prefix_tree::Reader::next(std::size_t index) {
	auto pos = 1 + (element_count - index - 1) * (jump_size + 1);
	file.seekg(pos);
	return next();
}
