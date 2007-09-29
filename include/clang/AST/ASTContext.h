//===--- ASTContext.h - Context to hold long-lived AST nodes ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTContext interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTCONTEXT_H
#define LLVM_CLANG_AST_ASTCONTEXT_H

#include "clang/Lex/IdentifierTable.h" // FIXME: Move IdentifierTable to Basic
#include "clang/AST/Builtins.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/FoldingSet.h"
#include <vector>

namespace clang {
  class TargetInfo;

/// ASTContext - This class holds long-lived AST nodes (such as types and
/// decls) that can be referred to throughout the semantic analysis of a file.
class ASTContext {
  std::vector<Type*> Types;
  llvm::FoldingSet<ComplexType> ComplexTypes;
  llvm::FoldingSet<PointerType> PointerTypes;
  llvm::FoldingSet<ReferenceType> ReferenceTypes;
  llvm::FoldingSet<ConstantArrayType> ArrayTypes;
  llvm::FoldingSet<VectorType> VectorTypes;
  llvm::FoldingSet<FunctionTypeNoProto> FunctionTypeNoProtos;
  llvm::FoldingSet<FunctionTypeProto> FunctionTypeProtos;
  llvm::DenseMap<const RecordDecl*, const RecordLayout*> RecordLayoutInfo;
  llvm::DenseMap<const IdentifierInfo*, ObjcProtocolDecl*> ProtocolNameInfo;
  llvm::SmallVector<ObjcImplementationDecl*, 8> ImplementationClassInfo;
  RecordDecl *CFConstantStringTypeDecl;
public:
  
  SourceManager &SourceMgr;
  TargetInfo &Target;
  IdentifierTable &Idents;
  llvm::FoldingSet<MultiKeywordSelector> &Selectors;
  Builtin::Context BuiltinInfo;

  // Builtin Types.
  QualType VoidTy;
  QualType BoolTy;
  QualType CharTy;
  QualType SignedCharTy, ShortTy, IntTy, LongTy, LongLongTy;
  QualType UnsignedCharTy, UnsignedShortTy, UnsignedIntTy, UnsignedLongTy;
  QualType UnsignedLongLongTy;
  QualType FloatTy, DoubleTy, LongDoubleTy;
  QualType FloatComplexTy, DoubleComplexTy, LongDoubleComplexTy;
  
  ASTContext(SourceManager &SM, TargetInfo &t, IdentifierTable &idents,
             llvm::FoldingSet<MultiKeywordSelector> &sels) : 
    CFConstantStringTypeDecl(0), SourceMgr(SM), Target(t), 
    Idents(idents), Selectors(sels) {
    InitBuiltinTypes();
    BuiltinInfo.InitializeBuiltins(idents, Target);
  }    
  ~ASTContext();
  
  void PrintStats() const;

  //===--------------------------------------------------------------------===//
  //                           Type Constructors
  //===--------------------------------------------------------------------===//
  
  /// getComplexType - Return the uniqued reference to the type for a complex
  /// number with the specified element type.
  QualType getComplexType(QualType T);
  
  /// getPointerType - Return the uniqued reference to the type for a pointer to
  /// the specified type.
  QualType getPointerType(QualType T);
  
  /// getReferenceType - Return the uniqued reference to the type for a
  /// reference to the specified type.
  QualType getReferenceType(QualType T);
  
  /// getVariableArrayType - Returns a non-unique reference to the type for a
  /// variable array of the specified element type.
  QualType getVariableArrayType(QualType EltTy, Expr *NumElts,
                                ArrayType::ArraySizeModifier ASM,
                                unsigned EltTypeQuals);

  /// getConstantArrayType - Return the unique reference to the type for a
  /// constant array of the specified element type.
  QualType getConstantArrayType(QualType EltTy, const llvm::APInt &ArySize,
                                ArrayType::ArraySizeModifier ASM,
                                unsigned EltTypeQuals);
                        
  /// getVectorType - Return the unique reference to a vector type of
  /// the specified element type and size. VectorType must be a built-in type.
  QualType getVectorType(QualType VectorType, unsigned NumElts);

  /// getOCUVectorType - Return the unique reference to an OCU vector type of
  /// the specified element type and size. VectorType must be a built-in type.
  QualType getOCUVectorType(QualType VectorType, unsigned NumElts);

  /// getFunctionTypeNoProto - Return a K&R style C function type like 'int()'.
  ///
  QualType getFunctionTypeNoProto(QualType ResultTy);
  
