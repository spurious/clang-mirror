//===--- CFG.cpp - Classes for representing and building CFGs----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Ted Kremenek and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the CFG and CFGBuilder classes for representing and
//  building Control-Flow Graphs (CFGs) from ASTs.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CFG.h"
#include "clang/AST/Expr.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/ADT/DenseMap.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
using namespace clang;

namespace {

// SaveAndRestore - A utility class that uses RIIA to save and restore
//  the value of a variable.
template<typename T>
struct SaveAndRestore {
  SaveAndRestore(T& x) : X(x), old_value(x) {}
  ~SaveAndRestore() { X = old_value; }
  
  T& X;
  T old_value;
};
  
/// CFGBuilder - This class is implements CFG construction from an AST.
///   The builder is stateful: an instance of the builder should be used to only
///   construct a single CFG.
///
///   Example usage:
///
///     CFGBuilder builder;
///     CFG* cfg = builder.BuildAST(stmt1);
///
///  CFG construction is done via a recursive walk of an AST.
///  We actually parse the AST in reverse order so that the successor
///  of a basic block is constructed prior to its predecessor.  This
///  allows us to nicely capture implicit fall-throughs without extra
///  basic blocks.
///
class CFGBuilder : public StmtVisitor<CFGBuilder,CFGBlock*> {    
  CFG* cfg;
  CFGBlock* Block;
  CFGBlock* Succ;
  CFGBlock* ContinueTargetBlock;
  CFGBlock* BreakTargetBlock;
  CFGBlock* SwitchTerminatedBlock;
  unsigned NumBlocks;
  
  typedef llvm::DenseMap<LabelStmt*,CFGBlock*> LabelMapTy;
  LabelMapTy LabelMap;
  
  typedef std::vector<CFGBlock*> BackpatchBlocksTy;
  BackpatchBlocksTy BackpatchBlocks;
  
public:  
  explicit CFGBuilder() : cfg(NULL), Block(NULL), Succ(NULL),
                          ContinueTargetBlock(NULL), BreakTargetBlock(NULL),
                          SwitchTerminatedBlock(NULL),
                          NumBlocks(0) {
    // Create an empty CFG.
    cfg = new CFG();                        
  }
  
  ~CFGBuilder() { delete cfg; }
  
  // buildCFG - Used by external clients to construct the CFG.
  CFG* buildCFG(Stmt* Statement);
  
  // Visitors to walk an AST and construct the CFG.  Called by
  // buildCFG.  Do not call directly!
  
