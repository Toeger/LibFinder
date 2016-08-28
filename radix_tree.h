#ifndef RADIX_TREE_H
#define RADIX_TREE_H

#include <experimental/string_view>
#include <string>
#include <vector>

using string_view = std::experimental::string_view;

struct Radix_tree {
	void insert(string_view key, string_view value);
	string_view find(string_view key) const;
	void shrink_to_fit();

	private:
	struct Node {
		std::string key_part;
		std::string value;
		std::vector<Node> children;
		void insert(string_view key, string_view value);
		string_view find(string_view key) const;
		void split(std::size_t pos);
		void assert_invariants() const;
		void shrink_to_fit();
	};
	std::vector<Node> children;
};

#endif // RADIX_TREE_H