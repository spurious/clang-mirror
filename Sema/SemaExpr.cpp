//===--- SemaExpr.cpp - Semantic Analysis for Expressions -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for expressions.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
using namespace clang;

/// ParseStringLiteral - The specified tokens were lexed as pasted string
/// fragments (e.g. "foo" "bar" L"baz").  The result string has to handle string
/// concatenation ([C99 5.1.1.2, translation phase #6]), so it may come from
/// multiple tokens.  However, the common case is that StringToks points to one
/// string.
/// 
Action::ExprResult
Sema::ParseStringLiteral(const Token *StringToks, unsigned NumStringToks) {
  assert(NumStringToks && "Must have at least one string!");

  StringLiteralParser Literal(StringToks, NumStringToks, PP, Context.Target);
  if (Literal.hadError)
    return ExprResult(true);

  llvm::SmallVector<SourceLocation, 4> StringTokLocs;
  for (unsigned i = 0; i != NumStringToks; ++i)
    StringTokLocs.push_back(StringToks[i].getLocation());
  
  // FIXME: handle wchar_t
  QualType t = Context.getPointerType(Context.CharTy);
  
  // Pass &StringTokLocs[0], StringTokLocs.size() to factory!
  return new StringLiteral(Literal.GetString(), Literal.GetStringLength(), 
                           Literal.AnyWide, t, StringToks[0].getLocation(),
                           StringToks[NumStringToks-1].getLocation());
}


/// ParseIdentifierExpr - The parser read an identifier in expression context,
/// validate it per-C99 6.5.1.  HasTrailingLParen indicates whether this
/// identifier is used in an function call context.
Sema::ExprResult Sema::ParseIdentifierExpr(Scope *S, SourceLocation Loc,
                                           IdentifierInfo &II,
                                           bool HasTrailingLParen) {
  // Could be enum-constant or decl.
  Decl *D = LookupScopedDecl(&II, Decl::IDNS_Ordinary, Loc, S);
  if (D == 0) {
    // Otherwise, this could be an implicitly declared function reference (legal
    // in C90, extension in C99).
    if (HasTrailingLParen &&
        // Not in C++.
        !getLangOptions().CPlusPlus)
      D = ImplicitlyDefineFunction(Loc, II, S);
    else {
      // If this name wasn't predeclared and if this is not a function call,
      // diagnose the problem.
      return Diag(Loc, diag::err_undeclared_var_use, II.getName());
    }
  }
  if (ValueDecl *VD = dyn_cast<ValueDecl>(D)) {
    ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(VD);
    
    // FIXME: generalize this for all decls.
    if (PVD && PVD->getInvalidType())
      return true;
    return new DeclRefExpr(VD, VD->getType(), Loc);
  }
  if (isa<TypedefDecl>(D))
    return Diag(Loc, diag::err_unexpected_typedef, II.getName());

  assert(0 && "Invalid decl");
  abort();
}

Sema::ExprResult Sema::ParsePreDefinedExpr(SourceLocation Loc,
                                           tok::TokenKind Kind) {
  PreDefinedExpr::IdentType IT;
  
  switch (Kind) {
  default:
    assert(0 && "Unknown simple primary expr!");
  case tok::kw___func__:       // primary-expression: __func__ [C99 6.4.2.2]
    IT = PreDefinedExpr::Func;
    break;
  case tok::kw___FUNCTION__:   // primary-expression: __FUNCTION__ [GNU]
    IT = PreDefinedExpr::Function;
    break;
  case tok::kw___PRETTY_FUNCTION__:  // primary-expression: __P..Y_F..N__ [GNU]
    IT = PreDefinedExpr::PrettyFunction;
    break;
  }
  
  // Pre-defined identifiers are always of type char *.
  return new PreDefinedExpr(Loc, Context.getPointerType(Context.CharTy), IT);
}

Sema::ExprResult Sema::ParseCharacterConstant(const Token &Tok) {
  llvm::SmallString<16> CharBuffer;
  CharBuffer.resize(Tok.getLength());
  const char *ThisTokBegin = &CharBuffer[0];
  unsigned ActualLength = PP.getSpelling(Tok, ThisTokBegin);
  
  CharLiteralParser Literal(ThisTokBegin, ThisTokBegin+ActualLength,
                            Tok.getLocation(), PP);
  if (Literal.hadError())
    return ExprResult(true);
  return new CharacterLiteral(Literal.getValue(), Context.IntTy, 
                              Tok.getLocation());
}

Action::ExprResult Sema::ParseNumericConstant(const Token &Tok) {
  // fast path for a single digit (which is quite common). A single digit 
  // cannot have a trigraph, escaped newline, radix prefix, or type suffix.
  if (Tok.getLength() == 1) {
    const char *t = PP.getSourceManager().getCharacterData(Tok.getLocation());
    
    unsigned IntSize = Context.getTypeSize(Context.IntTy, Tok.getLocation());
    return ExprResult(new IntegerLiteral(llvm::APInt(IntSize, *t-'0'),
                                         Context.IntTy, 
                                         Tok.getLocation()));
  }
  llvm::SmallString<512> IntegerBuffer;
  IntegerBuffer.resize(Tok.getLength());
  const char *ThisTokBegin = &IntegerBuffer[0];
  
  // Get the spelling of the token, which eliminates trigraphs, etc.
  unsigned ActualLength = PP.getSpelling(Tok, ThisTokBegin);
  NumericLiteralParser Literal(ThisTokBegin, ThisTokBegin+ActualLength, 
                               Tok.getLocation(), PP);
  if (Literal.hadError)
    return ExprResult(true);
  
  Expr *Res;
  
  if (Literal.isFloatingLiteral()) {
    // FIXME: handle float values > 32 (including compute the real type...).
    QualType Ty = Literal.isFloat ? Context.FloatTy : Context.DoubleTy;
    Res = new FloatingLiteral(Literal.GetFloatValue(), Ty, Tok.getLocation());
  } else if (!Literal.isIntegerLiteral()) {
    return ExprResult(true);
  } else {
    QualType t;

    // Get the value in the widest-possible width.
    llvm::APInt ResultVal(Context.Target.getIntMaxTWidth(Tok.getLocation()), 0);
   
    if (Literal.GetIntegerValue(ResultVal)) {
      // If this value didn't fit into uintmax_t, warn and force to ull.
      Diag(Tok.getLocation(), diag::warn_integer_too_large);
      t = Context.UnsignedLongLongTy;
      assert(Context.getTypeSize(t, Tok.getLocation()) == 
             ResultVal.getBitWidth() && "long long is not intmax_t?");
    } else {
      // If this value fits into a ULL, try to figure out what else it fits into
      // according to the rules of C99 6.4.4.1p5.
      
      // Octal, Hexadecimal, and integers with a U suffix are allowed to
      // be an unsigned int.
      bool AllowUnsigned = Literal.isUnsigned || Literal.getRadix() != 10;

      // Check from smallest to largest, picking the smallest type we can.
      if (!Literal.isLong && !Literal.isLongLong) {
        // Are int/unsigned possibilities?
        unsigned IntSize = Context.getTypeSize(Context.IntTy,Tok.getLocation());
        // Does it fit in a unsigned int?
        if (ResultVal.isIntN(IntSize)) {
          // Does it fit in a signed int?
          if (!Literal.isUnsigned && ResultVal[IntSize-1] == 0)
            t = Context.IntTy;
          else if (AllowUnsigned)
            t = Context.UnsignedIntTy;
        }
        
        if (!t.isNull())
          ResultVal.trunc(IntSize);
      }
      
      // Are long/unsigned long possibilities?
      if (t.isNull() && !Literal.isLongLong) {
        unsigned LongSize = Context.getTypeSize(Context.LongTy,
                                                Tok.getLocation());
     
        // Does it fit in a unsigned long?
        if (ResultVal.isIntN(LongSize)) {
          // Does it fit in a signed long?
          if (!Literal.isUnsigned && ResultVal[LongSize-1] == 0)
            t = Context.LongTy;
          else if (AllowUnsigned)
            t = Context.UnsignedLongTy;
        }
        if (!t.isNull())
          ResultVal.trunc(LongSize);
      }      
      
      // Finally, check long long if needed.
      if (t.isNull()) {
        unsigned LongLongSize =
          Context.getTypeSize(Context.LongLongTy, Tok.getLocation());
        
        // Does it fit in a unsigned long long?
        if (ResultVal.isIntN(LongLongSize)) {
          // Does it fit in a signed long long?
          if (!Literal.isUnsigned && ResultVal[LongLongSize-1] == 0)
            t = Context.LongLongTy;
          else if (AllowUnsigned)
            t = Context.UnsignedLongLongTy;
        }
      }
      
      // If we still couldn't decide a type, we probably have something that
      // does not fit in a signed long long, but has no U suffix.
      if (t.isNull()) {
        Diag(Tok.getLocation(), diag::warn_integer_too_large_for_signed);
        t = Context.UnsignedLongLongTy;
      }
    }

    Res = new IntegerLiteral(ResultVal, t, Tok.getLocation());
  }
  
  // If this is an imaginary literal, create the ImaginaryLiteral wrapper.
  if (Literal.isImaginary)
    Res = new ImaginaryLiteral(Res, Context.getComplexType(Res->getType()));
  
  return Res;
}