  CFGBlock* VisitStmt(Stmt* Statement);
  CFGBlock* VisitNullStmt(NullStmt* Statement);
  CFGBlock* VisitCompoundStmt(CompoundStmt* C);
  CFGBlock* VisitIfStmt(IfStmt* I);
  CFGBlock* VisitReturnStmt(ReturnStmt* R);
  CFGBlock* VisitLabelStmt(LabelStmt* L);
  CFGBlock* VisitGotoStmt(GotoStmt* G);
  CFGBlock* VisitForStmt(ForStmt* F);
  CFGBlock* VisitWhileStmt(WhileStmt* W);
  CFGBlock* VisitDoStmt(DoStmt* D);
  CFGBlock* VisitContinueStmt(ContinueStmt* C);
  CFGBlock* VisitBreakStmt(BreakStmt* B);
  CFGBlock* VisitSwitchStmt(SwitchStmt* S);
  CFGBlock* VisitSwitchCase(SwitchCase* S);
  
private:
  CFGBlock* createBlock(bool add_successor = true);
  void FinishBlock(CFGBlock* B);
  
};
    
/// BuildCFG - Constructs a CFG from an AST (a Stmt*).  The AST can
///  represent an arbitrary statement.  Examples include a single expression
///  or a function body (compound statement).  The ownership of the returned
///  CFG is transferred to the caller.  If CFG construction fails, this method
///  returns NULL.
CFG* CFGBuilder::buildCFG(Stmt* Statement) {
  if (!Statement) return NULL;

  // Create an empty block that will serve as the exit block for the CFG.
  // Since this is the first block added to the CFG, it will be implicitly
  // registered as the exit block.
  Block = createBlock();
  assert (Block == &cfg->getExit());
  
  // Visit the statements and create the CFG.
  if (CFGBlock* B = Visit(Statement)) {
    // Finalize the last constructed block.  This usually involves
    // reversing the order of the statements in the block.
    FinishBlock(B);
    cfg->setEntry(B);
    
    // Backpatch the gotos whose label -> block mappings we didn't know
    // when we encountered them.
    for (BackpatchBlocksTy::iterator I = BackpatchBlocks.begin(), 
         E = BackpatchBlocks.end(); I != E; ++I ) {
     
      CFGBlock* B = *I;
      GotoStmt* G = cast<GotoStmt>(B->getTerminator());
      LabelMapTy::iterator LI = LabelMap.find(G->getLabel());

      // If there is no target for the goto, then we are looking at an
      // incomplete AST.  Handle this by not registering a successor.
      if (LI == LabelMap.end()) continue;
      
      B->addSuccessor(LI->second);                   
    }                          
    
    // NULL out cfg so that repeated calls
    CFG* t = cfg;
    cfg = NULL;
    return t;
  }
  else return NULL;
}
  
/// createBlock - Used to lazily create blocks that are connected
///  to the current (global) succcessor.
CFGBlock* CFGBuilder::createBlock(bool add_successor) { 
  CFGBlock* B = cfg->createBlock(NumBlocks++);
  if (add_successor && Succ) B->addSuccessor(Succ);
  return B;
}
  
/// FinishBlock - When the last statement has been added to the block,
///  usually we must reverse the statements because they have been inserted
///  in reverse order.  When processing labels, however, there are cases
///  in the recursion where we may have already reversed the statements
///  in a block.  This method safely tidies up a block: if the block
///  has a label at the front, it has already been reversed.  Otherwise,
///  we reverse it.
void CFGBuilder::FinishBlock(CFGBlock* B) {
  assert (B);
  CFGBlock::iterator I = B->begin();
  if (I != B->end()) {
    Stmt* S = *I;

    if (isa<LabelStmt>(S) || isa<SwitchCase>(S))
      return;
      
    B->reverseStmts();
  }
}

/// VisitStmt - Handle statements with no branching control flow.
CFGBlock* CFGBuilder::VisitStmt(Stmt* Statement) {
  // We cannot assume that we are in the middle of a basic block, since
  // the CFG might only be constructed for this single statement.  If
  // we have no current basic block, just create one lazily.
  if (!Block) Block = createBlock();
  
  // Simply add the statement to the current block.  We actually
  // insert statements in reverse order; this order is reversed later
  // when processing the containing element in the AST.
  Block->appendStmt(Statement);
  
  return Block;
}

CFGBlock* CFGBuilder::VisitNullStmt(NullStmt* Statement) {
  return Block;
}

CFGBlock* CFGBuilder::VisitCompoundStmt(CompoundStmt* C) {
  //   The value returned from this function is the last created CFGBlock
  //   that represents the "entry" point for the translated AST node.
  CFGBlock* LastBlock;
  
  for (CompoundStmt::reverse_body_iterator I = C->body_rbegin(),
       E = C->body_rend(); I != E; ++I )
    // Add the statement to the current block.
    if (!(LastBlock=Visit(*I)))
      return NULL;

  return LastBlock;
}

CFGBlock* CFGBuilder::VisitIfStmt(IfStmt* I) {
  // We may see an if statement in the middle of a basic block, or
  // it may be the first statement we are processing.  In either case,
  // we create a new basic block.  First, we create the blocks for
  // the then...else statements, and then we create the block containing
  // the if statement.  If we were in the middle of a block, we
  // stop processing that block and reverse its statements.  That block
  // is then the implicit successor for the "then" and "else" clauses.
  
  // The block we were proccessing is now finished.  Make it the
  // successor block.
  if (Block) { 
    Succ = Block;
    FinishBlock(Block);
  }
  
  // Process the false branch.  NULL out Block so that the recursive
  // call to Visit will create a new basic block.
  // Null out Block so that all successor
  CFGBlock* ElseBlock = Succ;
  
  if (Stmt* Else = I->getElse()) {
    SaveAndRestore<CFGBlock*> sv(Succ);
    
    // NULL out Block so that the recursive call to Visit will
    // create a new basic block.          
    Block = NULL;
    ElseBlock = Visit(Else);          
    if (!ElseBlock) return NULL;
    FinishBlock(ElseBlock);
  }
  
  // Process the true branch.  NULL out Block so that the recursive
  // call to Visit will create a new basic block.
  // Null out Block so that all successor
  CFGBlock* ThenBlock;
  {
    Stmt* Then = I->getThen();
    assert (Then);
    SaveAndRestore<CFGBlock*> sv(Succ);
    Block = NULL;        
    ThenBlock = Visit(Then);        
    if (!ThenBlock) return NULL;
    FinishBlock(ThenBlock);
  }

  // Now create a new block containing the if statement.        
  Block = createBlock(false);

  // Add the condition as the last statement in the new block.
  Block->appendStmt(I->getCond());
  
  // Set the terminator of the new block to the If statement.
  Block->setTerminator(I);
  
  // Now add the successors.
  Block->addSuccessor(ThenBlock);
  Block->addSuccessor(ElseBlock);

  return Block;
}
    
CFGBlock* CFGBuilder::VisitReturnStmt(ReturnStmt* R) {
  // If we were in the middle of a block we stop processing that block
  // and reverse its statements.
  //
  // NOTE: If a "return" appears in the middle of a block, this means
  //       that the code afterwards is DEAD (unreachable).  We still
  //       keep a basic block for that code; a simple "mark-and-sweep"
  //       from the entry block will be able to report such dead
  //       blocks.
  if (Block) FinishBlock(Block);

  // Create the new block.
  Block = createBlock(false);
  
  // The Exit block is the only successor.
  Block->addSuccessor(&cfg->getExit());
  
  // Add the return statement to the block.
  Block->appendStmt(R);
  
  // Also add the return statement as the terminator.
  Block->setTerminator(R);
  
  return Block;
}

CFGBlock* CFGBuilder::VisitLabelStmt(LabelStmt* L) {
  // Get the block of the labeled statement.  Add it to our map.
  CFGBlock* LabelBlock = Visit(L->getSubStmt());
  assert (LabelBlock);    

  assert (LabelMap.find(L) == LabelMap.end() && "label already in map");
  LabelMap[ L ] = LabelBlock;
  
  // Labels partition blocks, so this is the end of the basic block
  // we were processing (the label is the first statement).    
  LabelBlock->appendStmt(L);
  FinishBlock(LabelBlock);
  
  // We set Block to NULL to allow lazy creation of a new block
  // (if necessary);
  Block = NULL;
  
  // This block is now the implicit successor of other blocks.
  Succ = LabelBlock;
  
  return LabelBlock;
}

CFGBlock* CFGBuilder::VisitGotoStmt(GotoStmt* G) {
  // Goto is a control-flow statement.  Thus we stop processing the
  // current block and create a new one.
  if (Block) FinishBlock(Block);
  Block = createBlock(false);
  Block->setTerminator(G);
  
  // If we already know the mapping to the label block add the
  // successor now.
  LabelMapTy::iterator I = LabelMap.find(G->getLabel());
  
  if (I == LabelMap.end())
    // We will need to backpatch this block later.
    BackpatchBlocks.push_back(Block);
  else
    Block->addSuccessor(I->second);

  return Block;            
}

CFGBlock* CFGBuilder::VisitForStmt(ForStmt* F) {
  // "for" is a control-flow statement.  Thus we stop processing the
  // current block.
  
  CFGBlock* LoopSuccessor = NULL;
  
  if (Block) {
    FinishBlock(Block);
    LoopSuccessor = Block;
  }
  else LoopSuccessor = Succ;
  
  // Create the condition block.
  CFGBlock* ConditionBlock = createBlock(false);
  ConditionBlock->setTerminator(F);
  if (Stmt* C = F->getCond()) ConditionBlock->appendStmt(C);

  // The condition block is the implicit successor for the loop body as
  // well as any code above the loop.
  Succ = ConditionBlock;
  
  // Now create the loop body.
  {
    assert (F->getBody());
    
    // Save the current values for Block, Succ, and continue and break targets
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ),
    save_continue(ContinueTargetBlock),
    save_break(BreakTargetBlock);      

    // All continues within this loop should go to the condition block
    ContinueTargetBlock = ConditionBlock;
    
    // All breaks should go to the code following the loop.
    BreakTargetBlock = LoopSuccessor;
    
    // Create a new block to contain the (bottom) of the loop body.      
    Block = createBlock();
    
    // If we have increment code, insert it at the end of the body block.
    if (Stmt* I = F->getInc()) Block->appendStmt(I);
    
    // Now populate the body block, and in the process create new blocks
    // as we walk the body of the loop.
    CFGBlock* BodyBlock = Visit(F->getBody());      
    assert (BodyBlock);      
    FinishBlock(BodyBlock);
    
    // This new body block is a successor to our condition block.
    ConditionBlock->addSuccessor(BodyBlock);
  }
  
  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  ConditionBlock->addSuccessor(LoopSuccessor);

