#include "host_gemm_fpga.h"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <cmath>

inline void host_serialize_A(float *A_to, float *A_from){
  unsigned int cnt = 0;
  for (int c0 = 0; c0 < TI; c0 += 1)
    for (int c2 = 0; c2 < TK; c2 += 1)
      for (int c3 = 0; c3 < apI; c3 += 1)
        for (int c5 = 0; c5 < apK; c5 += 1)
          A_to[cnt++] = A_from[(apI * c0 + c3) * GEMM_K + (apK * c2 + c5)];
}

inline void host_serialize_B(float *B_to, float *B_from) {
  unsigned int cnt = 0;
  for (int c1 = 0; c1 < TJ; c1 += 1)
    for (int c2 = 0; c2 < TK; c2 += 1)
      for (int c4 = 0; c4 < apK; c4 += 1)
        for (int c3 = 0; c3 < apJ; c3 += 1)
          B_to[cnt++] = B_from[(apK * c2 + c4) * GEMM_J + (apJ * c1 + c3)];
}

// Inverted logic: Takes the hardware's flat block-stream output and maps it back to standard 2D layout
inline void host_deserialize_C(float *C_to_2d, const float *C_from_hw) {
  unsigned int cnt = 0;
  for (int c0 = 0; c0 < TI; c0 += 1)
    for (int c1 = 0; c1 < TJ; c1 += 1)
      for (int c4 = 0; c4 < apI; c4 += 1)
        for (int c3 = 0; c3 < apJ; c3 += 1) {
          int row = apI * c0 + c4;
          int col = apJ * c1 + c3;
          C_to_2d[row * GEMM_J + col] = C_from_hw[cnt++];
        }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n  " << argv[0] << " <xclbin_path> [device_index] [iterations]" << std::endl;
        return 1;
    }

    const std::string xclbin_path = argv[1];
    const unsigned device_index = (argc >= 3) ? (unsigned)std::stoul(argv[2]) : 0;
    const unsigned iterations  = (argc >= 4) ? (unsigned)std::stoul(argv[3]) : 1;

    std::vector<float> A_rm(GEMM_A_SIZE);
    std::vector<float> B_rm(GEMM_B_SIZE);
    std::vector<float> C_gold_rm(GEMM_C_SIZE, 0.0f);

    // Init matrices directly into host vectors
    for (int i = 0; i < GEMM_A_SIZE; ++i) A_rm[i] = (float)std::rand() / RAND_MAX;
    for (int i = 0; i < GEMM_B_SIZE; ++i) B_rm[i] = (float)std::rand() / RAND_MAX;

    // Compute Golden C Reference
    for (int i = 0; i < GEMM_I; ++i) {
        for (int j = 0; j < GEMM_J; ++j) {
            float sum = 0.0f;
            for (int k = 0; k < GEMM_K; ++k) {
                sum += A_rm[i * GEMM_K + k] * B_rm[k * GEMM_J + j];
            }
            C_gold_rm[i * GEMM_J + j] = sum;
        }
    }

    FPGA_GEMM fpga;
    if (fpga.fpga_init(xclbin_path, device_index) != 0) {
        std::cerr << "FPGA init failed." << std::endl;
        return 1;
    }

    // Serialize inputs directly into XRT mapped BO memory
    host_serialize_A(fpga.get_inA_ptr(), A_rm.data());
    host_serialize_B(fpga.get_inB_ptr(), B_rm.data());

    // Optional warmup
    fpga.warmup(1);

    // Run Kernel
    for (unsigned i = 0; i < iterations; ++i) {
        fpga.run();
    }

    // Deserialize the hardware result back into standard 2D matrix formatting
    const float* outC = fpga.get_outC_ptr();
    std::vector<float> C_unserialized(GEMM_C_SIZE);
    host_deserialize_C(C_unserialized.data(), outC);

    bool pass = true;
    const float tol = 0.001f;
    std::size_t mismatches = 0;
    
    // Compare standard 2D coordinates (flat 1D arrays ordered naturally)
    for (std::size_t i = 0; i < GEMM_C_SIZE; ++i) {
        float diff = std::fabs(C_unserialized[i] - C_gold_rm[i]);
        if (diff > tol) {
            pass = false;
            ++mismatches;
        }
    }

    std::cout << "Total mismatches (absdiff > " << tol << "): " << mismatches << std::endl;

    fpga.print_performance_timings();
    fpga.save_results_to_csv("benchmark_results.csv");

    if (!pass) {
        std::cerr << "FAILED" << std::endl;
        return 1;
    }

    std::cout << "PASSED" << std::endl;
    return 0;
}