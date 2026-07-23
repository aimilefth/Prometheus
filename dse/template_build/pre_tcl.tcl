config_compile -pipeline_loops 0
config_interface -m_axi_conservative_mode=0
config_compile -enable_auto_rewind
config_array_partition -complete_threshold 1
#config_rtl -add_register_in_block_condition=false
#config_storage fifo -impl auto -auto_srl_max_bits 0 -auto_srl_max_depth 0
config_array_partition -throughput_driven off
#config_compile -unsafe_math_optimizations
config_rtl -register_reset_num 8