// PapaBench joint simulation top: two Ibex MCUs, as the two AVRs of the
// Paparazzi autopilot board.
//
//   u_fbw        Fly-By-Wire MCU: SPI slave, GPIO bank 0 (radio PPM), servos
//                on the PWM (pwm.log); SimCtrl output in fbw.log
//   u_autopilot  Autopilot MCU: SPI master, GPIO bank 1 (modem clock), GPS
//                on uart_rx; SimCtrl output in autopilot.log
//
// Each MCU is a complete, patched Secure-Ibex SoC (reference_system_core:
// Ibex, 128 KiB RAM, timers, UART, two GPIO banks, PWM, SPI master and
// slave), loaded
// with its own ELF (papabench_dual.cc: --meminit=fbw,<elf>
// --meminit=autopilot,<elf>). Both run from the same clock and reset.
//
// The inter-MCU link is the Autopilot's SPI master wired to the FBW's SPI
// slave (MISO pulled up while the slave does not drive it). The FBW's master
// and the Autopilot's slave are unused.
//
// One papabench_env drives both, as in the single-MCU top: each program uses
// only its own GPIO bank (harness/periph.h), so the env sees the FBW's bank 0
// and the Autopilot's bank 1; the other bank of each SoC is unconnected. The
// GPS stream goes to the Autopilot's UART; the FBW's UART RX idles high.
//
// End of run: a program that has printed its results sets bit 7 of its GPIO
// bank and keeps running (the other MCU may still need it on the SPI link);
// the simulation ends when both have. A SimCtrl halt (error path, crt0 after main returns)
// still ends it at once.
module papabench_dual #(
  parameter int GpiWidth   = 8,
  parameter int GpoWidth   = 16,
  parameter int PwmWidth   = 12,
  // 20-bit PWM counters, as in the single-MCU top (hw/patches/0001)
  parameter int PwmCtrSize = 20
) (
  input logic clk_sys_i,
  input logic rst_sys_ni
);
  localparam int unsigned GpoDone = 7;

  // FBW: bank 0; Autopilot: bank 1
  logic [GpiWidth-1:0] fbw_gp_i, ap_gp_i;
  logic [GpoWidth-1:0] fbw_gp_o, ap_gp_o;
  logic [PwmWidth-1:0] fbw_pwm_o, ap_pwm_o;
  logic                ap_uart_rx;

  // SPI link: Autopilot master -> FBW slave
  logic spi_sck, spi_cs_n, spi_mosi, spi_miso, spi_miso_en;

  reference_system_core #(
    .GpiWidth       (GpiWidth),
    .GpoWidth       (GpoWidth),
    .PwmWidth       (PwmWidth),
    .PwmCtrSize     (PwmCtrSize),
    .SimCtrlLogName ("fbw.log")
  ) u_fbw (
    .clk_sys_i,
    .rst_sys_ni,
    .gp_i            (fbw_gp_i),
    .gp_o            (fbw_gp_o),
    .gp1_i           ('0),
    .gp1_o           (),
    .pwm_o           (fbw_pwm_o),
    .uart_rx_i       (1'b1),
    .uart_tx_o       (),
    // unused SPI master
    .spi_m_sck_o     (),
    .spi_m_cs_no     (),
    .spi_m_mosi_o    (),
    .spi_m_miso_i    (1'b1),
    // SPI slave: the link
    .spi_s_sck_i     (spi_sck),
    .spi_s_cs_ni     (spi_cs_n),
    .spi_s_mosi_i    (spi_mosi),
    .spi_s_miso_o    (spi_miso),
    .spi_s_miso_en_o (spi_miso_en),
    // no debugger
    .dmi_rst_ni      (rst_sys_ni),
    .dmi_req_valid_i (1'b0),
    .dmi_req_ready_o (),
    .dmi_req_i       ('0),
    .dmi_resp_valid_o(),
    .dmi_resp_ready_i(1'b1),
    .dmi_resp_o      ()
  );

  reference_system_core #(
    .GpiWidth       (GpiWidth),
    .GpoWidth       (GpoWidth),
    .PwmWidth       (PwmWidth),
    .PwmCtrSize     (PwmCtrSize),
    .SimCtrlLogName ("autopilot.log")
  ) u_autopilot (
    .clk_sys_i,
    .rst_sys_ni,
    .gp_i            ('0),
    .gp_o            (),
    .gp1_i           (ap_gp_i),
    .gp1_o           (ap_gp_o),
    .pwm_o           (ap_pwm_o),
    .uart_rx_i       (ap_uart_rx),
    .uart_tx_o       (),
    // SPI master: the link
    .spi_m_sck_o     (spi_sck),
    .spi_m_cs_no     (spi_cs_n),
    .spi_m_mosi_o    (spi_mosi),
    .spi_m_miso_i    (spi_miso_en ? spi_miso : 1'b1),
    // unused SPI slave
    .spi_s_sck_i     (1'b0),
    .spi_s_cs_ni     (1'b1),
    .spi_s_mosi_i    (1'b0),
    .spi_s_miso_o    (),
    .spi_s_miso_en_o (),
    // no debugger
    .dmi_rst_ni      (rst_sys_ni),
    .dmi_req_valid_i (1'b0),
    .dmi_req_ready_o (),
    .dmi_req_i       ('0),
    .dmi_resp_valid_o(),
    .dmi_resp_ready_i(1'b1),
    .dmi_resp_o      ()
  );

  papabench_env u_papabench_env (
    .clk_i     (clk_sys_i),
    .rst_ni    (rst_sys_ni),
    .fbw_gpo_i (fbw_gp_o),
    .fbw_gpi_o (fbw_gp_i),
    .ap_gpo_i  (ap_gp_o),
    .ap_gpi_o  (ap_gp_i),
    .uart_rx_o (ap_uart_rx)
  );

  // FBW servos (pwm.log)
  papabench_pwm_monitor #(
    .Channels (PwmWidth)
  ) u_papabench_pwm_monitor (
    .clk_i  (clk_sys_i),
    .rst_ni (rst_sys_ni),
    .pwm_i  (fbw_pwm_o)
  );

  logic done_q;

  always_ff @(posedge clk_sys_i or negedge rst_sys_ni) begin
    if (!rst_sys_ni) begin
      done_q <= 1'b0;
    end else begin
      done_q <= fbw_gp_o[GpoDone] & ap_gp_o[GpoDone];
      if (done_q) begin
        $display("Both MCUs done: terminating simulation.");
        $finish;
      end
    end
  end

  logic unused_ap_pwm;
  assign unused_ap_pwm = ^ap_pwm_o;
endmodule
