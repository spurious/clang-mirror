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
#include "clang/AST/PrettyPrinter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/GraphWriter.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>

using namespace clang;

namespace {

// SaveAndRestore - A utility class that uses RIIA to save and restore
//  the value of a variable.
template<typename T>
struct SaveAndRestore {
  SaveAndRestore(T& x) : X(x), old_value(x) {}
  ~SaveAndRestore() { X = old_value; }
  T get() { return old_value; }

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
  
  // LabelMap records the mapping from Label expressions to their blocks.
  typedef llvm::DenseMap<LabelStmt*,CFGBlock*> LabelMapTy;
  LabelMapTy LabelMap;
  
  // A list of blocks that end with a "goto" that must be backpatched to
  // their resolved targets upon completion of CFG construction.
  typedef std::vector<CFGBlock*> BackpatchBlocksTy;
  BackpatchBlocksTy BackpatchBlocks;
  
  // A list of labels whose address has been taken (for indirect gotos).
  typedef llvm::SmallPtrSet<LabelStmt*,5> LabelSetTy;
  LabelSetTy AddressTakenLabels;
  
public:  
  explicit CFGBuilder() : cfg(NULL), Block(NULL), Succ(NULL),
                          ContinueTargetBlock(NULL), BreakTargetBlock(NULL),
                          SwitchTerminatedBlock(NULL) {
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
  CFGBlock* VisitIndirectGotoStmt(IndirectGotoStmt* I);
  
private:
  CFGBlock* createBlock(bool add_successor = true);
  CFGBlock* addStmt(Stmt* S);
  CFGBlock* WalkAST(Stmt* S, bool AlwaysAddStmt);
  CFGBlock* WalkAST_VisitChildren(Stmt* S);
  CFGBlock* WalkAST_VisitDeclSubExprs(StmtIterator& I);
  CFGBlock* WalkAST_VisitStmtExpr(StmtExpr* S);
  CFGBlock* WalkAST_VisitCallExpr(CallExpr* C);
  void FinishBlock(CFGBlock* B);
  
};
    
/// BuildCFG - Constructs a CFG from an AST (a Stmt*).  The AST can
///  represent an arbitrary statement.  Examples include a single expression
///  or a function body (compound statement).  The ownership of the returned
///  CFG is transferred to the caller.  If CFG construction fails, this method
///  returns NULL.
CFG* CFGBuilder::buildCFG(Stmt* Statement) {
  assert (cfg);
  if (!Statement) return NULL;

  // Create an empty block that will serve as the exit block for the CFG.
  // Since this is the first block added to the CFG, it will be implicitly
  // registered as the exit block.
  Succ = createBlock();
  assert (Succ == &cfg->getExit());
  Block = NULL;  // the EXIT block is empty.  Create all other blocks lazily.
  
  // Visit the statements and create the CFG.
  if (CFGBlock* B = Visit(Statement)) {
    // Finalize the last constructed block.  This usually involves
    // reversing the order of the statements in the block.
    if (Block) FinishBlock(B);
    
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
    
    // Add successors to the Indirect Goto Dispatch block (if we have one).
    if (CFGBlock* B = cfg->getIndirectGotoBlock())
      for (LabelSetTy::iterator I = AddressTakenLabels.begin(),
           E = AddressTakenLabels.end(); I != E; ++I ) {

        // Lookup the target block.
        LabelMapTy::iterator LI = LabelMap.find(*I);

        // If there is no target block that contains label, then we are looking
        // at an incomplete AST.  Handle this by not registering a successor.
        if (LI == LabelMap.end()) continue;
        
        B->addSuccessor(LI->second);           
      }
                                                          
    Succ = B;
  }
  
  // Create an empty entry block that has no predecessors.    
  cfg->setEntry(createBlock());
    
  // NULL out cfg so that repeated calls to the builder will fail and that
  // the ownership of the constructed CFG is passed to the caller.
  CFG* t = cfg;
  cfg = NULL;
  return t;
}
  
/// createBlock - Used to lazily create blocks that are connected
///  to the current (global) succcessor.
CFGBlock* CFGBuilder::createBlock(bool add_successor) { 
  CFGBlock* B = cfg->createBlock();
  if (add_successor && Succ) B->addSuccessor(Succ);
  return B;
}
  
/// FinishBlock - When the last statement has been added to the block,
///  we must reverse the statements because they have been inserted
///  in reverse order.
void CFGBuilder::FinishBlock(CFGBlock* B) {
  assert (B);
  B->reverseStmts();
}

/// addStmt - Used to add statements/expressions to the current CFGBlock 
///  "Block".  This method calls WalkAST on the passed statement to see if it
///  contains any short-circuit expressions.  If so, it recursively creates
///  the necessary blocks for such expressions.  It returns the "topmost" block
///  of the created blocks, or the original value of "Block" when this method
///  was called if no additional blocks are created.
CFGBlock* CFGBuilder::addStmt(Stmt* S) {
  if (!Block) Block = createBlock();
  return WalkAST(S,true);
}

/// WalkAST - Used by addStmt to walk the subtree of a statement and
///   add extra blocks for ternary operators, &&, and ||.  We also
///   process "," and DeclStmts (which may contain nested control-flow).
CFGBlock* CFGBuilder::WalkAST(Stmt* S, bool AlwaysAddStmt = false) {    
  switch (S->getStmtClass()) {
    case Stmt::ConditionalOperatorClass: {
      ConditionalOperator* C = cast<ConditionalOperator>(S);
      
      CFGBlock* ConfluenceBlock = (Block) ? Block : createBlock();  
      ConfluenceBlock->appendStmt(C);
      FinishBlock(ConfluenceBlock);
      
      Succ = ConfluenceBlock;
      Block = NULL;
      CFGBlock* LHSBlock = Visit(C->getLHS());
      FinishBlock(LHSBlock);
      
      Succ = ConfluenceBlock;
      Block = NULL;
      CFGBlock* RHSBlock = Visit(C->getRHS());
      FinishBlock(RHSBlock);
      
      Block = createBlock(false);
      Block->addSuccessor(LHSBlock);
      Block->addSuccessor(RHSBlock);
      Block->setTerminator(C);
      return addStmt(C->getCond());
    }
    
    case Stmt::ChooseExprClass: {
      ChooseExpr* C = cast<ChooseExpr>(S);      
      
      CFGBlock* ConfluenceBlock = (Block) ? Block : createBlock();  
      ConfluenceBlock->appendStmt(C);
      FinishBlock(ConfluenceBlock);
      
      Succ = ConfluenceBlock;
      Block = NULL;
      CFGBlock* LHSBlock = Visit(C->getLHS());
      FinishBlock(LHSBlock);

      Succ = ConfluenceBlock;
      Block = NULL;
      CFGBlock* RHSBlock = Visit(C->getRHS());
      FinishBlock(RHSBlock);
      
      Block = createBlock(false);
      Block->addSuccessor(LHSBlock);
      Block->addSuccessor(RHSBlock);
      Block->setTerminator(C);
      return addStmt(C->getCond());
    }

    case Stmt::DeclStmtClass: {
      ScopedDecl* D = cast<DeclStmt>(S)->getDecl();
      Block->appendStmt(S);
      
      StmtIterator I(D);
      return WalkAST_VisitDeclSubExprs(I);
    }
      
    case Stmt::AddrLabelExprClass: {
      AddrLabelExpr* A = cast<AddrLabelExpr>(S);
      AddressTakenLabels.insert(A->getLabel());
      
      if (AlwaysAddStmt) Block->appendStmt(S);
      return Block;
    }
    
    case Stmt::CallExprClass:
      return WalkAST_VisitCallExpr(cast<CallExpr>(S));
      
    case Stmt::StmtExprClass:
      return WalkAST_VisitStmtExpr(cast<StmtExpr>(S));

    case Stmt::BinaryOperatorClass: {
      BinaryOperator* B = cast<BinaryOperator>(S);

      if (B->isLogicalOp()) { // && or ||
        CFGBlock* ConfluenceBlock = (Block) ? Block : createBlock();  
        ConfluenceBlock->appendStmt(B);
        FinishBlock(ConfluenceBlock);

        // create the block evaluating the LHS
        CFGBlock* LHSBlock = createBlock(false);
        LHSBlock->addSuccessor(ConfluenceBlock);
        LHSBlock->setTerminator(B);        
        
        // create the block evaluating the RHS
        Succ = ConfluenceBlock;
        Block = NULL;
        CFGBlock* RHSBlock = Visit(B->getRHS());
        LHSBlock->addSuccessor(RHSBlock);
        
        // Generate the blocks for evaluating the LHS.
        Block = LHSBlock;
        return addStmt(B->getLHS());                                    
      }
      else if (B->getOpcode() == BinaryOperator::Comma) { // ,
        Block->appendStmt(B);
        addStmt(B->getRHS());
        return addStmt(B->getLHS());
      }

      // Fall through to the default case.
    }
    
    default:
      if (AlwaysAddStmt) Block->appendStmt(S);
      return WalkAST_VisitChildren(S);
  };
}

/// WalkAST_VisitDeclSubExprs - Utility method to handle Decls contained in
///  DeclStmts.  Because the initialization code (and sometimes the
///  the type declarations) for DeclStmts can contain arbitrary expressions, 
///  we must linearize declarations to handle arbitrary control-flow induced by
/// those expressions.  
CFGBlock* CFGBuilder::WalkAST_VisitDeclSubExprs(StmtIterator& I) {
  Stmt* S = *I;
  ++I;
  
  if (I != StmtIterator())
    WalkAST_VisitDeclSubExprs(I);
  
  Block = addStmt(S);
  return Block;
}

/// WalkAST_VisitChildren - Utility method to call WalkAST on the
///  children of a Stmt.
CFGBlock* CFGBuilder::WalkAST_VisitChildren(Stmt* S) {
  CFGBlock* B = Block;
  for (Stmt::child_iterator I = S->child_begin(), E = S->child_end() ;
       I != E; ++I)
    if (*I) B = WalkAST(*I);
  
  return B;
}

/// WalkAST_VisitStmtExpr - Utility method to handle (nested) statement
///  expressions (a GCC extension).
CFGBlock* CFGBuilder::WalkAST_VisitStmtExpr(StmtExpr* S) {
  Block->appendStmt(S);
  return VisitCompoundStmt(S->getSubStmt());  
}

/// WalkAST_VisitCallExpr - Utility method to handle function calls that
///  are nested in expressions.  The idea is that each function call should
///  appear as a distinct statement in the CFGBlock.
CFGBlock* CFGBuilder::WalkAST_VisitCallExpr(CallExpr* C) {
  Block->appendStmt(C);
  return WalkAST_VisitChildren(C);
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
  addStmt(Statement);

  return Block;
}

CFGBlock* CFGBuilder::VisitNullStmt(NullStmt* Statement) {
  return Block;
}

CFGBlock* CFGBuilder::VisitCompoundStmt(CompoundStmt* C) {
  //   The value returned from this function is the last created CFGBlock
  //   that represents the "entry" point for the translated AST node.
  CFGBlock* LastBlock = 0;
  
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
              
    if (!ElseBlock) // Can occur when the Else body has all NullStmts.
      ElseBlock = sv.get();
    else if (Block) 
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
    
    if (!ThenBlock) // Can occur when the Then body has all NullStmts.
      ThenBlock = sv.get();
    else if (Block)
      FinishBlock(ThenBlock);
  }

