@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NVCC="C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin\nvcc.exe"
%CMAKE% -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DBH_ENABLE_CUDA=ON -DCMAKE_CUDA_COMPILER=%NVCC% -B build_cuda .
%CMAKE% --build build_cuda
