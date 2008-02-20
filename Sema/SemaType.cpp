//===--- SemaType.cpp - Semantic Analysis for Types -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
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
QualType Sema::ConvertDeclSpecToType(DeclSpec &DS) {
  // FIXME: Should move the logic from DeclSpec::Finish to here for validity
  // checking.
  QualType Result;
  
  switch (DS.getTypeSpecType()) {
  default: return QualType(); // FIXME: Handle unimp cases!
  case DeclSpec::TST_void: return Context.VoidTy;
  case DeclSpec::TST_char:
    if (DS.getTypeSpecSign() == DeclSpec::TSS_unspecified)
      Result = Context.CharTy;
    else if (DS.getTypeSpecSign() == DeclSpec::TSS_signed)
      Result = Context.SignedCharTy;
    else {
      assert(DS.getTypeSpecSign() == DeclSpec::TSS_unsigned &&
             "Unknown TSS value");
      Result = Context.UnsignedCharTy;
    }
    break;
  case DeclSpec::TST_unspecified:  // Unspecific typespec defaults to int.
  case DeclSpec::TST_int: {
    if (DS.getTypeSpecSign() != DeclSpec::TSS_unsigned) {
      switch (DS.getTypeSpecWidth()) {
      case DeclSpec::TSW_unspecified: Result = Context.IntTy; break;
      case DeclSpec::TSW_short:       Result = Context.ShortTy; break;
      case DeclSpec::TSW_long:        Result = Context.LongTy; break;
      case DeclSpec::TSW_longlong:    Result = Context.LongLongTy; break;
      }
    } else {
      switch (DS.getTypeSpecWidth()) {
      case DeclSpec::TSW_unspecified: Result = Context.UnsignedIntTy; break;
      case DeclSpec::TSW_short:       Result = Context.UnsignedShortTy; break;
      case DeclSpec::TSW_long:        Result = Context.UnsignedLongTy; break;
      case DeclSpec::TSW_longlong:    Result =Context.UnsignedLongLongTy; break;
      }
    }
    break;
  }
  case DeclSpec::TST_float: Result = Context.FloatTy; break;
  case DeclSpec::TST_double:
    if (DS.getTypeSpecWidth() == DeclSpec::TSW_long)
      Result = Context.LongDoubleTy;
    else
      Result = Context.DoubleTy;
    break;
  case DeclSpec::TST_bool: Result = Context.BoolTy; break; // _Bool or bool
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
    Result = Context.getTagDeclType(cast<TagDecl>(D));
    break;
  }    
  case DeclSpec::TST_typedef: {
    Decl *D = static_cast<Decl *>(DS.getTypeRep());
    assert(D && "Didn't get a decl for a typedef?");
    assert(DS.getTypeSpecWidth() == 0 && DS.getTypeSpecComplex() == 0 &&
           DS.getTypeSpecSign() == 0 &&
           "Can't handle qualifiers on typedef names yet!");
    // FIXME: Adding a TST_objcInterface clause doesn't seem ideal, so
    // we have this "hack" for now... 
    if (ObjCInterfaceDecl *ObjCIntDecl = dyn_cast<ObjCInterfaceDecl>(D)) {
      if (DS.getProtocolQualifiers() == 0) {
        Result = Context.getObjCInterfaceType(ObjCIntDecl);
        break;
      }
      
      Action::DeclTy **PPDecl = &(*DS.getProtocolQualifiers())[0];
      Result = Context.getObjCQualifiedInterfaceType(ObjCIntDecl,
                                   reinterpret_cast<ObjCProtocolDecl**>(PPDecl),
                                                    DS.NumProtocolQualifiers());
      break;
    }
    else if (TypedefDecl *typeDecl = dyn_cast<TypedefDecl>(D)) {
      if (Context.getObjCIdType() == Context.getTypedefType(typeDecl)
          && DS.getProtocolQualifiers()) {
          // id<protocol-list>
        Action::DeclTy **PPDecl = &(*DS.getProtocolQualifiers())[0];
        Result = Context.getObjCQualifiedIdType(typeDecl->getUnderlyingType(),
                                 reinterpret_cast<ObjCProtocolDecl**>(PPDecl),
                                            DS.NumProtocolQualifiers());
        break;
      }
    }
    // TypeQuals handled by caller.
    Result = Context.getTypedefType(cast<TypedefDecl>(D));
    break;
  }
  case DeclSpec::TST_typeofType:
    Result = QualType::getFromOpaquePtr(DS.getTypeRep());
    assert(!Result.isNull() && "Didn't get a type for typeof?");
    // TypeQuals handled by caller.
    Result = Context.getTypeOfType(Result);
    break;
  case DeclSpec::TST_typeofExpr: {
    Expr *E = static_cast<Expr *>(DS.getTypeRep());
    assert(E && "Didn't get an expression for typeof?");
    // TypeQuals handled by caller.
    Result = Context.getTypeOfExpr(E);
    break;
  }
  }
  
  // Handle complex types.
  if (DS.getTypeSpecComplex() == DeclSpec::TSC_complex)
    Result = Context.getComplexType(Result);
  
  assert(DS.getTypeSpecComplex() != DeclSpec::TSC_imaginary &&
         "FIXME: imaginary types not supported yet!");
  
  // See if there are any attributes on the declspec that apply to the type (as
  // opposed to the decl).
  if (!DS.getAttributes())
    return Result;
  
  // Scan through and apply attributes to this type where it makes sense.  Some
  // attributes (such as __address_space__, __vector_size__, etc) apply to the
  // declspec, but others can be present in the decl spec even though they apply
  // to the decl.  Here we apply and delete attributes that apply to the
  // declspec and leave the others alone.
  llvm::SmallVector<AttributeList *, 8> LeftOverAttrs;
  AttributeList *AL = DS.getAttributes();
  while (AL) {
    AttributeList *ThisAttr = AL;
    AL = AL->getNext();
    
    LeftOverAttrs.push_back(ThisAttr);
  }
  
  // Rechain any attributes that haven't been deleted to the DeclSpec.
  AttributeList *List = 0;
  for (unsigned i = 0, e = LeftOverAttrs.size(); i != e; ++i) {
    LeftOverAttrs[i]->setNext(List);
    List = LeftOverAttrs[i];
  }
  
  DS.clearAttributes();
  DS.AddAttributes(List);
  //DS.setAttributes(List);
  return Result;
}

