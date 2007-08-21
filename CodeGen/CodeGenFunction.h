//===--- CodeGenFunction.h - Per-Function state for LLVM CodeGen ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the internal per-function state used for llvm translation. 
//
//===----------------------------------------------------------------------===//

#ifndef CODEGEN_CODEGENFUNCTION_H
#define CODEGEN_CODEGENFUNCTION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/LLVMBuilder.h"
#include <vector>

namespace llvm {
  class Module;
}

namespace clang {
  class ASTContext;
  class Decl;
  class FunctionDecl;
  class TargetInfo;
  class QualType;
  class FunctionTypeProto;
  
  class Stmt;
  class CompoundStmt;
  class LabelStmt;
  class GotoStmt;
  class IfStmt;
  class WhileStmt;
  class DoStmt;
  class ForStmt;
  class ReturnStmt;
  class DeclStmt;
  
  class Expr;
  class DeclRefExpr;
  class StringLiteral;
  class IntegerLiteral;
  class FloatingLiteral;
  class CharacterLiteral;
  class TypesCompatibleExpr;
  
  class ImplicitCastExpr;
  class CastExpr;
  class CallExpr;
  class UnaryOperator;
  class BinaryOperator;
  class CompoundAssignOperator;
  class ArraySubscriptExpr;
  class OCUVectorElementExpr;
  class ConditionalOperator;
  class ChooseExpr;
  class PreDefinedExpr;
  
  class BlockVarDecl;
  class EnumConstantDecl;
  class ParmVarDecl;
namespace CodeGen {
  class CodeGenModule;
  

/// RValue - This trivial value class is used to represent the result of an
/// expression that is evaluated.  It can be one of two things: either a simple
/// LLVM SSA value, or the address of an aggregate value in memory.  These two
/// possibilities are discriminated by isAggregate/isScalar.
class RValue {
  llvm::Value *V;
  // TODO: Encode this into the low bit of pointer for more efficient
  // return-by-value.
  bool IsAggregate;
  
  // FIXME: Aggregate rvalues need to retain information about whether they are
  // volatile or not.
public:
  
  bool isAggregate() const { return IsAggregate; }
  bool isScalar() const { return !IsAggregate; }
  
  /// getVal() - Return the Value* of this scalar value.
  llvm::Value *getVal() const {
    assert(!isAggregate() && "Not a scalar!");
    return V;
  }

  /// getAggregateAddr() - Return the Value* of the address of the aggregate.
  llvm::Value *getAggregateAddr() const {
    assert(isAggregate() && "Not an aggregate!");
    return V;
  }
  
  static RValue get(llvm::Value *V) {
    RValue ER;
    ER.V = V;
    ER.IsAggregate = false;
    return ER;
  }
  static RValue getAggregate(llvm::Value *V) {
    RValue ER;
    ER.V = V;
    ER.IsAggregate = true;
    return ER;
  }
};


/// LValue - This represents an lvalue references.  Because C/C++ allow
/// bitfields, this is not a simple LLVM pointer, it may be a pointer plus a
/// bitrange.
class LValue {
  // FIXME: Volatility.  Restrict?
  // alignment?
  
  enum {
    Simple,       // This is a normal l-value, use getAddress().
    VectorElt,    // This is a vector element l-value (V[i]), use getVector*
    BitField,     // This is a bitfield l-value, use getBitfield*.
    OCUVectorElt  // This is an ocu vector subset, use getOCUVectorComp
  } LVType;
  
  llvm::Value *V;
  
  union {
    llvm::Value *VectorIdx;   // Index into a vector subscript: V[i]
    unsigned VectorElts;      // Encoded OCUVector element subset: V.xyx
  };
public:
  bool isSimple() const { return LVType == Simple; }
  bool isVectorElt() const { return LVType == VectorElt; }
  bool isBitfield() const { return LVType == BitField; }
  bool isOCUVectorElt() const { return LVType == OCUVectorElt; }
  