  // If the loop contains initialization, create a new block for those
  // statements.  This block can also contain statements that precede
  // the loop.
  if (Stmt* I = F->getInit()) {
    Block = createBlock();
    Block->appendStmt(I);
    return Block;
  }
  else {
    // There is no loop initialization.   We are thus basically a while 
    // loop.  NULL out Block to force lazy block construction.
    Block = NULL;
    return ConditionBlock;    
  }
}

CFGBlock* CFGBuilder::VisitWhileStmt(WhileStmt* W) {
  // "while" is a control-flow statement.  Thus we stop processing the
  // current block.
  
  CFGBlock* LoopSuccessor = NULL;
  
  if (Block) {
    FinishBlock(Block);
    LoopSuccessor = Block;
  }
  else LoopSuccessor = Succ;
      
  // Create the condition block.
  CFGBlock* ConditionBlock = createBlock(false);
  ConditionBlock->setTerminator(W);
  if (Stmt* C = W->getCond()) ConditionBlock->appendStmt(C);        
  
  // The condition block is the implicit successor for the loop body as
  // well as any code above the loop.
  Succ = ConditionBlock;
  
  // Process the loop body.
  {
    assert (W->getBody());

    // Save the current values for Block, Succ, and continue and break targets
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ),
                              save_continue(ContinueTargetBlock),
                              save_break(BreakTargetBlock);
          
    // All continues within this loop should go to the condition block
    ContinueTargetBlock = ConditionBlock;
    
    // All breaks should go to the code following the loop.
    BreakTargetBlock = LoopSuccessor;
    
    // NULL out Block to force lazy instantiation of blocks for the body.
    Block = NULL;
    
    // Create the body.  The returned block is the entry to the loop body.
    CFGBlock* BodyBlock = Visit(W->getBody());
    assert (BodyBlock);
    FinishBlock(BodyBlock);
    
    // Add the loop body entry as a successor to the condition.
    ConditionBlock->addSuccessor(BodyBlock);
  }
  
  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  ConditionBlock->addSuccessor(LoopSuccessor);
  
  // There can be no more statements in the condition block
  // since we loop back to this block.  NULL out Block to force
  // lazy creation of another block.
  Block = NULL;
  
  // Return the condition block, which is the dominating block for the loop.
  return ConditionBlock;
}

