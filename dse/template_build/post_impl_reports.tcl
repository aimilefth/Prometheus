set build_dir "/home/aleftheriotis/Documents/GEMM_SA/2026_06_01_simple_free_running_flp_srl/build"
set vpl_dir   "$build_dir/_x.hw.xilinx_vck190_base_dfx_202320_1/link/vivado/vpl"
set out_dir   "$build_dir/vivado_post_reports"

file mkdir $out_dir

set impl_dir "$vpl_dir/prj/prj.runs/impl_1"

open_project "$vpl_dir/prj/prj.xpr"

puts "Available runs:"
foreach r [get_runs] {
    puts "  $r : STATUS=[get_property STATUS $r] PROGRESS=[get_property PROGRESS $r]"
}

if {[catch {open_run impl_1} msg]} {
    puts "WARNING: open_run impl_1 failed:"
    puts $msg
    puts "Trying to open an implementation checkpoint manually..."

    set dcp_candidates [concat \
        [glob -nocomplain "$impl_dir/*postroute*/*.dcp"] \
        [glob -nocomplain "$impl_dir/*route*/*.dcp"] \
        [glob -nocomplain "$impl_dir/*.dcp"] \
    ]

    if {[llength $dcp_candidates] == 0} {
        puts "ERROR: No DCP found under $impl_dir"
        exit 1
    }

    puts "DCP candidates:"
    foreach d $dcp_candidates {
        puts "  $d"
    }

    set dcp [lindex $dcp_candidates 0]
    puts "Opening checkpoint: $dcp"
    open_checkpoint $dcp
}

report_route_status -file "$out_dir/route_status.rpt"
report_route_status -route_type UNROUTED -list_all_nets -file "$out_dir/unrouted_nets.rpt"
report_route_status -route_type PARTIAL  -list_all_nets -file "$out_dir/partial_routes.rpt"
report_route_status -route_type CONFLICTS -list_all_nets -file "$out_dir/route_conflicts.rpt"

report_design_analysis -congestion -file "$out_dir/design_congestion.rpt"
report_design_analysis -complexity -file "$out_dir/design_complexity.rpt"
report_design_analysis -qor_summary -file "$out_dir/qor_summary.rpt"

report_utilization -file "$out_dir/utilization.rpt"
report_utilization -hierarchical -hierarchical_depth 5 -file "$out_dir/utilization_hier.rpt"
report_utilization -slr -file "$out_dir/utilization_slr.rpt"

report_timing_summary -delay_type max -max_paths 50 -file "$out_dir/timing_setup_summary.rpt"
report_timing_summary -delay_type min -max_paths 50 -file "$out_dir/timing_hold_summary.rpt"

report_clock_utilization -file "$out_dir/clock_utilization.rpt"
report_high_fanout_nets -max_nets 100 -file "$out_dir/high_fanout_nets.rpt"
report_methodology -file "$out_dir/methodology.rpt"
report_drc -file "$out_dir/drc.rpt"

report_qor_assessment -file "$out_dir/qor_assessment.rpt"
report_qor_suggestions -file "$out_dir/qor_suggestions.rpt"

puts "Reports written to $out_dir"

# Run (2023.2) with `vivado -mode batch -source post_impl_reports.tcl`
# Change the set build_dir 