  // simple lvalue
  llvm::Value *getAddress() const { assert(isSimple()); return V; }
  // vector elt lvalue
  llvm::Value *getVectorAddr() const { assert(isVectorElt()); return V; }
  llvm::Value *getVectorIdx() const { assert(isVectorElt()); return VectorIdx; }
  // ocu vector elements.
  llvm::Value *getOCUVectorAddr() const { assert(isOCUVectorElt()); return V; }
  unsigned getOCUVectorElts() const {
    assert(isOCUVectorElt());
    return VectorElts;
  }
  
  
  static LValue MakeAddr(llvm::Value *V) {
    LValue R;
    R.LVType = Simple;
    R.V = V;
    return R;
  }
  
  static LValue MakeVectorElt(llvm::Value *Vec, llvm::Value *Idx) {
    LValue R;
    R.LVType = VectorElt;
    R.V = Vec;
    R.VectorIdx = Idx;
    return R;
  }
  
  static LValue MakeOCUVectorElt(llvm::Value *Vec, unsigned Elements) {
    LValue R;
    R.LVType = OCUVectorElt;
    R.V = Vec;
    R.VectorElts = Elements;
    return R;
  }
};

/// CodeGenFunction - This class organizes the per-function state that is used
/// while generating LLVM code.
class CodeGenFunction {
  CodeGenModule &CGM;  // Per-module state.
  TargetInfo &Target;
public:
  llvm::LLVMBuilder Builder;
private:
  
  const FunctionDecl *CurFuncDecl;
  llvm::Function *CurFn;

  /// AllocaInsertPoint - This is an instruction in the entry block before which
  /// we prefer to insert allocas.
  llvm::Instruction *AllocaInsertPt;
  
  const llvm::Type *LLVMIntTy;
  unsigned LLVMPointerWidth;
  
  /// LocalDeclMap - This keeps track of the LLVM allocas or globals for local C
  /// decls.
  llvm::DenseMap<const Decl*, llvm::Value*> LocalDeclMap;

  /// LabelMap - This keeps track of the LLVM basic block for each C label.
  llvm::DenseMap<const LabelStmt*, llvm::BasicBlock*> LabelMap;
  
  // BreakContinueStack - This keeps track of where break and continue 
  // statements should jump to.
  struct BreakContinue {
    BreakContinue(llvm::BasicBlock *bb, llvm::BasicBlock *cb)
      : BreakBlock(bb), ContinueBlock(cb) {}
      
    llvm::BasicBlock *BreakBlock;
    llvm::BasicBlock *ContinueBlock;
  }; 
  llvm::SmallVector<BreakContinue, 8> BreakContinueStack;
  
public:
  CodeGenFunction(CodeGenModule &cgm);
  
  ASTContext &getContext() const;

  void GenerateCode(const FunctionDecl *FD);
  
  const llvm::Type *ConvertType(QualType T);
  
  /// hasAggregateLLVMType - Return true if the specified AST type will map into
  /// an aggregate LLVM type or is void.
  static bool hasAggregateLLVMType(QualType T);
  
  /// getBasicBlockForLabel - Return the LLVM basicblock that the specified
  /// label maps to.
  llvm::BasicBlock *getBasicBlockForLabel(const LabelStmt *S);
  
  
  void EmitBlock(llvm::BasicBlock *BB);

  //===--------------------------------------------------------------------===//
  //                                  Helpers
  //===--------------------------------------------------------------------===//
  
  /// CreateTempAlloca - This creates a alloca and inserts it into the entry
  /// block.
  llvm::AllocaInst *CreateTempAlloca(const llvm::Type *Ty,
                                     const char *Name = "tmp");
  
  /// EvaluateExprAsBool - Perform the usual unary conversions on the specified
  /// expression and compare the result against zero, returning an Int1Ty value.
  llvm::Value *EvaluateExprAsBool(const Expr *E);

  
  /// EmitLoadOfComplex - Given an RValue reference for a complex, emit code to
  /// load the real and imaginary pieces, returning them as Real/Imag.
  void EmitLoadOfComplex(llvm::Value *SrcPtr, llvm::Value *&Real,
                         llvm::Value *&Imag);
  
  /// EmitStoreOfComplex - Store the specified real/imag parts into the
  /// specified value pointer.
  void EmitStoreOfComplex(llvm::Value *Real, llvm::Value *Imag,
                          llvm::Value *ResPtr);

