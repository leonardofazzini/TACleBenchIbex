// PapaBench simulation monitor: logs every PWM pulse to pwm.log.
//
// Instantiated by the simulation top (hw/patches/0005-sim-top-pwm-monitor
// .patch). The PWM has no read-back, and the FBW servos are only on the PWM,
// so this is how a run shows what the servos received. One line per pulse,
// written when the pulse ends:
//
//   <rise> <channel> <width>
//
// rise = cycle of the rising edge (cycles since reset), width = cycles the
// output stayed high. The file is created in the simulator's current
// directory (build/<prog>/ for 'make run'); scripts/decode_pwm.py turns it
// into servo frames.
module papabench_pwm_monitor #(
  parameter int Channels = 12,
  parameter     LogFile  = "pwm.log"
) (
  input logic                clk_i,
  input logic                rst_ni,
  input logic [Channels-1:0] pwm_i
);
  logic [63:0]         cycle;
  logic [Channels-1:0] pwm_q;
  logic [63:0]         rise_q [Channels];
  int                  fd;

  initial begin
    fd = $fopen(LogFile, "w");
    $fdisplay(fd, "# rise channel width (cycles)");
  end

  final $fclose(fd);

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      cycle <= '0;
      pwm_q <= '0;
      for (int i = 0; i < Channels; i++) rise_q[i] <= '0;
    end else begin
      cycle <= cycle + 1;
      pwm_q <= pwm_i;
      for (int i = 0; i < Channels; i++) begin
        if (pwm_i[i] && !pwm_q[i]) rise_q[i] <= cycle;
        if (!pwm_i[i] && pwm_q[i])
          $fdisplay(fd, "%0d %0d %0d", rise_q[i], i, cycle - rise_q[i]);
      end
    end
  end
endmodule
