// cache_tagrow.v: the cache's set logic as cache_ctl.cpp has it: the hit compare, the victim, the touch (true LRU),
// and the tag row written (touch, a fill write's dirty, a discard's invalidation, the miss handler's install and
// fail). Combinational: a cross-check of count.py's operator costs.
module cache_tagrow(input [215:0] q, input [215:0] mrow, input [21:0] tag, input [21:0] mtag,
                    input slot_here, input [2:0] slot_way, input [2:0] mway, input [2:0] src, input m_alloc,
                    output hit, output [2:0] hway, output [2:0] vway, output [215:0] wrow);
  // row layout: tag[w] at 22w, valid at 176+w, dirty at 184+w, age[w] at 192+3w
  wire [7:0] valid = q[183:176];
  reg h; reg [2:0] hw, vw; reg vfound; integer w;
  always @* begin
    h = 0; hw = 0;
    for (w = 7; w >= 0; w = w - 1) if (valid[w] && q[22*w +: 22] == tag) begin h = 1; hw = w; end
    vfound = 0; vw = 0;
    for (w = 7; w >= 0; w = w - 1) if (!valid[w]) begin vfound = 1; vw = w; end
    if (!vfound)
      for (w = 0; w < 8; w = w + 1)
        if (!(slot_here && slot_way == w) && (q[192+3*w +: 3] == 7 || (q[192+3*w +: 3] == 6 && slot_here &&
            q[192+3*slot_way +: 3] == 7))) vw = w;
  end
  assign hit = h; assign hway = hw; assign vway = vw;
  // src: 0 touch hw, 1 touch hw + dirty, 2 invalidate hw, 3 install mway, 4 fail mway
  reg [215:0] r; reg [2:0] tw, a; integer k;
  always @* begin
    r = (src >= 3) ? mrow : q;
    tw = (src >= 3) ? mway : hw;
    if (src == 3) begin r[22*tw +: 22] = mtag; r[176+tw] = 1; r[184+tw] = m_alloc; end
    if (src == 1) r[184+tw] = 1;
    if (src == 2 || src == 4) begin r[176+tw] = 0; r[184+tw] = 0; end
    if (src <= 1 || src == 3) begin
      a = r[192+3*tw +: 3];
      for (k = 0; k < 8; k = k + 1) if (r[192+3*k +: 3] < a) r[192+3*k +: 3] = r[192+3*k +: 3] + 1;
      r[192+3*tw +: 3] = 0;
    end
  end
  assign wrow = r;
endmodule