CFGBlock* CFGBuilder::VisitDoStmt(DoStmt* D) {
  // "do...while" is a control-flow statement.  Thus we stop processing the
  // current block.
  
  CFGBlock* LoopSuccessor = NULL;
  
  if (Block) {
    FinishBlock(Block);
    LoopSuccessor = Block;
  }
  else LoopSuccessor = Succ;
  
  // Create the condition block.
  CFGBlock* ConditionBlock = createBlock(false);
  ConditionBlock->setTerminator(D);
  if (Stmt* C = D->getCond()) ConditionBlock->appendStmt(C);        
  
  // The condition block is the implicit successor for the loop body.
  Succ = ConditionBlock;
  
  CFGBlock* BodyBlock = NULL;
  // Process the loop body.
  {
    assert (D->getBody());
    
    // Save the current values for Block, Succ, and continue and break targets
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ),
    save_continue(ContinueTargetBlock),
    save_break(BreakTargetBlock);
    
    // All continues within this loop should go to the condition block
    ContinueTargetBlock = ConditionBlock;
    
    // All breaks should go to the code following the loop.
    BreakTargetBlock = LoopSuccessor;
    
    // NULL out Block to force lazy instantiation of blocks for the body.
    Block = NULL;
    
    // Create the body.  The returned block is the entry to the loop body.
    BodyBlock = Visit(D->getBody());
    assert (BodyBlock);
    FinishBlock(BodyBlock);
    
    // Add the loop body entry as a successor to the condition.
    ConditionBlock->addSuccessor(BodyBlock);
  }
  
  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  ConditionBlock->addSuccessor(LoopSuccessor);
  
  // There can be no more statements in the condition block
  // since we loop back to this block.  NULL out Block to force
  // lazy creation of another block.
  Block = NULL;
  
  // Return the loop body, which is the dominating block for the loop.
  return BodyBlock;
}

