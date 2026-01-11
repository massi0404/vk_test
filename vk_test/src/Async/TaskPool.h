#pragma once

#include <thread>
#include <functional>
#include <mutex>
#include <deque>
#include <vector>
#include <condition_variable>

struct ThreadContext
{
	int threadIndex;
};

class TaskPool
{
public:
	using Task = std::function<void()>;

public:
	TaskPool() = default;

	void Start(uint32_t threadCount)
	{
		m_Workers.reserve(threadCount);
		for (uint32_t i = 0; i < threadCount; i++)
		{
			m_Workers.emplace_back([this, i]() {
				while (true)
				{
					std::unique_lock<std::mutex> lck(m_QueueMutex);
					m_CondVar.wait(lck, [this]() { return !m_TaskQueue.empty() || m_Stop; });

					if (m_Stop)
					{
						lck.unlock();
						break;
					}

					Task task = std::move(m_TaskQueue.front());
					m_TaskQueue.pop_front();
					m_DequeuedTasks++;
					lck.unlock();

					// task({i})? puo essere utile per alcuni sistemi che utilizzano le risorse separate per thread?
					task();
				}
			});
		}
	}

	~TaskPool()
	{
		Stop();
	}

	template<typename T>
	void AddTask(T taskProc)
	{
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		m_TaskQueue.emplace_back(taskProc);
		m_CondVar.notify_one();
	}

	void RequestStop()
	{
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		m_Stop = true;
		m_CondVar.notify_all();
	}

	void Stop()
	{
		RequestStop();

		for (auto& worker : m_Workers)
		{
			if (worker.joinable())
				worker.join();
		}
	}

	inline uint32_t NumWorkers() const { return (uint32_t)m_Workers.size(); }

private:
	std::vector<std::thread> m_Workers;
	std::deque<Task> m_TaskQueue;
	std::mutex m_QueueMutex;
	std::condition_variable m_CondVar;

	uint32_t m_DequeuedTasks = 0; // debug
	bool m_Stop = false;
};