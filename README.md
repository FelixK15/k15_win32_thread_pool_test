# Win32 Thread Pool Testing
This is just a simple testing bed to test some characteristics about the [builtin win32 thread pool](https://learn.microsoft.com/en-us/windows/win32/procthread/thread-pool-api)

My main motivation for this was driven by the fact that msvc's std::for_each() uses [this API under the hood](https://github.com/microsoft/STL/blob/main/stl/src/parallel_algorithms.cpp#L25)

## Findings
* There's no need to specifically create a thread pool object ([CreateThreadpool()](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-createthreadpool))
    * You only need this when you want to control prioritization, work with IO, or want to limit the amount of active worker threads (don't worry, it won't oversubscribe on it's own)
* The default name for threads spawned by the thread pool will be "Thread Pool"
   * Unfortunately, there's not straight forward way to change it
* The threads will only be created in these scenarios:
    * When [SubmitThreadpoolWork()](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-submitthreadpoolwork) is called and all threads created up to this point are busy (up to a maximum, which is usually the amount of logical cores in your system)
    * When [SetThreadPoolThreadMinimum()](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-setthreadpoolthreadminimum) is called and the current number of threads is less than the minimum
* There's no way to directly control on which processor the threads will be running on
    * There's [SetThreadPoolCallbackRunsLong()](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadpoolcallbackrunslong), which might be a hint to the thread pool to let thread's be run on efficiency cores only.
* Overhead of repeatedly calling SubmitThreadpoolWork() could add up (~4-5us per call, if you have 100s of jobs this can quickly add up)

## Conclusion
For a quick and easy "I just want to have an easy-to-use thread pool and I don't care about the details" this might very well work out for you. std::for_each() is basically the perfect use-case for this API.
That being said, I feel like the API would've benefited greatly from a batch version of SubmitThreadpoolWork().

For anything other, especially if you want more control, I'd still recommend rolling your own simple queue-based job system.
