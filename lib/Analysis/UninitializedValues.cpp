//==- UninitializedValues.cpp - Find Uninitialized Values -------*- C++ --*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements uninitialized values analysis for source-level CFGs.
//
//===----------------------------------------------------------------------===//

#include <utility>
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "clang/AST/Decl.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/Analysis/Visitors/CFGRecStmtDeclVisitor.h"
#include "clang/Analysis/Analyses/UninitializedValues.h"
#include "clang/Analysis/Support/SaveAndRestore.h"

using namespace clang;

static bool isTrackedVar(const VarDecl *vd, const DeclContext *dc) {
  if (vd->isLocalVarDecl() && !vd->hasGlobalStorage() &&
      vd->getDeclContext() == dc) {
    QualType ty = vd->getType();
    return ty->isScalarType() || ty->isVectorType();
  }
  return false;
}

//------------------------------------------------------------------------====//
// DeclToIndex: a mapping from Decls we track to value indices.
//====------------------------------------------------------------------------//

namespace {
class DeclToIndex {
  llvm::DenseMap<const VarDecl *, unsigned> map;
public:
  DeclToIndex() {}
  
  /// Compute the actual mapping from declarations to bits.
  void computeMap(const DeclContext &dc);
  
  /// Return the number of declarations in the map.
  unsigned size() const { return map.size(); }
  
  /// Returns the bit vector index for a given declaration.
  llvm::Optional<unsigned> getValueIndex(const VarDecl *d);
};
}

void DeclToIndex::computeMap(const DeclContext &dc) {
  unsigned count = 0;
  DeclContext::specific_decl_iterator<VarDecl> I(dc.decls_begin()),
                                               E(dc.decls_end());
  for ( ; I != E; ++I) {
    const VarDecl *vd = *I;
    if (isTrackedVar(vd, &dc))
      map[vd] = count++;
  }
}

llvm::Optional<unsigned> DeclToIndex::getValueIndex(const VarDecl *d) {
  llvm::DenseMap<const VarDecl *, unsigned>::iterator I = map.find(d);
  if (I == map.end())
    return llvm::Optional<unsigned>();
  return I->second;
}

//------------------------------------------------------------------------====//
// CFGBlockValues: dataflow values for CFG blocks.
//====------------------------------------------------------------------------//

// These values are defined in such a way that a merge can be done using
// a bitwise OR.
enum Value { Unknown = 0x0,         /* 00 */
             Initialized = 0x1,     /* 01 */
             Uninitialized = 0x2,   /* 10 */
             MayUninitialized = 0x3 /* 11 */ };

static bool isUninitialized(const Value v) {
  return v >= Uninitialized;
}
static bool isAlwaysUninit(const Value v) {
  return v == Uninitialized;
}

namespace {
class ValueVector {
  llvm::BitVector vec;
public:
  ValueVector() {}
  ValueVector(unsigned size) : vec(size << 1) {}
  void resize(unsigned n) { vec.resize(n << 1); }
  void merge(const ValueVector &rhs) { vec |= rhs.vec; }
  bool operator!=(const ValueVector &rhs) const { return vec != rhs.vec; }
  void reset() { vec.reset(); }
  
  class reference {
    ValueVector &vv;
    const unsigned idx;

    reference();  // Undefined    
  public:
    reference(ValueVector &vv, unsigned idx) : vv(vv), idx(idx) {}    
    ~reference() {}
    
    reference &operator=(Value v) {
      vv.vec[idx << 1] = (((unsigned) v) & 0x1) ? true : false;
      vv.vec[(idx << 1) | 1] = (((unsigned) v) & 0x2) ? true : false;
      return *this;
    }
    operator Value() {
      unsigned x = (vv.vec[idx << 1] ? 1 : 0) | (vv.vec[(idx << 1) | 1] ? 2 :0);
      return (Value) x;      
    }
  };
    
  reference operator[](unsigned idx) { return reference(*this, idx); }
};

typedef std::pair<ValueVector *, ValueVector *> BVPair;

class CFGBlockValues {
  const CFG &cfg;
  BVPair *vals;
  ValueVector scratch;
  DeclToIndex declToIndex;
  