  // Now create a new block containing the if statement.        
  Block = createBlock(false);
  
  // Set the terminator of the new block to the If statement.
  Block->setTerminator(I);
  
  // Now add the successors.
  Block->addSuccessor(ThenBlock);
  Block->addSuccessor(ElseBlock);
  
  // Add the condition as the last statement in the new block.  This
  // may create new blocks as the condition may contain control-flow.  Any
  // newly created blocks will be pointed to be "Block".
  return addStmt(I->getCond());
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
    
  // Add the return statement to the block.  This may create new blocks
  // if R contains control-flow (short-circuit operations).
  return addStmt(R);
}

CFGBlock* CFGBuilder::VisitLabelStmt(LabelStmt* L) {
  // Get the block of the labeled statement.  Add it to our map.
  CFGBlock* LabelBlock = Visit(L->getSubStmt());
  
  if (!LabelBlock)            // This can happen when the body is empty, i.e.
    LabelBlock=createBlock(); // scopes that only contains NullStmts.
  
  assert (LabelMap.find(L) == LabelMap.end() && "label already in map");
  LabelMap[ L ] = LabelBlock;
  
  // Labels partition blocks, so this is the end of the basic block
  // we were processing (L is the block's label).  Because this is
  // label (and we have already processed the substatement) there is no
  // extra control-flow to worry about.
  LabelBlock->setLabel(L);
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
  
  // Because of short-circuit evaluation, the condition of the loop
  // can span multiple basic blocks.  Thus we need the "Entry" and "Exit"
  // blocks that evaluate the condition.
  CFGBlock* ExitConditionBlock = createBlock(false);
  CFGBlock* EntryConditionBlock = ExitConditionBlock;
  
  // Set the terminator for the "exit" condition block.
  ExitConditionBlock->setTerminator(F);  
  
  // Now add the actual condition to the condition block.  Because the
  // condition itself may contain control-flow, new blocks may be created.
  if (Stmt* C = F->getCond()) {
    Block = ExitConditionBlock;
    EntryConditionBlock = addStmt(C);
    if (Block) FinishBlock(EntryConditionBlock);
  }

  // The condition block is the implicit successor for the loop body as
  // well as any code above the loop.
  Succ = EntryConditionBlock;
  
  // Now create the loop body.
  {
    assert (F->getBody());
    
    // Save the current values for Block, Succ, and continue and break targets
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ),
    save_continue(ContinueTargetBlock),
    save_break(BreakTargetBlock);      

    // All continues within this loop should go to the condition block
    ContinueTargetBlock = EntryConditionBlock;
    
    // All breaks should go to the code following the loop.
    BreakTargetBlock = LoopSuccessor;
    
    // Create a new block to contain the (bottom) of the loop body.
    Block = NULL;
    
    // If we have increment code, insert it at the end of the body block.
    if (Stmt* I = F->getInc()) Block = addStmt(I);
    
    // Now populate the body block, and in the process create new blocks
    // as we walk the body of the loop.
    CFGBlock* BodyBlock = Visit(F->getBody());      

    if (!BodyBlock)
      BodyBlock = ExitConditionBlock; // can happen for "for (...;...; ) ;"
    else if (Block)
      FinishBlock(BodyBlock);
    
    // This new body block is a successor to our "exit" condition block.
    ExitConditionBlock->addSuccessor(BodyBlock);
  }
  
  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  ExitConditionBlock->addSuccessor(LoopSuccessor);
  
