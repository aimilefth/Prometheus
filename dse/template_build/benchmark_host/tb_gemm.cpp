#include "host_gemm_fpga.h"
#include <iostream>
#include <cstdlib>
#include <chrono>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage:\n  " << argv[0]
                  << " <xclbin_path> <device_index> <benchmark|power> [seconds] [iterations]" << std::endl;
        return 1;
    }

    const std::string xclbin_path = argv[1];
    
    unsigned device_index = 0;
    try {
        device_index = (unsigned)std::stoul(argv[2]);
    } catch (...) {
        std::cerr << "Error: Invalid device_index." << std::endl;
        return 1;
    }

    const std::string mode = argv[3];

    unsigned int seconds = 0;
    unsigned int iterations = 0;

    // Gracefully parse seconds if provided
    if (argc > 4) {
        try {
            seconds = (unsigned int)std::stoul(argv[4]);
        } catch (...) { /* default to 0 */ }
    }
    // Gracefully parse iterations if provided
    if (argc > 5) {
        try {
            iterations = (unsigned int)std::stoul(argv[5]);
        } catch (...) { /* default to 0 */ }
    }

    if (seconds == 0 && iterations == 0) {
        std::cerr << "Error: Both seconds and iterations cannot be 0." << std::endl;
        return 1;
    }

    FPGA_GEMM fpga;
    if (fpga.fpga_init(xclbin_path, device_index) != 0) {
        std::cerr << "FPGA init failed." << std::endl;
        return 1;
    }

    // Directly populate inputs array using the XRT mapped BO memory and time it
    using clock = std::chrono::high_resolution_clock;
    auto start_pop = clock::now();

    float* inA = fpga.get_inA_ptr();
    float* inB = fpga.get_inB_ptr();

    for (std::size_t i = 0; i < FPGA_GEMM::A_ELEMS; ++i) {
        inA[i] = (float)std::rand() / RAND_MAX;
    }
    for (std::size_t i = 0; i < FPGA_GEMM::B_ELEMS; ++i) {
        inB[i] = (float)std::rand() / RAND_MAX;
    }

    auto end_pop = clock::now();
    std::chrono::duration<double, std::micro> pop_time = end_pop - start_pop;
    std::cout << "Time to populate input arrays: " << pop_time.count() << " us" << std::endl;

    if (mode == "benchmark") {
        fpga.warmup(1);

        std::cout << "Starting benchmarking runs..." << std::endl;
        fpga.runs_with_save(iterations, seconds, "benchmark_runs.csv");
        fpga.print_performance_timings();

        std::cout << "PASSED" << std::endl;
    } else if (mode == "power") {
        std::cout << "Starting continuous power mode runs..." << std::endl;

        auto start_power = std::chrono::steady_clock::now();
        unsigned int i = 0;
        
        while (true) {
            if (iterations > 0 && i >= iterations) break;
            if (seconds > 0) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - start_power).count() >= seconds) {
                    break;
                }
            }
            fpga.bare_run();
            i++;
        }

        std::cout << "Power run finished." << std::endl;
        std::cout << "PASSED" << std::endl;
    } else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        std::cerr << "Mode must be either 'benchmark' or 'power'" << std::endl;
        return 1;
    }

    return 0;
}