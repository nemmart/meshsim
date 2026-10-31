#include "tracer/trace_owner.cpp"
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <string>
using namespace tracer; using namespace tracer::isa; using namespace tracer::isa::virt;
static VirtualBytecode *CODE;
static i32 g_so = 0;   // shift_offset for the next vector (821e1bd: ShiftData's offset)
static const u64 BASE=0x80000000ull, PC=BASE+0x100;
struct V { OpCode op; u64 a,b; bool ai,bi; u64 rdinit; u64 pc; u8 inc; std::vector<u64> log; const char* note; int rdreg; u64 rdimm; bool rdimmflag; };
static std::string hx(u64 v){ char b[32]; snprintf(b,32,"0x%016llx",(unsigned long long)v); return b; }
static void run(const V& v){
  int fd[2]; pipe(fd);
  pid_t p=fork();
  if(p==0){ close(fd[0]);
    InterpreterState st{}; st.code=CODE; st.risc_pc=v.pc; st.instr_idx=CODE->get_instr_idx(v.pc); st.cycle=0;
    st.ram=nullptr; st.mem_log=std::span<const MemWord>(v.log.data(),v.log.size()); st.mem_log_idx=0;
    // registers: rs1 in x6, rs2 in x7, rd x5 (or v.rdreg)
    st.regs[6]=v.a; st.regs[7]=v.b; int rd=v.rdreg; if(rd>0 && rd!=6 && rd!=7) st.regs[rd]=v.rdinit;
    VirtualInstruction in{}; in.risc_pc=v.pc; in.opcode=v.op;
    in.rd = v.rdimmflag? v.rdimm : (u64)rd; in.rd_is_imm=v.rdimmflag;
    in.rs1 = v.ai? v.a : 6; in.rs1_is_imm=v.ai;
    in.rs2 = v.bi? v.b : 7; in.rs2_is_imm=v.bi;
    in.pc_increment=v.inc; in.virt_seq_remaining=0; in.shift_offset=g_so;
    u32 idx0=st.instr_idx;
    execute_instruction<false>(in, st);
    const char* nx = st.risc_pc==0 ? "end: pc 0, instr_idx 0" : (st.risc_pc==v.pc+v.inc && st.instr_idx==idx0+1) ? "sequential" : "transfer";
    char out[256]; snprintf(out,256,"%s|%s|%s|%llu", v.rdimmflag?"—":hx(st.regs[rd]).c_str(), hx(st.risc_pc).c_str(), nx, (unsigned long long)st.mem_log_idx);
    write(fd[1],out,strlen(out)); _exit(0);
  }
  close(fd[1]); char buf[300]={0}; int n=read(fd[0],buf,299); close(fd[0]); int status; waitpid(p,&status,0);
  std::string res = (WIFEXITED(status)&&WEXITSTATUS(status)==0&&n>0)? std::string(buf) : std::string("ABORT|—|—|—");
  printf("%s\t%s\t%s\t%s\t%s\t%s\n", opcode_to_string(v.op), (v.ai?("imm "+hx(v.a)):("rs1 "+hx(v.a))).c_str(), (v.bi?("imm "+hx(v.b)):("rs2 "+hx(v.b))).c_str(), res.c_str(), v.note?v.note:"", v.rdimmflag?("rd-slot imm "+hx(v.rdimm)).c_str():"");
}
int main(){
  std::vector<u8> code(0x400,0); for(size_t i=0;i<code.size();i+=4){ u32 w=0x00000013; memcpy(&code[i],&w,4);} // addi x0,x0,0
  static VirtualBytecode bc=VirtualBytecode::from_riscv_code(BASE, std::span<const u8>(code.data(),code.size())); CODE=&bc;
  const u64 M1=~0ull, MIN=0x8000000000000000ull, MAX=0x7fffffffffffffffull, I32M=0xffffffff80000000ull;
  auto R=[&](OpCode o,u64 a,u64 b,const char*n=""){ run(V{o,a,b,false,false,0xdeadull,PC,4,{},n,5,0,false}); };
  auto I=[&](OpCode o,u64 a,u64 b,const char*n=""){ run(V{o,a,b,false,true,0xdeadull,PC,4,{},n,5,0,false}); };
  auto U=[&](OpCode o,u64 imm,u8 inc,const char*n=""){ run(V{o,imm,0,true,true,0xdeadull,PC,inc,{},n,5,0,false}); };
  auto B=[&](OpCode o,u64 a,u64 b,u64 imm,const char*n=""){ run(V{o,a,b,false,false,0,PC,4,{},n,5,imm,true}); };
  auto H=[&](OpCode o,u64 a,u64 imm,const char*n=""){ run(V{o,a,imm,false,true,0,PC,4,{},n,5,0,true}); };
  // arithmetic
  R(ADD,MAX,1,"wraps"); R(ADD,M1,1); I(ADDI,MIN,M1,"imm -1");
  R(SUB,0,1); R(SUB,MIN,1,"wraps");
  R(XOR,0xf0f0f0f0f0f0f0f0ull,M1); R(OR,0xf0ull,0x0full); R(AND,M1,0x8000000000000001ull); I(XORI,5,M1,"imm -1"); I(ORI,0,0xfffffffffffff800ull); I(ANDI,M1,0x7ff);
  R(SLT,M1,0); R(SLT,0,M1); R(SLT,MIN,MAX); R(SLT,5,5); I(SLTI,M1,0);
  R(SLTU,M1,0); R(SLTU,0,M1); R(SLTU,1,1); I(SLTIU,0,1,"seqz idiom"); I(SLTIU,5,M1,"imm -1 = 2^64-1 unsigned");
  R(ADDW,0x7fffffffull,1,"overflow to INT32_MIN"); R(ADDW,0xffffffffull,1); R(ADDW,0x100000005ull,0,"upper bits dropped"); I(ADDIW,MIN,0);
  R(SUBW,I32M,1); R(SUBW,0,1); R(SUBW,0x80000000ull,0x100000000ull);
  // multiply
  R(MUL,MIN,M1); R(MUL,M1,M1); R(MUL,1ull<<32,1ull<<32); I(VirtualMULI,3,1ull<<63,"SLLI 63 form");
  R(MULW,0x10000,0x10000); R(MULW,0x8000,0x10000); R(MULW,M1,M1); R(MULW,MIN,2); I(VirtualMULIW,0x7fffffff,2,"SLLIW 1 form");
  R(MULHU,M1,M1,"top of range"); R(MULHU,MIN,2); R(MULHU,1ull<<32,1ull<<32); R(MULHU,M1,1); R(MULHU,MIN,MIN);
  // shifts/rotates
  for(u64 s: {0ull,1ull,31ull,32ull,63ull,64ull,65ull}){ char n[32]; snprintf(n,32,"rs1&63=%llu",(unsigned long long)(s&63)); R(VirtualPow2,s,0,n);} 
  for(u64 s: {0ull,31ull,32ull,63ull}) R(VirtualPow2W,s,0,"zero-extended");
  for(u64 s: {0ull,1ull,31ull,32ull,63ull,64ull}) R(VirtualShiftRightBitmask,s,0);
  for(u64 s: {0ull,1ull,31ull,32ull,63ull}) R(VirtualShiftRightBitmaskW,s,0);
  for(u64 m: {1ull,1ull<<31,1ull<<32,1ull<<63,0xfffffffffffffff0ull,0ull}){ R(VirtualSRL,MIN|1,m,m==0?"tz(0)=64: C++ UB":""); I(VirtualSRLI,MIN|1,m,m==0?"tz(0)=64: C++ UB":""); }
  for(u64 m: {1ull,1ull<<31,1ull<<32,1ull<<63,0ull}){ R(VirtualSRA,MIN|1,m,m==0?"tz(0)=64: C++ UB":""); I(VirtualSRAI,MIN|1,m,m==0?"tz(0)=64: C++ UB":""); }
  for(u64 m: {1ull,1ull<<31,1ull<<32,0ull}){ R(VirtualSRLW,0xffffffff80000001ull,m,"tz&31; result sign-extended"); I(VirtualSRLIW,0x0000000180000001ull,m); }
  for(u64 m: {1ull,1ull<<31,1ull<<32,0ull}){ R(VirtualSRAW,0x80000001ull,m,"tz&31"); I(VirtualSRAIW,0x7fffffff,m); }
  for(u64 m: {2ull,1ull<<63,1ull<<32,0ull}) I(VirtualROTRI,1,m,m==0?"tz(0)=64: rotr by 64 = identity":"");
  for(u64 m: {2ull,1ull<<31,1ull<<32,1ull<<40,0ull}) I(VirtualROTRIW,0xabcd000000000001ull,m,"min(32,tz); zero-extended");
  R(VirtualRev8W,0x0102030405060708ull,0); I(VirtualRev8W,0x0102030405060708ull,0);
  R(VirtualXORROT63,1,0); R(VirtualXORROT32,1,0); R(VirtualXORROT24,1,0); R(VirtualXORROT16,0xff,0xff,"xor cancels");
  R(VirtualXORROTW16,0xffffffff00000001ull,0,"upper bits dropped; zero-extended"); R(VirtualXORROTW12,1,0); R(VirtualXORROTW8,1,0); R(VirtualXORROTW7,1,0);
  R(VirtualXORROTW22,1,0); R(VirtualXORROTW19,1,0); R(VirtualXORROTW6,1,0);
  R(VirtualXORROTL1,0,MIN); R(VirtualXORROTL1,M1,1);
  R(ANDN,M1,0xff); R(ANDN,0,M1);
  // memory helpers
  for(u64 a: {0ull,5ull,7ull,8ull}) I(VirtualWindowMaskB,a,0);
  for(u64 a: {0ull,6ull,7ull}) I(VirtualWindowMaskH,a,0);
  for(u64 a: {0ull,3ull,4ull,7ull}) I(VirtualWindowMaskW,a,0);
  I(VirtualWindowMaskB,M1,6,"rs1+imm wraps to 5");
  // ShiftData at 821e1bd: rs2 is the base register, the lane comes from base + shift_offset
  auto S=[&](OpCode o,u64 val,u64 base,i32 so,const char*n=""){ g_so=so; R(o,val,base,n); g_so=0; };
  S(VirtualShiftDataB,0x1ff,5,0); S(VirtualShiftDataB,0x1ff,8,7,"base+off&7=7"); S(VirtualShiftDataB,0x1ff,9,-2,"negative offset: lane 7");
  S(VirtualShiftDataH,0x1ffff,7,0,"(base+off)&6=6"); S(VirtualShiftDataH,0x1ffff,0,2,"lane 2"); S(VirtualShiftDataH,0x1ffff,4,-4,"lane 0");
  S(VirtualShiftDataW,0x1ffffffffull,4,0); S(VirtualShiftDataW,0x1ffffffffull,0,4,"lane 4 via offset"); S(VirtualShiftDataW,0x1ffffffffull,8,-4,"lane 4, negative offset");
  R(VirtualPext,0x1234567890abcdefull,0xff00ull); R(VirtualPext,0x1234567890abcdefull,0xffffffff00000000ull); R(VirtualPext,M1,0xaull,"non-contiguous mask (never emitted)"); R(VirtualPext,0x123456789abcdef0ull,0xf0f0f0f0f0f0f0f0ull,"8 runs"); R(VirtualPext,0x8000000000000001ull,0x8000000000000001ull,"two single-bit runs"); R(VirtualPext,0xdeadbeefcafef00dull,0x5555555555555555ull,"32 runs"); R(VirtualPext,M1,0,"mask 0");
  R(VirtualPextSigned,0x8000ull,0xff00ull); R(VirtualPextSigned,0x7f00ull,0xff00ull); R(VirtualPextSigned,0x80000000ull,0xffffffffull); R(VirtualPextSigned,0xffff000000000000ull,0xffff000000000000ull,"top window: no extension needed"); R(VirtualPextSigned,M1,0,"mask 0"); R(VirtualPextSigned,0x8000000000000001ull,0x8000000000000001ull,"non-contiguous: sign from the last bit placed"); R(VirtualPextSigned,0x00f000000000f000ull,0x00f000000000f000ull,"two runs, top bit set"); R(VirtualPextSigned,0x0070000000000000ull,0x00f000000000ff00ull,"two runs, top bit clear");
  // misc ALU
  R(VirtualNegateIf,M1,5); R(VirtualNegateIf,0,5); R(VirtualNegateIf,MIN,MIN); R(VirtualNegateIf,MIN,0);
  R(VirtualMovsign,MIN,0); R(VirtualMovsign,MAX,0); R(VirtualMovsign,0,0);
  R(VirtualSignExtendWord,0x80000000ull,0); R(VirtualSignExtendWord,0x17fffffffull,0);
  R(VirtualZeroExtendWord,M1,0); R(VirtualZeroExtendWord,I32M,0);
  // advice (tracer-computed; replayer takes the log)
  R(VirtualAdviceDivU,5,0); R(VirtualAdviceDivU,M1,2); R(VirtualAdviceDiv,MIN,M1); R(VirtualAdviceDiv,(u64)-7,2); R(VirtualAdviceDiv,5,0);
  R(VirtualAdviceRem,(u64)-7,2,"quotient magnitude, not the remainder"); R(VirtualAdviceRem,MIN,M1); R(VirtualAdviceRem,5,0);
  R(VirtualAdviceDivUW,5,0); R(VirtualAdviceDivUW,0x1ffffffffull,0x100000002ull,"low words only");
  R(VirtualAdviceDivW,I32M,M1,"zero-extended INT32_MIN"); R(VirtualAdviceDivW,(u64)-7,2); R(VirtualAdviceDivW,5,0);
  R(VirtualAdviceRemW,(u64)-7,2); R(VirtualAdviceRemW,I32M,M1); R(VirtualAdviceRemW,5,0);
  R(VirtualAdviceRemU,7,2,"dead case: computes the quotient"); R(VirtualAdviceRemUW,7,2,"dead case");
  // assertions
  B(VirtualAssertEQ,1,1,0); B(VirtualAssertEQ,1,2,0);
  B(VirtualAssertLTE,0,M1,0); B(VirtualAssertLTE,M1,0,0,"unsigned compare"); B(VirtualAssertLTE,5,5,0);
  B(VirtualAssertValidDiv0,0,M1,0); B(VirtualAssertValidDiv0,0,5,0); B(VirtualAssertValidDiv0,3,5,0);
  B(VirtualAssertValidUnsignedRemainder,2,3,0); B(VirtualAssertValidUnsignedRemainder,5,3,0); B(VirtualAssertValidUnsignedRemainder,5,0,0,"divisor 0: always passes");
  B(VirtualAssertMulUNoOverflow,1ull<<32,1ull<<32,0); B(VirtualAssertMulUNoOverflow,0xffffffffull,0x100000001ull,0,"product 2^64-1");
  // branches (imm in rd slot)
  for(OpCode o: {BEQ,BNE,BLT,BGE,BLTU,BGEU}){ B(o,M1,0,16); B(o,5,5,(u64)-16); B(o,5,5,0,"imm 0: target = pc"); }
  B(BEQ,5,5,0x10,"pc_increment 4"); 
  // jumps
  U(JAL,16,4); U(JAL,(u64)-16,2,"compressed: link pc+2"); U(JAL,0,4,"imm 0: program end");
  run(V{JALR,PC+0x21,0,false,true,0,PC,4,{},"bit 0 cleared",5,0,false});
  run(V{JALR,PC,0,false,true,0,PC,4,{},"target = pc: sequential",5,0,false});
  run(V{JALR,PC+0x20,(u64)-0x20,false,true,0,PC,2,{},"rs1+imm = pc, compressed",5,0,false});
  run(V{JALR,PC+0x40,0,false,true,0,PC,4,{},"rd = rs1: reads old rs1",6,0,false});
  U(AUIPC,0x1000,4); U(AUIPC,0xfffffffffffff000ull,4,"negative"); U(LUI,0xffffffff80000000ull,4); U(LUI,0x12345000,4);
  run(V{ADD,1,2,false,false,0,PC,0,{},"not the last row: pc_increment 0",5,0,false});
  run(V{JALR,0,0,false,true,0,PC,4,{},"target 0: program end",5,0,false});
  run(V{JAL,0,0,true,true,0,PC,4,{},"EBREAK's JAL v40, 0: program end",40,0,false});
  // memory, advice, misc
  run(V{LD,0x1000,8,false,true,0,PC,4,{0x1122334455667788ull},"rd = log word",5,0,false});
  run(V{LD,0x1000,8,false,true,0,PC,4,{0x1122334455667788ull},"rd = x0: dropped",0,0,false});
  run(V{SD,0x1000,0xabc,false,false,0,PC,4,{0x99ull},"pops the log; writes nothing",5,8,true});
  run(V{VirtualAdvice,0,0,true,true,0,PC,4,{0x42ull},"rd = log word",5,0,false});
  run(V{VirtualHostIO,0,0,true,true,0,PC,4,{},"operand-free at 821e1bd: no register read or write",5,0,true});
  // the narrow memory rows (821e1bd): one log word each, like LD/SD; the address is (rs1 + imm) rounded down to 8
  for(OpCode o: {VirtualLoadB,VirtualLoadH,VirtualLoadW}) run(V{o,0x1003,5,false,true,0,PC,4,{0x1122334455667788ull},"rd = log word (the doubleword)",5,0,false});
  run(V{VirtualLoadW,0x1000,4,false,true,0,PC,4,{0x99ull},"rd = x0: dropped",0,0,false});
  for(OpCode o: {VirtualStoreB,VirtualStoreH,VirtualStoreW}) run(V{o,0x1000,0xabc,false,false,0,PC,4,{0x99ull},"pops the log; writes nothing",5,6,true});
  run(V{FENCE,0,0,true,true,0,PC,4,{},"",5,0,true});
}