  ValueVector &lazyCreate(ValueVector *&bv);
public:
  CFGBlockValues(const CFG &cfg);
  ~CFGBlockValues();
  
  void computeSetOfDeclarations(const DeclContext &dc);  
  ValueVector &getValueVector(const CFGBlock *block,
                                const CFGBlock *dstBlock);

  BVPair &getValueVectors(const CFGBlock *block, bool shouldLazyCreate);

  void mergeIntoScratch(ValueVector const &source, bool isFirst);
  bool updateValueVectorWithScratch(const CFGBlock *block);
  bool updateValueVectors(const CFGBlock *block, const BVPair &newVals);
  
  bool hasNoDeclarations() const {
    return declToIndex.size() == 0;
  }
  
  void resetScratch();
  ValueVector &getScratch() { return scratch; }
  
  ValueVector::reference operator[](const VarDecl *vd);
};  
} // end anonymous namespace

CFGBlockValues::CFGBlockValues(const CFG &c) : cfg(c), vals(0) {
  unsigned n = cfg.getNumBlockIDs();
  if (!n)
    return;
  vals = new std::pair<ValueVector*, ValueVector*>[n];
  memset(vals, 0, sizeof(*vals) * n);
}

CFGBlockValues::~CFGBlockValues() {
  unsigned n = cfg.getNumBlockIDs();
  if (n == 0)
    return;
  for (unsigned i = 0; i < n; ++i) {
    delete vals[i].first;
    delete vals[i].second;
  }
  delete [] vals;
}

void CFGBlockValues::computeSetOfDeclarations(const DeclContext &dc) {
  declToIndex.computeMap(dc);
  scratch.resize(declToIndex.size());
}

ValueVector &CFGBlockValues::lazyCreate(ValueVector *&bv) {
  if (!bv)
    bv = new ValueVector(declToIndex.size());
  return *bv;
}

/// This function pattern matches for a '&&' or '||' that appears at
/// the beginning of a CFGBlock that also (1) has a terminator and 
/// (2) has no other elements.  If such an expression is found, it is returned.
static BinaryOperator *getLogicalOperatorInChain(const CFGBlock *block) {
  if (block->empty())
    return 0;

  const CFGStmt *cstmt = block->front().getAs<CFGStmt>();
  if (!cstmt)
    return 0;

  BinaryOperator *b = llvm::dyn_cast_or_null<BinaryOperator>(cstmt->getStmt());
  
  if (!b || !b->isLogicalOp())
    return 0;
  
  if (block->pred_size() == 2 &&
      ((block->succ_size() == 2 && block->getTerminatorCondition() == b) ||
       block->size() == 1))
    return b;
  
  return 0;
}

ValueVector &CFGBlockValues::getValueVector(const CFGBlock *block,
                                            const CFGBlock *dstBlock) {
  unsigned idx = block->getBlockID();
  if (dstBlock && getLogicalOperatorInChain(block)) {
    if (*block->succ_begin() == dstBlock)
      return lazyCreate(vals[idx].first);
    assert(*(block->succ_begin()+1) == dstBlock);
    return lazyCreate(vals[idx].second);
  }

  assert(vals[idx].second == 0);
  return lazyCreate(vals[idx].first);
}

BVPair &CFGBlockValues::getValueVectors(const clang::CFGBlock *block,
                                        bool shouldLazyCreate) {
  unsigned idx = block->getBlockID();
  lazyCreate(vals[idx].first);
  if (shouldLazyCreate)
    lazyCreate(vals[idx].second);
  return vals[idx];
}

void CFGBlockValues::mergeIntoScratch(ValueVector const &source,
                                      bool isFirst) {
  if (isFirst)
    scratch = source;
  else
    scratch.merge(source);
}
#if 0
static void printVector(const CFGBlock *block, ValueVector &bv,
                        unsigned num) {
  
  llvm::errs() << block->getBlockID() << " :";
  for (unsigned i = 0; i < bv.size(); ++i) {
    llvm::errs() << ' ' << bv[i];
  }
  llvm::errs() << " : " << num << '\n';
}
#endif

