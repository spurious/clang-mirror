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
#include "clang/AST/DeclObjC.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Basic/LangOptions.h"
using namespace clang;

/// ConvertDeclSpecToType - Convert the specified declspec to the appropriate
/// type object.  This returns null on error.
static QualType ConvertDeclSpecToType(const DeclSpec &DS, ASTContext &Ctx) {
  // FIXME: Should move the logic from DeclSpec::Finish to here for validity
  // checking.
  
  switch (DS.getTypeSpecType()) {
  default: return QualType(); // FIXME: Handle unimp cases!
  case DeclSpec::TST_void: return Ctx.VoidTy;
  case DeclSpec::TST_char:
    if (DS.getTypeSpecSign() == DeclSpec::TSS_unspecified)
      return Ctx.CharTy;
    else if (DS.getTypeSpecSign() == DeclSpec::TSS_signed)
      return Ctx.SignedCharTy;
    else {
      assert(DS.getTypeSpecSign() == DeclSpec::TSS_unsigned &&
             "Unknown TSS value");
      return Ctx.UnsignedCharTy;
    }
  case DeclSpec::TST_unspecified:  // Unspecific typespec defaults to int.
  case DeclSpec::TST_int: {
    QualType Result;
    if (DS.getTypeSpecSign() != DeclSpec::TSS_unsigned) {
      switch (DS.getTypeSpecWidth()) {
      case DeclSpec::TSW_unspecified: Result = Ctx.IntTy; break;
      case DeclSpec::TSW_short:       Result = Ctx.ShortTy; break;
      case DeclSpec::TSW_long:        Result = Ctx.LongTy; break;
      case DeclSpec::TSW_longlong:    Result = Ctx.LongLongTy; break;
      }
    } else {
      switch (DS.getTypeSpecWidth()) {
      case DeclSpec::TSW_unspecified: Result = Ctx.UnsignedIntTy; break;
      case DeclSpec::TSW_short:       Result = Ctx.UnsignedShortTy; break;
      case DeclSpec::TSW_long:        Result = Ctx.UnsignedLongTy; break;
      case DeclSpec::TSW_longlong:    Result = Ctx.UnsignedLongLongTy; break;
      }
    }
    // Handle complex integer types.
    if (DS.getTypeSpecComplex() == DeclSpec::TSC_unspecified)
      return Result;
    assert(DS.getTypeSpecComplex() == DeclSpec::TSC_complex &&
           "FIXME: imaginary types not supported yet!");
    return Ctx.getComplexType(Result);
  }
  case DeclSpec::TST_float:
    if (DS.getTypeSpecComplex() == DeclSpec::TSC_unspecified)
      return Ctx.FloatTy;
    assert(DS.getTypeSpecComplex() == DeclSpec::TSC_complex &&
           "FIXME: imaginary types not supported yet!");
    return Ctx.getComplexType(Ctx.FloatTy);
    
  case DeclSpec::TST_double: {
    bool isLong = DS.getTypeSpecWidth() == DeclSpec::TSW_long;
    QualType T = isLong ? Ctx.LongDoubleTy : Ctx.DoubleTy;
    if (DS.getTypeSpecComplex() == DeclSpec::TSC_unspecified)
      return T;
    assert(DS.getTypeSpecComplex() == DeclSpec::TSC_complex &&
           "FIXME: imaginary types not supported yet!");
    return Ctx.getComplexType(T);
  }
  case DeclSpec::TST_bool:         // _Bool or bool
    return Ctx.BoolTy;
  case DeclSpec::TST_decimal32:    // _Decimal32
  case DeclSpec::TST_decimal64:    // _Decimal64
  case DeclSpec::TST_decimal128:   // _Decimal128
    assert(0 && "FIXME: GNU decimal extensions not supported yet!"); 
  case DeclSpec::TST_enum:
  case DeclSpec::TST_union:
  case DeclSpec::TST_struct: {
    Decl *D = static_cast<Decl *>(DS.getTypeRep());
    assert(D && "Didn't get a decl for a enum/union/struct?");
    assert(DS.getTypeSpecWidth() == 0 && DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == 0 &&
           "Can't handle qualifiers on typedef names yet!");
    // TypeQuals handled by caller.
    return Ctx.getTagDeclType(cast<TagDecl>(D));
  }    
  case DeclSpec::TST_typedef: {
    Decl *D = static_cast<Decl *>(DS.getTypeRep());
    assert(D && "Didn't get a decl for a typedef?");
    assert(DS.getTypeSpecWidth() == 0 && DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == 0 &&
           "Can't handle qualifiers on typedef names yet!");
    // FIXME: Adding a TST_objcInterface clause doesn't seem ideal, so
    // we have this "hack" for now... 
    if (ObjcInterfaceDecl *ObjcIntDecl = dyn_cast<ObjcInterfaceDecl>(D)) {
      if (DS.getProtocolQualifiers() == 0)
        return Ctx.getObjcInterfaceType(ObjcIntDecl);
      
      Action::DeclTy **PPDecl = &(*DS.getProtocolQualifiers())[0];
      return Ctx.getObjcQualifiedInterfaceType(ObjcIntDecl,
               reinterpret_cast<ObjcProtocolDecl**>(PPDecl),
              DS.NumProtocolQualifiers());
    }
    else if (TypedefDecl *typeDecl = dyn_cast<TypedefDecl>(D)) {
      if (Ctx.getObjcIdType() == Ctx.getTypedefType(typeDecl)
          && DS.getProtocolQualifiers()) {
          // id<protocol-list>
        Action::DeclTy **PPDecl = &(*DS.getProtocolQualifiers())[0];
        return Ctx.getObjcQualifiedIdType(typeDecl,
                 reinterpret_cast<ObjcProtocolDecl**>(PPDecl),
                 DS.NumProtocolQualifiers());
      }
    }
    // TypeQuals handled by caller.
    return Ctx.getTypedefType(cast<TypedefDecl>(D));
  }
  case DeclSpec::TST_typeofType: {
    QualType T = QualType::getFromOpaquePtr(DS.getTypeRep());
    assert(!T.isNull() && "Didn't get a type for typeof?");
    // TypeQuals handled by caller.
    return Ctx.getTypeOfType(T);
  }
  case DeclSpec::TST_typeofExpr: {
    Expr *E = static_cast<Expr *>(DS.getTypeRep());
    assert(E && "Didn't get an expression for typeof?");
    // TypeQuals handled by caller.
    return Ctx.getTypeOfExpr(E);
  }
  }
}