CFGBlock* CFGBuilder::VisitContinueStmt(ContinueStmt* C) {
  // "continue" is a control-flow statement.  Thus we stop processing the
  // current block.
  if (Block) FinishBlock(Block);
  
  // Now create a new block that ends with the continue statement.
  Block = createBlock(false);
  Block->setTerminator(C);
  
  // If there is no target for the continue, then we are looking at an
  // incomplete AST.  Handle this by not registering a successor.
  if (ContinueTargetBlock) Block->addSuccessor(ContinueTargetBlock);
  
  return Block;
}

CFGBlock* CFGBuilder::VisitBreakStmt(BreakStmt* B) {
  // "break" is a control-flow statement.  Thus we stop processing the
  // current block.
  if (Block) FinishBlock(Block);
  
  // Now create a new block that ends with the continue statement.
  Block = createBlock(false);
  Block->setTerminator(B);
  
  // If there is no target for the break, then we are looking at an
  // incomplete AST.  Handle this by not registering a successor.
  if (BreakTargetBlock) Block->addSuccessor(BreakTargetBlock);

  return Block;  
}

CFGBlock* CFGBuilder::VisitSwitchStmt(SwitchStmt* S) {
  // "switch" is a control-flow statement.  Thus we stop processing the
  // current block.    
  CFGBlock* SwitchSuccessor = NULL;
  
  if (Block) {
    FinishBlock(Block);
    SwitchSuccessor = Block;
  }
  else SwitchSuccessor = Succ;

  // Save the current "switch" context.
  SaveAndRestore<CFGBlock*> save_switch(SwitchTerminatedBlock),
                            save_break(BreakTargetBlock);
  
  // Create a new block that will contain the switch statement.
  SwitchTerminatedBlock = createBlock(false);
  
  // Add the terminator and condition in the switch block.
  assert (S->getCond() && "switch condition must be non-NULL");
  SwitchTerminatedBlock->appendStmt(S->getCond());
  SwitchTerminatedBlock->setTerminator(S);
  
  
  // Now process the switch body.  The code after the switch is the implicit
  // successor.
  Succ = SwitchSuccessor;
  BreakTargetBlock = SwitchSuccessor;

  assert (S->getBody() && "switch must contain a non-NULL body");
  Block = NULL;
  
  // When visiting the body, the case statements should automatically get
  // linked up to the switch.  We also don't keep a pointer to the body,
  // since all control-flow from the switch goes to case/default statements.
  Visit(S->getBody());
  
  Block = SwitchTerminatedBlock;
  return SwitchTerminatedBlock;
}

CFGBlock* CFGBuilder::VisitSwitchCase(SwitchCase* S) {
  // A SwitchCase is either a "default" or "case" statement.  We handle
  // both in the same way.  They are essentially labels, so they are the
  // first statement in a block.      
  CFGBlock* CaseBlock = Visit(S->getSubStmt());
  assert (CaseBlock);
  
  // Cases/Default statements parition block, so this is the top of
  // the basic block we were processing (the case/default is the first stmt).
  CaseBlock->appendStmt(S);
  FinishBlock(CaseBlock);
  
  // Add this block to the list of successors for the block with the
  // switch statement.
  if (SwitchTerminatedBlock) SwitchTerminatedBlock->addSuccessor(CaseBlock);
  
  // We set Block to NULL to allow lazy creation of a new block (if necessary)
  Block = NULL;
  
  // This block is now the implicit successor of other blocks.
  Succ = CaseBlock;
  
  return CaseBlock;    
}


} // end anonymous namespace

/// createBlock - Constructs and adds a new CFGBlock to the CFG.  The
///  block has no successors or predecessors.  If this is the first block
///  created in the CFG, it is automatically set to be the Entry and Exit
///  of the CFG.
CFGBlock* CFG::createBlock(unsigned blockID) {
  bool first_block = begin() == end();

  // Create the block.
  Blocks.push_front(CFGBlock(blockID));

  // If this is the first block, set it as the Entry and Exit.
  if (first_block) Entry = Exit = &front();

  // Return the block.
  return &front();
}

/// buildCFG - Constructs a CFG from an AST.  Ownership of the returned
///  CFG is returned to the caller.
CFG* CFG::buildCFG(Stmt* Statement) {
  CFGBuilder Builder;
  return Builder.buildCFG(Statement);
}

/// reverseStmts - Reverses the orders of statements within a CFGBlock.
void CFGBlock::reverseStmts() { std::reverse(Stmts.begin(),Stmts.end()); }

