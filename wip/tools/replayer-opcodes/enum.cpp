#include "tracer/isa/virtual.cpp"
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
using namespace tracer; using namespace tracer::isa;
static const char* nm(OpCode o){ static std::string s; s=isa::opcode_to_string(o); return s.c_str(); }
int main(){
  std::vector<VirtualInstruction> buf(5000);
  // per row opcode: set of "form|source|last"
  std::map<int,std::map<std::string,long>> occ;
  auto run=[&](Instruction in,const char* tag){
    u64 n=expand_virt_seq(std::span<VirtualInstruction>(buf.data(),buf.size()),in);
    for(u64 i=0;i<n;i++){ auto &r=buf[i];
      char f[200];
      snprintf(f,sizeof f,"rd=%s rs1=%s rs2=%s | src=%s%s | %s",
        r.rd_is_imm?"I":(r.rd==0?"R(x0)":"R"), r.rs1_is_imm?"I":(r.rs1==0?"R(x0)":"R"), r.rs2_is_imm?"I":(r.rs2==0?"R(x0)":"R"),
        nm(in.opcode),tag, r.virt_seq_remaining==0?"LAST":"notlast");
      occ[(int)r.opcode][f]++;
    }
  };
  for(int op=1; op<(int)OpCode::NUM_OPCODES; op++){
    OpCode o=(OpCode)op; if(isa::is_virtual(o)) continue;
    std::string s=isa::opcode_to_string(o);
    if(o==OpCode::UNIMP) continue;
    // choose format
    bool sys = (o==OpCode::ECALL||o==OpCode::EBREAK||o==OpCode::MRET||o==OpCode::FENCE);
    bool utype=(o==OpCode::LUI||o==OpCode::AUIPC||o==OpCode::JAL);
    bool stype=(o==OpCode::SB||o==OpCode::SH||o==OpCode::SW||o==OpCode::SD||(op>=(int)OpCode::BEQ&&op<=(int)OpCode::BGEU)||o==OpCode::VirtualAssertEQ);
    bool csr=(o==OpCode::CSRRW||o==OpCode::CSRRS);
    bool itype = (s.back()=='I' || (s.size()>2 && s.substr(s.size()-2)=="IW") || s=="SLTIU") && o!=OpCode::JAL;
    itype = itype || o==OpCode::JALR || o==OpCode::VirtualHostIO || o==OpCode::VirtualRev8W ||
      o==OpCode::LB||o==OpCode::LH||o==OpCode::LW||o==OpCode::LBU||o==OpCode::LHU||o==OpCode::LWU||o==OpCode::LD;
    for(int rd: {5,0}){
      Instruction in;
      if(sys) in=Instruction(o,0,0,0,true,true,true);
      else if(utype) in=Instruction(o,rd,o==OpCode::JAL?8:0x1000,0,false,true,true);
      else if(stype) in=Instruction(o,8,6,7,true,false,false);
      else if(csr) in=Instruction(o,rd,6,0x341,false,false,true);
      else if(itype) in=Instruction(o,rd,6,3,false,false,true);
      else in=Instruction(o,rd,6,7,false,false,false);
      if((sys||stype) && rd==0) continue;
      run(in, rd==0?" (rd=x0)":"");
      if(o==OpCode::JAL && rd==5){ Instruction j0(o,5,0,0,false,true,true); run(j0," (imm=0)"); }
      if(itype && (s=="SLLI"||s=="SRLI"||s=="SRAI"||s=="SLLIW"||s=="SRLIW"||s=="SRAIW") && rd==5)
        for(int sh: {0,31,32,63}){ Instruction k(o,5,6,sh,false,false,true); run(k,(" (sh="+std::to_string(sh)+")").c_str()); }
    }
  }
  for(auto &[op,m]: occ){ printf("== %d %s\n",op,nm((OpCode)op)); for(auto&[f,c]:m) printf("   %s  x%ld\n",f.c_str(),c); }
}
