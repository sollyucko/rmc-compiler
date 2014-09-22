// BUG: this whole thing depends on the specifics of how the clang version I
// am using emits llvm bitcode for the hacky RMC protocol.
// We rely on how basic blocks get named, on the labels forcing things
// into their own basic blocks, and probably will rely on this block
// having one predecessor and one successor. We could probably even
// force those to be empty without too much work by adding more labels...



#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/raw_ostream.h>

#include <ostream>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace llvm;

namespace {

/* To do bogus data dependencies, we will emit inline assembly
  instructions. This is sort of tasteless; we should add an intrinsic;
  but it means we don't need to modify llvm. This is what the
  instruction should look like (probably the constraints aren't
  right?):

  %2 = call i32 asm sideeffect "eor $0, $0;", "=r,0"(i32 %1) #2, !srcloc !11

  Also for ctrlisb we do:
  call void asm sideeffect "cmp $0, $0;beq 1f;1: isb", "r,~{memory}"(i32 %1) #2, !srcloc !4

  And for dmb:
  call void asm sideeffect "dmb", "~{memory}"() #2, !srcloc !5


  We can probably do a bogus inline asm on x86 to prevent reordering:
  %2 = call i32 asm sideeffect "", "=r,r,0,~{dirflag},~{fpsr},~{flags}"(i32 %1, i32 %0) #3, !srcloc !9

  What about this makes the right value come out:
    =r specifies an output parameter and the 0 says that that input parameter
    is paired with param 0.

  Hm. Does this *actually* work?? What does "sideeffect" actually mean
  for asm and do we need it.
*/


//// Some auxillary data structures
enum RMCEdgeType {
  VisbilityEdge,
  ExecutionEdge
};

raw_ostream& operator<<(raw_ostream& os, const RMCEdgeType& t) {
  switch (t) {
    case VisbilityEdge:
      os << "v";
      break;
    case ExecutionEdge:
      os << "x";
      break;
    default:
      os << "?";
      break;
  }
  return os;
}


struct RMCEdge {
  RMCEdgeType edgeType;
  BasicBlock *src;
  BasicBlock *dst;

  bool operator<(const RMCEdge& rhs) const {
    return std::tie(edgeType, src, dst)
      < std::tie(rhs.edgeType, rhs.src, rhs.dst);
  }

  void print(raw_ostream &os) const {
    // substr(5) is to drop "_rmc_" from the front
    os << src->getName().substr(5) << " -" << edgeType <<
      "-> " << dst->getName().substr(5);
  }
};

raw_ostream& operator<<(raw_ostream& os, const RMCEdge& e) {
  e.print(os);
  return os;
}

//// Actual code for the pass
class RMCPass : public FunctionPass {
private:
public:
  static char ID;
  RMCPass() : FunctionPass(ID) {}
  ~RMCPass() { }
  std::vector<RMCEdge> findEdges(Function &F);
  virtual bool runOnFunction(Function &F);
};

bool nameMatches(StringRef blockName, StringRef target) {
  StringRef name = "_rmc_" + target.str() + "_";
  if (!blockName.startswith(name)) return false;
  // Now make sure the rest is an int
  APInt dummy;
  return !blockName.drop_front(name.size()).getAsInteger(10, dummy);
}

StringRef getStringArg(Value *v) {
  const Value *g = cast<Constant>(v)->stripPointerCasts();
  const Constant *ptr = cast<GlobalVariable>(g)->getInitializer();
  StringRef str = cast<ConstantDataSequential>(ptr)->getAsString();
  // Drop the '\0' at the end
  return str.drop_back();
}

std::vector<RMCEdge> RMCPass::findEdges(Function &F) {
  std::vector<RMCEdge> edges;

  for (inst_iterator is = inst_begin(F), ie = inst_end(F); is != ie;) {
    // Grab the instruction and advance the iterator at the start, since
    // we might delete the instruction.
    Instruction *i = &*is;
    is++;

    CallInst *load = dyn_cast<CallInst>(i);
    if (!load) continue;
    Function *target = load->getCalledFunction();
    // We look for calls to the bogus function
    // __rmc_edge_register, pull out the information about them,
    // and delete the calls.
    if (!target || target->getName() != "__rmc_edge_register") continue;

    // Pull out what the operands have to be.
    // We just assert if something is wrong, which is not great UX.
    bool isVisibility = cast<ConstantInt>(load->getOperand(0))
      ->getValue().getBoolValue();
    RMCEdgeType edgeType = isVisibility ? VisbilityEdge : ExecutionEdge;
    StringRef srcName = getStringArg(load->getOperand(1));
    StringRef dstName = getStringArg(load->getOperand(2));

    // Since multiple blocks can have the same tag, we search for
    // them by name.
    // We could make this more efficient by building maps but I don't think
    // it is going to matter.
    for (auto & src : F) {
      if (!nameMatches(src.getName(), srcName)) continue;
      for (auto & dst : F) {
        if (!nameMatches(dst.getName(), dstName)) continue;

        edges.push_back({edgeType, &src, &dst});
      }
    }

    // Delete the bogus call.
    i->eraseFromParent();
  }

  return edges;
}

bool RMCPass::runOnFunction(Function &F) {
  auto edges = findEdges(F);

  for (auto & edge : edges) {
    errs() << "Found an edge: " << edge << "\n";
  }

  return !edges.empty();
}

char RMCPass::ID = 0;
RegisterPass<RMCPass> X("rmc-pass", "RMC");
}