Action::ExprResult Sema::ParseParenExpr(SourceLocation L, SourceLocation R,
                                        ExprTy *Val) {
  Expr *e = (Expr *)Val;
  assert((e != 0) && "ParseParenExpr() missing expr");
  return new ParenExpr(L, R, e);
}

/// The UsualUnaryConversions() function is *not* called by this routine.
/// See C99 6.3.2.1p[2-4] for more details.
QualType Sema::CheckSizeOfAlignOfOperand(QualType exprType, 
                                         SourceLocation OpLoc, bool isSizeof) {
  // C99 6.5.3.4p1:
  if (isa<FunctionType>(exprType) && isSizeof)
    // alignof(function) is allowed.
    Diag(OpLoc, diag::ext_sizeof_function_type);
  else if (exprType->isVoidType())
    Diag(OpLoc, diag::ext_sizeof_void_type, isSizeof ? "sizeof" : "__alignof");
  else if (exprType->isIncompleteType()) {
    Diag(OpLoc, isSizeof ? diag::err_sizeof_incomplete_type : 
                           diag::err_alignof_incomplete_type,
         exprType.getAsString());
    return QualType(); // error
  }
  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return Context.getSizeType();
}

Action::ExprResult Sema::
ParseSizeOfAlignOfTypeExpr(SourceLocation OpLoc, bool isSizeof, 
                           SourceLocation LPLoc, TypeTy *Ty,
                           SourceLocation RPLoc) {
  // If error parsing type, ignore.
  if (Ty == 0) return true;
  
  // Verify that this is a valid expression.
  QualType ArgTy = QualType::getFromOpaquePtr(Ty);
  
  QualType resultType = CheckSizeOfAlignOfOperand(ArgTy, OpLoc, isSizeof);

  if (resultType.isNull())
    return true;
  return new SizeOfAlignOfTypeExpr(isSizeof, ArgTy, resultType, OpLoc, RPLoc);
}

QualType Sema::CheckRealImagOperand(Expr *&V, SourceLocation Loc) {
  DefaultFunctionArrayConversion(V);
  
  // These operators return the element type of a complex type.
  if (const ComplexType *CT = V->getType()->getAsComplexType())
    return CT->getElementType();
  
  // Otherwise they pass through real integer and floating point types here.
  if (V->getType()->isArithmeticType())
    return V->getType();
  
  // Reject anything else.
  Diag(Loc, diag::err_realimag_invalid_type, V->getType().getAsString());
  return QualType();
}



Action::ExprResult Sema::ParsePostfixUnaryOp(SourceLocation OpLoc, 
                                             tok::TokenKind Kind,
                                             ExprTy *Input) {
  UnaryOperator::Opcode Opc;
  switch (Kind) {
  default: assert(0 && "Unknown unary op!");
  case tok::plusplus:   Opc = UnaryOperator::PostInc; break;
  case tok::minusminus: Opc = UnaryOperator::PostDec; break;
  }
  QualType result = CheckIncrementDecrementOperand((Expr *)Input, OpLoc);
  if (result.isNull())
    return true;
  return new UnaryOperator((Expr *)Input, Opc, result, OpLoc);
}

Action::ExprResult Sema::
ParseArraySubscriptExpr(ExprTy *Base, SourceLocation LLoc,
                        ExprTy *Idx, SourceLocation RLoc) {
  Expr *LHSExp = static_cast<Expr*>(Base), *RHSExp = static_cast<Expr*>(Idx);

  // Perform default conversions.
  DefaultFunctionArrayConversion(LHSExp);
  DefaultFunctionArrayConversion(RHSExp);
  
  QualType LHSTy = LHSExp->getType(), RHSTy = RHSExp->getType();

  // C99 6.5.2.1p2: the expression e1[e2] is by definition precisely equivalent
  // to the expression *((e1)+(e2)). This means the array "Base" may actually be 
  // in the subscript position. As a result, we need to derive the array base 
  // and index from the expression types.
  Expr *BaseExpr, *IndexExpr;
  QualType ResultType;
  if (const PointerType *PTy = LHSTy->getAsPointerType()) {
    BaseExpr = LHSExp;
    IndexExpr = RHSExp;
    // FIXME: need to deal with const...
    ResultType = PTy->getPointeeType();
  } else if (const PointerType *PTy = RHSTy->getAsPointerType()) {
     // Handle the uncommon case of "123[Ptr]".
    BaseExpr = RHSExp;
    IndexExpr = LHSExp;
    // FIXME: need to deal with const...
    ResultType = PTy->getPointeeType();
  } else if (const VectorType *VTy = LHSTy->getAsVectorType()) {
    BaseExpr = LHSExp;    // vectors: V[123]
    IndexExpr = RHSExp;
    
    // Component access limited to variables (reject vec4.rg[1]).
    if (!isa<DeclRefExpr>(BaseExpr)) 
      return Diag(LLoc, diag::err_ocuvector_component_access, 
                  SourceRange(LLoc, RLoc));
    // FIXME: need to deal with const...
    ResultType = VTy->getElementType();
  } else {
    return Diag(LHSExp->getLocStart(), diag::err_typecheck_subscript_value, 
                RHSExp->getSourceRange());
  }              
  // C99 6.5.2.1p1
  if (!IndexExpr->getType()->isIntegerType())
    return Diag(IndexExpr->getLocStart(), diag::err_typecheck_subscript,
                IndexExpr->getSourceRange());

  // C99 6.5.2.1p1: "shall have type "pointer to *object* type".  In practice,
  // the following check catches trying to index a pointer to a function (e.g.
  // void (*)(int)). Functions are not objects in C99.
  if (!ResultType->isObjectType())
    return Diag(BaseExpr->getLocStart(), 
                diag::err_typecheck_subscript_not_object,
                BaseExpr->getType().getAsString(), BaseExpr->getSourceRange());

  return new ArraySubscriptExpr(LHSExp, RHSExp, ResultType, RLoc);
}

QualType Sema::
CheckOCUVectorComponent(QualType baseType, SourceLocation OpLoc,
                        IdentifierInfo &CompName, SourceLocation CompLoc) {
  const OCUVectorType *vecType = baseType->getAsOCUVectorType();
  
  // The vector accessor can't exceed the number of elements.
  const char *compStr = CompName.getName();
  if (strlen(compStr) > vecType->getNumElements()) {
    Diag(OpLoc, diag::err_ocuvector_component_exceeds_length, 
                baseType.getAsString(), SourceRange(CompLoc));
    return QualType();
  }
  // The component names must come from the same set.
  if (vecType->getPointAccessorIdx(*compStr) != -1) {
    do
      compStr++;
    while (*compStr && vecType->getPointAccessorIdx(*compStr) != -1);
  } else if (vecType->getColorAccessorIdx(*compStr) != -1) {
    do
      compStr++;
    while (*compStr && vecType->getColorAccessorIdx(*compStr) != -1);
  } else if (vecType->getTextureAccessorIdx(*compStr) != -1) {
    do 
      compStr++;
    while (*compStr && vecType->getTextureAccessorIdx(*compStr) != -1);
  }
    
  if (*compStr) { 
    // We didn't get to the end of the string. This means the component names
    // didn't come from the same set *or* we encountered an illegal name.
    Diag(OpLoc, diag::err_ocuvector_component_name_illegal, 
         std::string(compStr,compStr+1), SourceRange(CompLoc));
    return QualType();
  }
  // Each component accessor can't exceed the vector type.
  compStr = CompName.getName();
  while (*compStr) {
    if (vecType->isAccessorWithinNumElements(*compStr))
      compStr++;
    else
      break;
  }
  if (*compStr) { 
    // We didn't get to the end of the string. This means a component accessor
    // exceeds the number of elements in the vector.
    Diag(OpLoc, diag::err_ocuvector_component_exceeds_length, 
                baseType.getAsString(), SourceRange(CompLoc));
    return QualType();
  }
  // The component accessor looks fine - now we need to compute the actual type.
  // The vector type is implied by the component accessor. For example, 
  // vec4.b is a float, vec4.xy is a vec2, vec4.rgb is a vec3, etc.
  unsigned CompSize = strlen(CompName.getName());
  if (CompSize == 1)
    return vecType->getElementType();
    
  QualType VT = Context.getOCUVectorType(vecType->getElementType(), CompSize);
  // Now look up the TypeDefDecl from the vector type. Without this, 
  // diagostics look bad. We want OCU vector types to appear built-in.
  for (unsigned i = 0, e = OCUVectorDecls.size(); i != e; ++i) {
    if (OCUVectorDecls[i]->getUnderlyingType() == VT)
      return Context.getTypedefType(OCUVectorDecls[i]);
  }
  return VT; // should never get here (a typedef type should always be found).
}

