// Filled from gemm_dse.py for GEMM_I/J/K

// Buffer sizes (float elements)
#define GEMM_A_SIZE (GEMM_I*GEMM_K)
#define GEMM_B_SIZE (GEMM_K*GEMM_J)
#define GEMM_C_SIZE (GEMM_I*GEMM_J)