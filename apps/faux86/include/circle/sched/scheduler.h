#pragma once
/*
 * Zephyr/desktop shim for Circle's <circle/sched/scheduler.h>.
 * TaskManager.cpp's non-Windows tick() path calls CScheduler::Get()->Yield();
 * on Zephyr we run single-threaded (Config::singleThreaded = true), the tasks
 * run inline from tick(), and there is no cooperative scheduler to yield to,
 * so these are no-ops.
 */
class CScheduler {
public:
	static CScheduler *Get(void)
	{
		static CScheduler instance;
		return &instance;
	}
	void Yield(void) {}
	void MsSleep(unsigned) {}
	void usSleep(unsigned) {}
};