bool CFGBlockValues::updateValueVectorWithScratch(const CFGBlock *block) {
  ValueVector &dst = getValueVector(block, 0);
  bool changed = (dst != scratch);
  if (changed)
    dst = scratch;
#if 0
  printVector(block, scratch, 0);
#endif
  return changed;
}

bool CFGBlockValues::updateValueVectors(const CFGBlock *block,
                                      const BVPair &newVals) {
  BVPair &vals = getValueVectors(block, true);
  bool changed = *newVals.first != *vals.first ||
                 *newVals.second != *vals.second;
  *vals.first = *newVals.first;
  *vals.second = *newVals.second;
#if 0
  printVector(block, *vals.first, 1);
  printVector(block, *vals.second, 2);
#endif
  return changed;
}

void CFGBlockValues::resetScratch() {
  scratch.reset();
}

ValueVector::reference CFGBlockValues::operator[](const VarDecl *vd) {
  const llvm::Optional<unsigned> &idx = declToIndex.getValueIndex(vd);
  assert(idx.hasValue());
  return scratch[idx.getValue()];
}

//------------------------------------------------------------------------====//
// Worklist: worklist for dataflow analysis.
//====------------------------------------------------------------------------//

namespace {
class DataflowWorklist {
  llvm::SmallVector<const CFGBlock *, 20> worklist;
  llvm::BitVector enqueuedBlocks;
public:
  DataflowWorklist(const CFG &cfg) : enqueuedBlocks(cfg.getNumBlockIDs()) {}
  
  void enqueue(const CFGBlock *block);
  void enqueueSuccessors(const CFGBlock *block);
  const CFGBlock *dequeue();
  
};
}

void DataflowWorklist::enqueue(const CFGBlock *block) {
  if (!block)
    return;
  unsigned idx = block->getBlockID();
  if (enqueuedBlocks[idx])
    return;
  worklist.push_back(block);
  enqueuedBlocks[idx] = true;
}

void DataflowWorklist::enqueueSuccessors(const clang::CFGBlock *block) {
  for (CFGBlock::const_succ_iterator I = block->succ_begin(),
       E = block->succ_end(); I != E; ++I) {
    enqueue(*I);
  }
}

const CFGBlock *DataflowWorklist::dequeue() {
  if (worklist.empty())
    return 0;
  const CFGBlock *b = worklist.back();
  worklist.pop_back();
  enqueuedBlocks[b->getBlockID()] = false;
  return b;
}

//------------------------------------------------------------------------====//
// Transfer function for uninitialized values analysis.
//====------------------------------------------------------------------------//

namespace {
class FindVarResult {
  const VarDecl *vd;
  const DeclRefExpr *dr;
public:
  FindVarResult(VarDecl *vd, DeclRefExpr *dr) : vd(vd), dr(dr) {}
  
  const DeclRefExpr *getDeclRefExpr() const { return dr; }
  const VarDecl *getDecl() const { return vd; }
};
  
class TransferFunctions : public CFGRecStmtVisitor<TransferFunctions> {
  CFGBlockValues &vals;
  const CFG &cfg;
  AnalysisContext &ac;
  UninitVariablesHandler *handler;
  const DeclRefExpr *currentDR;
  const Expr *currentVoidCast;
  const bool flagBlockUses;
public:
  TransferFunctions(CFGBlockValues &vals, const CFG &cfg,
                    AnalysisContext &ac,
                    UninitVariablesHandler *handler,
                    bool flagBlockUses)
    : vals(vals), cfg(cfg), ac(ac), handler(handler), currentDR(0),
      currentVoidCast(0), flagBlockUses(flagBlockUses) {}
  
  const CFG &getCFG() { return cfg; }
  void reportUninit(const DeclRefExpr *ex, const VarDecl *vd,
                    bool isAlwaysUninit);

