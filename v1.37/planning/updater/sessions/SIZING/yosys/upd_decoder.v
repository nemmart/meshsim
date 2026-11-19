// upd_decoder.v: the Updater's decoder as updater_ctl.h's edge has it (17.10.2): the bit buffer, its count, the LRU
// table, one code a cycle, a refill of 128 bits at 0 to 65. A cross-check of count.py's operator costs, not a design.
module upd_decoder(input clk, input lru_en, input en, input out_free, input wc_nz, input wc_after_nz,
                   input fifo_nz, input [127:0] word, input clear,
                   output reg dec, output reg [63:0] v, output reg refill);
  reg [193:0] bb; reg [7:0] nb; reg [63:0] lru [0:31];
  function [6:0] need_of(input [1:0] b, input [7:0] n, input l);
    if (!l) need_of = 64; else if (n < 1) need_of = 0; else if (b[0]) need_of = 6;
    else if (n < 2) need_of = 0; else need_of = b[1] ? 66 : 34;
  endfunction
  wire [6:0] need = need_of(bb[1:0], nb, lru_en);
  reg [193:0] sb; reg [7:0] nb1; reg [6:0] need2; integer k; reg [4:0] i;
  always @* begin
    dec = en && out_free && wc_nz && need != 0 && nb >= need;
    i = bb[5:1];
    if (!lru_en) v = bb[63:0];
    else if (need == 6) v = lru[i];
    else if (need == 34) v = {32'b0, bb[33:2]};
    else v = bb[65:2];
    sb = dec ? bb >> need : bb;
    nb1 = dec ? nb - need : nb;
    need2 = need_of(sb[1:0], nb1, lru_en);
    refill = en && wc_after_nz && fifo_nz && (need2 == 0 || nb1 < need2);
  end
  always @(posedge clk) begin
    if (clear) begin bb <= 0; nb <= 0; end
    else begin bb <= sb | (refill ? ({66'b0, word} << nb1) : 194'b0); nb <= refill ? nb1 + 128 : nb1; end
    for (k = 1; k < 32; k = k + 1)
      if (clear) lru[k] <= 0;
      else if (dec && lru_en && (need != 6 || k <= i)) lru[k] <= lru[k-1];
    if (clear) lru[0] <= 0; else if (dec && lru_en) lru[0] <= v;
  end
endmodule