  // If the loop contains initialization, create a new block for those
  // statements.  This block can also contain statements that precede
  // the loop.
  if (Stmt* I = F->getInit()) {
    Block = createBlock();
    return addStmt(I);
  }
  else {
    // There is no loop initialization.   We are thus basically a while 
    // loop.  NULL out Block to force lazy block construction.
    Block = NULL;
    return EntryConditionBlock;
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
            
  // Because of short-circuit evaluation, the condition of the loop
  // can span multiple basic blocks.  Thus we need the "Entry" and "Exit"
  // blocks that evaluate the condition.
  CFGBlock* ExitConditionBlock = createBlock(false);
  CFGBlock* EntryConditionBlock = ExitConditionBlock;
  
  // Set the terminator for the "exit" condition block.
  ExitConditionBlock->setTerminator(W);
  
  // Now add the actual condition to the condition block.  Because the
  // condition itself may contain control-flow, new blocks may be created.
  // Thus we update "Succ" after adding the condition.
  if (Stmt* C = W->getCond()) {
    Block = ExitConditionBlock;
    EntryConditionBlock = addStmt(C);
    if (Block) FinishBlock(EntryConditionBlock);
  }
  
  // The condition block is the implicit successor for the loop body as
  // well as any code above the loop.
  Succ = EntryConditionBlock;
  
  // Process the loop body.
  {
    assert (W->getBody());

    // Save the current values for Block, Succ, and continue and break targets
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ),
                              save_continue(ContinueTargetBlock),
                              save_break(BreakTargetBlock);
          
    // All continues within this loop should go to the condition block
    ContinueTargetBlock = EntryConditionBlock;
    
    // All breaks should go to the code following the loop.
    BreakTargetBlock = LoopSuccessor;
    
    // NULL out Block to force lazy instantiation of blocks for the body.
    Block = NULL;
    
    // Create the body.  The returned block is the entry to the loop body.
    CFGBlock* BodyBlock = Visit(W->getBody());
    
    if (!BodyBlock)
      BodyBlock = ExitConditionBlock; // can happen for "while(...) ;"
    else if (Block)
      FinishBlock(BodyBlock);
    
    // Add the loop body entry as a successor to the condition.
    ExitConditionBlock->addSuccessor(BodyBlock);
  }
  
  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  ExitConditionBlock->addSuccessor(LoopSuccessor);
  