/// GetTypeForDeclarator - Convert the type for the specified declarator to Type
/// instances.
QualType Sema::GetTypeForDeclarator(Declarator &D, Scope *S) {
  // long long is a C99 feature.
  if (!getLangOptions().C99 && !getLangOptions().CPlusPlus0x &&
      D.getDeclSpec().getTypeSpecWidth() == DeclSpec::TSW_longlong)
    Diag(D.getDeclSpec().getTypeSpecWidthLoc(), diag::ext_longlong);
  
  QualType T = ConvertDeclSpecToType(D.getDeclSpec());
  
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
             D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
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
             D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
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
             D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
        T = Context.getPointerType(T);
        D.setInvalidType(true);
      } else if (const ReferenceType *RT = T->getAsReferenceType()) {
        // C++ 8.3.2p4: There shall be no ... arrays of references ...
        Diag(D.getIdentifierLoc(), diag::err_illegal_decl_array_of_references,
             D.getIdentifier() ? D.getIdentifier()->getName() : "type name");
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
      if (!ArraySize) {
        T = Context.getIncompleteArrayType(T, ASM, ATI.TypeQuals);
      } else if (!ArraySize->isIntegerConstantExpr(ConstVal, Context)) {
        T = Context.getVariableArrayType(T, ArraySize, ASM, ATI.TypeQuals);
      } else {
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
      
      // C99 6.7.5.3p1: The return type may not be a function or array type.
      if (T->isArrayType() || T->isFunctionType()) {
        Diag(DeclType.Loc, diag::err_func_returning_array_function,
             T.getAsString());
        T = Context.IntTy;
        D.setInvalidType(true);
      }
        
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
          if (const ArrayType *AT = ArgTy->getAsArrayType()) {
            // int x[restrict 4] ->  int *restrict
            ArgTy = Context.getPointerType(AT->getElementType());
            ArgTy = ArgTy.getQualifiedType(AT->getIndexTypeQualifier());
          } else if (ArgTy->isFunctionType())
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
              if (ArgTy.getCVRQualifiers())
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

/// ObjCGetTypeForMethodDefinition - Builds the type for a method definition
/// declarator
QualType Sema::ObjCGetTypeForMethodDefinition(DeclTy *D) {
  ObjCMethodDecl *MDecl = dyn_cast<ObjCMethodDecl>(static_cast<Decl *>(D));
  QualType T = MDecl->getResultType();
  llvm::SmallVector<QualType, 16> ArgTys;
  
  // Add the first two invisible argument types for self and _cmd.
  if (MDecl->isInstance()) {
    QualType selfTy = Context.getObjCInterfaceType(MDecl->getClassInterface());
    selfTy = Context.getPointerType(selfTy);
    ArgTys.push_back(selfTy);
  }
  else
    ArgTys.push_back(Context.getObjCIdType());
  ArgTys.push_back(Context.getObjCSelType());
      
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
