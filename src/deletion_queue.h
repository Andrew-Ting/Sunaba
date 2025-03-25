#pragma once

#include <deque>
#include <functional>

// this struct is very small, so making a separate cpp file didn't feel justified
struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}
	void flush() {
		// reverse iterate the deletion queue and execute all the callback functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};