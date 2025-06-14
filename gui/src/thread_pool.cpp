/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// This file was taken from the tev image viewer and is re-released here
// under the NVIDIA Source Code License with permission from the author.

#include <neural-graphics-primitives/common.h>
#include <neural-graphics-primitives/thread_pool.h>

#include <chrono>

namespace ngp {

ThreadPool::ThreadPool()
: ThreadPool{std::thread::hardware_concurrency()} {}

ThreadPool::ThreadPool(size_t max_num_threads, bool force) {
	if (!force) {
		max_num_threads = std::min((size_t)std::thread::hardware_concurrency(), max_num_threads);
	}
	start_threads(max_num_threads);
}

ThreadPool::~ThreadPool() {
	wait_until_queue_completed();
	shutdown_threads(m_threads.size());
}

void ThreadPool::start_threads(size_t num) {
	m_num_threads += num;
	for (size_t i = m_threads.size(); i < m_num_threads; ++i) {
		m_threads.emplace_back([this, i] {
			while (true) {
				std::unique_lock<std::mutex> lock{m_task_queue_mutex};

				// look for a work item
				while (i < m_num_threads && m_task_queue.empty()) {
					// if there are none, signal that the queue is completed
					// and wait for notification of new work items.
					m_task_queue_completed_condition.notify_all();
					m_worker_condition.wait(lock);
				}

				if (i >= m_num_threads) {
					break;
				}

				std::function<void()> task{std::move(m_task_queue.front())};
				m_task_queue.pop_front();

				// Unlock the lock, so we can process the task without blocking other threads
				lock.unlock();

				task();
			}
		});
	}
}

void ThreadPool::shutdown_threads(size_t num) {
	auto num_to_close = std::min(num, m_num_threads);

	{
		std::lock_guard<std::mutex> lock{m_task_queue_mutex};
		m_num_threads -= num_to_close;
	}

	// Wake up all the threads to have them quit
	m_worker_condition.notify_all();
	for (auto i = 0u; i < num_to_close; ++i) {
		m_threads.back().join();
		m_threads.pop_back();
	}
}

void ThreadPool::set_n_threads(size_t num) {
	if (m_num_threads > num) {
		shutdown_threads(m_num_threads - num);
	} else if (m_num_threads < num) {
		start_threads(num - m_num_threads);
	}
}

void ThreadPool::wait_until_queue_completed() {
	std::unique_lock<std::mutex> lock{m_task_queue_mutex};
	m_task_queue_completed_condition.wait(lock, [this]() { return m_task_queue.empty(); });
}

void ThreadPool::flush_queue() {
	std::lock_guard<std::mutex> lock{m_task_queue_mutex};
	m_task_queue.clear();
}

}