/// GetTypeForDeclarator - Convert the type for the specified declarator to Type
/// instances.
QualType Sema::GetTypeForDeclarator(Declarator &D, Scope *S) {
  // long long is a C99 feature.
  if (!getLangOptions().C99 && !getLangOptions().CPlusPlus0x &&
      D.getDeclSpec().getTypeSpecWidth() == DeclSpec::TSW_longlong)
    Diag(D.getDeclSpec().getTypeSpecWidthLoc(), diag::ext_longlong);
  
  QualType T = ConvertDeclSpecToType(D.getDeclSpec(), Context);
  
  // Apply const/volatile/restrict qualifiers to T.
  T = T.getQualifiedType(D.getDeclSpec().getTypeQualifiers());
  
  // Walk the DeclTypeInfo, building the recursive type as we go.  DeclTypeInfos
  // are ordered from the identifier out, which is opposite of what we want :).
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    const DeclaratorChunk &DeclType = D.getTypeObject(e-i-1);
    switch (DeclType.Kind) {
    default: assert(0 && "Unknown decltype!");
    case DeclaratorChunk::Pointer:
      if (T->isReferenceType()) {
        // C++ 8.3.2p4: There shall be no ... pointers to references ...
        Diag(D.getIdentifierLoc(), diag::err_illegal_decl_pointer_to_reference,
             D.getIdentifier()->getName());
        D.setInvalidType(true);
        T = Context.IntTy;
      }

      // Apply the pointer typequals to the pointer object.
      T = Context.getPointerType(T).getQualifiedType(DeclType.Ptr.TypeQuals);
      break;
    case DeclaratorChunk::Reference:
      if (const ReferenceType *RT = T->getAsReferenceType()) {
        // C++ 8.3.2p4: There shall be no references to references ...
        Diag(D.getIdentifierLoc(),
             diag::err_illegal_decl_reference_to_reference,
             D.getIdentifier()->getName());
        D.setInvalidType(true);
        T = RT->getReferenceeType();
      }

      T = Context.getReferenceType(T);
      break;
    case DeclaratorChunk::Array: {
      const DeclaratorChunk::ArrayTypeInfo &ATI = DeclType.Arr;
      Expr *ArraySize = static_cast<Expr*>(ATI.NumElts);
      ArrayType::ArraySizeModifier ASM;
      if (ATI.isStar)
        ASM = ArrayType::Star;
      else if (ATI.hasStatic)
        ASM = ArrayType::Static;
      else
        ASM = ArrayType::Normal;

      // C99 6.7.5.2p1: If the element type is an incomplete or function type, 
      // reject it (e.g. void ary[7], struct foo ary[7], void ary[7]())
      if (T->isIncompleteType()) { 
        Diag(D.getIdentifierLoc(), diag::err_illegal_decl_array_incomplete_type,
             T.getAsString());
        T = Context.IntTy;
        D.setInvalidType(true);
      } else if (T->isFunctionType()) {
        Diag(D.getIdentifierLoc(), diag::err_illegal_decl_array_of_functions,
             D.getIdentifier()->getName());
        T = Context.getPointerType(T);
        D.setInvalidType(true);
      } else if (const ReferenceType *RT = T->getAsReferenceType()) {
        // C++ 8.3.2p4: There shall be no ... arrays of references ...
        Diag(D.getIdentifierLoc(), diag::err_illegal_decl_array_of_references,
             D.getIdentifier()->getName());
        T = RT->getReferenceeType();
        D.setInvalidType(true);
      } else if (const RecordType *EltTy = T->getAsRecordType()) {
        // If the element type is a struct or union that contains a variadic
        // array, reject it: C99 6.7.2.1p2.
        if (EltTy->getDecl()->hasFlexibleArrayMember()) {
          Diag(DeclType.Loc, diag::err_flexible_array_in_array,
               T.getAsString());
          T = Context.IntTy;
          D.setInvalidType(true);
        }
      }
      // C99 6.7.5.2p1: The size expression shall have integer type.
      if (ArraySize && !ArraySize->getType()->isIntegerType()) {
        Diag(ArraySize->getLocStart(), diag::err_array_size_non_int, 
             ArraySize->getType().getAsString(), ArraySize->getSourceRange());
        D.setInvalidType(true);
      }
      llvm::APSInt ConstVal(32);
      // If no expression was provided, we consider it a VLA.
      if (!ArraySize || !ArraySize->isIntegerConstantExpr(ConstVal, Context))
        T = Context.getVariableArrayType(T, ArraySize, ASM, ATI.TypeQuals);
      else {
        // C99 6.7.5.2p1: If the expression is a constant expression, it shall
        // have a value greater than zero.
        if (ConstVal.isSigned()) {
          if (ConstVal.isNegative()) {
            Diag(ArraySize->getLocStart(), 
                 diag::err_typecheck_negative_array_size,
                 ArraySize->getSourceRange());
            D.setInvalidType(true);
          } else if (ConstVal == 0) {
            // GCC accepts zero sized static arrays.
            Diag(ArraySize->getLocStart(), diag::ext_typecheck_zero_array_size,
                 ArraySize->getSourceRange());
          }
        } 
        T = Context.getConstantArrayType(T, ConstVal, ASM, ATI.TypeQuals);
      }
      // If this is not C99, extwarn about VLA's and C99 array size modifiers.
      if (!getLangOptions().C99 && 
          (ASM != ArrayType::Normal ||
           (ArraySize && !ArraySize->isIntegerConstantExpr(Context))))
        Diag(D.getIdentifierLoc(), diag::ext_vla);
      break;
    }
    case DeclaratorChunk::Function:
      // If the function declarator has a prototype (i.e. it is not () and
      // does not have a K&R-style identifier list), then the arguments are part
      // of the type, otherwise the argument list is ().
      const DeclaratorChunk::FunctionTypeInfo &FTI = DeclType.Fun;
      if (!FTI.hasPrototype) {
        // Simple void foo(), where the incoming T is the result type.
        T = Context.getFunctionTypeNoProto(T);

        // C99 6.7.5.3p3: Reject int(x,y,z) when it's not a function definition.
        if (FTI.NumArgs != 0)
          Diag(FTI.ArgInfo[0].IdentLoc, diag::err_ident_list_in_fn_declaration);
        
      } else {
        // Otherwise, we have a function with an argument list that is
        // potentially variadic.
        llvm::SmallVector<QualType, 16> ArgTys;
        
        for (unsigned i = 0, e = FTI.NumArgs; i != e; ++i) {
          QualType ArgTy = QualType::getFromOpaquePtr(FTI.ArgInfo[i].TypeInfo);
          assert(!ArgTy.isNull() && "Couldn't parse type?");
          //
          // Perform the default function/array conversion (C99 6.7.5.3p[7,8]).
          // This matches the conversion that is done in 
          // Sema::ActOnParamDeclarator(). Without this conversion, the
          // argument type in the function prototype *will not* match the
          // type in ParmVarDecl (which makes the code generator unhappy).
          //
          // FIXME: We still apparently need the conversion in 
          // Sema::ParseParamDeclarator(). This doesn't make any sense, since
          // it should be driving off the type being created here.
          // 
          // FIXME: If a source translation tool needs to see the original type,
          // then we need to consider storing both types somewhere...
          // 
          if (const ArrayType *AT = ArgTy->getAsArrayType())
            ArgTy = Context.getPointerType(AT->getElementType());
          else if (ArgTy->isFunctionType())
            ArgTy = Context.getPointerType(ArgTy);
          // Look for 'void'.  void is allowed only as a single argument to a
          // function with no other parameters (C99 6.7.5.3p10).  We record
          // int(void) as a FunctionTypeProto with an empty argument list.
          else if (ArgTy->isVoidType()) {
            // If this is something like 'float(int, void)', reject it.  'void'
            // is an incomplete type (C99 6.2.5p19) and function decls cannot
            // have arguments of incomplete type.
            if (FTI.NumArgs != 1 || FTI.isVariadic) {
              Diag(DeclType.Loc, diag::err_void_only_param);
              ArgTy = Context.IntTy;
              FTI.ArgInfo[i].TypeInfo = ArgTy.getAsOpaquePtr();
            } else if (FTI.ArgInfo[i].Ident) {
              // Reject, but continue to parse 'int(void abc)'.
              Diag(FTI.ArgInfo[i].IdentLoc,
                   diag::err_param_with_void_type);
              ArgTy = Context.IntTy;
              FTI.ArgInfo[i].TypeInfo = ArgTy.getAsOpaquePtr();
            } else {
              // Reject, but continue to parse 'float(const void)'.
              if (ArgTy.getQualifiers())
                Diag(DeclType.Loc, diag::err_void_param_qualified);
              
              // Do not add 'void' to the ArgTys list.
              break;
            }
          }
          
          ArgTys.push_back(ArgTy);
        }
        T = Context.getFunctionType(T, &ArgTys[0], ArgTys.size(),
                                    FTI.isVariadic);
      }
      break;
    }
  }
  
  return T;
}