  /// getFunctionType - Return a normal function type with a typed argument
  /// list.  isVariadic indicates whether the argument list includes '...'.
  QualType getFunctionType(QualType ResultTy, QualType *ArgArray,
                           unsigned NumArgs, bool isVariadic);
  
  /// getTypedefType - Return the unique reference to the type for the
  /// specified typename decl.
  QualType getTypedefType(TypedefDecl *Decl);
  QualType getObjcInterfaceType(ObjcInterfaceDecl *Decl);

  /// getTypeOfType - GCC extension.
  QualType getTypeOfExpr(Expr *e);
  QualType getTypeOfType(QualType t);
  
  /// getTagDeclType - Return the unique reference to the type for the
  /// specified TagDecl (struct/union/class/enum) decl.
  QualType getTagDeclType(TagDecl *Decl);
  
  /// getSizeType - Return the unique type for "size_t" (C99 7.17), defined
  /// in <stddef.h>. The sizeof operator requires this (C99 6.5.3.4p4).
  QualType getSizeType() const;
  
  /// getPointerDiffType - Return the unique type for "ptrdiff_t" (ref?)
  /// defined in <stddef.h>. Pointer - pointer requires this (C99 6.5.6p9).
  QualType getPointerDiffType() const;
  
  // getCFConstantStringType - Return the type used for constant CFStrings. 
  QualType getCFConstantStringType(); 
  
  //===--------------------------------------------------------------------===//
  //                         Type Sizing and Analysis
  //===--------------------------------------------------------------------===//
  
  /// getTypeInfo - Get the size and alignment of the specified complete type in
  /// bits.
  std::pair<uint64_t, unsigned> getTypeInfo(QualType T, SourceLocation L);
  
  /// getTypeSize - Return the size of the specified type, in bits.  This method
  /// does not work on incomplete types.
  uint64_t getTypeSize(QualType T, SourceLocation L) {
    return getTypeInfo(T, L).first;
  }
  
  /// getTypeAlign - Return the alignment of the specified type, in bits.  This
  /// method does not work on incomplete types.
  unsigned getTypeAlign(QualType T, SourceLocation L) {
    return getTypeInfo(T, L).second;
  }
  
  /// getRecordLayout - Get or compute information about the layout of the
  /// specified record (struct/union/class), which indicates its size and field
  /// position information.
  const RecordLayout &getRecordLayout(const RecordDecl *D, SourceLocation L);
  
  ObjcProtocolDecl* getObjCProtocolDecl(const IdentifierInfo* ProtocolName) 
  { return ProtocolNameInfo[ProtocolName]; }
  void setObjCProtocolDecl(const IdentifierInfo* ProtocolName,
                            ObjcProtocolDecl* ProtocolDecl)
  { ProtocolNameInfo[ProtocolName] = ProtocolDecl; }
  
  ObjcImplementationDecl* getObjcImplementationClass(unsigned ix) {
    return ImplementationClassInfo[ix];
  }
  void setObjcImplementationClass(ObjcImplementationDecl* ImplDecl) {
    ImplementationClassInfo.push_back(ImplDecl);
  }
  unsigned sizeObjcImplementationClass() const {
    return ImplementationClassInfo.size();
  }
  
  //===--------------------------------------------------------------------===//
  //                            Type Operators
  //===--------------------------------------------------------------------===//
  
  /// maxIntegerType - Returns the highest ranked integer type. Handles 3
  /// different type combos: unsigned/unsigned, signed/signed, signed/unsigned.
  static QualType maxIntegerType(QualType lhs, QualType rhs);
  
  /// compareFloatingType - Handles 3 different combos: 
  /// float/float, float/complex, complex/complex. 
  /// If lt > rt, return 1. If lt == rt, return 0. If lt < rt, return -1. 
  static int compareFloatingType(QualType lt, QualType rt);

  /// getFloatingTypeOfSizeWithinDomain - Returns a real floating 
  /// point or a complex type (based on typeDomain/typeSize). 
  /// 'typeDomain' is a real floating point or complex type.
  /// 'typeSize' is a real floating point or complex type.
  QualType getFloatingTypeOfSizeWithinDomain(QualType typeSize, 
                                             QualType typeDomain) const;
private:
  ASTContext(const ASTContext&); // DO NOT IMPLEMENT
  void operator=(const ASTContext&); // DO NOT IMPLEMENT
  
  void InitBuiltinTypes();
  void InitBuiltinType(QualType &R, BuiltinType::Kind K);
};
  
}  // end namespace clang

#endif
