// cache_line.v: the walker's entry out of a 1,024-bit line, and an update's patch into it (entry_of, set_entry_of).
module cache_line(input [1023:0] line, input [4:0] i, input [31:0] e, output [31:0] entry, output [1023:0] patched);
  assign entry = line[32*i +: 32];
  genvar g;
  generate for (g = 0; g < 32; g = g + 1) begin : p
    assign patched[32*g +: 32] = (i == g) ? e : line[32*g +: 32];
  end endgenerate
endmodule