  //===--------------------------------------------------------------------===//
  //                                Conversions
  //===--------------------------------------------------------------------===//
  
  /// EmitConversion - Convert the value specied by Val, whose type is ValTy, to
  /// the type specified by DstTy, following the rules of C99 6.3.
  RValue EmitConversion(RValue Val, QualType ValTy, QualType DstTy);
  
  /// ConvertScalarValueToBool - Convert the specified expression value to a
  /// boolean (i1) truth value.  This is equivalent to "Val == 0".
  llvm::Value *ConvertScalarValueToBool(RValue Val, QualType Ty);
  
  //===--------------------------------------------------------------------===//
  //                            Declaration Emission
  //===--------------------------------------------------------------------===//
  
  void EmitDecl(const Decl &D);
  void EmitEnumConstantDecl(const EnumConstantDecl &D);
  void EmitBlockVarDecl(const BlockVarDecl &D);
  void EmitLocalBlockVarDecl(const BlockVarDecl &D);
  void EmitParmDecl(const ParmVarDecl &D, llvm::Value *Arg);
  
  //===--------------------------------------------------------------------===//
  //                             Statement Emission
  //===--------------------------------------------------------------------===//

  void EmitStmt(const Stmt *S);
  void EmitCompoundStmt(const CompoundStmt &S);
  void EmitLabelStmt(const LabelStmt &S);
  void EmitGotoStmt(const GotoStmt &S);
  void EmitIfStmt(const IfStmt &S);
  void EmitWhileStmt(const WhileStmt &S);
  void EmitDoStmt(const DoStmt &S);
  void EmitForStmt(const ForStmt &S);
  void EmitReturnStmt(const ReturnStmt &S);
  void EmitDeclStmt(const DeclStmt &S);
  void EmitBreakStmt();
  void EmitContinueStmt();
  
  //===--------------------------------------------------------------------===//
  //                         LValue Expression Emission
  //===--------------------------------------------------------------------===//

  /// EmitLValue - Emit code to compute a designator that specifies the location
  /// of the expression.
  ///
  /// This can return one of two things: a simple address or a bitfield
  /// reference.  In either case, the LLVM Value* in the LValue structure is
  /// guaranteed to be an LLVM pointer type.
  ///
  /// If this returns a bitfield reference, nothing about the pointee type of
  /// the LLVM value is known: For example, it may not be a pointer to an
  /// integer.
  ///
  /// If this returns a normal address, and if the lvalue's C type is fixed
  /// size, this method guarantees that the returned pointer type will point to
  /// an LLVM type of the same size of the lvalue's type.  If the lvalue has a
  /// variable length type, this is not possible.
  ///
  LValue EmitLValue(const Expr *E);
  
  /// EmitLoadOfLValue - Given an expression that represents a value lvalue,
  /// this method emits the address of the lvalue, then loads the result as an
  /// rvalue, returning the rvalue.
  RValue EmitLoadOfLValue(const Expr *E);
  RValue EmitLoadOfLValue(LValue V, QualType LVType);
  RValue EmitLoadOfOCUElementLValue(LValue V, QualType LVType);

  
  /// EmitStoreThroughLValue - Store the specified rvalue into the specified
  /// lvalue, where both are guaranteed to the have the same type, and that type
  /// is 'Ty'.
  void EmitStoreThroughLValue(RValue Src, LValue Dst, QualType Ty);
  void EmitStoreThroughOCUComponentLValue(RValue Src, LValue Dst, QualType Ty);
  
  LValue EmitDeclRefLValue(const DeclRefExpr *E);
  LValue EmitStringLiteralLValue(const StringLiteral *E);
  LValue EmitPreDefinedLValue(const PreDefinedExpr *E);
  LValue EmitUnaryOpLValue(const UnaryOperator *E);
  LValue EmitArraySubscriptExpr(const ArraySubscriptExpr *E);
  LValue EmitOCUVectorElementExpr(const OCUVectorElementExpr *E);
    
  //===--------------------------------------------------------------------===//
  //                         Scalar Expression Emission
  //===--------------------------------------------------------------------===//