/// dump - A simple pretty printer of a CFG that outputs to stderr.
void CFG::dump() { print(std::cerr); }

/// print - A simple pretty printer of a CFG that outputs to an ostream.
void CFG::print(std::ostream& OS) {
  // Print the Entry block.
  if (begin() != end()) {
    CFGBlock& Entry = getEntry();
    OS << "\n [ B" << Entry.getBlockID() << " (ENTRY) ]\n";
    Entry.print(OS);
  }

  // Iterate through the CFGBlocks and print them one by one.
  for (iterator I = Blocks.begin(), E = Blocks.end() ; I != E ; ++I) {
    // Skip the entry block, because we already printed it.
    if (&(*I) == &getEntry() || &(*I) == &getExit()) continue;
      
    OS << "\n  [ B" << I->getBlockID() << " ]\n";    
    I->print(OS);
  }

  // Print the Exit Block.  
  if (begin() != end()) {
    CFGBlock& Exit = getExit();
    OS << "\n [ B" << Exit.getBlockID() << " (EXIT) ]\n";
    Exit.print(OS);
  }    

  OS << "\n";
}


namespace {

class CFGBlockTerminatorPrint : public StmtVisitor<CFGBlockTerminatorPrint,
                                                   void > {
  std::ostream& OS;
public:
  CFGBlockTerminatorPrint(std::ostream& os) : OS(os) {}
  
  void VisitIfStmt(IfStmt* I) {
    OS << "if ";
    I->getCond()->printPretty(std::cerr);
    OS << "\n";
  }
  
  // Default case.
  void VisitStmt(Stmt* S) { S->printPretty(OS); }
  
  void VisitForStmt(ForStmt* F) {
    OS << "for (" ;
    if (Stmt* I = F->getInit()) I->printPretty(OS);
    OS << " ; ";
    if (Stmt* C = F->getCond()) C->printPretty(OS);
    OS << " ; ";
    if (Stmt* I = F->getInc()) I->printPretty(OS);
    OS << ")\n";                                                       
  }
  
  void VisitWhileStmt(WhileStmt* W) {
    OS << "while " ;
    if (Stmt* C = W->getCond()) C->printPretty(OS);
    OS << "\n";
  }
  
  void VisitDoStmt(DoStmt* D) {
    OS << "do ... while ";
    if (Stmt* C = D->getCond()) C->printPretty(OS);
    OS << "\n";
  }                                                       
};
} // end anonymous namespace

/// dump - A simply pretty printer of a CFGBlock that outputs to stderr.
void CFGBlock::dump() { print(std::cerr); }

/// print - A simple pretty printer of a CFGBlock that outputs to an ostream.
///   Generally this will only be called from CFG::print.
void CFGBlock::print(std::ostream& OS) {

  // Iterate through the statements in the block and print them.
  OS << "    ------------------------\n";
  unsigned j = 1;
  for (iterator I = Stmts.begin(), E = Stmts.end() ; I != E ; ++I, ++j ) {
    // Print the statement # in the basic block.
    OS << "    " << std::setw(3) << j << ": ";    

    // Print the statement/expression.
    Stmt* S = *I;
    
    if (LabelStmt* L = dyn_cast<LabelStmt>(S))
      OS << L->getName() << ": (LABEL)\n";
    else
      (*I)->printPretty(OS);
      
    // Expressions need a newline.
    if (isa<Expr>(*I)) OS << '\n';
  }
  OS << "    ------------------------\n";

  // Print the predecessors of this block.
  OS << "    Predecessors (" << pred_size() << "):";
  unsigned i = 0;
  for (pred_iterator I = pred_begin(), E = pred_end(); I != E; ++I, ++i ) {
    if (i == 8 || (i-8) == 0) {
      OS << "\n     ";
    }
    OS << " B" << (*I)->getBlockID();
  }
  
  // Print the terminator of this block.
  OS << "\n    Terminator: ";
  if (ControlFlowStmt)
    CFGBlockTerminatorPrint(OS).Visit(ControlFlowStmt);
  else
    OS << "<NULL>\n";

  // Print the successors of this block.
  OS << "    Successors (" << succ_size() << "):";
  i = 0;
  for (succ_iterator I = succ_begin(), E = succ_end(); I != E; ++I, ++i ) {
    if (i == 8 || (i-8) % 10 == 0) {
      OS << "\n    ";
    }
    OS << " B" << (*I)->getBlockID();
  }
  OS << '\n';
}
