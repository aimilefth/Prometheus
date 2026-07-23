#include "host_gemm_fpga.h"

#include <experimental/xrt_xclbin.h>
#include <iostream>


int FPGA_GEMM::fpga_init(const std::string& xclbin_path, const unsigned int device_index) {
    using clock = std::chrono::high_resolution_clock;
    auto start = clock::now();
    device = xrt::device(device_index);

    std::cout << "Trying to program device[" << device_index << "] with name: "
              << device.get_info<xrt::info::device::name>()
              << " and bdf: " << device.get_info<xrt::info::device::bdf>()
              << std::endl;

    auto uuid = device.load_xclbin(xclbin_path);

    // Kernel name must match the HLS top function: "gemm"
    gemm_kernel = xrt::kernel(device, uuid, "kernel_nlp");

    auto end_program = clock::now();
    time_program_fpga = end_program - start;

    std::cout << "Device[" << device_index << "]: programmed successfully. Kernel handle: "
              << gemm_kernel.get_handle() << std::endl;

    std::cout << "Allocate buffers in global memory" << std::endl;
    auto start_alloc = clock::now();

    const std::size_t A_BYTES = A_ELEMS * sizeof(float);
    const std::size_t B_BYTES = B_ELEMS * sizeof(float);
    const std::size_t C_BYTES = C_ELEMS * sizeof(float);

    // Change due to Prometheus ordering
    inA_bo  = xrt::bo(device, A_BYTES, gemm_kernel.group_id(1));
    inB_bo  = xrt::bo(device, B_BYTES, gemm_kernel.group_id(2));
    outC_bo = xrt::bo(device, C_BYTES, gemm_kernel.group_id(0));

    inA_host_ptr  = inA_bo.map<float*>();
    inB_host_ptr  = inB_bo.map<float*>();
    outC_host_ptr = outC_bo.map<float*>();

    std::cout << "Mapped pointers: A=" << (void*)inA_host_ptr
            << " B=" << (void*)inB_host_ptr
            << " C=" << (void*)outC_host_ptr << std::endl;

        // 3. CRITICAL CHECK: Ensure pointers are valid
    if (inA_host_ptr == nullptr || inB_host_ptr == nullptr || outC_host_ptr == nullptr) {
        std::cerr << "Error: Failed to map XRT Buffer Objects to host memory!" << std::endl;
        std::cerr << "  inA: " << inA_host_ptr << std::endl;
        std::cerr << "  inB: " << inB_host_ptr << std::endl;
        std::cerr << "  outC: " << outC_host_ptr << std::endl;
        return -1; // Fail gracefully
    }

    run_gemm = xrt::run(gemm_kernel);
    run_gemm.set_arg(1, inA_bo);
    run_gemm.set_arg(2, inB_bo);
    run_gemm.set_arg(0, outC_bo);

    auto end_alloc = clock::now();
    time_allocate_buffers = end_alloc - start_alloc;

    return 0;
}

void FPGA_GEMM::warmup(unsigned int iterations) {
    using clock = std::chrono::high_resolution_clock;
    warmup_timings.clear();
    warmup_timings.reserve(iterations);

    for (unsigned int i = 0; i < iterations; ++i) {
        auto start = clock::now();
        run_gemm.start();
        run_gemm.wait();
        auto end = clock::now();
        warmup_timings.push_back(end - start);
        std::cout << "Warmed up (" << (i + 1) << "/" << iterations << ")" << std::endl;
    }
}

void FPGA_GEMM::run() {
    using clock = std::chrono::high_resolution_clock;
    // Copy inputs to device
    auto start_copy_in = clock::now();
    inA_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    inB_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    auto end_copy_in = clock::now();
    time_copy_input_to_device.push_back(end_copy_in - start_copy_in);

    // Execute kernel
    auto start_kernel = clock::now();
    run_gemm.start();
    run_gemm.wait();
    auto end_kernel = clock::now();
    time_kernel_execution.push_back(end_kernel - start_kernel);

    // Copy output to host
    auto start_copy_out = clock::now();
    outC_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    auto end_copy_out = clock::now();
    time_copy_output_to_host.push_back(end_copy_out - start_copy_out);
}

// NEW
void FPGA_GEMM::bare_run() {
    run_gemm.start();
    run_gemm.wait();
}

void FPGA_GEMM::runs_with_save(unsigned int runs, unsigned int seconds, const std::string& filename) {
    std::ofstream csv_file(filename, std::ios::app);
    if (!csv_file.is_open()) {
        std::cerr << "Error: Could not open " << filename << " for writing." << std::endl;
        return;
    }

    auto start_time = std::chrono::steady_clock::now();
    unsigned int i = 0;

    while (true) {
        // Break condition checking
        if (runs > 0 && i >= runs) break;
        if (seconds > 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() >= seconds) {
                break;
            }
        }

        run(); // execute a full run with syncs
        
        // Grab the most recent timings from the back of the vectors
        double copy_in  = time_copy_input_to_device.back().count();
        double kernel   = time_kernel_execution.back().count();
        double copy_out = time_copy_output_to_host.back().count();
        double total    = copy_in + kernel + copy_out;

        // Save: RunIndex, CopyIn(us), Kernel(us), CopyOut(us), Total(us)
        csv_file << i << "," << copy_in << "," << kernel << "," << copy_out << "," << total << "\n";
        i++;
    }
    csv_file.close();
}

