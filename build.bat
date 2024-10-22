@echo off

set C_FILES=k15_worker_thread_profiling.cpp
set OUTPUT_FILE_NAME=k15_worker_thread_profiling
set BUILD_CONFIGURATION=%1
call build_cl.bat

exit /b 0