  void EmitCompoundAssignmentOperands(const CompoundAssignOperator *CAO,
                                      LValue &LHSLV, RValue &LHS, RValue &RHS);
  RValue EmitCompoundAssignmentResult(const CompoundAssignOperator *E,
                                      LValue LHSLV, RValue ResV);
  
  RValue EmitExpr(const Expr *E);
  RValue EmitIntegerLiteral(const IntegerLiteral *E);
  RValue EmitFloatingLiteral(const FloatingLiteral *E);
  RValue EmitCharacterLiteral(const CharacterLiteral *E);
  RValue EmitTypesCompatibleExpr(const TypesCompatibleExpr *E);
   
  RValue EmitImplicitCastExpr(const ImplicitCastExpr *Op);
  RValue EmitCastExpr(const Expr *Op, QualType DestTy);
  RValue EmitCallExpr(const CallExpr *E);
  RValue EmitBuiltinExpr(unsigned builtinID, const CallExpr *E);
  RValue EmitArraySubscriptExprRV(const ArraySubscriptExpr *E);

  // Unary Operators.
  RValue EmitUnaryOperator(const UnaryOperator *E);
  RValue EmitUnaryIncDec  (const UnaryOperator *E);
  RValue EmitUnaryAddrOf  (const UnaryOperator *E);
  RValue EmitUnaryPlus    (const UnaryOperator *E);
  RValue EmitUnaryMinus   (const UnaryOperator *E);
  RValue EmitUnaryNot     (const UnaryOperator *E);
  RValue EmitUnaryLNot    (const UnaryOperator *E);
  RValue EmitSizeAlignOf  (QualType TypeToSize, QualType RetType,bool isSizeOf);
  // FIXME: real/imag
  
  // Binary Operators.
  RValue EmitBinaryOperator(const BinaryOperator *E);
  RValue EmitBinaryMul(const BinaryOperator *E);
  RValue EmitBinaryDiv(const BinaryOperator *E);
  RValue EmitBinaryRem(const BinaryOperator *E);
  RValue EmitMul(RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitDiv(RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitRem(RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitAdd(RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitPointerAdd(RValue LHS, QualType LHSTy,
                        RValue RHS, QualType RHSTy, QualType EltTy);
  RValue EmitSub(RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitPointerSub(RValue LHS, QualType LHSTy,
                        RValue RHS, QualType RHSTy, QualType EltTy);
  RValue EmitShl(RValue LHS, RValue RHS, QualType ResTy);
  RValue EmitShr(RValue LHS, RValue RHS, QualType ResTy);
  RValue EmitBinaryCompare(const BinaryOperator *E, unsigned UICmpOpc,
                           unsigned SICmpOpc, unsigned FCmpOpc);
  RValue EmitAnd(RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitOr (RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitXor(RValue LHS, RValue RHS, QualType EltTy);
  RValue EmitBinaryLAnd(const BinaryOperator *E);
  RValue EmitBinaryLOr(const BinaryOperator *E);
  
  RValue EmitBinaryAssign(const BinaryOperator *E);
  RValue EmitBinaryComma(const BinaryOperator *E);
  
  // Conditional Operator.
  RValue EmitConditionalOperator(const ConditionalOperator *E);
  RValue EmitChooseExpr(const ChooseExpr *E);
  
  //===--------------------------------------------------------------------===//
  //                       Aggregate Expression Emission
  //===--------------------------------------------------------------------===//
  
  void EmitAggregateCopy(llvm::Value *DestPtr, llvm::Value *SrcPtr,
                         QualType EltTy);
  
  /// EmitAggExpr - Emit the computation of the specified expression of
  /// aggregate type.  The result is computed into DestPtr.  Note that if
  /// DestPtr is null, the value of the aggregate expression is not needed.
  void EmitAggExpr(const Expr *E, llvm::Value *DestPtr, bool VolatileDest);
  
  /// EmitComplexExpr - Emit the computation of the specified expression of
  /// complex type, ignoring the result.
  void EmitComplexExpr(const Expr *E);
};
}  // end namespace CodeGen
}  // end namespace clang

#endif