  // There can be no more statements in the condition block
  // since we loop back to this block.  NULL out Block to force
  // lazy creation of another block.
  Block = NULL;
  
  // Return the condition block, which is the dominating block for the loop.
  return EntryConditionBlock;
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
  
  // Because of short-circuit evaluation, the condition of the loop
  // can span multiple basic blocks.  Thus we need the "Entry" and "Exit"
  // blocks that evaluate the condition.
  CFGBlock* ExitConditionBlock = createBlock(false);
  CFGBlock* EntryConditionBlock = ExitConditionBlock;
        
  // Set the terminator for the "exit" condition block.
  ExitConditionBlock->setTerminator(D);  
  
  // Now add the actual condition to the condition block.  Because the
  // condition itself may contain control-flow, new blocks may be created.
  if (Stmt* C = D->getCond()) {
    Block = ExitConditionBlock;
    EntryConditionBlock = addStmt(C);
    if (Block) FinishBlock(EntryConditionBlock);
  }
  
  // The condition block is the implicit successor for the loop body as
  // well as any code above the loop.
  Succ = EntryConditionBlock;


  // Process the loop body.
  CFGBlock* BodyBlock = NULL;
  {
    assert (D->getBody());
    
    // Save the current values for Block, Succ, and continue and break targets
    SaveAndRestore<CFGBlock*> save_Block(Block), save_Succ(Succ),
    save_continue(ContinueTargetBlock),
    save_break(BreakTargetBlock);
    
    // All continues within this loop should go to the condition block
    ContinueTargetBlock = EntryConditionBlock;
    
    // All breaks should go to the code following the loop.
    BreakTargetBlock = LoopSuccessor;
    
    // NULL out Block to force lazy instantiation of blocks for the body.
    Block = NULL;
    
    // Create the body.  The returned block is the entry to the loop body.
    BodyBlock = Visit(D->getBody());
    
    if (!BodyBlock)
      BodyBlock = ExitConditionBlock; // can happen for "do ; while(...)"
    else if (Block)
      FinishBlock(BodyBlock);
        
    // Add the loop body entry as a successor to the condition.
    ExitConditionBlock->addSuccessor(BodyBlock);
  }
  