  void VisitBlockExpr(BlockExpr *be);
  void VisitDeclStmt(DeclStmt *ds);
  void VisitDeclRefExpr(DeclRefExpr *dr);
  void VisitUnaryOperator(UnaryOperator *uo);
  void VisitBinaryOperator(BinaryOperator *bo);
  void VisitCastExpr(CastExpr *ce);
  void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *se);
  void BlockStmt_VisitObjCForCollectionStmt(ObjCForCollectionStmt *fs);
  
  bool isTrackedVar(const VarDecl *vd) {
    return ::isTrackedVar(vd, cast<DeclContext>(ac.getDecl()));
  }
  
  FindVarResult findBlockVarDecl(Expr *ex);
};
}

void TransferFunctions::reportUninit(const DeclRefExpr *ex,
                                     const VarDecl *vd, bool isAlwaysUnit) {
  if (handler) handler->handleUseOfUninitVariable(ex, vd, isAlwaysUnit);
}

FindVarResult TransferFunctions::findBlockVarDecl(Expr* ex) {
  if (DeclRefExpr* dr = dyn_cast<DeclRefExpr>(ex->IgnoreParenCasts()))
    if (VarDecl *vd = dyn_cast<VarDecl>(dr->getDecl()))
      if (isTrackedVar(vd))
        return FindVarResult(vd, dr);  
  return FindVarResult(0, 0);
}

void TransferFunctions::BlockStmt_VisitObjCForCollectionStmt(
    ObjCForCollectionStmt *fs) {
  
  Visit(fs->getCollection());
  
  // This represents an initialization of the 'element' value.
  Stmt *element = fs->getElement();
  const VarDecl* vd = 0;
  
  if (DeclStmt* ds = dyn_cast<DeclStmt>(element)) {
    vd = cast<VarDecl>(ds->getSingleDecl());
    if (!isTrackedVar(vd))
      vd = 0;
  }
  else {
    // Initialize the value of the reference variable.
    const FindVarResult &res = findBlockVarDecl(cast<Expr>(element));
    vd = res.getDecl();
    if (!vd) {
      Visit(element);
      return;
    }
  }
  
  if (vd)
    vals[vd] = Initialized;
}

void TransferFunctions::VisitBlockExpr(BlockExpr *be) {
  if (!flagBlockUses || !handler)
    return;
  AnalysisContext::referenced_decls_iterator i, e;
  llvm::tie(i, e) = ac.getReferencedBlockVars(be->getBlockDecl());
  for ( ; i != e; ++i) {
    const VarDecl *vd = *i;
    if (vd->getAttr<BlocksAttr>() || !vd->hasLocalStorage() || 
        !isTrackedVar(vd))
      continue;
    Value v = vals[vd];
    if (isUninitialized(v))
      handler->handleUseOfUninitVariable(be, vd, isAlwaysUninit(v));
  }
}

void TransferFunctions::VisitDeclStmt(DeclStmt *ds) {
  for (DeclStmt::decl_iterator DI = ds->decl_begin(), DE = ds->decl_end();
       DI != DE; ++DI) {
    if (VarDecl *vd = dyn_cast<VarDecl>(*DI)) {
      if (isTrackedVar(vd)) {
        vals[vd] = Uninitialized;
        if (Stmt *init = vd->getInit()) {
          Visit(init);
          vals[vd] = Initialized;
        }
      }
      else if (Stmt *init = vd->getInit()) {
        Visit(init);
      }
    }
  }
}

void TransferFunctions::VisitDeclRefExpr(DeclRefExpr *dr) {
  // We assume that DeclRefExprs wrapped in an lvalue-to-rvalue cast
  // cannot be block-level expressions.  Therefore, we determine if
  // a DeclRefExpr is involved in a "load" by comparing it to the current
  // DeclRefExpr found when analyzing the last lvalue-to-rvalue CastExpr.
  // If a DeclRefExpr is not involved in a load, we are essentially computing
  // its address, either for assignment to a reference or via the '&' operator.
  // In such cases, treat the variable as being initialized, since this
  // analysis isn't powerful enough to do alias tracking.
  if (dr != currentDR)
    if (const VarDecl *vd = dyn_cast<VarDecl>(dr->getDecl()))
      if (isTrackedVar(vd))
        vals[vd] = Initialized;
}

