// PapaBench GPIO bank: the Secure-Ibex gpio.sv register map plus edge
// interrupts.
//
// Instantiated twice by the patched SoC (hw/patches/0003-soc-gpio-banks.patch):
// bank 0 at 0x80002000 (fast IRQ 17, in place of the stock gpio.sv and of the
// raw gp_i[0] line) and bank 1 at 0x80060000 (fast IRQ 24). Each PapaBench
// program owns one bank (harness/periph.h), so the two programs share no
// GPIO register and each bank sits in its own 4 KiB window.
//
//   0x00 OUT         rw  gp_o (byte enables honoured)            as gpio.sv
//   0x04 IN          ro  gp_i, 2-flop synchronised               as gpio.sv
//   0x08 IN_DBNC     ro  gp_i, debounced                         as gpio.sv
//   0x0C IRQ_EN      rw  per input: the latched edge raises irq_o
//   0x10 IRQ_STATUS  rw  per input: edge seen; write 1 to clear
//   0x14 IRQ_FALL    rw  per input: falling edges set IRQ_STATUS
//   0x18 IRQ_RISE    rw  per input: rising edges set IRQ_STATUS
//
// IRQ_STATUS latches an enabled edge whether or not IRQ_EN is set (like an
// AVR interrupt flag); an edge in the same cycle as its clear wins.
// irq_o = |(IRQ_STATUS & IRQ_EN), level-sensitive, as the core expects.
// Other offsets read 0 and ignore writes. A read returns the value of the
// request cycle one cycle later (rvalid), as gpio.sv.
module papabench_gpio #(
  parameter int GpiWidth = 8,
  parameter int GpoWidth = 16
) (
  input  logic                clk_i,
  input  logic                rst_ni,

  input  logic                device_req_i,
  input  logic [31:0]         device_addr_i,
  input  logic                device_we_i,
  input  logic [ 3:0]         device_be_i,
  input  logic [31:0]         device_wdata_i,
  output logic                device_rvalid_o,
  output logic [31:0]         device_rdata_o,

  input  logic [GpiWidth-1:0] gp_i,
  output logic [GpoWidth-1:0] gp_o,
  output logic                irq_o
);

  localparam logic [11:0] OutReg       = 12'h000;
  localparam logic [11:0] InReg        = 12'h004;
  localparam logic [11:0] InDbncReg    = 12'h008;
  localparam logic [11:0] IrqEnReg     = 12'h00C;
  localparam logic [11:0] IrqStatusReg = 12'h010;
  localparam logic [11:0] IrqFallReg   = 12'h014;
  localparam logic [11:0] IrqRiseReg   = 12'h018;

  logic [11:0]         reg_addr;
  logic [31:0]         be_mask, wdata_masked;
  logic                wr, rd;

  logic [2:0][GpiWidth-1:0] gp_i_q;
  logic [GpiWidth-1:0] gp_i_dbnc;
  logic [GpiWidth-1:0] irq_en_q, irq_status_q, irq_fall_q, irq_rise_q;
  logic [GpiWidth-1:0] edge_rise, edge_fall, edge_set, status_clr;
  logic [31:0]         rdata_d;

  assign reg_addr = device_addr_i[11:0];
  assign wr       = device_req_i &  device_we_i;
  assign rd       = device_req_i & ~device_we_i;
  assign be_mask  = {{8{device_be_i[3]}}, {8{device_be_i[2]}},
                     {8{device_be_i[1]}}, {8{device_be_i[0]}}};
  assign wdata_masked = device_wdata_i & be_mask;

  for (genvar i = 0; i < GpiWidth; i++) begin : g_dbnc
    debounce #(
      .ClkCount(500)
    ) u_dbnc (
      .clk_i,
      .rst_ni,
      .btn_i(gp_i_q[2][i]),
      .btn_o(gp_i_dbnc[i])
    );
  end

  // Edges between the last two synchroniser stages: when IRQ_STATUS shows an
  // edge, IN already reads the new level
  assign edge_rise  =  gp_i_q[1] & ~gp_i_q[2];
  assign edge_fall  = ~gp_i_q[1] &  gp_i_q[2];
  assign edge_set   = (edge_rise & irq_rise_q) | (edge_fall & irq_fall_q);
  assign status_clr = (wr && reg_addr == IrqStatusReg) ?
                      wdata_masked[GpiWidth-1:0] : '0;

  // A register after a write, byte enables honoured
  function automatic logic [31:0] merge(logic [31:0] old);
    return (old & ~be_mask) | wdata_masked;
  endfunction

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      gp_i_q       <= '0;
      gp_o         <= '0;
      irq_en_q     <= '0;
      irq_status_q <= '0;
      irq_fall_q   <= '0;
      irq_rise_q   <= '0;
    end else begin
      gp_i_q       <= {gp_i_q[1:0], gp_i};
      irq_status_q <= (irq_status_q & ~status_clr) | edge_set;
      if (wr) begin
        unique case (reg_addr)
          OutReg:     gp_o       <= GpoWidth'(merge(32'(gp_o)));
          IrqEnReg:   irq_en_q   <= GpiWidth'(merge(32'(irq_en_q)));
          IrqFallReg: irq_fall_q <= GpiWidth'(merge(32'(irq_fall_q)));
          IrqRiseReg: irq_rise_q <= GpiWidth'(merge(32'(irq_rise_q)));
          default: ;
        endcase
      end
    end
  end

  always_comb begin
    unique case (reg_addr)
      OutReg:       rdata_d = 32'(gp_o);
      InReg:        rdata_d = 32'(gp_i_q[2]);
      InDbncReg:    rdata_d = 32'(gp_i_dbnc);
      IrqEnReg:     rdata_d = 32'(irq_en_q);
      IrqStatusReg: rdata_d = 32'(irq_status_q);
      IrqFallReg:   rdata_d = 32'(irq_fall_q);
      IrqRiseReg:   rdata_d = 32'(irq_rise_q);
      default:      rdata_d = '0;
    endcase
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      device_rvalid_o <= 1'b0;
      device_rdata_o  <= '0;
    end else begin
      device_rvalid_o <= device_req_i;
      if (rd) device_rdata_o <= rdata_d;
    end
  end

  assign irq_o = |(irq_status_q & irq_en_q);

  logic unused_device_addr;
  assign unused_device_addr = ^device_addr_i[31:12];
endmodule
