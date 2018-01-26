#pragma once

#include <functional>
#include <future>
#include "ObjectPool.h"

template <int N>
struct TupleUnpacker
{
	template <class RetType, class ObjType, class... FuncArgs, class... TupleArgs, class... Args>
	static RetType DoExecute(ObjType* obj, RetType (ObjType::*memfunc)(FuncArgs...), const std::tuple<TupleArgs...>& targ, Args&&... args)
	{
		return TupleUnpacker<N - 1>::DoExecute(obj, memfunc, targ, std::get<N - 1>(targ), std::forward<Args>(args)...);
	}
};

template <>
struct TupleUnpacker<0>
{
	template <class RetType, class ObjType, class... FuncArgs, class... TupleArgs, class... Args>
	static RetType DoExecute(ObjType* obj, RetType (ObjType::*memfunc)(FuncArgs...), const std::tuple<TupleArgs...>& targ, Args&&... args)
	{
		return (obj->*memfunc)(std::forward<Args>(args)...);
	}
};


template <class RetType, class ObjType, class... FuncArgs, class... TupleArgs >
RetType DoExecuteTuple(ObjType* obj, RetType (ObjType::*memfunc)(FuncArgs...), std::tuple<TupleArgs...> const& targ)
{
	return TupleUnpacker<sizeof...(TupleArgs)>::DoExecute(obj, memfunc, targ);
}


struct NodeEntry
{
	NodeEntry() : mNext(nullptr) {}
	NodeEntry* volatile mNext;
};

struct JobEntry
{
	JobEntry(): mRefCount(1) {}
	virtual ~JobEntry() {}

	virtual void OnExecute()
	{}

	void AddRefForThis()
	{
		mRefCount.fetch_add(1);
	}

	void ReleaseRefForThis()
	{
		if (mRefCount.fetch_sub(1) == 1)
		{
			delete this;
		}
	}

	std::atomic<int32_t>	mRefCount;
	NodeEntry	mNodeEntry;
} ;


template <typename R>
class Future {
public:
	Future(JobEntry* job, std::future<R>&& future) : mJob(job), mFuture(std::move(future)) {
		mJob->AddRefForThis();
	}

	~Future() {
		if (mJob)
		{
			mJob->ReleaseRefForThis();
		}
	}

	Future(Future&& other)
	{
		mJob = other.mJob;
		mFuture = std::move(other.mFuture);
		other.mJob = nullptr;
	}

	Future& operator=(Future&& other)
	{
		if (this != &other) {
			mJob = other.mJob;
			mFuture = std::move(other.mFuture);
			other.mJob = nullptr;
		}

		return *this;
	}

	R Get() {
		R ret = mFuture.get();
		return ret;
	}

	JobEntry*				mJob;
	std::future<R>			mFuture;
};

template <>
class Future<void> {
public:
	Future(JobEntry* job, std::future<void>&& future) : mJob(job), mFuture(std::move(future)) {
		mJob->AddRefForThis();
	}

	Future(Future&& other)
	{
		mJob = other.mJob;
		mFuture = std::move(other.mFuture);
		other.mJob = nullptr;
	}

	Future& operator=(Future&& other)
	{
		if (this != &other) {
			mJob = other.mJob;
			mFuture = std::move(other.mFuture);
			other.mJob = nullptr;
		}

		return *this;
	}

	void Get() {
		mFuture.get();
	}

	JobEntry*					mJob;
	std::future<void>			mFuture;
};


template <class RetType, class ObjType, class... ArgTypes>
struct Job : public JobEntry, ObjectPool<Job<RetType, ObjType, ArgTypes...>>
{
	typedef RetType(ObjType::*MemFunc_)(ArgTypes... args);
	typedef std::tuple<ArgTypes...> Args_;


	Job(ObjType* obj, MemFunc_ memfunc, ArgTypes&&... args)
		: mObject(obj), mMemFunc(memfunc), mArgs(std::forward<ArgTypes>(args)...)
	{}

	virtual ~Job() {}

	virtual void OnExecute()
	{
		(&mPromise)->set_value(DoExecuteTuple(mObject, mMemFunc, mArgs));
	}

	Future<RetType> GetFuture()
	{
		return Future<RetType>(this, mPromise.get_future());
	}

	ObjType*				mObject;
	MemFunc_				mMemFunc;
	Args_					mArgs;
	std::promise<RetType>	mPromise;
};

template <class ObjType, class... ArgTypes>
struct Job<void, ObjType, ArgTypes...> : public JobEntry, ObjectPool<Job<void, ObjType, ArgTypes...>>
{
	typedef void (ObjType::*MemFunc_)(ArgTypes... args);
	typedef std::tuple<ArgTypes...> Args_;


	Job(ObjType* obj, MemFunc_ memfunc, ArgTypes&&... args)
		: mObject(obj), mMemFunc(memfunc), mArgs(std::forward<ArgTypes>(args)...)
	{}

	virtual ~Job() {}

	virtual void OnExecute()
	{
		DoExecuteTuple(mObject, mMemFunc, mArgs);
		mPromise.set_value();
	}

	Future<void> GetFuture()
	{
		return Future<void>(this, mPromise.get_future());
	}

	ObjType*				mObject;
	MemFunc_				mMemFunc;
	Args_					mArgs;
	std::promise<void>		mPromise;
};