void TransferFunctions::VisitBinaryOperator(clang::BinaryOperator *bo) {
  if (bo->isAssignmentOp()) {
    const FindVarResult &res = findBlockVarDecl(bo->getLHS());
    if (const VarDecl* vd = res.getDecl()) {
      // We assume that DeclRefExprs wrapped in a BinaryOperator "assignment"
      // cannot be block-level expressions.  Therefore, we determine if
      // a DeclRefExpr is involved in a "load" by comparing it to the current
      // DeclRefExpr found when analyzing the last lvalue-to-rvalue CastExpr.
      SaveAndRestore<const DeclRefExpr*> lastDR(currentDR, 
                                                res.getDeclRefExpr());
      Visit(bo->getRHS());
      Visit(bo->getLHS());

      ValueVector::reference val = vals[vd];
      if (isUninitialized(val)) {
        if (bo->getOpcode() != BO_Assign)
          reportUninit(res.getDeclRefExpr(), vd, isAlwaysUninit(val));
        val = Initialized;
      }
      return;
    }
  }
  Visit(bo->getRHS());
  Visit(bo->getLHS());
}

void TransferFunctions::VisitUnaryOperator(clang::UnaryOperator *uo) {
  switch (uo->getOpcode()) {
    case clang::UO_PostDec:
    case clang::UO_PostInc:
    case clang::UO_PreDec:
    case clang::UO_PreInc: {
      const FindVarResult &res = findBlockVarDecl(uo->getSubExpr());
      if (const VarDecl *vd = res.getDecl()) {
        // We assume that DeclRefExprs wrapped in a unary operator ++/--
        // cannot be block-level expressions.  Therefore, we determine if
        // a DeclRefExpr is involved in a "load" by comparing it to the current
        // DeclRefExpr found when analyzing the last lvalue-to-rvalue CastExpr.
        SaveAndRestore<const DeclRefExpr*> lastDR(currentDR, 
                                                  res.getDeclRefExpr());
        Visit(uo->getSubExpr());

        ValueVector::reference val = vals[vd];
        if (isUninitialized(val)) {
          reportUninit(res.getDeclRefExpr(), vd, isAlwaysUninit(val));
          // Don't cascade warnings.
          val = Initialized;
        }
        return;
      }
      break;
    }
    default:
      break;
  }
  Visit(uo->getSubExpr());
}

void TransferFunctions::VisitCastExpr(clang::CastExpr *ce) {
  if (ce->getCastKind() == CK_LValueToRValue) {
    const FindVarResult &res = findBlockVarDecl(ce->getSubExpr());
    if (const VarDecl *vd = res.getDecl()) {
      // We assume that DeclRefExprs wrapped in an lvalue-to-rvalue cast
      // cannot be block-level expressions.  Therefore, we determine if
      // a DeclRefExpr is involved in a "load" by comparing it to the current
      // DeclRefExpr found when analyzing the last lvalue-to-rvalue CastExpr.
      // Here we update 'currentDR' to be the one associated with this
      // lvalue-to-rvalue cast.  Then, when we analyze the DeclRefExpr, we
      // will know that we are not computing its lvalue for other purposes
      // than to perform a load.
      SaveAndRestore<const DeclRefExpr*> lastDR(currentDR, 
                                                res.getDeclRefExpr());
      Visit(ce->getSubExpr());
      if (currentVoidCast != ce) {
        Value val = vals[vd];
        if (isUninitialized(val)) {
          reportUninit(res.getDeclRefExpr(), vd, isAlwaysUninit(val));
          // Don't cascade warnings.
          vals[vd] = Initialized;
        }
      }
      return;
    }
  }
  else if (CStyleCastExpr *cse = dyn_cast<CStyleCastExpr>(ce)) {
    if (cse->getType()->isVoidType()) {
      // e.g. (void) x;
      SaveAndRestore<const Expr *>
        lastVoidCast(currentVoidCast, cse->getSubExpr()->IgnoreParens());
      Visit(cse->getSubExpr());
      return;
    }
  }
  Visit(ce->getSubExpr());
}