Action::ExprResult Sema::
ParseMemberReferenceExpr(ExprTy *Base, SourceLocation OpLoc,
                         tok::TokenKind OpKind, SourceLocation MemberLoc,
                         IdentifierInfo &Member) {
  Expr *BaseExpr = static_cast<Expr *>(Base);
  assert(BaseExpr && "no record expression");
  
  QualType BaseType = BaseExpr->getType();
  assert(!BaseType.isNull() && "no type for member expression");
  
  if (OpKind == tok::arrow) {
    if (const PointerType *PT = BaseType->getAsPointerType())
      BaseType = PT->getPointeeType();
    else
      return Diag(OpLoc, diag::err_typecheck_member_reference_arrow,
                  SourceRange(MemberLoc));
  }
  // The base type is either a record or an OCUVectorType.
  if (const RecordType *RTy = BaseType->getAsRecordType()) {
    RecordDecl *RDecl = RTy->getDecl();
    if (RTy->isIncompleteType())
      return Diag(OpLoc, diag::err_typecheck_incomplete_tag, RDecl->getName(),
                  BaseExpr->getSourceRange());
    // The record definition is complete, now make sure the member is valid.
    FieldDecl *MemberDecl = RDecl->getMember(&Member);
    if (!MemberDecl)
      return Diag(OpLoc, diag::err_typecheck_no_member, Member.getName(),
                  SourceRange(MemberLoc));
    return new MemberExpr(BaseExpr, OpKind==tok::arrow, MemberDecl, MemberLoc);
  } else if (BaseType->isOCUVectorType() && OpKind == tok::period) {
    // Component access limited to variables (reject vec4.rg.g).
    if (!isa<DeclRefExpr>(BaseExpr)) 
      return Diag(OpLoc, diag::err_ocuvector_component_access, 
                  SourceRange(MemberLoc));
    QualType ret = CheckOCUVectorComponent(BaseType, OpLoc, Member, MemberLoc);
    if (ret.isNull())
      return true;
    return new OCUVectorElementExpr(ret, BaseExpr, Member, MemberLoc);
  } else
    return Diag(OpLoc, diag::err_typecheck_member_reference_structUnion,
                SourceRange(MemberLoc));
}

/// ParseCallExpr - Handle a call to Fn with the specified array of arguments.
/// This provides the location of the left/right parens and a list of comma
/// locations.
Action::ExprResult Sema::
ParseCallExpr(ExprTy *fn, SourceLocation LParenLoc,
              ExprTy **args, unsigned NumArgsInCall,
              SourceLocation *CommaLocs, SourceLocation RParenLoc) {
  Expr *Fn = static_cast<Expr *>(fn);
  Expr **Args = reinterpret_cast<Expr**>(args);
  assert(Fn && "no function call expression");
  
  UsualUnaryConversions(Fn);
  QualType funcType = Fn->getType();

  // C99 6.5.2.2p1 - "The expression that denotes the called function shall have
  // type pointer to function".
  const PointerType *PT = funcType->getAsPointerType();
  if (PT == 0)
    return Diag(Fn->getLocStart(), diag::err_typecheck_call_not_function,
                SourceRange(Fn->getLocStart(), RParenLoc));
  
  const FunctionType *funcT = PT->getPointeeType()->getAsFunctionType();
  if (funcT == 0)
    return Diag(Fn->getLocStart(), diag::err_typecheck_call_not_function,
                SourceRange(Fn->getLocStart(), RParenLoc));
    
  // If a prototype isn't declared, the parser implicitly defines a func decl
  QualType resultType = funcT->getResultType();
    
  if (const FunctionTypeProto *proto = dyn_cast<FunctionTypeProto>(funcT)) {
    // C99 6.5.2.2p7 - the arguments are implicitly converted, as if by 
    // assignment, to the types of the corresponding parameter, ...
    
    unsigned NumArgsInProto = proto->getNumArgs();
    unsigned NumArgsToCheck = NumArgsInCall;
    
    if (NumArgsInCall < NumArgsInProto)
      Diag(RParenLoc, diag::err_typecheck_call_too_few_args,
           Fn->getSourceRange());
    else if (NumArgsInCall > NumArgsInProto) {
      if (!proto->isVariadic()) {
        Diag(Args[NumArgsInProto]->getLocStart(), 
             diag::err_typecheck_call_too_many_args, Fn->getSourceRange(),
             SourceRange(Args[NumArgsInProto]->getLocStart(),
                         Args[NumArgsInCall-1]->getLocEnd()));
      }
      NumArgsToCheck = NumArgsInProto;
    }
    // Continue to check argument types (even if we have too few/many args).
    for (unsigned i = 0; i < NumArgsToCheck; i++) {
      Expr *argExpr = Args[i];
      assert(argExpr && "ParseCallExpr(): missing argument expression");
      
      QualType lhsType = proto->getArgType(i);
      QualType rhsType = argExpr->getType();

      // If necessary, apply function/array conversion. C99 6.7.5.3p[7,8]. 
      if (const ArrayType *ary = lhsType->getAsArrayType())
        lhsType = Context.getPointerType(ary->getElementType());
      else if (lhsType->isFunctionType())
        lhsType = Context.getPointerType(lhsType);

      AssignmentCheckResult result = CheckSingleAssignmentConstraints(lhsType,
                                                                      argExpr);
      if (Args[i] != argExpr) // The expression was converted.
        Args[i] = argExpr; // Make sure we store the converted expression.
      SourceLocation l = argExpr->getLocStart();

      // decode the result (notice that AST's are still created for extensions).
      switch (result) {
      case Compatible:
        break;
      case PointerFromInt:
        // check for null pointer constant (C99 6.3.2.3p3)
        if (!argExpr->isNullPointerConstant(Context)) {
          Diag(l, diag::ext_typecheck_passing_pointer_int, 
               lhsType.getAsString(), rhsType.getAsString(),
               Fn->getSourceRange(), argExpr->getSourceRange());
        }
        break;
      case IntFromPointer:
        Diag(l, diag::ext_typecheck_passing_pointer_int, 
             lhsType.getAsString(), rhsType.getAsString(),
             Fn->getSourceRange(), argExpr->getSourceRange());
        break;
      case IncompatiblePointer:
        Diag(l, diag::ext_typecheck_passing_incompatible_pointer, 
             rhsType.getAsString(), lhsType.getAsString(),
             Fn->getSourceRange(), argExpr->getSourceRange());
        break;
      case CompatiblePointerDiscardsQualifiers:
        Diag(l, diag::ext_typecheck_passing_discards_qualifiers,
             rhsType.getAsString(), lhsType.getAsString(),
             Fn->getSourceRange(), argExpr->getSourceRange());
        break;
      case Incompatible:
        return Diag(l, diag::err_typecheck_passing_incompatible,
                 rhsType.getAsString(), lhsType.getAsString(),
                 Fn->getSourceRange(), argExpr->getSourceRange());
      }
    }
    // Even if the types checked, bail if we had the wrong number of arguments.
    if (NumArgsInCall != NumArgsInProto && !proto->isVariadic())
      return true;
  }
  
  // Do special checking on direct calls to functions.
  if (ImplicitCastExpr *IcExpr = dyn_cast<ImplicitCastExpr>(Fn))
    if (DeclRefExpr *DRExpr = dyn_cast<DeclRefExpr>(IcExpr->getSubExpr()))
      if (FunctionDecl *FDecl = dyn_cast<FunctionDecl>(DRExpr->getDecl()))
        if (CheckFunctionCall(Fn, LParenLoc, RParenLoc, FDecl, Args, NumArgsInCall))
          return true;

  return new CallExpr(Fn, Args, NumArgsInCall, resultType, RParenLoc);
}

Action::ExprResult Sema::
ParseCompoundLiteral(SourceLocation LParenLoc, TypeTy *Ty,
                     SourceLocation RParenLoc, ExprTy *InitExpr) {
  assert((Ty != 0) && "ParseCompoundLiteral(): missing type");
  QualType literalType = QualType::getFromOpaquePtr(Ty);
  // FIXME: put back this assert when initializers are worked out.
  //assert((InitExpr != 0) && "ParseCompoundLiteral(): missing expression");
  Expr *literalExpr = static_cast<Expr*>(InitExpr);
  
  // FIXME: add semantic analysis (C99 6.5.2.5).
  return new CompoundLiteralExpr(literalType, literalExpr);
}

Action::ExprResult Sema::
ParseInitList(SourceLocation LParenLoc, ExprTy **InitList, unsigned NumInit,
              SourceLocation RParenLoc) {
  // FIXME: add semantic analysis (C99 6.7.8). This involves 
  // knowledge of the object being intialized. As a result, the code for
  // doing the semantic analysis will likely be located elsewhere (i.e. in 
  // consumers of InitListExpr (e.g. ParseDeclarator, ParseCompoundLiteral).
  return false; // FIXME instantiate an InitListExpr.
}

Action::ExprResult Sema::
ParseCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
              SourceLocation RParenLoc, ExprTy *Op) {
  assert((Ty != 0) && (Op != 0) && "ParseCastExpr(): missing type or expr");

  Expr *castExpr = static_cast<Expr*>(Op);
  QualType castType = QualType::getFromOpaquePtr(Ty);

  // C99 6.5.4p2: the cast type needs to be void or scalar and the expression
  // type needs to be scalar.
  if (!castType->isScalarType() && !castType->isVoidType()) { 
    return Diag(LParenLoc, diag::err_typecheck_cond_expect_scalar, 
                castType.getAsString(), SourceRange(LParenLoc, RParenLoc));
  }
  if (!castExpr->getType()->isScalarType()) {
    return Diag(castExpr->getLocStart(), 
                diag::err_typecheck_expect_scalar_operand, 
                castExpr->getType().getAsString(), castExpr->getSourceRange());
  }
  return new CastExpr(castType, castExpr, LParenLoc);
}

