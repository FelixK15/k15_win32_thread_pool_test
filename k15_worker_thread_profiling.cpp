#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>

#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <immintrin.h>

#define PROFILE_AND_PRINT(name, fncCall)                                                                    \
{                                                                                                           \
    LARGE_INTEGER _start, _end;                                                                             \
    QueryPerformanceCounter(&_start);                                                                       \
    fncCall;                                                                                                    \
    QueryPerformanceCounter(&_end);                                                                         \
    const float deltaInMs = (float)((_end.QuadPart - _start.QuadPart) * 1000) / (float)frequency.QuadPart;  \
    printf("%s took %.3fms\n", name, deltaInMs);                                                            \
    fflush(stdout);                                                                                         \
}

LARGE_INTEGER frequency;

struct worker_thread_context_t;
typedef void(*mainThreadWaitForWorkFnc)(worker_thread_context_t*);
typedef void(*waitForWorkFnc)(worker_thread_context_t*);
typedef void(*spawnWorkerThreadsFnc)(worker_thread_context_t*, const uint32_t);
typedef void(*queueWorkFnc)(worker_thread_context_t*, const uint32_t);

struct worker_thread_context_t
{
    uint32_t                    jobQueueSize;
    uint32_t                    jobCount;
    HANDLE                      pQueueSemaphore;
    mainThreadWaitForWorkFnc    mainThreadWaitForWork;
    waitForWorkFnc              waitForWork;
    spawnWorkerThreadsFnc       spawnWorkerThreads;
    queueWorkFnc                queueWork;

    PTP_POOL                    pThreadPool;
    PTP_WORK                    pWorkForThreadPool;
    TP_CALLBACK_ENVIRON         threadPoolEnvironment;
};


#define WORKER_THREAD_PROFILING_USE_SEMAPHORE_WAIT_OBJECT
//#define WORKER_THREAD_PROFILING_USE_SPIN_LOCK

//#define USE_OWN_JOB_SYSTEM
#define USE_WIN32_JOB_SYSTEM

void workerJob()
{
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    QueryPerformanceCounter(&start);

    float dummyValue = 1.0f;
    while(true)
    {
        QueryPerformanceCounter(&end);

        const float deltaInMs = (float)(( end.QuadPart - start.QuadPart ) * 1000) / (float)frequency.QuadPart;
        if(deltaInMs < 100.f)
        {
            dummyValue += 20.f * 196.f;
            continue;
        }

        break;
    }
}

void waitForWorkWaitForSingleObject(worker_thread_context_t* pWorkerThreadContext)
{
    while(true)
    {
        WaitForSingleObject(pWorkerThreadContext->pQueueSemaphore, INFINITE);
        workerJob();
        InterlockedDecrement(&pWorkerThreadContext->jobCount);
        InterlockedDecrement(&pWorkerThreadContext->jobQueueSize);
    }
}

void waitForWorkSpinlock(worker_thread_context_t* pWorkerThreadContext)
{
    while(true)
    {
        const uint32_t currentJobQueueSize = pWorkerThreadContext->jobQueueSize;
        const uint32_t newJobQueueSize = pWorkerThreadContext->jobQueueSize - 1;
        if(currentJobQueueSize == InterlockedCompareExchange(&pWorkerThreadContext->jobQueueSize, newJobQueueSize, currentJobQueueSize))
        {
            workerJob();
            InterlockedDecrement(&pWorkerThreadContext->jobCount);
        }
    }
}

DWORD threadStartRoutine(LPVOID pParameter)
{
    worker_thread_context_t* pWorkerThreadContext = (worker_thread_context_t*)pParameter;
    pWorkerThreadContext->waitForWork(pWorkerThreadContext);
    return 0u;
}

void spawnWorkerThreadsOwnJobSystem(worker_thread_context_t* pWorkerThreadContext, const uint32_t numWorkerThreads)
{
    for(uint32_t workerIndex = 0u; workerIndex < numWorkerThreads; ++workerIndex)
    {
        CreateThread(nullptr, 0u, &threadStartRoutine, pWorkerThreadContext, 0, nullptr);
    }
}

VOID Win32WorkCallback(PTP_CALLBACK_INSTANCE pInstance, void* pContext, PTP_WORK pWorkItem)
{
    worker_thread_context_t* pWorkerThreadContext = (worker_thread_context_t*)pContext;
    workerJob();
}

void spawnWorkerThreadsWin32JobSystem(worker_thread_context_t* pWorkerThreadContext, const uint32_t numWorkerThreads)
{
    pWorkerThreadContext->pThreadPool = CreateThreadpool(nullptr);
    SetThreadpoolThreadMinimum(pWorkerThreadContext->pThreadPool, numWorkerThreads);
    SetThreadpoolThreadMaximum(pWorkerThreadContext->pThreadPool, numWorkerThreads);

    InitializeThreadpoolEnvironment(&pWorkerThreadContext->threadPoolEnvironment);
    SetThreadpoolCallbackPool(&pWorkerThreadContext->threadPoolEnvironment, pWorkerThreadContext->pThreadPool);

    pWorkerThreadContext->pWorkForThreadPool = CreateThreadpoolWork(&Win32WorkCallback, pWorkerThreadContext, &pWorkerThreadContext->threadPoolEnvironment);
}

