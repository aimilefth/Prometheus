#include "host_visible.h"

#include <xrt/xrt_bo.h>
#include <xrt/xrt_device.h>
#include <xrt/xrt_kernel.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

using std::abs;

/*
 * CPU reference implementation.
 *
 * GEMM dimensions are updated by gemm_dse.py in host/host_visible.h:
 *
 *   GEMM_I: number of rows in A and C
 *   GEMM_J: number of columns in B and C
 *   GEMM_K: reduction dimension
 */
static void kernel_gemm(float A[GEMM_I][GEMM_K],
                        float B[GEMM_K][GEMM_J],
                        float C[GEMM_I][GEMM_J]) {
  for (int i = 0; i < GEMM_I; ++i) {
    for (int j = 0; j < GEMM_J; ++j) {
      C[i][j] = 0.0f;
    }
  }

  for (int i = 0; i < GEMM_I; ++i) {
    for (int k = 0; k < GEMM_K; ++k) {
      for (int j = 0; j < GEMM_J; ++j) {
        C[i][j] += A[i][k] * B[k][j];
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <kernel.xclbin>\n";
    return EXIT_FAILURE;
  }

  try {
    const std::string xclbin_path = argv[1];

    std::printf("Preparing GEMM %d x %d x %d\n",
                GEMM_I, GEMM_J, GEMM_K);

    // Keep input generation repeatable across DSE points.
    std::srand(0);

    /*
     * gemm_dse.py replaces this block with the initialization,
     * padding, tiling, and serialization code extracted from Prometheus's
     * generated src/host.cpp.
     *
     * The inserted code declares and initializes:
     *
     *   A_ori
     *   B_ori
     *   C_ori
     *   A_new_0
     *   B_new_0
     *   C_new_0
     *   C_new_before_trans_0
     */
    // PROMETHEUS_DSE_BEGIN_DATA_LAYOUT
    // Replaced by gemm_dse.py.
    // PROMETHEUS_DSE_END_DATA_LAYOUT

    /*
     * The template uses the standard Prometheus interface:
     *
     *   kernel_nlp(C, A, B)
     */
    xrt::device device(0);
    const auto uuid = device.load_xclbin(xclbin_path);
    xrt::kernel kernel(device, uuid, "kernel_nlp");

    /*
     * Prometheus argument ordering:
     *
     *   argument 0: C
     *   argument 1: A
     *   argument 2: B
     */
    xrt::bo buffer_c(device, sizeof(C_new_0), kernel.group_id(0));
    xrt::bo buffer_a(device, sizeof(A_new_0), kernel.group_id(1));
    xrt::bo buffer_b(device, sizeof(B_new_0), kernel.group_id(2));

    buffer_a.write(A_new_0, sizeof(A_new_0));
    buffer_b.write(B_new_0, sizeof(B_new_0));

    /*
     * C is overwritten by the kernel. It is initialized here to keep
     * hardware and software emulation behavior deterministic.
     */
    buffer_c.write(C_new_0, sizeof(C_new_0));

    buffer_a.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    buffer_b.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    buffer_c.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    std::printf("Launching kernel_nlp(C, A, B)...\n");

    const auto start = std::chrono::steady_clock::now();

    auto run = kernel(buffer_c, buffer_a, buffer_b);
    run.wait();

    const auto stop = std::chrono::steady_clock::now();

    buffer_c.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    buffer_c.read(C_new_0, sizeof(C_new_0));

    /*
     * gemm_dse.py replaces this block with:
     *
     *   1. Prometheus's C output de-serialization
     *   2. CPU kernel_gemm invocation
     *   3. Element-by-element result verification
     *
     * It must appear after C_new_0 has been copied from the FPGA.
     */
    // PROMETHEUS_DSE_BEGIN_RESULT_LAYOUT
    // Replaced by gemm_dse.py.
    // PROMETHEUS_DSE_END_RESULT_LAYOUT

    const std::chrono::duration<double> elapsed = stop - start;

    std::printf("C-simulation passed!\n");
    std::cout << "Kernel execution time: "
              << elapsed.count()
              << " seconds\n";

    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Host execution failed: "
              << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}