inline QualType Sema::CheckConditionalOperands( // C99 6.5.15
  Expr *&cond, Expr *&lex, Expr *&rex, SourceLocation questionLoc) {
  UsualUnaryConversions(cond);
  UsualUnaryConversions(lex);
  UsualUnaryConversions(rex);
  QualType condT = cond->getType();
  QualType lexT = lex->getType();
  QualType rexT = rex->getType();

  // first, check the condition.
  if (!condT->isScalarType()) { // C99 6.5.15p2
    Diag(cond->getLocStart(), diag::err_typecheck_cond_expect_scalar, 
         condT.getAsString());
    return QualType();
  }
  // now check the two expressions.
  if (lexT->isArithmeticType() && rexT->isArithmeticType()) { // C99 6.5.15p3,5
    UsualArithmeticConversions(lex, rex);
    return lex->getType();
  }
  if (const RecordType *LHSRT = lexT->getAsRecordType()) {    // C99 6.5.15p3
    if (const RecordType *RHSRT = rexT->getAsRecordType()) {
      
      if (LHSRT->getDecl()->getIdentifier() ==RHSRT->getDecl()->getIdentifier()) 
        return lexT;
      
      Diag(questionLoc, diag::err_typecheck_cond_incompatible_operands,
           lexT.getAsString(), rexT.getAsString(),
           lex->getSourceRange(), rex->getSourceRange());
      return QualType();
    }
  }
  // C99 6.5.15p3
  if (lexT->isPointerType() && rex->isNullPointerConstant(Context))
    return lexT;
  if (rexT->isPointerType() && lex->isNullPointerConstant(Context))
    return rexT;
    
  if (const PointerType *LHSPT = lexT->getAsPointerType()) { // C99 6.5.15p3,6
    if (const PointerType *RHSPT = rexT->getAsPointerType()) {
      // get the "pointed to" types
      QualType lhptee = LHSPT->getPointeeType();
      QualType rhptee = RHSPT->getPointeeType();

      // ignore qualifiers on void (C99 6.5.15p3, clause 6)
      if (lhptee->isVoidType() &&
          (rhptee->isObjectType() || rhptee->isIncompleteType()))
        return lexT;
      if (rhptee->isVoidType() &&
          (lhptee->isObjectType() || lhptee->isIncompleteType()))
        return rexT;

      if (!Type::typesAreCompatible(lhptee.getUnqualifiedType(), 
                                    rhptee.getUnqualifiedType())) {
        Diag(questionLoc, diag::ext_typecheck_cond_incompatible_pointers,
             lexT.getAsString(), rexT.getAsString(),
             lex->getSourceRange(), rex->getSourceRange());
        return lexT; // FIXME: this is an _ext - is this return o.k?
      }
      // The pointer types are compatible.
      // C99 6.5.15p6: If both operands are pointers to compatible types *or* to 
      // differently qualified versions of compatible types, the result type is a 
      // pointer to an appropriately qualified version of the *composite* type.
      return lexT; // FIXME: Need to return the composite type.
    }
  }
  
  if (lexT->isVoidType() && rexT->isVoidType()) // C99 6.5.15p3
    return lexT;
    
  Diag(questionLoc, diag::err_typecheck_cond_incompatible_operands,
       lexT.getAsString(), rexT.getAsString(),
       lex->getSourceRange(), rex->getSourceRange());
  return QualType();
}

/// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
/// in the case of a the GNU conditional expr extension.
Action::ExprResult Sema::ParseConditionalOp(SourceLocation QuestionLoc, 
                                            SourceLocation ColonLoc,
                                            ExprTy *Cond, ExprTy *LHS,
                                            ExprTy *RHS) {
  Expr *CondExpr = (Expr *) Cond;
  Expr *LHSExpr = (Expr *) LHS, *RHSExpr = (Expr *) RHS;
  QualType result = CheckConditionalOperands(CondExpr, LHSExpr, 
                                             RHSExpr, QuestionLoc);
  if (result.isNull())
    return true;
  return new ConditionalOperator(CondExpr, LHSExpr, RHSExpr, result);
}

// promoteExprToType - a helper function to ensure we create exactly one 
// ImplicitCastExpr. As a convenience (to the caller), we return the type.
static void promoteExprToType(Expr *&expr, QualType type) {
  if (ImplicitCastExpr *impCast = dyn_cast<ImplicitCastExpr>(expr))
    impCast->setType(type);
  else 
    expr = new ImplicitCastExpr(type, expr);
  return;
}

/// DefaultFunctionArrayConversion (C99 6.3.2.1p3, C99 6.3.2.1p4).
void Sema::DefaultFunctionArrayConversion(Expr *&e) {
  QualType t = e->getType();
  assert(!t.isNull() && "DefaultFunctionArrayConversion - missing type");

  if (const ReferenceType *ref = t->getAsReferenceType()) {
    promoteExprToType(e, ref->getReferenceeType()); // C++ [expr]
    t = e->getType();
  }
  if (t->isFunctionType())
    promoteExprToType(e, Context.getPointerType(t));
  else if (const ArrayType *ary = t->getAsArrayType())
    promoteExprToType(e, Context.getPointerType(ary->getElementType()));
}

/// UsualUnaryConversion - Performs various conversions that are common to most
/// operators (C99 6.3). The conversions of array and function types are 
/// sometimes surpressed. For example, the array->pointer conversion doesn't
/// apply if the array is an argument to the sizeof or address (&) operators.
/// In these instances, this routine should *not* be called.
void Sema::UsualUnaryConversions(Expr *&expr) {
  QualType t = expr->getType();
  assert(!t.isNull() && "UsualUnaryConversions - missing type");
  
  if (const ReferenceType *ref = t->getAsReferenceType()) {
    promoteExprToType(expr, ref->getReferenceeType()); // C++ [expr]
    t = expr->getType();
  }
  if (t->isPromotableIntegerType()) // C99 6.3.1.1p2
    promoteExprToType(expr, Context.IntTy);
  else
    DefaultFunctionArrayConversion(expr);
}

/// UsualArithmeticConversions - Performs various conversions that are common to 
/// binary operators (C99 6.3.1.8). If both operands aren't arithmetic, this
/// routine returns the first non-arithmetic type found. The client is 
/// responsible for emitting appropriate error diagnostics.
QualType Sema::UsualArithmeticConversions(Expr *&lhsExpr, Expr *&rhsExpr,
                                          bool isCompAssign) {
  if (!isCompAssign) {
    UsualUnaryConversions(lhsExpr);
    UsualUnaryConversions(rhsExpr);
  }
  QualType lhs = lhsExpr->getType();
  QualType rhs = rhsExpr->getType();
  
  // If both types are identical, no conversion is needed.
  if (lhs == rhs) 
    return lhs;
  
  // If either side is a non-arithmetic type (e.g. a pointer), we are done.
  // The caller can deal with this (e.g. pointer + int).
  if (!lhs->isArithmeticType() || !rhs->isArithmeticType())
    return lhs;
    
  // At this point, we have two different arithmetic types. 
  
  // Handle complex types first (C99 6.3.1.8p1).
  if (lhs->isComplexType() || rhs->isComplexType()) {
    // if we have an integer operand, the result is the complex type.
    if (rhs->isIntegerType()) { // convert the rhs to the lhs complex type.
      if (!isCompAssign) promoteExprToType(rhsExpr, lhs);
      return lhs;
    }
    if (lhs->isIntegerType()) { // convert the lhs to the rhs complex type.
      if (!isCompAssign) promoteExprToType(lhsExpr, rhs);
      return rhs;
    }
    // This handles complex/complex, complex/float, or float/complex.
    // When both operands are complex, the shorter operand is converted to the 
    // type of the longer, and that is the type of the result. This corresponds 
    // to what is done when combining two real floating-point operands. 
    // The fun begins when size promotion occur across type domains. 
    // From H&S 6.3.4: When one operand is complex and the other is a real
    // floating-point type, the less precise type is converted, within it's 
    // real or complex domain, to the precision of the other type. For example,
    // when combining a "long double" with a "double _Complex", the 
    // "double _Complex" is promoted to "long double _Complex".
    int result = Context.compareFloatingType(lhs, rhs);
    
    if (result > 0) { // The left side is bigger, convert rhs. 
      rhs = Context.getFloatingTypeOfSizeWithinDomain(lhs, rhs);
      if (!isCompAssign)
        promoteExprToType(rhsExpr, rhs);
    } else if (result < 0) { // The right side is bigger, convert lhs. 
      lhs = Context.getFloatingTypeOfSizeWithinDomain(rhs, lhs);
      if (!isCompAssign)
        promoteExprToType(lhsExpr, lhs);
    } 
    // At this point, lhs and rhs have the same rank/size. Now, make sure the
    // domains match. This is a requirement for our implementation, C99
    // does not require this promotion.
    if (lhs != rhs) { // Domains don't match, we have complex/float mix.
      if (lhs->isRealFloatingType()) { // handle "double, _Complex double".
        if (!isCompAssign)
          promoteExprToType(lhsExpr, rhs);
        return rhs;
      } else { // handle "_Complex double, double".
        if (!isCompAssign)
          promoteExprToType(rhsExpr, lhs);
        return lhs;
      }
    }
    return lhs; // The domain/size match exactly.
  }
  // Now handle "real" floating types (i.e. float, double, long double).
  if (lhs->isRealFloatingType() || rhs->isRealFloatingType()) {
    // if we have an integer operand, the result is the real floating type.
    if (rhs->isIntegerType()) { // convert rhs to the lhs floating point type.
      if (!isCompAssign) promoteExprToType(rhsExpr, lhs);
      return lhs;
    }
    if (lhs->isIntegerType()) { // convert lhs to the rhs floating point type.
      if (!isCompAssign) promoteExprToType(lhsExpr, rhs);
      return rhs;
    }
    // We have two real floating types, float/complex combos were handled above.
    // Convert the smaller operand to the bigger result.
    int result = Context.compareFloatingType(lhs, rhs);
    
    if (result > 0) { // convert the rhs
      if (!isCompAssign) promoteExprToType(rhsExpr, lhs);
      return lhs;
    }
    if (result < 0) { // convert the lhs
      if (!isCompAssign) promoteExprToType(lhsExpr, rhs); // convert the lhs
      return rhs;
    }
    assert(0 && "Sema::UsualArithmeticConversions(): illegal float comparison");
  }
  // Finally, we have two differing integer types.
  if (Context.maxIntegerType(lhs, rhs) == lhs) { // convert the rhs
    if (!isCompAssign) promoteExprToType(rhsExpr, lhs);
    return lhs;
  }
  if (!isCompAssign) promoteExprToType(lhsExpr, rhs); // convert the lhs
  return rhs;
}