void queueWorkOwnJobSystem(worker_thread_context_t* pWorkerThreadContext, const uint32_t numJobsToQueue)
{
    InterlockedExchange(&pWorkerThreadContext->jobQueueSize, numJobsToQueue);
    InterlockedExchange(&pWorkerThreadContext->jobCount, numJobsToQueue);

#ifdef WORKER_THREAD_PROFILING_USE_SEMAPHORE_WAIT_OBJECT
    ReleaseSemaphore(pWorkerThreadContext->pQueueSemaphore, numJobsToQueue, nullptr);
#elif defined WORKER_THREAD_PROFILING_USE_SPIN_LOCK
#else
#error
#endif
}

void queueWorkWin32JobSystem(worker_thread_context_t* pWorkerThreadContext, const uint32_t numJobsToQueue)
{
    for(uint32_t jobIndex = 0u; jobIndex < numJobsToQueue; ++jobIndex)
    {
        SubmitThreadpoolWork(pWorkerThreadContext->pWorkForThreadPool);
    }
}

void mainThreadWitForWorkOwnJobSystem(worker_thread_context_t* pWorkerThreadContext)
{
    while(pWorkerThreadContext->jobCount > 0u)
    {
        _mm_pause();
    }
}

void mainThreadWaitForWorkWin32JobSystem(worker_thread_context_t* pWorkerThreadContext)
{
    WaitForThreadpoolWorkCallbacks(pWorkerThreadContext->pWorkForThreadPool, FALSE);
}

void setupWorkerThreads(worker_thread_context_t* pWorkerThreadContext, const uint32_t numWorkerThreads)
{
#ifdef WORKER_THREAD_PROFILING_USE_SEMAPHORE_WAIT_OBJECT
    pWorkerThreadContext->waitForWork = waitForWorkWaitForSingleObject;
    pWorkerThreadContext->pQueueSemaphore = CreateSemaphoreA(nullptr, 0u, 0xFFFF, nullptr);
#elif defined WORKER_THREAD_PROFILING_USE_SPIN_LOCK
    pWorkerThreadContext->waitForWork = waitForWorkSpinlock;
#else
#error
#endif

#ifdef USE_OWN_JOB_SYSTEM
    pWorkerThreadContext->spawnWorkerThreads    = spawnWorkerThreadsOwnJobSystem;
    pWorkerThreadContext->queueWork             = queueWorkOwnJobSystem;
    pWorkerThreadContext->mainThreadWaitForWork = mainThreadWitForWorkOwnJobSystem;
#elif defined USE_WIN32_JOB_SYSTEM
    pWorkerThreadContext->spawnWorkerThreads    = spawnWorkerThreadsWin32JobSystem;
    pWorkerThreadContext->queueWork             = queueWorkWin32JobSystem;
    pWorkerThreadContext->mainThreadWaitForWork = mainThreadWaitForWorkWin32JobSystem;
#else
#error
#endif

    PROFILE_AND_PRINT("Spawn Worker Threads", pWorkerThreadContext->spawnWorkerThreads(pWorkerThreadContext, numWorkerThreads));
}

void queueWorkForWorkerThreads(worker_thread_context_t* pWorkerThreadContext, const uint32_t numJobsToQueue)
{
    PROFILE_AND_PRINT("Queue Work", pWorkerThreadContext->queueWork(pWorkerThreadContext, numJobsToQueue));
}

void waitForWorkToFinish(worker_thread_context_t* pWorkerThreadContext)
{
    PROFILE_AND_PRINT("Wait For Work", pWorkerThreadContext->mainThreadWaitForWork(pWorkerThreadContext));
}

uint32_t queryLogicalProcessorCount()
{
    DWORD sizeInBytes = 0u;
    GetLogicalProcessorInformation(nullptr, &sizeInBytes);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION pLPI = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(sizeInBytes);
    GetLogicalProcessorInformation(pLPI, &sizeInBytes);

    DWORD offset = 0u;
    uint32_t processorCount = 0u;
    while(true)
    {
        if(offset == sizeInBytes)
        {
            break;
        }

        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION pCurrentLPI = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)((uint8_t*)pLPI + offset);
        if(pCurrentLPI->Relationship == RelationProcessorCore)
        {
            processorCount += (uint32_t)_mm_popcnt_u64(pCurrentLPI->ProcessorMask);
        }
        offset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    }
    
    return processorCount;
}

int main()
{
    QueryPerformanceFrequency(&frequency);

    worker_thread_context_t workerThreadContext;
    const uint32_t numWorkerThreads = queryLogicalProcessorCount();
    setupWorkerThreads(&workerThreadContext, numWorkerThreads);

    const uint32_t numJobsToQueue = 1000;
    queueWorkForWorkerThreads(&workerThreadContext, numJobsToQueue);
    waitForWorkToFinish(&workerThreadContext);
    Sleep(1000);
    queueWorkForWorkerThreads(&workerThreadContext, numJobsToQueue);
    waitForWorkToFinish(&workerThreadContext);

    
    return 0u;
}