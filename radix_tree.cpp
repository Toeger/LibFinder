#include "radix_tree.h"

#include <algorithm>
#include <cassert>

static std::size_t get_common_prefix_length(string_view lhs, string_view rhs) {
	std::size_t length = 0;
	while (lhs.size() && rhs.size() && lhs.front() == rhs.front()) {
		//TODO: make this loop better
		length++;
		lhs.remove_prefix(1);
		rhs.remove_prefix(1);
	}
	return length;
}

void Radix_tree::insert(string_view key, string_view value) {
	assert(!key.empty()); //not sure what to do in this case yet
	for (auto &child : children) {
		if (child.key_part.front() == key.front()) {
			child.insert(key, value);
			return;
		}
	}
	children.push_back({key.data(), value.data(), {}});
}

string_view Radix_tree::find(string_view key) const {
	assert(!key.empty()); //not sure what to do in this case yet
	for (auto &child : children) {
		if (child.key_part.front() == key.front()) {
			return child.find(key);
		}
	}
	return {};
}

void Radix_tree::shrink_to_fit() {
	children.shrink_to_fit();
	for (auto &child : children) {
		child.shrink_to_fit();
	}
}

void Radix_tree::Node::insert(string_view key, string_view value) {
	assert(!key.empty());
	auto common_prefix_length = get_common_prefix_length(key, key_part);
	if (common_prefix_length > 0 && common_prefix_length < key_part.size()) { //need to split this node
		split(common_prefix_length);
		if (common_prefix_length == key.size()) {
			this->value = value.data();
			return;
		}
	} else if (key == key_part) { //updating value
		this->value = value.data();
		return;
	}
	key.remove_prefix(common_prefix_length);
	for (auto &child : children) {
		if (child.key_part.front() == key.front()) {
			child.insert(key, value); //delegate to child
			return;
		}
	}
	children.push_back({key.data(), value.data(), {}}); //create new child
}

string_view Radix_tree::Node::find(string_view key) const {
	if (key.size() < key_part.size()) { //key does not exist
		return {};
	}
	if (key.size() > key_part.size()) {
		if (key.substr(0, key_part.size()) == key_part) {
			key.remove_prefix(key_part.size());
			for (auto &child : children) {
				if (child.key_part.front() == key.front()) {
					return child.find(key); //matching child, delegate
				}
			}
			return {}; //no matching child
		}
		return {}; //no match
	}
	if (key == key_part) {
		return value; //match
	}
	return {}; //no match
}

void Radix_tree::Node::split(std::size_t pos) {
	assert(pos > 0 && pos < key_part.size());
	Node n{{key_part.begin() + pos, key_part.end()}, std::move(value), std::move(children)};
	value.clear();
	children.clear();
	key_part = key_part.substr(0, pos);
	children.push_back(std::move(n));
}

void Radix_tree::Node::assert_invariants() const {
	std::string starts;
	starts.reserve(children.size());
	for (auto &child : children) {
		assert(!child.key_part.empty());
		starts.push_back(child.key_part.front());
	}
	std::sort(begin(starts), end(starts));
	assert(std::unique(begin(starts), end(starts)) == end(starts));
}

void Radix_tree::Node::shrink_to_fit() {
	key_part.shrink_to_fit();
	value.shrink_to_fit();
	children.shrink_to_fit();
	for (auto &child : children) {
		child.shrink_to_fit();
	}
}