// CheckPointerTypesForAssignment - This is a very tricky routine (despite
// being closely modeled after the C99 spec:-). The odd characteristic of this 
// routine is it effectively iqnores the qualifiers on the top level pointee.
// This circumvents the usual type rules specified in 6.2.7p1 & 6.7.5.[1-3].
// FIXME: add a couple examples in this comment.
Sema::AssignmentCheckResult 
Sema::CheckPointerTypesForAssignment(QualType lhsType, QualType rhsType) {
  QualType lhptee, rhptee;
  
  // get the "pointed to" type (ignoring qualifiers at the top level)
  lhptee = lhsType->getAsPointerType()->getPointeeType();
  rhptee = rhsType->getAsPointerType()->getPointeeType();
  
  // make sure we operate on the canonical type
  lhptee = lhptee.getCanonicalType();
  rhptee = rhptee.getCanonicalType();

  AssignmentCheckResult r = Compatible;
  
  // C99 6.5.16.1p1: This following citation is common to constraints 
  // 3 & 4 (below). ...and the type *pointed to* by the left has all the 
  // qualifiers of the type *pointed to* by the right; 
  if ((lhptee.getQualifiers() & rhptee.getQualifiers()) != 
       rhptee.getQualifiers())
    r = CompatiblePointerDiscardsQualifiers;

  // C99 6.5.16.1p1 (constraint 4): If one operand is a pointer to an object or 
  // incomplete type and the other is a pointer to a qualified or unqualified 
  // version of void...
  if (lhptee.getUnqualifiedType()->isVoidType() &&
      (rhptee->isObjectType() || rhptee->isIncompleteType()))
    ;
  else if (rhptee.getUnqualifiedType()->isVoidType() &&
      (lhptee->isObjectType() || lhptee->isIncompleteType()))
    ;
  // C99 6.5.16.1p1 (constraint 3): both operands are pointers to qualified or 
  // unqualified versions of compatible types, ...
  else if (!Type::typesAreCompatible(lhptee.getUnqualifiedType(), 
                                     rhptee.getUnqualifiedType()))
    r = IncompatiblePointer; // this "trumps" PointerAssignDiscardsQualifiers
  return r;
}

/// CheckAssignmentConstraints (C99 6.5.16) - This routine currently 
/// has code to accommodate several GCC extensions when type checking 
/// pointers. Here are some objectionable examples that GCC considers warnings:
///
///  int a, *pint;
///  short *pshort;
///  struct foo *pfoo;
///
///  pint = pshort; // warning: assignment from incompatible pointer type
///  a = pint; // warning: assignment makes integer from pointer without a cast
///  pint = a; // warning: assignment makes pointer from integer without a cast
///  pint = pfoo; // warning: assignment from incompatible pointer type
///
/// As a result, the code for dealing with pointers is more complex than the
/// C99 spec dictates. 
/// Note: the warning above turn into errors when -pedantic-errors is enabled. 
///
Sema::AssignmentCheckResult
Sema::CheckAssignmentConstraints(QualType lhsType, QualType rhsType) {
  if (lhsType == rhsType) // common case, fast path...
    return Compatible;

  if (lhsType->isArithmeticType() && rhsType->isArithmeticType()) {
    if (lhsType->isVectorType() || rhsType->isVectorType()) {
      if (lhsType.getCanonicalType() != rhsType.getCanonicalType())
        return Incompatible;
    }
    return Compatible;
  } else if (lhsType->isPointerType()) {
    if (rhsType->isIntegerType())
      return PointerFromInt;
      
    if (rhsType->isPointerType())
      return CheckPointerTypesForAssignment(lhsType, rhsType);
  } else if (rhsType->isPointerType()) {
    // C99 6.5.16.1p1: the left operand is _Bool and the right is a pointer.
    if ((lhsType->isIntegerType()) && (lhsType != Context.BoolTy))
      return IntFromPointer;

    if (lhsType->isPointerType()) 
      return CheckPointerTypesForAssignment(lhsType, rhsType);
  } else if (isa<TagType>(lhsType) && isa<TagType>(rhsType)) {
    if (Type::tagTypesAreCompatible(lhsType, rhsType))
      return Compatible;
  } else if (lhsType->isReferenceType() || rhsType->isReferenceType()) {
    if (Type::referenceTypesAreCompatible(lhsType, rhsType))
      return Compatible;
  }
  return Incompatible;
}

Sema::AssignmentCheckResult
Sema::CheckSingleAssignmentConstraints(QualType lhsType, Expr *&rExpr) {
  // This check seems unnatural, however it is necessary to insure the proper
  // conversion of functions/arrays. If the conversion were done for all
  // DeclExpr's (created by ParseIdentifierExpr), it would mess up the unary
  // expressions that surpress this implicit conversion (&, sizeof).
  DefaultFunctionArrayConversion(rExpr);

  Sema::AssignmentCheckResult result;
  
  result = CheckAssignmentConstraints(lhsType, rExpr->getType());
  
  // C99 6.5.16.1p2: The value of the right operand is converted to the
  // type of the assignment expression.
  if (rExpr->getType() != lhsType)
    promoteExprToType(rExpr, lhsType);
  return result;
}

Sema::AssignmentCheckResult
Sema::CheckCompoundAssignmentConstraints(QualType lhsType, QualType rhsType) {
  return CheckAssignmentConstraints(lhsType, rhsType);
}

inline void Sema::InvalidOperands(SourceLocation loc, Expr *&lex, Expr *&rex) {
  Diag(loc, diag::err_typecheck_invalid_operands, 
       lex->getType().getAsString(), rex->getType().getAsString(),
       lex->getSourceRange(), rex->getSourceRange());
}

inline QualType Sema::CheckVectorOperands(SourceLocation loc, Expr *&lex, 
                                                              Expr *&rex) {
  QualType lhsType = lex->getType(), rhsType = rex->getType();
  
  // make sure the vector types are identical. 
  if (lhsType == rhsType)
    return lhsType;
  // You cannot convert between vector values of different size.
  Diag(loc, diag::err_typecheck_vector_not_convertable, 
       lex->getType().getAsString(), rex->getType().getAsString(),
       lex->getSourceRange(), rex->getSourceRange());
  return QualType();
}    