void FPGA_GEMM::run_benchmark(unsigned int iterations) {
    using clock = std::chrono::high_resolution_clock;
    std::vector<double> kernel_ms;
    kernel_ms.reserve(iterations);

    for (unsigned int i = 0; i < iterations; ++i) {
        auto start = clock::now();
        run_gemm.start();
        run_gemm.wait();
        auto end = clock::now();
        kernel_ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    double sum = 0.0;
    for (auto v : kernel_ms) sum += v;
    std::cout << "Kernel-only benchmark over " << iterations
              << " iters, avg: " << (sum / iterations) << " ms" << std::endl;
}

void FPGA_GEMM::run_benchmark_hostdatatransfer(unsigned int iterations) {
    using clock = std::chrono::high_resolution_clock;
    std::vector<double> copy_in_ms, kernel_ms, copy_out_ms, total_ms;
    copy_in_ms.reserve(iterations);
    kernel_ms.reserve(iterations);
    copy_out_ms.reserve(iterations);
    total_ms.reserve(iterations);

    for (unsigned int i = 0; i < iterations; ++i) {
        auto start_total = clock::now();

        auto start_ci = clock::now();
        inA_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        inB_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE);
        auto end_ci = clock::now();
        copy_in_ms.push_back(std::chrono::duration<double, std::milli>(end_ci - start_ci).count());

        auto start_k = clock::now();
        run_gemm.start();
        run_gemm.wait();
        auto end_k = clock::now();
        kernel_ms.push_back(std::chrono::duration<double, std::milli>(end_k - start_k).count());

        auto start_co = clock::now();
        outC_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
        auto end_co = clock::now();
        copy_out_ms.push_back(std::chrono::duration<double, std::milli>(end_co - start_co).count());

        auto end_total = clock::now();
        total_ms.push_back(std::chrono::duration<double, std::milli>(end_total - start_total).count());
    }

    // Compute average times
    double total_copy_in_ms = 0;
    double total_kernel_ms = 0;
    double total_copy_out_ms = 0;
    double total_total_ms = 0;

    for (unsigned int i = 0; i < iterations; ++i) {
        total_copy_in_ms += copy_in_ms[i];
        total_kernel_ms += kernel_ms[i];
        total_copy_out_ms += copy_out_ms[i];
        total_total_ms += total_ms[i];
    }

    double avg_copy_in_ms = total_copy_in_ms / iterations;
    double avg_kernel_ms = total_kernel_ms / iterations;
    double avg_copy_out_ms = total_copy_out_ms / iterations;
    double avg_total_ms = total_total_ms / iterations;

    std::cout << "Benchmarking with data transfers over " << iterations << " iterations." << std::endl;
    std::cout << "Average Input Transfer ms: " << avg_copy_in_ms << " ms" << std::endl;
    std::cout << "Average Kernel Execution ms: " << avg_kernel_ms << " ms" << std::endl;
    std::cout << "Average Output Transfer ms: " << avg_copy_out_ms << " ms" << std::endl;
    std::cout << "Average Total ms: " << avg_total_ms << " ms" << std::endl;
}

void FPGA_GEMM::print_performance_timings() const {
    std::cout << "FPGA Initialization Timings:" << std::endl;
    std::cout << "  Time to program FPGA: " << time_program_fpga.count() << " us" << std::endl;
    std::cout << "  Time to allocate buffers: " << time_allocate_buffers.count() << " us" << std::endl;

    if (!warmup_timings.empty()) {
        double sum = 0.0;
        for (auto& t : warmup_timings) sum += t.count();
        std::cout << "Warmup Timings:" << std::endl;
        std::cout << "  iters: " << warmup_timings.size()
                  << ", avg: " << (sum / warmup_timings.size()) << " us" << std::endl;
    }

    std::cout << "Run Timings:" << std::endl;
    for (std::size_t i = 0; i < time_kernel_execution.size(); ++i) {
        std::cout << "  Run " << (i + 1) << ":" << std::endl;
        std::cout << "    copy-in : " << time_copy_input_to_device[i].count() << " us" << std::endl;
        std::cout << "    kernel  : " << time_kernel_execution[i].count() << " us" << std::endl;
        std::cout << "    copy-out: " << time_copy_output_to_host[i].count() << " us" << std::endl;
    }
}

void FPGA_GEMM::save_results_to_csv(const std::string& filename) const {
    std::ofstream csv_file(filename, std::ios::app); // Append mode
    if (!csv_file.is_open()) return;

    size_t n = time_kernel_execution.size();
    if (n == 0) return;

    double avg_in = 0, avg_k = 0, avg_out = 0;
    for (size_t i = 0; i < n; ++i) {
        avg_in  += time_copy_input_to_device[i].count();
        avg_k   += time_kernel_execution[i].count();
        avg_out += time_copy_output_to_host[i].count();
    }
    
    avg_in /= n;
    avg_k /= n;
    avg_out /= n;

    // Format: Iterations, Avg Copy In (us), Avg Kernel (us), Avg Copy Out (us), Total (us)
    csv_file << n << "," << avg_in << "," << avg_k << "," << avg_out << "," << (avg_in + avg_k + avg_out) << "\n";
    
    csv_file.close();
}

float* FPGA_GEMM::get_inA_ptr() { return inA_host_ptr; }
float* FPGA_GEMM::get_inB_ptr() { return inB_host_ptr; }
float* FPGA_GEMM::get_outC_ptr() { return outC_host_ptr; }