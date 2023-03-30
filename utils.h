#pragma once

#include <map>

template<typename T, typename U>
U get_from_map(std::map<T, U> the_map, T key, U default_value) {
	if (the_map.count(key) > 0) {
		return the_map[key];
	}

	return default_value;
}