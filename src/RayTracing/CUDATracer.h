#pragma once

#include "CPUTracer.h" // reuses CameraFrame/RenderSettings/BackgroundSampler/DiskSampler

namespace RayTracing {

// True if a CUDA-capable device is present (checked once, cheap to call
// repeatedly). The app uses this to enable/disable the GPU option in the
// UI rather than fail when clicked.
bool CudaAvailable();

// Same signature and same output as CPUTracer::RenderImage -- the geodesic
// integration itself runs on the GPU (cuda/geodesic_kernel.cu, one thread
// per ray), but the background/disk coloring runs on the host afterward
// using the exact same StarField/AccretionDisk sampler callbacks the CPU
// path uses, so a CUDA render and a CPU render of the same scene are
// directly comparable pixel-for-pixel (see the app's CPU vs GPU validation
// button).
std::vector<glm::vec3> RenderImageCUDA(const CameraFrame& camera, const RenderSettings& settings,
                                        const BackgroundSampler& background, const DiskSampler& disk = nullptr);

} // namespace RayTracing
