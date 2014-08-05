#include <stdint.h>
#include <algorithm>
#include "ThreadLocal.h"
#include "STLAllocator.h"
#include "LoadBalancer.h"
#include "JobDispatcher.h"

LoadBalancingTask::LoadBalancingTask(int threadCount) : mRefCount(threadCount), mWorkerThreadCount(threadCount)
{
	mCurrentHandOverThreadId = 0;
	mHandOverList = new DispatcherList[mWorkerThreadCount]; ///< don't need to use custom memory pool due to occasionalness
}

LoadBalancingTask::~LoadBalancingTask()
{
	delete[] mHandOverList;
}


void LoadBalancingTask::HandOverToOtherThreads(AsyncExecutable* dispatcher)
{
	if (LWorkerThreadId == mCurrentHandOverThreadId)
		++mCurrentHandOverThreadId;

	if (mCurrentHandOverThreadId == mWorkerThreadCount)
		mCurrentHandOverThreadId = 0;

	mHandOverList[mCurrentHandOverThreadId++].push_back(dispatcher);

}

bool LoadBalancingTask::OnThreadLocalExecute()
{
	const DispatcherList& listForThisThread = mHandOverList[LWorkerThreadId];

	for (auto& it : listForThisThread)
	{
		LExecuterList->push_back(it);
	}
	
	if (1 == mRefCount.fetch_sub(1))
		return true;

	return false;
}




LoadBalancer::LoadBalancer(int threadCount) : mWorkerThreadCount(threadCount), mLoadBalancingTaskCount(0)
{
	for (int i = 0; i < LB_MAX_TASK_SIZE; ++i)
		mLoadBalancingTasks[i].store(nullptr);
}

LoadBalancer::~LoadBalancer()
{
}


void LoadBalancer::DoLoadBalancing()
{
	if (LRecentElapsedLoopTick < LB_HANDOVER_THRESHOLD)
		return;

	if (LExecuterList->size() < mWorkerThreadCount)
		return;

	LoadBalancingTask* lbTask = new LoadBalancingTask(mWorkerThreadCount);

	while (!LExecuterList->empty())
	{
		AsyncExecutable* dispacher = LExecuterList->front();
		LExecuterList->pop_front();
		lbTask->HandOverToOtherThreads(dispacher);
	}

	PushLoadBalancingTask(lbTask);
}

void LoadBalancer::DoLoadSharing()
{
	while (true)
	{
		if (LCurrentLoadBalancingTaskIndex >= mLoadBalancingTaskCount.load())
			break;
		
		LoadBalancingTask* lbTask = mLoadBalancingTasks[LCurrentLoadBalancingTaskIndex & LB_MAX_TAST_MASK].load();

		if (lbTask == nullptr) ///< possible if another thread proceeds between (1) and (2)
			continue;

		if (lbTask->OnThreadLocalExecute())
		{
			/// final call only in one thread
			PopLoadBalancingTask(LCurrentLoadBalancingTaskIndex);
		}

		++LCurrentLoadBalancingTaskIndex;
	}
}

void LoadBalancer::PushLoadBalancingTask(LoadBalancingTask* lbTask)
{
	int64_t index = mLoadBalancingTaskCount.fetch_add(1); ///< (1)

	LoadBalancingTask* prevTask = std::atomic_exchange(&mLoadBalancingTasks[index & LB_MAX_TAST_MASK], lbTask); ///<(2)
	_ASSERT_CRASH(prevTask == nullptr);
}

void LoadBalancer::PopLoadBalancingTask(int64_t index)
{
	LoadBalancingTask* prevTask = std::atomic_exchange(&mLoadBalancingTasks[index & LB_MAX_TAST_MASK], (LoadBalancingTask*)nullptr);
	_ASSERT_CRASH(prevTask != nullptr);
	delete prevTask;
}