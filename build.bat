@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

:: Auto-detect the CUDA toolkit (nvcc) on this machine -- if it's present,
:: build the GPU-accelerated ray tracer/planet physics in, same output
:: binary either way. A CUDA-enabled binary still runtime-detects an actual
:: CUDA-capable GPU (RayTracing::CudaAvailable) and falls back to the CPU
:: path automatically if none is found, so one build works on both kinds
:: of end-user machine -- only the BUILD machine needs nvcc to produce it.
set BH_CUDA_FLAGS=
set NVCC_PATH=
for /f "delims=" %%P in ('where nvcc 2^>nul') do (
    if not defined NVCC_PATH set NVCC_PATH=%%P
)
if defined NVCC_PATH (
    echo CUDA toolkit found ^(%NVCC_PATH%^) -- building with GPU acceleration enabled.
    set BH_CUDA_FLAGS=-DBH_ENABLE_CUDA=ON "-DCMAKE_CUDA_COMPILER=%NVCC_PATH%"
) else (
    echo No CUDA toolkit ^(nvcc^) found -- building CPU-only.
)

%CMAKE% -G "Ninja" -DCMAKE_BUILD_TYPE=Release %BH_CUDA_FLAGS% -B build .
if errorlevel 1 (
    echo.
    echo *** CMake configure failed -- see errors above. ***
    pause
    exit /b 1
)
%CMAKE% --build build
if errorlevel 1 (
    echo.
    echo *** Build failed -- see errors above. ***
    pause
    exit /b 1
)

echo.
echo Build succeeded -- launching BlackHoleSimulator.exe
start "" "%~dp0build\BlackHoleSimulator.exe"