/// ObjcGetTypeForMethodDefinition - Builds the type for a method definition
/// declarator
QualType Sema::ObjcGetTypeForMethodDefinition(DeclTy *D) {
  ObjcMethodDecl *MDecl = dyn_cast<ObjcMethodDecl>(static_cast<Decl *>(D));
  QualType T = MDecl->getResultType();
  llvm::SmallVector<QualType, 16> ArgTys;
  
  // Add the first two invisible argument types for self and _cmd.
  if (MDecl->isInstance()) {
    QualType selfTy = Context.getObjcInterfaceType(MDecl->getClassInterface());
    selfTy = Context.getPointerType(selfTy);
    ArgTys.push_back(selfTy);
  }
  else
    ArgTys.push_back(Context.getObjcIdType());
  ArgTys.push_back(Context.getObjcSelType());
      
  for (int i = 0; i <  MDecl->getNumParams(); i++) {
    ParmVarDecl *PDecl = MDecl->getParamDecl(i);
    QualType ArgTy = PDecl->getType();
    assert(!ArgTy.isNull() && "Couldn't parse type?");
    // Perform the default function/array conversion (C99 6.7.5.3p[7,8]).
    // This matches the conversion that is done in 
    // Sema::ParseParamDeclarator(). 
    if (const ArrayType *AT = ArgTy->getAsArrayType())
      ArgTy = Context.getPointerType(AT->getElementType());
    else if (ArgTy->isFunctionType())
      ArgTy = Context.getPointerType(ArgTy);
    ArgTys.push_back(ArgTy);
  }
  T = Context.getFunctionType(T, &ArgTys[0], ArgTys.size(),
                              MDecl->isVariadic());
  return T;
}

Sema::TypeResult Sema::ActOnTypeName(Scope *S, Declarator &D) {
  // C99 6.7.6: Type names have no identifier.  This is already validated by
  // the parser.
  assert(D.getIdentifier() == 0 && "Type name should have no identifier!");
  
  QualType T = GetTypeForDeclarator(D, S);

  assert(!T.isNull() && "GetTypeForDeclarator() returned null type");
  
  // In this context, we *do not* check D.getInvalidType(). If the declarator
  // type was invalid, GetTypeForDeclarator() still returns a "valid" type,
  // though it will not reflect the user specified type.
  return T.getAsOpaquePtr();
}

// Called from Parser::ParseParenDeclarator().
Sema::TypeResult Sema::ActOnParamDeclaratorType(Scope *S, Declarator &D) {
  // Note: parameters have identifiers, but we don't care about them here, we
  // just want the type converted.
  QualType T = GetTypeForDeclarator(D, S);
  
  assert(!T.isNull() && "GetTypeForDeclarator() returned null type");

  // In this context, we *do not* check D.getInvalidType(). If the declarator
  // type was invalid, GetTypeForDeclarator() still returns a "valid" type,
  // though it will not reflect the user specified type.
  return T.getAsOpaquePtr();
}
