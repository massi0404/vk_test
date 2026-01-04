#pragma once

#include <functional>
#include <deque>

class DeletionQueue
{
public:
	DeletionQueue() = default;

	~DeletionQueue()
	{
		Flush();
	}

	void PushBack(std::function<void()>&& function)
	{
		m_DeletionQueue.emplace_back(function);
	}

	void Flush()
	{
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = m_DeletionQueue.rbegin(); it != m_DeletionQueue.rend(); it++)
			(*it)(); //call functors

		m_DeletionQueue.clear();
	}

private:
	std::deque<std::function<void()>> m_DeletionQueue;
};