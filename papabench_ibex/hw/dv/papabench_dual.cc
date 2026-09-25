// Verilator testbench of the joint PapaBench simulation (hw/rtl/
// papabench_dual.sv): two Ibex MCUs, each with its own RAM.
//
//   Vpapabench_dual --meminit=fbw,<fbw.elf> --meminit=autopilot,<ap.elf>
//
// Same flow as Secure-Ibex dv/verilator/reference_system{.cc,_main.cc},
// with the RAM of each SoC registered under the name of its program. A named
// ELF load writes the flattened image at the start of that RAM, so the two
// areas only need disjoint (unused) base addresses. No performance-counter
// dump: each harness measures its own tasks with mcycle.

#include <iostream>

#include "verilated_toplevel.h"
#include "verilator_memutil.h"
#include "verilator_sim_ctrl.h"

#define PAPABENCH_RAM( soc ) \
  "TOP.papabench_dual." soc ".u_ram.u_ram.gen_generic.u_impl_generic"

int main(int argc, char **argv) {
  papabench_dual top;
  VerilatorMemUtil memutil;
  // Same size argument as Secure-Ibex reference_system_main.cc
  MemArea ram_fbw(PAPABENCH_RAM("u_fbw"), 1024 * 1024, 4);
  MemArea ram_autopilot(PAPABENCH_RAM("u_autopilot"), 1024 * 1024, 4);

  VerilatorSimCtrl &simctrl = VerilatorSimCtrl::GetInstance();
  simctrl.SetTop(&top, &top.clk_sys_i, &top.rst_sys_ni,
                 VerilatorSimCtrlFlags::ResetPolarityNegative);

  memutil.RegisterMemoryArea("fbw", 0x0, &ram_fbw);
  memutil.RegisterMemoryArea("autopilot", 0x10000000, &ram_autopilot);
  simctrl.RegisterExtension(&memutil);

  bool exit_app = false;
  int ret_code = simctrl.ParseCommandArgs(argc, argv, exit_app);
  if (exit_app) {
    return ret_code;
  }

  std::cout << "Simulation of two RISC-V reference systems "
               "(PapaBench FBW + Autopilot)" << std::endl
            << "======================================================"
               "========" << std::endl
            << std::endl;

  simctrl.RunSimulation();

  return simctrl.WasSimulationSuccessful() ? 0 : 1;
}
