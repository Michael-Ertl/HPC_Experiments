#include <gtest/gtest.h>
#if HAVE_CUDA
#include <cuda.h>
#include <cuda_runtime.h>
#endif

#if false
TEST(CudaTest, DeviceZeroIsAvailable) {
    CUdevice cuDevice;
    cuDeviceGet(&cuDevice, 0);
    CUcontext cuContext;
    cuCtxCreate(&cuContext, CU_CTX_SCHED_SPIN, cuDevice);
    cuCtxDestroy(cuContext);
}
#endif