  // Link up the condition block with the code that follows the loop.
  // (the false branch).
  ExitConditionBlock->addSuccessor(LoopSuccessor);
  
  // There can be no more statements in the body block(s)
  // since we loop back to the body.  NULL out Block to force
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
  
  // Now process the switch body.  The code after the switch is the implicit
  // successor.
  Succ = SwitchSuccessor;
  BreakTargetBlock = SwitchSuccessor;
  
  // When visiting the body, the case statements should automatically get
  // linked up to the switch.  We also don't keep a pointer to the body,
  // since all control-flow from the switch goes to case/default statements.
  assert (S->getBody() && "switch must contain a non-NULL body");
  Block = NULL;
  CFGBlock *BodyBlock = Visit(S->getBody());
  if (Block) FinishBlock(BodyBlock);

  // Add the terminator and condition in the switch block.
  SwitchTerminatedBlock->setTerminator(S);
  assert (S->getCond() && "switch condition must be non-NULL");
  Block = SwitchTerminatedBlock;
  return addStmt(S->getCond());
}

CFGBlock* CFGBuilder::VisitSwitchCase(SwitchCase* S) {
  // A SwitchCase is either a "default" or "case" statement.  We handle
  // both in the same way.  They are essentially labels, so they are the
  // first statement in a block.      

  if (S->getSubStmt()) Visit(S->getSubStmt());
  CFGBlock* CaseBlock = Block;
  if (!CaseBlock) CaseBlock = createBlock();  
    
  // Cases/Default statements partition block, so this is the top of
  // the basic block we were processing (the case/default is the label).
  CaseBlock->setLabel(S);
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

CFGBlock* CFGBuilder::VisitIndirectGotoStmt(IndirectGotoStmt* I) {
  // Lazily create the indirect-goto dispatch block if there isn't one
  // already.
  CFGBlock* IBlock = cfg->getIndirectGotoBlock();
  
  if (!IBlock) {
    IBlock = createBlock(false);
    cfg->setIndirectGotoBlock(IBlock);
  }
  
  // IndirectGoto is a control-flow statement.  Thus we stop processing the
  // current block and create a new one.
  if (Block) FinishBlock(Block);
  Block = createBlock(false);
  Block->setTerminator(I);
  Block->addSuccessor(IBlock);
  return addStmt(I->getTarget());
}


} // end anonymous namespace

