#include "test_radix_tree.h"
#include "radix_tree.h"
#include "asserts.h"

#include <vector>
#include <string>

void test_radix_tree()
{
	const std::vector<std::vector<std::string>> test_cases = {
		{"hello"},
		{"hello", "world"},
		{"hello", "world", "hello world"},
		{"hello", "world", "hello world", "world hello"},
		{"a", "aa", "aaa", "aaaa", "aaaaa"},
		{"aaaaa", "aaaa", "aaa", "aa", "a"},
		{"aa", "ba", "ca", "da", "ea", "fa"},
		{"aa", "ab", "ac", "ad", "ae", "af"},
	};

	for (auto &test_case : test_cases){
		Radix_tree rt;
		for (auto &key : test_case){
			rt.insert(key, "value: " + key);
		}
		for (auto &key : test_case){
			assert_equal(rt.find(key), "value: " + key);
		}
	}
	Radix_tree rt;
	for (auto &test_case : test_cases){
		for (auto &key : test_case){
			rt.insert(key, "value: " + key);
		}
		for (auto &key : test_case){
			assert_equal(rt.find(key), "value: " + key);
		}
	}
	for (auto &test_case : test_cases){
		for (auto &key : test_case){
			assert_equal(rt.find(key), "value: " + key);
		}
	}
}