inline QualType Sema::CheckMultiplyDivideOperands(
  Expr *&lex, Expr *&rex, SourceLocation loc, bool isCompAssign) 
{
  QualType lhsType = lex->getType(), rhsType = rex->getType();

  if (lhsType->isVectorType() || rhsType->isVectorType())
    return CheckVectorOperands(loc, lex, rex);
    
  QualType compType = UsualArithmeticConversions(lex, rex, isCompAssign);
  
  if (lex->getType()->isArithmeticType() && rex->getType()->isArithmeticType())
    return compType;
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckRemainderOperands(
  Expr *&lex, Expr *&rex, SourceLocation loc, bool isCompAssign) 
{
  QualType lhsType = lex->getType(), rhsType = rex->getType();

  QualType compType = UsualArithmeticConversions(lex, rex, isCompAssign);
  
  if (lex->getType()->isIntegerType() && rex->getType()->isIntegerType())
    return compType;
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckAdditionOperands( // C99 6.5.6
  Expr *&lex, Expr *&rex, SourceLocation loc, bool isCompAssign) 
{
  if (lex->getType()->isVectorType() || rex->getType()->isVectorType())
    return CheckVectorOperands(loc, lex, rex);

  QualType compType = UsualArithmeticConversions(lex, rex, isCompAssign);
  
  // handle the common case first (both operands are arithmetic).
  if (lex->getType()->isArithmeticType() && rex->getType()->isArithmeticType())
    return compType;

  if (lex->getType()->isPointerType() && rex->getType()->isIntegerType())
    return lex->getType();
  if (lex->getType()->isIntegerType() && rex->getType()->isPointerType())
    return rex->getType();
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckSubtractionOperands( // C99 6.5.6
  Expr *&lex, Expr *&rex, SourceLocation loc, bool isCompAssign) 
{
  if (lex->getType()->isVectorType() || rex->getType()->isVectorType())
    return CheckVectorOperands(loc, lex, rex);
    
  QualType compType = UsualArithmeticConversions(lex, rex, isCompAssign);
  
  // handle the common case first (both operands are arithmetic).
  if (lex->getType()->isArithmeticType() && rex->getType()->isArithmeticType())
    return compType;
    
  if (lex->getType()->isPointerType() && rex->getType()->isIntegerType())
    return compType;
  if (lex->getType()->isPointerType() && rex->getType()->isPointerType())
    return Context.getPointerDiffType();
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckShiftOperands( // C99 6.5.7
  Expr *&lex, Expr *&rex, SourceLocation loc, bool isCompAssign)
{
  // FIXME: Shifts don't perform usual arithmetic conversions.  This is wrong
  // for int << longlong -> the result type should be int, not long long.
  QualType compType = UsualArithmeticConversions(lex, rex, isCompAssign);
  
  // handle the common case first (both operands are arithmetic).
  if (lex->getType()->isIntegerType() && rex->getType()->isIntegerType())
    return compType;
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckCompareOperands( // C99 6.5.8
  Expr *&lex, Expr *&rex, SourceLocation loc, bool isRelational)
{
  // C99 6.5.8p3 / C99 6.5.9p4
  if (lex->getType()->isArithmeticType() && rex->getType()->isArithmeticType())
    UsualArithmeticConversions(lex, rex);
  else {
    UsualUnaryConversions(lex);
    UsualUnaryConversions(rex);
  }
  QualType lType = lex->getType();
  QualType rType = rex->getType();
  
  if (isRelational) {
    if (lType->isRealType() && rType->isRealType())
      return Context.IntTy;
  } else {
    if (lType->isArithmeticType() && rType->isArithmeticType())
      return Context.IntTy;
  }
  
  bool LHSIsNull = lex->isNullPointerConstant(Context);
  bool RHSIsNull = rex->isNullPointerConstant(Context);
  
  // All of the following pointer related warnings are GCC extensions, except
  // when handling null pointer constants. One day, we can consider making them
  // errors (when -pedantic-errors is enabled).
  if (lType->isPointerType() && rType->isPointerType()) { // C99 6.5.8p2
    if (!LHSIsNull && !RHSIsNull &&
        !Type::pointerTypesAreCompatible(lType.getUnqualifiedType(),
                                         rType.getUnqualifiedType())) {
      Diag(loc, diag::ext_typecheck_comparison_of_distinct_pointers,
           lType.getAsString(), rType.getAsString(),
           lex->getSourceRange(), rex->getSourceRange());
    }
    promoteExprToType(rex, lType); // promote the pointer to pointer
    return Context.IntTy;
  }
  if (lType->isPointerType() && rType->isIntegerType()) {
    if (!RHSIsNull)
      Diag(loc, diag::ext_typecheck_comparison_of_pointer_integer,
           lType.getAsString(), rType.getAsString(),
           lex->getSourceRange(), rex->getSourceRange());
    promoteExprToType(rex, lType); // promote the integer to pointer
    return Context.IntTy;
  }
  if (lType->isIntegerType() && rType->isPointerType()) {
    if (!LHSIsNull)
      Diag(loc, diag::ext_typecheck_comparison_of_pointer_integer,
           lType.getAsString(), rType.getAsString(),
           lex->getSourceRange(), rex->getSourceRange());
    promoteExprToType(lex, rType); // promote the integer to pointer
    return Context.IntTy;
  }
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckBitwiseOperands(
  Expr *&lex, Expr *&rex, SourceLocation loc, bool isCompAssign) 
{
  if (lex->getType()->isVectorType() || rex->getType()->isVectorType())
    return CheckVectorOperands(loc, lex, rex);

  QualType compType = UsualArithmeticConversions(lex, rex, isCompAssign);
  
  if (lex->getType()->isIntegerType() && rex->getType()->isIntegerType())
    return compType;
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckLogicalOperands( // C99 6.5.[13,14]
  Expr *&lex, Expr *&rex, SourceLocation loc) 
{
  UsualUnaryConversions(lex);
  UsualUnaryConversions(rex);
  
  if (lex->getType()->isScalarType() || rex->getType()->isScalarType())
    return Context.IntTy;
  InvalidOperands(loc, lex, rex);
  return QualType();
}

inline QualType Sema::CheckAssignmentOperands( // C99 6.5.16.1
  Expr *lex, Expr *&rex, SourceLocation loc, QualType compoundType) 
{
  QualType lhsType = lex->getType();
  QualType rhsType = compoundType.isNull() ? rex->getType() : compoundType;
  bool hadError = false;
  Expr::isModifiableLvalueResult mlval = lex->isModifiableLvalue(); 

  switch (mlval) { // C99 6.5.16p2
    case Expr::MLV_Valid: 
      break;
    case Expr::MLV_ConstQualified:
      Diag(loc, diag::err_typecheck_assign_const, lex->getSourceRange());
      hadError = true;
      break;
    case Expr::MLV_ArrayType: 
      Diag(loc, diag::err_typecheck_array_not_modifiable_lvalue,
           lhsType.getAsString(), lex->getSourceRange());
      return QualType(); 
    case Expr::MLV_NotObjectType: 
      Diag(loc, diag::err_typecheck_non_object_not_modifiable_lvalue,
           lhsType.getAsString(), lex->getSourceRange());
      return QualType();
    case Expr::MLV_InvalidExpression:
      Diag(loc, diag::err_typecheck_expression_not_modifiable_lvalue,
           lex->getSourceRange());
      return QualType();
    case Expr::MLV_IncompleteType:
    case Expr::MLV_IncompleteVoidType:
      Diag(loc, diag::err_typecheck_incomplete_type_not_modifiable_lvalue,
           lhsType.getAsString(), lex->getSourceRange());
      return QualType();
    case Expr::MLV_DuplicateVectorComponents:
      Diag(loc, diag::err_typecheck_duplicate_vector_components_not_mlvalue,
           lex->getSourceRange());
      return QualType();
  }
  AssignmentCheckResult result;
  
  if (compoundType.isNull())
    result = CheckSingleAssignmentConstraints(lhsType, rex);
  else
    result = CheckCompoundAssignmentConstraints(lhsType, rhsType);

  // decode the result (notice that extensions still return a type).
  switch (result) {
  case Compatible:
    break;
  case Incompatible:
    Diag(loc, diag::err_typecheck_assign_incompatible, 
         lhsType.getAsString(), rhsType.getAsString(),
         lex->getSourceRange(), rex->getSourceRange());
    hadError = true;
    break;
  case PointerFromInt:
    // check for null pointer constant (C99 6.3.2.3p3)
    if (compoundType.isNull() && !rex->isNullPointerConstant(Context)) {
      Diag(loc, diag::ext_typecheck_assign_pointer_int,
           lhsType.getAsString(), rhsType.getAsString(),
           lex->getSourceRange(), rex->getSourceRange());
    }
    break;
  case IntFromPointer: 
    Diag(loc, diag::ext_typecheck_assign_pointer_int, 
         lhsType.getAsString(), rhsType.getAsString(),
         lex->getSourceRange(), rex->getSourceRange());
    break;
  case IncompatiblePointer:
    Diag(loc, diag::ext_typecheck_assign_incompatible_pointer,
         lhsType.getAsString(), rhsType.getAsString(),
         lex->getSourceRange(), rex->getSourceRange());
    break;
  case CompatiblePointerDiscardsQualifiers:
    Diag(loc, diag::ext_typecheck_assign_discards_qualifiers,
         lhsType.getAsString(), rhsType.getAsString(),
         lex->getSourceRange(), rex->getSourceRange());
    break;
  }
  // C99 6.5.16p3: The type of an assignment expression is the type of the
  // left operand unless the left operand has qualified type, in which case
  // it is the unqualified version of the type of the left operand. 
  // C99 6.5.16.1p2: In simple assignment, the value of the right operand
  // is converted to the type of the assignment expression (above).
  // C++ 5.17p1: the type of the assignment expression is that of its left oprdu.
  return hadError ? QualType() : lhsType.getUnqualifiedType();
}

inline QualType Sema::CheckCommaOperands( // C99 6.5.17
  Expr *&lex, Expr *&rex, SourceLocation loc) {
  UsualUnaryConversions(rex);
  return rex->getType();
}

/// CheckIncrementDecrementOperand - unlike most "Check" methods, this routine
/// doesn't need to call UsualUnaryConversions or UsualArithmeticConversions.
QualType Sema::CheckIncrementDecrementOperand(Expr *op, SourceLocation OpLoc) {
  QualType resType = op->getType();
  assert(!resType.isNull() && "no type for increment/decrement expression");

  // C99 6.5.2.4p1: We allow complex as a GCC extension.
  if (const PointerType *pt = dyn_cast<PointerType>(resType)) {
    if (!pt->getPointeeType()->isObjectType()) { // C99 6.5.2.4p2, 6.5.6p2
      Diag(OpLoc, diag::err_typecheck_arithmetic_incomplete_type,
           resType.getAsString(), op->getSourceRange());
      return QualType();
    }
  } else if (!resType->isRealType()) {
    if (resType->isComplexType()) 
      // C99 does not support ++/-- on complex types.
      Diag(OpLoc, diag::ext_integer_increment_complex,
           resType.getAsString(), op->getSourceRange());
    else {
      Diag(OpLoc, diag::err_typecheck_illegal_increment_decrement,
           resType.getAsString(), op->getSourceRange());
      return QualType();
    }
  }
  // At this point, we know we have a real, complex or pointer type. 
  // Now make sure the operand is a modifiable lvalue.
  Expr::isModifiableLvalueResult mlval = op->isModifiableLvalue();
  if (mlval != Expr::MLV_Valid) {
    // FIXME: emit a more precise diagnostic...
    Diag(OpLoc, diag::err_typecheck_invalid_lvalue_incr_decr,
         op->getSourceRange());
    return QualType();
  }
  return resType;
}

/// getPrimaryDeclaration - Helper function for CheckAddressOfOperand().
/// This routine allows us to typecheck complex/recursive expressions
/// where the declaration is needed for type checking. Here are some
/// examples: &s.xx, &s.zz[1].yy, &(1+2), &(XX), &"123"[2].
static Decl *getPrimaryDeclaration(Expr *e) {
  switch (e->getStmtClass()) {
  case Stmt::DeclRefExprClass:
    return cast<DeclRefExpr>(e)->getDecl();
  case Stmt::MemberExprClass:
    return getPrimaryDeclaration(cast<MemberExpr>(e)->getBase());
  case Stmt::ArraySubscriptExprClass:
    return getPrimaryDeclaration(cast<ArraySubscriptExpr>(e)->getBase());
  case Stmt::CallExprClass:
    return getPrimaryDeclaration(cast<CallExpr>(e)->getCallee());
  case Stmt::UnaryOperatorClass:
    return getPrimaryDeclaration(cast<UnaryOperator>(e)->getSubExpr());
  case Stmt::ParenExprClass:
    return getPrimaryDeclaration(cast<ParenExpr>(e)->getSubExpr());
  default:
    return 0;
  }
}

/// CheckAddressOfOperand - The operand of & must be either a function
/// designator or an lvalue designating an object. If it is an lvalue, the 
/// object cannot be declared with storage class register or be a bit field.
/// Note: The usual conversions are *not* applied to the operand of the & 
/// operator (C99 6.3.2.1p[2-4]), and its result is never an lvalue.
QualType Sema::CheckAddressOfOperand(Expr *op, SourceLocation OpLoc) {
  Decl *dcl = getPrimaryDeclaration(op);
  Expr::isLvalueResult lval = op->isLvalue();
  
  if (lval != Expr::LV_Valid) { // C99 6.5.3.2p1
    if (dcl && isa<FunctionDecl>(dcl)) // allow function designators
      ;  
    else { // FIXME: emit more specific diag...
      Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof, 
           op->getSourceRange());
      return QualType();
    }
  } else if (dcl) {
    // We have an lvalue with a decl. Make sure the decl is not declared 
    // with the register storage-class specifier.
    if (const VarDecl *vd = dyn_cast<VarDecl>(dcl)) {
      if (vd->getStorageClass() == VarDecl::Register) {
        Diag(OpLoc, diag::err_typecheck_address_of_register, 
             op->getSourceRange());
        return QualType();
      }
    } else 
      assert(0 && "Unknown/unexpected decl type");
    
    // FIXME: add check for bitfields!
  }
  // If the operand has type "type", the result has type "pointer to type".
  return Context.getPointerType(op->getType());
}

QualType Sema::CheckIndirectionOperand(Expr *op, SourceLocation OpLoc) {
  UsualUnaryConversions(op);
  QualType qType = op->getType();
  
  if (const PointerType *PT = qType->getAsPointerType()) {
    QualType ptype = PT->getPointeeType();
    // C99 6.5.3.2p4. "if it points to an object,...".
    if (ptype->isIncompleteType()) { // An incomplete type is not an object
      // GCC compat: special case 'void *' (treat as warning).
      if (ptype->isVoidType()) {
        Diag(OpLoc, diag::ext_typecheck_deref_ptr_to_void, 
             qType.getAsString(), op->getSourceRange());
      } else {
        Diag(OpLoc, diag::err_typecheck_deref_incomplete_type, 
             ptype.getAsString(), op->getSourceRange());
        return QualType();
      }
    }
    return ptype;
  }
  Diag(OpLoc, diag::err_typecheck_indirection_requires_pointer, 
       qType.getAsString(), op->getSourceRange());
  return QualType();
}

static inline BinaryOperator::Opcode ConvertTokenKindToBinaryOpcode(
  tok::TokenKind Kind) {
  BinaryOperator::Opcode Opc;
  switch (Kind) {
  default: assert(0 && "Unknown binop!");
  case tok::star:                 Opc = BinaryOperator::Mul; break;
  case tok::slash:                Opc = BinaryOperator::Div; break;
  case tok::percent:              Opc = BinaryOperator::Rem; break;
  case tok::plus:                 Opc = BinaryOperator::Add; break;
  case tok::minus:                Opc = BinaryOperator::Sub; break;
  case tok::lessless:             Opc = BinaryOperator::Shl; break;
  case tok::greatergreater:       Opc = BinaryOperator::Shr; break;
  case tok::lessequal:            Opc = BinaryOperator::LE; break;
  case tok::less:                 Opc = BinaryOperator::LT; break;
  case tok::greaterequal:         Opc = BinaryOperator::GE; break;
  case tok::greater:              Opc = BinaryOperator::GT; break;
  case tok::exclaimequal:         Opc = BinaryOperator::NE; break;
  case tok::equalequal:           Opc = BinaryOperator::EQ; break;
  case tok::amp:                  Opc = BinaryOperator::And; break;
  case tok::caret:                Opc = BinaryOperator::Xor; break;
  case tok::pipe:                 Opc = BinaryOperator::Or; break;
  case tok::ampamp:               Opc = BinaryOperator::LAnd; break;
  case tok::pipepipe:             Opc = BinaryOperator::LOr; break;
  case tok::equal:                Opc = BinaryOperator::Assign; break;
  case tok::starequal:            Opc = BinaryOperator::MulAssign; break;
  case tok::slashequal:           Opc = BinaryOperator::DivAssign; break;
  case tok::percentequal:         Opc = BinaryOperator::RemAssign; break;
  case tok::plusequal:            Opc = BinaryOperator::AddAssign; break;
  case tok::minusequal:           Opc = BinaryOperator::SubAssign; break;
  case tok::lesslessequal:        Opc = BinaryOperator::ShlAssign; break;
  case tok::greatergreaterequal:  Opc = BinaryOperator::ShrAssign; break;
  case tok::ampequal:             Opc = BinaryOperator::AndAssign; break;
  case tok::caretequal:           Opc = BinaryOperator::XorAssign; break;
  case tok::pipeequal:            Opc = BinaryOperator::OrAssign; break;
  case tok::comma:                Opc = BinaryOperator::Comma; break;
  }
  return Opc;
}

static inline UnaryOperator::Opcode ConvertTokenKindToUnaryOpcode(
  tok::TokenKind Kind) {
  UnaryOperator::Opcode Opc;
  switch (Kind) {
  default: assert(0 && "Unknown unary op!");
  case tok::plusplus:     Opc = UnaryOperator::PreInc; break;
  case tok::minusminus:   Opc = UnaryOperator::PreDec; break;
  case tok::amp:          Opc = UnaryOperator::AddrOf; break;
  case tok::star:         Opc = UnaryOperator::Deref; break;
  case tok::plus:         Opc = UnaryOperator::Plus; break;
  case tok::minus:        Opc = UnaryOperator::Minus; break;
  case tok::tilde:        Opc = UnaryOperator::Not; break;
  case tok::exclaim:      Opc = UnaryOperator::LNot; break;
  case tok::kw_sizeof:    Opc = UnaryOperator::SizeOf; break;
  case tok::kw___alignof: Opc = UnaryOperator::AlignOf; break;
  case tok::kw___real:    Opc = UnaryOperator::Real; break;
  case tok::kw___imag:    Opc = UnaryOperator::Imag; break;
  case tok::kw___extension__: Opc = UnaryOperator::Extension; break;
  }
  return Opc;
}

// Binary Operators.  'Tok' is the token for the operator.
Action::ExprResult Sema::ParseBinOp(SourceLocation TokLoc, tok::TokenKind Kind,
                                    ExprTy *LHS, ExprTy *RHS) {
  BinaryOperator::Opcode Opc = ConvertTokenKindToBinaryOpcode(Kind);
  Expr *lhs = (Expr *)LHS, *rhs = (Expr*)RHS;

  assert((lhs != 0) && "ParseBinOp(): missing left expression");
  assert((rhs != 0) && "ParseBinOp(): missing right expression");

  QualType ResultTy;  // Result type of the binary operator.
  QualType CompTy;    // Computation type for compound assignments (e.g. '+=')
  
  switch (Opc) {
  default:
    assert(0 && "Unknown binary expr!");
  case BinaryOperator::Assign:
    ResultTy = CheckAssignmentOperands(lhs, rhs, TokLoc, QualType());
    break;
  case BinaryOperator::Mul: 
  case BinaryOperator::Div:
    ResultTy = CheckMultiplyDivideOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Rem:
    ResultTy = CheckRemainderOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Add:
    ResultTy = CheckAdditionOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Sub:
    ResultTy = CheckSubtractionOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Shl: 
  case BinaryOperator::Shr:
    ResultTy = CheckShiftOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::LE:
  case BinaryOperator::LT:
  case BinaryOperator::GE:
  case BinaryOperator::GT:
    ResultTy = CheckCompareOperands(lhs, rhs, TokLoc, true);
    break;
  case BinaryOperator::EQ:
  case BinaryOperator::NE:
    ResultTy = CheckCompareOperands(lhs, rhs, TokLoc, false);
    break;
  case BinaryOperator::And:
  case BinaryOperator::Xor:
  case BinaryOperator::Or:
    ResultTy = CheckBitwiseOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::LAnd:
  case BinaryOperator::LOr:
    ResultTy = CheckLogicalOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::MulAssign:
  case BinaryOperator::DivAssign:
    CompTy = CheckMultiplyDivideOperands(lhs, rhs, TokLoc, true);
    if (!CompTy.isNull())
      ResultTy = CheckAssignmentOperands(lhs, rhs, TokLoc, CompTy);
    break;
  case BinaryOperator::RemAssign:
    CompTy = CheckRemainderOperands(lhs, rhs, TokLoc, true);
    if (!CompTy.isNull())
      ResultTy = CheckAssignmentOperands(lhs, rhs, TokLoc, CompTy);
    break;
  case BinaryOperator::AddAssign:
    CompTy = CheckAdditionOperands(lhs, rhs, TokLoc, true);
    if (!CompTy.isNull())
      ResultTy = CheckAssignmentOperands(lhs, rhs, TokLoc, CompTy);
    break;
  case BinaryOperator::SubAssign:
    CompTy = CheckSubtractionOperands(lhs, rhs, TokLoc, true);
    if (!CompTy.isNull())
      ResultTy = CheckAssignmentOperands(lhs, rhs, TokLoc, CompTy);
    break;
  case BinaryOperator::ShlAssign:
  case BinaryOperator::ShrAssign:
    CompTy = CheckShiftOperands(lhs, rhs, TokLoc, true);
    if (!CompTy.isNull())
      ResultTy = CheckAssignmentOperands(lhs, rhs, TokLoc, CompTy);
    break;
  case BinaryOperator::AndAssign:
  case BinaryOperator::XorAssign:
  case BinaryOperator::OrAssign:
    CompTy = CheckBitwiseOperands(lhs, rhs, TokLoc, true);
    if (!CompTy.isNull())
      ResultTy = CheckAssignmentOperands(lhs, rhs, TokLoc, CompTy);
    break;
  case BinaryOperator::Comma:
    ResultTy = CheckCommaOperands(lhs, rhs, TokLoc);
    break;
  }
  if (ResultTy.isNull())
    return true;
  if (CompTy.isNull())
    return new BinaryOperator(lhs, rhs, Opc, ResultTy);
  else
    return new CompoundAssignOperator(lhs, rhs, Opc, ResultTy, CompTy);
}

// Unary Operators.  'Tok' is the token for the operator.
Action::ExprResult Sema::ParseUnaryOp(SourceLocation OpLoc, tok::TokenKind Op,
                                      ExprTy *input) {
  Expr *Input = (Expr*)input;
  UnaryOperator::Opcode Opc = ConvertTokenKindToUnaryOpcode(Op);
  QualType resultType;
  switch (Opc) {
  default:
    assert(0 && "Unimplemented unary expr!");
  case UnaryOperator::PreInc:
  case UnaryOperator::PreDec:
    resultType = CheckIncrementDecrementOperand(Input, OpLoc);
    break;
  case UnaryOperator::AddrOf: 
    resultType = CheckAddressOfOperand(Input, OpLoc);
    break;
  case UnaryOperator::Deref: 
    resultType = CheckIndirectionOperand(Input, OpLoc);
    break;
  case UnaryOperator::Plus:
  case UnaryOperator::Minus:
    UsualUnaryConversions(Input);
    resultType = Input->getType();
    if (!resultType->isArithmeticType())  // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr, 
                  resultType.getAsString());
    break;
  case UnaryOperator::Not: // bitwise complement
    UsualUnaryConversions(Input);
    resultType = Input->getType();
    // C99 6.5.3.3p1. We allow complex as a GCC extension.
    if (!resultType->isIntegerType()) {
      if (resultType->isComplexType())
        // C99 does not support '~' for complex conjugation.
        Diag(OpLoc, diag::ext_integer_complement_complex,
                    resultType.getAsString());
      else
        return Diag(OpLoc, diag::err_typecheck_unary_expr,
                    resultType.getAsString());
    }
    break;
  case UnaryOperator::LNot: // logical negation
    // Unlike +/-/~, integer promotions aren't done here (C99 6.5.3.3p5).
    DefaultFunctionArrayConversion(Input);
    resultType = Input->getType();
    if (!resultType->isScalarType()) // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr,
                  resultType.getAsString());
    // LNot always has type int. C99 6.5.3.3p5.
    resultType = Context.IntTy;
    break;
  case UnaryOperator::SizeOf:
    resultType = CheckSizeOfAlignOfOperand(Input->getType(), OpLoc, true);
    break;
  case UnaryOperator::AlignOf:
    resultType = CheckSizeOfAlignOfOperand(Input->getType(), OpLoc, false);
    break;
  case UnaryOperator::Real:
  case UnaryOperator::Imag:
    resultType = CheckRealImagOperand(Input, OpLoc);
    break;
  case UnaryOperator::Extension:
    resultType = Input->getType();
    break;
  }
  if (resultType.isNull())
    return true;
  return new UnaryOperator(Input, Opc, resultType, OpLoc);
}

/// ParseAddrLabel - Parse the GNU address of label extension: "&&foo".
Sema::ExprResult Sema::ParseAddrLabel(SourceLocation OpLoc, 
                                      SourceLocation LabLoc,
                                      IdentifierInfo *LabelII) {
  // Look up the record for this label identifier.
  LabelStmt *&LabelDecl = LabelMap[LabelII];
  
  // If we haven't seen this label yet, create a forward reference.
  if (LabelDecl == 0)
    LabelDecl = new LabelStmt(LabLoc, LabelII, 0);
  
  // Create the AST node.  The address of a label always has type 'void*'.
  return new AddrLabelExpr(OpLoc, LabLoc, LabelDecl,
                           Context.getPointerType(Context.VoidTy));
}

Sema::ExprResult Sema::ParseStmtExpr(SourceLocation LPLoc, StmtTy *substmt,
                                     SourceLocation RPLoc) { // "({..})"
  Stmt *SubStmt = static_cast<Stmt*>(substmt);
  assert(SubStmt && isa<CompoundStmt>(SubStmt) && "Invalid action invocation!");
  CompoundStmt *Compound = cast<CompoundStmt>(SubStmt);

  // FIXME: there are a variety of strange constraints to enforce here, for
  // example, it is not possible to goto into a stmt expression apparently.
  // More semantic analysis is needed.
  
  // FIXME: the last statement in the compount stmt has its value used.  We
  // should not warn about it being unused.

  // If there are sub stmts in the compound stmt, take the type of the last one
  // as the type of the stmtexpr.
  QualType Ty = Context.VoidTy;
  
  if (!Compound->body_empty())
    if (Expr *LastExpr = dyn_cast<Expr>(Compound->body_back()))
      Ty = LastExpr->getType();
  
  return new StmtExpr(Compound, Ty, LPLoc, RPLoc);
}

Sema::ExprResult Sema::ParseTypesCompatibleExpr(SourceLocation BuiltinLoc, 
                                                TypeTy *arg1, TypeTy *arg2,
                                                SourceLocation RPLoc) {
  QualType argT1 = QualType::getFromOpaquePtr(arg1);
  QualType argT2 = QualType::getFromOpaquePtr(arg2);
  
  assert((!argT1.isNull() && !argT2.isNull()) && "Missing type argument(s)");
  
  return new TypesCompatibleExpr(Context.IntTy, BuiltinLoc, argT1, argT2, RPLoc);
}

Sema::ExprResult Sema::ParseChooseExpr(SourceLocation BuiltinLoc, ExprTy *cond, 
                                       ExprTy *expr1, ExprTy *expr2,
                                       SourceLocation RPLoc) {
  Expr *CondExpr = static_cast<Expr*>(cond);
  Expr *LHSExpr = static_cast<Expr*>(expr1);
  Expr *RHSExpr = static_cast<Expr*>(expr2);
  
  assert((CondExpr && LHSExpr && RHSExpr) && "Missing type argument(s)");

  // The conditional expression is required to be a constant expression.
  llvm::APSInt condEval(32);
  SourceLocation ExpLoc;
  if (!CondExpr->isIntegerConstantExpr(condEval, Context, &ExpLoc))
    return Diag(ExpLoc, diag::err_typecheck_choose_expr_requires_constant,
                 CondExpr->getSourceRange());

  // If the condition is > zero, then the AST type is the same as the LSHExpr.
  QualType resType = condEval.getZExtValue() ? LHSExpr->getType() : 
                                               RHSExpr->getType();
  return new ChooseExpr(BuiltinLoc, CondExpr, LHSExpr, RHSExpr, resType, RPLoc);
}

// TODO: Move this to SemaObjC.cpp
Sema::ExprResult Sema::ParseObjCStringLiteral(ExprTy *string) {
  StringLiteral* S = static_cast<StringLiteral *>(string);
  
  if (CheckBuiltinCFStringArgument(S))
    return true;
  
  QualType t = Context.getCFConstantStringType();
  t = t.getQualifiedType(QualType::Const);
  t = Context.getPointerType(t);

  return new ObjCStringLiteral(S, t);
}

Sema::ExprResult Sema::ParseObjCEncodeExpression(SourceLocation AtLoc,
                                                 SourceLocation LParenLoc,
                                                 TypeTy *Ty,
                                                 SourceLocation RParenLoc) {
  QualType EncodedType = QualType::getFromOpaquePtr(Ty);

  QualType t = Context.getPointerType(Context.CharTy);
  return new ObjCEncodeExpr(t, EncodedType, AtLoc, RParenLoc);
}