void TransferFunctions::VisitUnaryExprOrTypeTraitExpr(
                                          UnaryExprOrTypeTraitExpr *se) {
  if (se->getKind() == UETT_SizeOf) {
    if (se->getType()->isConstantSizeType())
      return;
    // Handle VLAs.
    Visit(se->getArgumentExpr());
  }
}

//------------------------------------------------------------------------====//
// High-level "driver" logic for uninitialized values analysis.
//====------------------------------------------------------------------------//

static bool runOnBlock(const CFGBlock *block, const CFG &cfg,
                       AnalysisContext &ac, CFGBlockValues &vals,
                       UninitVariablesHandler *handler = 0,
                       bool flagBlockUses = false) {
  
  if (const BinaryOperator *b = getLogicalOperatorInChain(block)) {
    CFGBlock::const_pred_iterator itr = block->pred_begin();
    BVPair vA = vals.getValueVectors(*itr, false);
    ++itr;
    BVPair vB = vals.getValueVectors(*itr, false);

    BVPair valsAB;
    
    if (b->getOpcode() == BO_LAnd) {
      // Merge the 'F' bits from the first and second.
      vals.mergeIntoScratch(*(vA.second ? vA.second : vA.first), true);
      vals.mergeIntoScratch(*(vB.second ? vB.second : vB.first), false);
      valsAB.first = vA.first;
      valsAB.second = &vals.getScratch();
    }
    else {
      // Merge the 'T' bits from the first and second.
      assert(b->getOpcode() == BO_LOr);
      vals.mergeIntoScratch(*vA.first, true);
      vals.mergeIntoScratch(*vB.first, false);
      valsAB.first = &vals.getScratch();
      valsAB.second = vA.second ? vA.second : vA.first;
    }
    return vals.updateValueVectors(block, valsAB);
  }

  // Default behavior: merge in values of predecessor blocks.
  vals.resetScratch();
  bool isFirst = true;
  for (CFGBlock::const_pred_iterator I = block->pred_begin(),
       E = block->pred_end(); I != E; ++I) {
    vals.mergeIntoScratch(vals.getValueVector(*I, block), isFirst);
    isFirst = false;
  }
  // Apply the transfer function.
  TransferFunctions tf(vals, cfg, ac, handler, flagBlockUses);
  for (CFGBlock::const_iterator I = block->begin(), E = block->end(); 
       I != E; ++I) {
    if (const CFGStmt *cs = dyn_cast<CFGStmt>(&*I)) {
      tf.BlockStmt_Visit(cs->getStmt());
    }
  }
  return vals.updateValueVectorWithScratch(block);
}

void clang::runUninitializedVariablesAnalysis(const DeclContext &dc,
                                              const CFG &cfg,
                                              AnalysisContext &ac,
                                              UninitVariablesHandler &handler) {
  CFGBlockValues vals(cfg);
  vals.computeSetOfDeclarations(dc);
  if (vals.hasNoDeclarations())
    return;
  DataflowWorklist worklist(cfg);
  llvm::BitVector previouslyVisited(cfg.getNumBlockIDs());
  
  worklist.enqueueSuccessors(&cfg.getEntry());

  while (const CFGBlock *block = worklist.dequeue()) {
    // Did the block change?
    bool changed = runOnBlock(block, cfg, ac, vals);    
    if (changed || !previouslyVisited[block->getBlockID()])
      worklist.enqueueSuccessors(block);    
    previouslyVisited[block->getBlockID()] = true;
  }
  
  // Run through the blocks one more time, and report uninitialized variabes.
  for (CFG::const_iterator BI = cfg.begin(), BE = cfg.end(); BI != BE; ++BI) {
    runOnBlock(*BI, cfg, ac, vals, &handler, /* flagBlockUses */ true);
  }
}

UninitVariablesHandler::~UninitVariablesHandler() {}

