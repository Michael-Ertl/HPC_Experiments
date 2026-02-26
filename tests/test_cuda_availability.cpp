#include <gtest/gtest.h>
#include <cuda.h>
#include <cuda_runtime.h>

#if false
TEST(CudaTest, DeviceZeroIsAvailable) {
    CUdevice cuDevice;
    cuDeviceGet(&cuDevice, 0);
    CUcontext cuContext;
    cuCtxCreate(&cuContext, CU_CTX_SCHED_SPIN, cuDevice);
    cuCtxDestroy(cuContext);
}
#endif