/// createBlock - Constructs and adds a new CFGBlock to the CFG.  The
///  block has no successors or predecessors.  If this is the first block
///  created in the CFG, it is automatically set to be the Entry and Exit
///  of the CFG.
CFGBlock* CFG::createBlock() {
  bool first_block = begin() == end();

  // Create the block.
  Blocks.push_front(CFGBlock(NumBlockIDs++));

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

//===----------------------------------------------------------------------===//
// CFG: Queries for BlkExprs.
//===----------------------------------------------------------------------===//

namespace {
  typedef llvm::DenseMap<const Expr*,unsigned> BlkExprMapTy;
}

static BlkExprMapTy* PopulateBlkExprMap(CFG& cfg) {
  BlkExprMapTy* M = new BlkExprMapTy();
  
  for (CFG::iterator I=cfg.begin(), E=cfg.end(); I != E; ++I)
    for (CFGBlock::iterator BI=I->begin(), EI=I->end(); BI != EI; ++BI)
      if (const Expr* E = dyn_cast<Expr>(*BI))
        (*M)[E] = M->size();
  
  return M;
}

bool CFG::isBlkExpr(const Stmt* S) {
  assert (S != NULL);
  if (const Expr* E = dyn_cast<Expr>(S)) return getBlkExprNum(E);
  else return true;  // Statements are by default "block-level expressions."
}

CFG::BlkExprNumTy CFG::getBlkExprNum(const Expr* E) {
  assert(E != NULL);
  if (!BlkExprMap) { BlkExprMap = (void*) PopulateBlkExprMap(*this); }
  
  BlkExprMapTy* M = reinterpret_cast<BlkExprMapTy*>(BlkExprMap);
  BlkExprMapTy::iterator I = M->find(E);
  
  if (I == M->end()) return CFG::BlkExprNumTy();
  else return CFG::BlkExprNumTy(I->second);
}

unsigned CFG::getNumBlkExprs() {
  if (const BlkExprMapTy* M = reinterpret_cast<const BlkExprMapTy*>(BlkExprMap))
    return M->size();
  else {
    // We assume callers interested in the number of BlkExprs will want
    // the map constructed if it doesn't already exist.
    BlkExprMap = (void*) PopulateBlkExprMap(*this);
    return reinterpret_cast<BlkExprMapTy*>(BlkExprMap)->size();
  }
}

CFG::~CFG() {
  delete reinterpret_cast<const BlkExprMapTy*>(BlkExprMap);
}
  
//===----------------------------------------------------------------------===//
// CFG pretty printing
//===----------------------------------------------------------------------===//

namespace {

class StmtPrinterHelper : public PrinterHelper  {
                          
  typedef llvm::DenseMap<Stmt*,std::pair<unsigned,unsigned> > StmtMapTy;
  StmtMapTy StmtMap;
  signed CurrentBlock;
  unsigned CurrentStmt;

public:

  StmtPrinterHelper(const CFG* cfg) : CurrentBlock(0), CurrentStmt(0) {
    for (CFG::const_iterator I = cfg->begin(), E = cfg->end(); I != E; ++I ) {
      unsigned j = 1;
      for (CFGBlock::const_iterator BI = I->begin(), BEnd = I->end() ;
           BI != BEnd; ++BI, ++j )
        StmtMap[*BI] = std::make_pair(I->getBlockID(),j);
      }
  }
            
  virtual ~StmtPrinterHelper() {}
  
  void setBlockID(signed i) { CurrentBlock = i; }
  void setStmtID(unsigned i) { CurrentStmt = i; }
  
  virtual bool handledStmt(Stmt* S, std::ostream& OS) {
    
    StmtMapTy::iterator I = StmtMap.find(S);

    if (I == StmtMap.end())
      return false;
    
    if (CurrentBlock >= 0 && I->second.first == (unsigned) CurrentBlock 
                          && I->second.second == CurrentStmt)
      return false;
      
      OS << "[B" << I->second.first << "." << I->second.second << "]";
    return true;
  }
};

class CFGBlockTerminatorPrint : public StmtVisitor<CFGBlockTerminatorPrint,
                                                    void > 
{
  std::ostream& OS;
  StmtPrinterHelper* Helper;
public:
  CFGBlockTerminatorPrint(std::ostream& os, StmtPrinterHelper* helper)
    : OS(os), Helper(helper) {}
  
  void VisitIfStmt(IfStmt* I) {
    OS << "if ";
    I->getCond()->printPretty(OS,Helper);
    OS << "\n";
  }
  
  // Default case.
  void VisitStmt(Stmt* S) { S->printPretty(OS); }
  
  void VisitForStmt(ForStmt* F) {
    OS << "for (" ;
    if (F->getInit()) OS << "...";
    OS << "; ";
    if (Stmt* C = F->getCond()) C->printPretty(OS,Helper);
    OS << "; ";
    if (F->getInc()) OS << "...";
    OS << ")\n";                                                       
  }
  
  void VisitWhileStmt(WhileStmt* W) {
    OS << "while " ;
    if (Stmt* C = W->getCond()) C->printPretty(OS,Helper);
    OS << "\n";
  }
  
  void VisitDoStmt(DoStmt* D) {
    OS << "do ... while ";
    if (Stmt* C = D->getCond()) C->printPretty(OS,Helper);
    OS << '\n';
  }
  
  void VisitSwitchStmt(SwitchStmt* S) {
    OS << "switch ";
    S->getCond()->printPretty(OS,Helper);
    OS << '\n';
  }
  
  void VisitConditionalOperator(ConditionalOperator* C) {
    C->getCond()->printPretty(OS,Helper);
    OS << " ? ... : ...\n";  
  }
  
  void VisitChooseExpr(ChooseExpr* C) {
    OS << "__builtin_choose_expr( ";
    C->getCond()->printPretty(OS,Helper);
    OS << " )\n";
  }
  
  void VisitIndirectGotoStmt(IndirectGotoStmt* I) {
    OS << "goto *";
    I->getTarget()->printPretty(OS,Helper);
    OS << '\n';
  }
  
  void VisitBinaryOperator(BinaryOperator* B) {
    if (!B->isLogicalOp()) {
      VisitExpr(B);
      return;
    }
    
    B->getLHS()->printPretty(OS,Helper);
    
    switch (B->getOpcode()) {
      case BinaryOperator::LOr:
        OS << " || ...\n";
        return;
      case BinaryOperator::LAnd:
        OS << " && ...\n";
        return;
      default:
        assert(false && "Invalid logical operator.");
    }  
  }
  
  void VisitExpr(Expr* E) {
    E->printPretty(OS,Helper);
    OS << '\n';
  }                                                       
};
  
  
void print_stmt(std::ostream&OS, StmtPrinterHelper* Helper, Stmt* S) {    
  if (Helper) {
    // special printing for statement-expressions.
    if (StmtExpr* SE = dyn_cast<StmtExpr>(S)) {
      CompoundStmt* Sub = SE->getSubStmt();
      
      if (Sub->child_begin() != Sub->child_end()) {
        OS << "({ ... ; ";
        Helper->handledStmt(*SE->getSubStmt()->body_rbegin(),OS);
        OS << " })\n";
        return;
      }
    }
    
    // special printing for comma expressions.
    if (BinaryOperator* B = dyn_cast<BinaryOperator>(S)) {
      if (B->getOpcode() == BinaryOperator::Comma) {
        OS << "... , ";
        Helper->handledStmt(B->getRHS(),OS);
        OS << '\n';
        return;
      }          
    }  
  }
  
  S->printPretty(OS, Helper);
  
  // Expressions need a newline.
  if (isa<Expr>(S)) OS << '\n';
}
  
void print_block(std::ostream& OS, const CFG* cfg, const CFGBlock& B,
                 StmtPrinterHelper* Helper, bool print_edges) {
 
  if (Helper) Helper->setBlockID(B.getBlockID());
  
  // Print the header.
  OS << "\n [ B" << B.getBlockID();  
    
  if (&B == &cfg->getEntry())
    OS << " (ENTRY) ]\n";
  else if (&B == &cfg->getExit())
    OS << " (EXIT) ]\n";
  else if (&B == cfg->getIndirectGotoBlock())
    OS << " (INDIRECT GOTO DISPATCH) ]\n";
  else
    OS << " ]\n";
 
  // Print the label of this block.
  if (Stmt* S = const_cast<Stmt*>(B.getLabel())) {

    if (print_edges)
      OS << "    ";
  
    if (LabelStmt* L = dyn_cast<LabelStmt>(S))
      OS << L->getName();
    else if (CaseStmt* C = dyn_cast<CaseStmt>(S)) {
      OS << "case ";
      C->getLHS()->printPretty(OS);
      if (C->getRHS()) {
        OS << " ... ";
        C->getRHS()->printPretty(OS);
      }
    }  
    else if (isa<DefaultStmt>(S))
      OS << "default";
    else
      assert(false && "Invalid label statement in CFGBlock.");
 
    OS << ":\n";
  }
 
  // Iterate through the statements in the block and print them.
  unsigned j = 1;
  
  for (CFGBlock::const_iterator I = B.begin(), E = B.end() ;
       I != E ; ++I, ++j ) {
       
    // Print the statement # in the basic block and the statement itself.
    if (print_edges)
      OS << "    ";
      
    OS << std::setw(3) << j << ": ";
    
    if (Helper)
      Helper->setStmtID(j);
     
    print_stmt(OS,Helper,*I);
  }
 
  // Print the terminator of this block.
  if (B.getTerminator()) {
    if (print_edges)
      OS << "    ";
      
    OS << "  T: ";
    
    if (Helper) Helper->setBlockID(-1);
    
    CFGBlockTerminatorPrint TPrinter(OS,Helper);
    TPrinter.Visit(const_cast<Stmt*>(B.getTerminator()));
  }
 
  if (print_edges) {
    // Print the predecessors of this block.
    OS << "    Predecessors (" << B.pred_size() << "):";
    unsigned i = 0;

    for (CFGBlock::const_pred_iterator I = B.pred_begin(), E = B.pred_end();
         I != E; ++I, ++i) {
                  
      if (i == 8 || (i-8) == 0)
        OS << "\n     ";
      
      OS << " B" << (*I)->getBlockID();
    }
    
    OS << '\n';
 
    // Print the successors of this block.
    OS << "    Successors (" << B.succ_size() << "):";
    i = 0;

    for (CFGBlock::const_succ_iterator I = B.succ_begin(), E = B.succ_end();
         I != E; ++I, ++i) {
         
      if (i == 8 || (i-8) % 10 == 0)
        OS << "\n    ";

      OS << " B" << (*I)->getBlockID();
    }
    
    OS << '\n';
  }
}                   

} // end anonymous namespace

/// dump - A simple pretty printer of a CFG that outputs to stderr.
void CFG::dump() const { print(std::cerr); }

/// print - A simple pretty printer of a CFG that outputs to an ostream.
void CFG::print(std::ostream& OS) const {
  
  StmtPrinterHelper Helper(this);
  
  // Print the entry block.
  print_block(OS, this, getEntry(), &Helper, true);
                    
  // Iterate through the CFGBlocks and print them one by one.
  for (const_iterator I = Blocks.begin(), E = Blocks.end() ; I != E ; ++I) {
    // Skip the entry block, because we already printed it.
    if (&(*I) == &getEntry() || &(*I) == &getExit())
      continue;
      
    print_block(OS, this, *I, &Helper, true);
  }
  
  // Print the exit block.
  print_block(OS, this, getExit(), &Helper, true);
}  

/// dump - A simply pretty printer of a CFGBlock that outputs to stderr.
void CFGBlock::dump(const CFG* cfg) const { print(std::cerr, cfg); }

/// print - A simple pretty printer of a CFGBlock that outputs to an ostream.
///   Generally this will only be called from CFG::print.
void CFGBlock::print(std::ostream& OS, const CFG* cfg) const {
  StmtPrinterHelper Helper(cfg);
  print_block(OS, cfg, *this, &Helper, true);
}

//===----------------------------------------------------------------------===//
// CFG Graphviz Visualization
//===----------------------------------------------------------------------===//


#ifndef NDEBUG
static StmtPrinterHelper* GraphHelper;  
#endif

void CFG::viewCFG() const {
#ifndef NDEBUG
  StmtPrinterHelper H(this);
  GraphHelper = &H;
  llvm::ViewGraph(this,"CFG");
  GraphHelper = NULL;
#else
  std::cerr << "CFG::viewCFG is only available in debug builds on "
            << "systems with Graphviz or gv!\n";
#endif
}

namespace llvm {
template<>
struct DOTGraphTraits<const CFG*> : public DefaultDOTGraphTraits {
  static std::string getNodeLabel(const CFGBlock* Node, const CFG* Graph) {

#ifndef NDEBUG
    std::ostringstream Out;
    print_block(Out,Graph, *Node, GraphHelper, false);
    std::string OutStr = Out.str();

    if (OutStr[0] == '\n') OutStr.erase(OutStr.begin());

    // Process string output to make it nicer...
    for (unsigned i = 0; i != OutStr.length(); ++i)
      if (OutStr[i] == '\n') {                            // Left justify
        OutStr[i] = '\\';
        OutStr.insert(OutStr.begin()+i+1, 'l');
      }
      
    return OutStr;
#else
    return "";
#endif
  }
};
} // end namespace llvm
