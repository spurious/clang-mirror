//===--- SemaType.cpp - Semantic Analysis for Types -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements type-related semantic analysis.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Parse/DeclSpec.h"
using namespace llvm;
using namespace clang;

/// ConvertDeclSpecToType - Convert the specified declspec to the appropriate
/// type object.  This returns null on error.
static TypeRef ConvertDeclSpecToType(const DeclSpec &DS, ASTContext &Ctx) {
  // FIXME: Should move the logic from DeclSpec::Finish to here for validity
  // checking.
  
  switch (DS.TypeSpecType) {
  default: return TypeRef(); // FIXME: Handle unimp cases!
  case DeclSpec::TST_void: return Ctx.VoidTy;
  case DeclSpec::TST_char:
    if (DS.TypeSpecSign == DeclSpec::TSS_unspecified)
      return Ctx.CharTy;
    else if (DS.TypeSpecSign == DeclSpec::TSS_signed)
      return Ctx.SignedCharTy;
    else {
      assert(DS.TypeSpecSign == DeclSpec::TSS_unsigned && "Unknown TSS value");
      return Ctx.UnsignedCharTy;
    }
  case DeclSpec::TST_int:
    if (DS.TypeSpecSign != DeclSpec::TSS_unsigned) {
      switch (DS.TypeSpecWidth) {
      case DeclSpec::TSW_unspecified: return Ctx.IntTy;
      case DeclSpec::TSW_short:       return Ctx.ShortTy;
      case DeclSpec::TSW_long:        return Ctx.LongTy;
      case DeclSpec::TSW_longlong:    return Ctx.LongLongTy;
      }
    } else {
      switch (DS.TypeSpecWidth) {
      case DeclSpec::TSW_unspecified: return Ctx.UnsignedIntTy;
      case DeclSpec::TSW_short:       return Ctx.UnsignedShortTy;
      case DeclSpec::TSW_long:        return Ctx.UnsignedLongTy;
      case DeclSpec::TSW_longlong:    return Ctx.UnsignedLongLongTy;
      }
    }
  case DeclSpec::TST_float:
    if (DS.TypeSpecComplex == DeclSpec::TSC_unspecified)
      return Ctx.FloatTy;
    assert(DS.TypeSpecComplex == DeclSpec::TSC_complex &&
           "FIXME: imaginary types not supported yet!");
    return Ctx.FloatComplexTy;
    
  case DeclSpec::TST_double: {
    bool isLong = DS.TypeSpecWidth == DeclSpec::TSW_long;
    if (DS.TypeSpecComplex == DeclSpec::TSC_unspecified)
      return isLong ? Ctx.LongDoubleTy : Ctx.DoubleTy;
    assert(DS.TypeSpecComplex == DeclSpec::TSC_complex &&
           "FIXME: imaginary types not supported yet!");
    return isLong ? Ctx.LongDoubleComplexTy : Ctx.DoubleComplexTy;
  }
  case DeclSpec::TST_bool:         // _Bool
    return Ctx.BoolTy;
  case DeclSpec::TST_decimal32:    // _Decimal32
  case DeclSpec::TST_decimal64:    // _Decimal64
  case DeclSpec::TST_decimal128:   // _Decimal128
    assert(0 && "FIXME: GNU decimal extensions not supported yet!"); 
//case DeclSpec::TST_enum:
//case DeclSpec::TST_union:
//case DeclSpec::TST_struct:
  case DeclSpec::TST_typedef: {
    Decl *D = (Decl *)DS.TypenameRep;
    assert(D && "Didn't get a decl for a typedef?");
    assert(DS.TypeSpecWidth == 0 && DS.TypeSpecComplex == 0 &&
           DS.TypeSpecSign == 0 &&
           "Can't handle qualifiers on typedef names yet!");
    // TypeQuals handled by caller.
    return Ctx.getTypeDeclType(cast<TypedefDecl>(D));
  }
  }
}

/// GetTypeForDeclarator - Convert the type for the specified declarator to Type
/// instances.
TypeRef Sema::GetTypeForDeclarator(Declarator &D, Scope *S) {
  TypeRef T = ConvertDeclSpecToType(D.getDeclSpec(), Context);
  
  // Apply const/volatile/restrict qualifiers to T.
  T = T.getQualifiedType(D.getDeclSpec().TypeQualifiers);
  
  // Walk the DeclTypeInfo, building the recursive type as we go.  DeclTypeInfos
  // are ordered from the identifier out, which is opposite of what we want :).
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    const DeclaratorTypeInfo &DeclType = D.getTypeObject(e-i-1);
    switch (DeclType.Kind) {
    default: assert(0 && "Unknown decltype!");
    case DeclaratorTypeInfo::Pointer:
      T = Context.getPointerType(T);

      // Apply the pointer typequals to the pointer object.
      T = T.getQualifiedType(DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorTypeInfo::Array: {
      const DeclaratorTypeInfo::ArrayTypeInfo &ATI = DeclType.Arr;
      ArrayType::ArraySizeModifier ASM;
      if (ATI.isStar)
        ASM = ArrayType::Star;
      else if (ATI.hasStatic)
        ASM = ArrayType::Static;
      else
        ASM = ArrayType::Normal;
      
      T = Context.getArrayType(T, ASM, ATI.TypeQuals, ATI.NumElts);
      break;
    }
    case DeclaratorTypeInfo::Function:
      return TypeRef();   // FIXME: implement these!
    }
  }
  
  return T;
}

Sema::TypeResult Sema::ParseTypeName(Scope *S, Declarator &D) {
  // C99 6.7.6: Type names have no identifier.  This is already validated by
  // the parser.
  assert(D.getIdentifier() == 0 && "Type name should have no identifier!");
  
  TypeRef T = GetTypeForDeclarator(D, S);
  
  // If the type of the declarator was invalid, this is an invalid typename.
  if (T.isNull())
    return true;
  
  return T.getAsOpaquePtr();
}
