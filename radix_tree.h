#ifndef RADIX_TREE_H
#define RADIX_TREE_H

/*
 * This radix tree shall implement a std::map<std::string, std::string> in a more efficient way.
 * It is more memory efficient by saving common prefixes only once in both the key and the value.
 * Nodes in the tree need to have stable iterators and it must be possible to reconstruct the key from a Node.
 * The tree needs to be serialized to disk.
 * Lookups need to be efficient on a serialized tree.
 * The memory representation does not need to be efficient.
 */

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