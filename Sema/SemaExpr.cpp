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
using namespace llvm;
using namespace clang;

// Sema.h avoids including Expr.h. As a result, all the Check* functions take 
// an unsigned which is really an enum. These typedefs provide a short hand
// notiation for casting (to keep the lines within 80 columns:-)
typedef BinaryOperator::Opcode BOP;
typedef UnaryOperator::Opcode UOP;

/// ParseStringLiteral - The specified tokens were lexed as pasted string
/// fragments (e.g. "foo" "bar" L"baz").  The result string has to handle string
/// concatenation ([C99 5.1.1.2, translation phase #6]), so it may come from
/// multiple tokens.  However, the common case is that StringToks points to one
/// string.
/// 
Action::ExprResult
Sema::ParseStringLiteral(const LexerToken *StringToks, unsigned NumStringToks) {
  assert(NumStringToks && "Must have at least one string!");

  StringLiteralParser Literal(StringToks, NumStringToks, PP, Context.Target);
  if (Literal.hadError)
    return ExprResult(true);

  SmallVector<SourceLocation, 4> StringTokLocs;
  for (unsigned i = 0; i != NumStringToks; ++i)
    StringTokLocs.push_back(StringToks[i].getLocation());
  
  // FIXME: handle wchar_t
  QualType t = Context.getPointerType(Context.CharTy);
  
  // FIXME: use factory.
  // Pass &StringTokLocs[0], StringTokLocs.size() to factory!
  return new StringLiteral(Literal.GetString(), Literal.GetStringLength(), 
                           Literal.AnyWide, t);
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
  
  if (ValueDecl *VD = dyn_cast<ValueDecl>(D))
    return new DeclRefExpr(VD, VD->getType());
  if (isa<TypedefDecl>(D))
    return Diag(Loc, diag::err_unexpected_typedef, II.getName());

  assert(0 && "Invalid decl");
}

Sema::ExprResult Sema::ParseSimplePrimaryExpr(SourceLocation Loc,
                                              tok::TokenKind Kind) {
  switch (Kind) {
  default:
    assert(0 && "Unknown simple primary expr!");
  // TODO: MOVE this to be some other callback.
  case tok::kw___func__:       // primary-expression: __func__ [C99 6.4.2.2]
  case tok::kw___FUNCTION__:   // primary-expression: __FUNCTION__ [GNU]
  case tok::kw___PRETTY_FUNCTION__:  // primary-expression: __P..Y_F..N__ [GNU]
    return 0;
  }
}

Sema::ExprResult Sema::ParseCharacterConstant(const LexerToken &Tok) {
  SmallString<16> CharBuffer;
  CharBuffer.resize(Tok.getLength());
  const char *ThisTokBegin = &CharBuffer[0];
  unsigned ActualLength = PP.getSpelling(Tok, ThisTokBegin);
  
  CharLiteralParser Literal(ThisTokBegin, ThisTokBegin+ActualLength,
                            Tok.getLocation(), PP);
  if (Literal.hadError())
    return ExprResult(true);
  return new CharacterLiteral(Literal.getValue(), Context.IntTy);
}

Action::ExprResult Sema::ParseNumericConstant(const LexerToken &Tok) {
  // fast path for a single digit (which is quite common). A single digit 
  // cannot have a trigraph, escaped newline, radix prefix, or type suffix.
  if (Tok.getLength() == 1) {
    const char *t = PP.getSourceManager().getCharacterData(Tok.getLocation());
    return ExprResult(new IntegerLiteral(*t-'0', Context.IntTy));
  }
  SmallString<512> IntegerBuffer;
  IntegerBuffer.resize(Tok.getLength());
  const char *ThisTokBegin = &IntegerBuffer[0];
  
  // Get the spelling of the token, which eliminates trigraphs, etc.  Notes:
  // - We know that ThisTokBuf points to a buffer that is big enough for the 
  //   whole token and 'spelled' tokens can only shrink.
  // - In practice, the local buffer is only used when the spelling doesn't
  //   match the original token (which is rare). The common case simply returns
  //   a pointer to a *constant* buffer (avoiding a copy). 
  
  unsigned ActualLength = PP.getSpelling(Tok, ThisTokBegin);
  NumericLiteralParser Literal(ThisTokBegin, ThisTokBegin+ActualLength, 
                               Tok.getLocation(), PP);
  if (Literal.hadError)
    return ExprResult(true);

  if (Literal.isIntegerLiteral()) {
    QualType t;
    if (Literal.hasSuffix()) {
      if (Literal.isLong) 
        t = Literal.isUnsigned ? Context.UnsignedLongTy : Context.LongTy;
      else if (Literal.isLongLong) 
        t = Literal.isUnsigned ? Context.UnsignedLongLongTy : Context.LongLongTy;
      else 
        t = Context.UnsignedIntTy;
    } else {
      t = Context.IntTy; // implicit type is "int"
    }
    uintmax_t val;
    if (Literal.GetIntegerValue(val)) {
      return new IntegerLiteral(val, t);
    } 
  } else if (Literal.isFloatingLiteral()) {
    // FIXME: fill in the value and compute the real type...
    return new FloatingLiteral(7.7, Context.FloatTy);
  }
  return ExprResult(true);
}

Action::ExprResult Sema::ParseParenExpr(SourceLocation L, SourceLocation R,
                                        ExprTy *Val) {
  Expr *e = (Expr *)Val;
  assert((e != 0) && "ParseParenExpr() missing expr");
  return e;
}

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
                           SourceLocation LParenLoc, TypeTy *Ty,
                           SourceLocation RParenLoc) {
  // If error parsing type, ignore.
  if (Ty == 0) return true;
  
  // Verify that this is a valid expression.
  QualType ArgTy = QualType::getFromOpaquePtr(Ty);
  
  QualType resultType = CheckSizeOfAlignOfOperand(ArgTy, OpLoc, isSizeof);

  if (resultType.isNull())
    return true;
  return new SizeOfAlignOfTypeExpr(isSizeof, ArgTy, resultType);
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
  return new UnaryOperator((Expr *)Input, Opc, result);
}

Action::ExprResult Sema::
ParseArraySubscriptExpr(ExprTy *Base, SourceLocation LLoc,
                        ExprTy *Idx, SourceLocation RLoc) {
  QualType t1 = ((Expr *)Base)->getType();
  QualType t2 = ((Expr *)Idx)->getType();

  assert(!t1.isNull() && "no type for array base expression");
  assert(!t2.isNull() && "no type for array index expression");

  QualType canonT1 = t1.getCanonicalType();
  QualType canonT2 = t2.getCanonicalType();
  
  // C99 6.5.2.1p2: the expression e1[e2] is by definition precisely equivalent
  // to the expression *((e1)+(e2)). This means the array "Base" may actually be 
  // in the subscript position. As a result, we need to derive the array base 
  // and index from the expression types.
  
  QualType baseType, indexType;
  if (isa<ArrayType>(canonT1) || isa<PointerType>(canonT1)) {
    baseType = canonT1;
    indexType = canonT2;
  } else if (isa<ArrayType>(canonT2) || isa<PointerType>(canonT2)) { // uncommon
    baseType = canonT2;
    indexType = canonT1;
  } else 
    return Diag(LLoc, diag::err_typecheck_subscript_value);

  // C99 6.5.2.1p1
  if (!indexType->isIntegerType())
    return Diag(LLoc, diag::err_typecheck_subscript);

  // FIXME: need to deal with const...
  QualType resultType;
  if (ArrayType *ary = dyn_cast<ArrayType>(baseType)) {
    resultType = ary->getElementType();
  } else if (PointerType *ary = dyn_cast<PointerType>(baseType)) {
    resultType = ary->getPointeeType();
    // in practice, the following check catches trying to index a pointer
    // to a function (e.g. void (*)(int)). Functions are not objects in c99.
    if (!resultType->isObjectType())
      return Diag(LLoc, diag::err_typecheck_subscript_not_object, baseType);    
  } 
  return new ArraySubscriptExpr((Expr*)Base, (Expr*)Idx, resultType);
}

Action::ExprResult Sema::
ParseMemberReferenceExpr(ExprTy *Base, SourceLocation OpLoc,
                         tok::TokenKind OpKind, SourceLocation MemberLoc,
                         IdentifierInfo &Member) {
  QualType qualifiedType = ((Expr *)Base)->getType();
  
  assert(!qualifiedType.isNull() && "no type for member expression");
  
  QualType canonType = qualifiedType.getCanonicalType();

  if (OpKind == tok::arrow) {
    if (PointerType *PT = dyn_cast<PointerType>(canonType)) {
      qualifiedType = PT->getPointeeType();
      canonType = qualifiedType.getCanonicalType();
    } else
      return Diag(OpLoc, diag::err_typecheck_member_reference_arrow);
  }
  if (!isa<RecordType>(canonType))
    return Diag(OpLoc, diag::err_typecheck_member_reference_structUnion);
  
  // get the struct/union definition from the type.
  RecordDecl *RD = cast<RecordType>(canonType)->getDecl();
    
  if (canonType->isIncompleteType())
    return Diag(OpLoc, diag::err_typecheck_incomplete_tag, RD->getName());
    
  FieldDecl *MemberDecl = RD->getMember(&Member);
  if (!MemberDecl)
    return Diag(OpLoc, diag::err_typecheck_no_member, Member.getName());
    
  return new MemberExpr((Expr*)Base, OpKind == tok::arrow, MemberDecl);
}

/// ParseCallExpr - Handle a call to Fn with the specified array of arguments.
/// This provides the location of the left/right parens and a list of comma
/// locations.
Action::ExprResult Sema::
ParseCallExpr(ExprTy *Fn, SourceLocation LParenLoc,
              ExprTy **Args, unsigned NumArgsInCall,
              SourceLocation *CommaLocs, SourceLocation RParenLoc) {
  QualType qType = ((Expr *)Fn)->getType();

  assert(!qType.isNull() && "no type for function call expression");

  const FunctionType *funcT = dyn_cast<FunctionType>(qType.getCanonicalType());
  
  assert(funcT && "ParseCallExpr(): not a function type");
    
  // If a prototype isn't declared, the parser implicitly defines a func decl
  QualType resultType = funcT->getResultType();
    
  if (const FunctionTypeProto *proto = dyn_cast<FunctionTypeProto>(funcT)) {
    // C99 6.5.2.2p7 - the arguments are implicitly converted, as if by 
    // assignment, to the types of the corresponding parameter, ...
    
    unsigned NumArgsInProto = proto->getNumArgs();
    unsigned NumArgsToCheck = NumArgsInCall;
    
    if (NumArgsInCall < NumArgsInProto)
      Diag(LParenLoc, diag::err_typecheck_call_too_few_args);
    else if (NumArgsInCall > NumArgsInProto) {
      if (!proto->isVariadic())
        Diag(LParenLoc, diag::err_typecheck_call_too_many_args);
      NumArgsToCheck = NumArgsInProto;
    }
    // Continue to check argument types (even if we have too few/many args).
    for (unsigned i = 0; i < NumArgsToCheck; i++) {
      QualType lhsType = proto->getArgType(i);
      QualType rhsType = ((Expr **)Args)[i]->getType();
      
      if (lhsType == rhsType) // common case, fast path...
        continue;
      AssignmentConversionResult result;
      UsualAssignmentConversions(lhsType, rhsType, result);

      SourceLocation l = (i == 0) ? LParenLoc : CommaLocs[i-1];

      // decode the result (notice that AST's are still created for extensions).
      // FIXME: consider fancier error diagnostics (since this is quite common).
      // #1: emit the actual prototype arg...requires adding source loc info.
      // #2: pass Diag the offending argument type...requires hacking Diag.
      switch (result) {
      case Compatible:
        break;
      case PointerFromInt:
        // check for null pointer constant (C99 6.3.2.3p3)
        if (!((Expr **)Args)[i]->isNullPointerConstant())
          Diag(l, diag::ext_typecheck_passing_pointer_from_int, utostr(i+1));
        break;
      case IntFromPointer:
        Diag(l, diag::ext_typecheck_passing_int_from_pointer, utostr(i+1));
        break;
      case IncompatiblePointer:
        Diag(l, diag::ext_typecheck_passing_incompatible_pointer, utostr(i+1));
        break;
      case CompatiblePointerDiscardsQualifiers:
        Diag(l, diag::ext_typecheck_passing_discards_qualifiers, utostr(i+1));
        break;
      case Incompatible:
        return Diag(l, diag::err_typecheck_passing_incompatible, utostr(i+1));
      }
    }
    // Even if the types checked, bail if we had the wrong number of arguments.
    if ((NumArgsInCall != NumArgsInProto) && !proto->isVariadic())
      return true;
  }
  return new CallExpr((Expr*)Fn, (Expr**)Args, NumArgsInCall, resultType);
}

Action::ExprResult Sema::
ParseCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
              SourceLocation RParenLoc, ExprTy *Op) {
  // If error parsing type, ignore.
  assert((Ty != 0) && "ParseCastExpr(): missing type");
  return new CastExpr(QualType::getFromOpaquePtr(Ty), (Expr*)Op);
}

inline QualType Sema::CheckConditionalOperands( // C99 6.5.15
  Expr *Cond, Expr *LHS, Expr *RHS, SourceLocation questionLoc) {
  QualType cond = Cond->getType().getCanonicalType();
  QualType lhs = LHS->getType().getCanonicalType();
  QualType rhs = RHS->getType().getCanonicalType();

  assert(!cond.isNull() && "ParseConditionalOp(): no conditional type");
  assert(!lhs.isNull() && "ParseConditionalOp(): no lhs type");
  assert(!rhs.isNull() && "ParseConditionalOp(): no rhs type");

  // C99 6.5.15p2,3
  return rhs;
}

/// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
/// in the case of a the GNU conditional expr extension.
Action::ExprResult Sema::ParseConditionalOp(SourceLocation QuestionLoc, 
                                            SourceLocation ColonLoc,
                                            ExprTy *Cond, ExprTy *LHS,
                                            ExprTy *RHS) {
  QualType result = CheckConditionalOperands((Expr *)Cond, (Expr *)LHS, 
                                             (Expr *)RHS, QuestionLoc);
  if (result.isNull())
    return true;
  return new ConditionalOperator((Expr*)Cond, (Expr*)LHS, (Expr*)RHS, result);
}

/// UsualUnaryConversion - Performs various conversions that are common to most
/// operators (C99 6.3). The conversions of array and function types are 
/// sometimes surpressed. For example, the array->pointer conversion doesn't
/// apply if the array is an argument to the sizeof or address (&) operators.
/// In these instances, this routine should *not* be called.
QualType Sema::UsualUnaryConversion(QualType t) {
  assert(!t.isNull() && "UsualUnaryConversion - missing type");
  
  if (t->isPromotableIntegerType()) // C99 6.3.1.1p2
    return Context.IntTy;
  else if (t->isFunctionType()) // C99 6.3.2.1p4
    return Context.getPointerType(t);
  else if (t->isArrayType()) // C99 6.3.2.1p3
    return Context.getPointerType(cast<ArrayType>(t)->getElementType());
  return t;
}

/// UsualArithmeticConversions - Performs various conversions that are common to 
/// binary operators (C99 6.3.1.8). If both operands aren't arithmetic, this
/// routine returns the first non-arithmetic type found. The client is 
/// responsible for emitting appropriate error diagnostics.
QualType Sema::UsualArithmeticConversions(QualType t1, QualType t2) {
  QualType lhs = UsualUnaryConversion(t1);
  QualType rhs = UsualUnaryConversion(t2);
  
  // if either operand is not of arithmetic type, no conversion is possible.
  if (!lhs->isArithmeticType())
    return lhs;
  if (!rhs->isArithmeticType())
    return rhs;
    
  // if both arithmetic types are identical, no conversion is needed.
  if (lhs == rhs) 
    return lhs;
  
  // at this point, we have two different arithmetic types. 
  
  // Handle complex types first (C99 6.3.1.8p1).
  if (lhs->isComplexType() || rhs->isComplexType()) {
    // if we have an integer operand, the result is the complex type.
    if (rhs->isIntegerType())
      return lhs;
    if (lhs->isIntegerType())
      return rhs;

    return Context.maxComplexType(lhs, rhs);
  }
  // Now handle "real" floating types (i.e. float, double, long double).
  if (lhs->isRealFloatingType() || rhs->isRealFloatingType()) {
    // if we have an integer operand, the result is the real floating type.
    if (rhs->isIntegerType())
      return lhs;
    if (lhs->isIntegerType())
      return rhs;

    // we have two real floating types, float/complex combos were handled above.
    return Context.maxFloatingType(lhs, rhs);
  }
  return Context.maxIntegerType(lhs, rhs);
}

// CheckPointerTypesForAssignment - This is a very tricky routine (despite
// being closely modeled after the C99 spec:-). The odd characteristic of this 
// routine is it effectively iqnores the qualifiers on the top level pointee.
// This circumvents the usual type rules specified in 6.2.7p1 & 6.7.5.[1-3].
// FIXME: add a couple examples in this comment.
QualType Sema::CheckPointerTypesForAssignment(QualType lhsType, 
                                              QualType rhsType,
                                              AssignmentConversionResult &r) {
  QualType lhptee, rhptee;
  
  // get the "pointed to" type (ignoring qualifiers at the top level)
  lhptee = cast<PointerType>(lhsType.getCanonicalType())->getPointeeType();
  rhptee = cast<PointerType>(rhsType.getCanonicalType())->getPointeeType();
  
  // make sure we operate on the canonical type
  lhptee = lhptee.getCanonicalType();
  rhptee = rhptee.getCanonicalType();

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
  return rhsType;
}

/// UsualAssignmentConversions (C99 6.5.16) - This routine currently 
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
QualType Sema::UsualAssignmentConversions(QualType lhsType, QualType rhsType,
                                          AssignmentConversionResult &r) {
  // this check seems unnatural, however it necessary to insure the proper
  // conversion of functions/arrays. If the conversion where done for all
  // DeclExpr's (created by ParseIdentifierExpr), it would mess up the
  // unary expressions that surpress this implicit conversion (&, sizeof).
  if (rhsType->isFunctionType() || rhsType->isArrayType())
    rhsType = UsualUnaryConversion(rhsType);
    
  r = Compatible;
  if (lhsType->isArithmeticType() && rhsType->isArithmeticType())
    return lhsType;
  else if (lhsType->isPointerType()) {
    if (rhsType->isIntegerType()) {
      r = PointerFromInt;
      return rhsType;
    }
    if (rhsType->isPointerType())
      return CheckPointerTypesForAssignment(lhsType, rhsType, r);
  } else if (rhsType->isPointerType()) {
    if (lhsType->isIntegerType()) {
      // C99 6.5.16.1p1: the left operand is _Bool and the right is a pointer.
      if (lhsType != Context.BoolTy)
        r = IntFromPointer;
      return rhsType;
    }
    if (lhsType->isPointerType()) 
      return CheckPointerTypesForAssignment(lhsType, rhsType, r);
  } else if (isa<TagType>(lhsType) && isa<TagType>(rhsType)) {
    if (Type::tagTypesAreCompatible(lhsType, rhsType))
      return rhsType;
  } 
  r = Incompatible;
  return QualType();
}

inline QualType Sema::CheckMultiplyDivideOperands(
  Expr *lex, Expr *rex, SourceLocation loc) 
{
  QualType resType = UsualArithmeticConversions(lex->getType(), rex->getType());
  
  if (resType->isArithmeticType())
    return resType;
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckRemainderOperands(
  Expr *lex, Expr *rex, SourceLocation loc) 
{
  QualType resType = UsualArithmeticConversions(lex->getType(), rex->getType());
  
  if (resType->isIntegerType())
    return resType;
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckAdditionOperands( // C99 6.5.6
  Expr *lex, Expr *rex, SourceLocation loc) 
{
  QualType lhsType = lex->getType(), rhsType = rex->getType();
  QualType resType = UsualArithmeticConversions(lhsType, rhsType);
  
  // handle the common case first (both operands are arithmetic).
  if (resType->isArithmeticType())
    return resType;

  if ((lhsType->isPointerType() && rhsType->isIntegerType()) ||
      (lhsType->isIntegerType() && rhsType->isPointerType()))
    return resType;
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckSubtractionOperands( // C99 6.5.6
  Expr *lex, Expr *rex, SourceLocation loc) 
{
  QualType lhsType = lex->getType(), rhsType = rex->getType();
  QualType resType = UsualArithmeticConversions(lhsType, rhsType);
  
  // handle the common case first (both operands are arithmetic).
  if (resType->isArithmeticType())
    return resType;
  if ((lhsType->isPointerType() && rhsType->isIntegerType()) ||
      (lhsType->isPointerType() && rhsType->isPointerType()))
    return resType;
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckShiftOperands( // C99 6.5.7
  Expr *lex, Expr *rex, SourceLocation loc)
{
  QualType resType = UsualArithmeticConversions(lex->getType(), rex->getType());
  
  if (resType->isIntegerType())
    return resType;
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckRelationalOperands( // C99 6.5.8
  Expr *lex, Expr *rex, SourceLocation loc)
{
  QualType lType = lex->getType(), rType = rex->getType();
  
  if (lType->isRealType() && rType->isRealType())
    return Context.IntTy;
  
  if (lType->isPointerType() &&  rType->isPointerType())
    return Context.IntTy;

  if (lType->isIntegerType() || rType->isIntegerType()) { // GCC extension.
    Diag(loc, diag::ext_typecheck_comparison_of_pointer_integer);
    return Context.IntTy;
  }
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckEqualityOperands( // C99 6.5.9
  Expr *lex, Expr *rex, SourceLocation loc)
{
  QualType lType = lex->getType(), rType = rex->getType();
  
  if (lType->isArithmeticType() && rType->isArithmeticType())
    return Context.IntTy;
  if (lType->isPointerType() &&  rType->isPointerType())
    return Context.IntTy;
    
  if (lType->isIntegerType() || rType->isIntegerType()) { // GCC extension.
    Diag(loc, diag::ext_typecheck_comparison_of_pointer_integer);
    return Context.IntTy;
  }
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckBitwiseOperands(
  Expr *lex, Expr *rex, SourceLocation loc) 
{
  QualType resType = UsualArithmeticConversions(lex->getType(), rex->getType());
  
  if (resType->isIntegerType())
    return resType;
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckLogicalOperands( // C99 6.5.[13,14]
  Expr *lex, Expr *rex, SourceLocation loc) 
{
  QualType lhsType = UsualUnaryConversion(lex->getType());
  QualType rhsType = UsualUnaryConversion(rex->getType());
  
  if (lhsType->isScalarType() || rhsType->isScalarType())
    return Context.IntTy;
  Diag(loc, diag::err_typecheck_invalid_operands);
  return QualType();
}

inline QualType Sema::CheckAssignmentOperands( // C99 6.5.16.1
  Expr *lex, Expr *rex, SourceLocation loc, QualType compoundType) 
{
  QualType lhsType = lex->getType();
  QualType rhsType = compoundType.isNull() ? rex->getType() : compoundType;
  bool hadError = false;
  
  // this check is done first to give a more precise diagnostic.
  // isModifiableLvalue() will also check for "const".
  if (lhsType.isConstQualified()) {
    Diag(loc, diag::err_typecheck_assign_const);
    hadError = true;
  } else if (!lex->isModifiableLvalue()) { // C99 6.5.16p2
    Diag(loc, diag::err_typecheck_assign_non_lvalue);
    return QualType(); // no need to continue checking...
  }
  if (lhsType == rhsType) // common case, fast path...
    return lhsType;
  
  AssignmentConversionResult result;
  QualType resType = UsualAssignmentConversions(lhsType, rhsType, result);

  // decode the result (notice that extensions still return a type).
  switch (result) {
  case Compatible:
    break;
  case Incompatible:
    Diag(loc, diag::err_typecheck_assign_incompatible);
    hadError = true;
    break;
  case PointerFromInt:
    // check for null pointer constant (C99 6.3.2.3p3)
    if (compoundType.isNull() && !rex->isNullPointerConstant())
      Diag(loc, diag::ext_typecheck_assign_pointer_from_int);
    break;
  case IntFromPointer:
    Diag(loc, diag::ext_typecheck_assign_int_from_pointer);
    break;
  case IncompatiblePointer:
    Diag(loc, diag::ext_typecheck_assign_incompatible_pointer);
    break;
  case CompatiblePointerDiscardsQualifiers:
    Diag(loc, diag::ext_typecheck_assign_discards_qualifiers);
    break;
  }
  return hadError ? QualType() : resType;
}

inline QualType Sema::CheckCommaOperands( // C99 6.5.17
  Expr *lex, Expr *rex, SourceLocation loc) 
{
  return UsualUnaryConversion(rex->getType());
}

QualType Sema::CheckIncrementDecrementOperand(Expr *op, SourceLocation OpLoc) {
  QualType resType = UsualArithmeticConversions(op->getType(), Context.IntTy);
  assert(!resType.isNull() && "no type for increment/decrement expression");

  // C99 6.5.2.4p1
  if (const PointerType *pt = dyn_cast<PointerType>(resType)) {
    if (!pt->getPointeeType()->isObjectType()) { // C99 6.5.2.4p2, 6.5.6p2
      Diag(OpLoc, diag::err_typecheck_arithmetic_incomplete_type, resType);
      return QualType();
    }
  } else if (!resType->isRealType()) { 
    // FIXME: Allow Complex as a GCC extension.
    Diag(OpLoc, diag::err_typecheck_illegal_increment_decrement, resType);
    return QualType(); 
  }
  // At this point, we know we have a real or pointer type. Now make sure
  // the operand is a modifiable lvalue.
  if (!op->isModifiableLvalue()) {
    Diag(OpLoc, diag::err_typecheck_invalid_lvalue_incr_decr);
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
/// operator, and its result is never an lvalue.
QualType Sema::CheckAddressOfOperand(Expr *op, SourceLocation OpLoc) {
  Decl *dcl = getPrimaryDeclaration(op);
  
  if (!op->isLvalue()) { // C99 6.5.3.2p1
    if (dcl && isa<FunctionDecl>(dcl)) // allow function designators
      ;  
    else {
      Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof);
      return QualType();
    }
  } else if (dcl) {
    // We have an lvalue with a decl. Make sure the decl is not declared 
    // with the register storage-class specifier.
    if (const VarDecl *vd = dyn_cast<VarDecl>(dcl)) {
      if (vd->getStorageClass() == VarDecl::Register) {
        Diag(OpLoc, diag::err_typecheck_address_of_register);
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
  QualType qType = UsualUnaryConversion(op->getType());
  
  assert(!qType.isNull() && "no type for * expression");

  if (PointerType *PT = dyn_cast<PointerType>(qType))
    return PT->getPointeeType();
  Diag(OpLoc, diag::err_typecheck_unary_expr, qType);
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
  case tok::ampamp:       Opc = UnaryOperator::AddrLabel; break;
  // FIXME: case tok::kw___extension__: 
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

  QualType result;
  
  switch (Opc) {
  default:
    assert(0 && "Unknown binary expr!");
  case BinaryOperator::Assign:
    result = CheckAssignmentOperands(lhs, rhs, TokLoc, QualType());
    break;
  case BinaryOperator::Mul: 
  case BinaryOperator::Div:
    result = CheckMultiplyDivideOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Rem:
    result = CheckRemainderOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Add:
    result = CheckAdditionOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Sub:
    result = CheckSubtractionOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::Shl: 
  case BinaryOperator::Shr:
    result = CheckShiftOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::LE:
  case BinaryOperator::LT:
  case BinaryOperator::GE:
  case BinaryOperator::GT:
    result = CheckRelationalOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::EQ:
  case BinaryOperator::NE:
    result = CheckEqualityOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::And:
  case BinaryOperator::Xor:
  case BinaryOperator::Or:
    result = CheckBitwiseOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::LAnd:
  case BinaryOperator::LOr:
    result = CheckLogicalOperands(lhs, rhs, TokLoc);
    break;
  case BinaryOperator::MulAssign:
  case BinaryOperator::DivAssign:
    result = CheckMultiplyDivideOperands(lhs, rhs, TokLoc);
    if (result.isNull())
      return true;
    result = CheckAssignmentOperands(lhs, rhs, TokLoc, result);
    break;
  case BinaryOperator::RemAssign:
    result = CheckRemainderOperands(lhs, rhs, TokLoc);
    if (result.isNull())
      return true;
    result = CheckAssignmentOperands(lhs, rhs, TokLoc, result);
    break;
  case BinaryOperator::AddAssign:
    result = CheckAdditionOperands(lhs, rhs, TokLoc);
    if (result.isNull())
      return true;
    result = CheckAssignmentOperands(lhs, rhs, TokLoc, result);
    break;
  case BinaryOperator::SubAssign:
    result = CheckSubtractionOperands(lhs, rhs, TokLoc);
    if (result.isNull())
      return true;
    result = CheckAssignmentOperands(lhs, rhs, TokLoc, result);
    break;
  case BinaryOperator::ShlAssign:
  case BinaryOperator::ShrAssign:
    result = CheckShiftOperands(lhs, rhs, TokLoc);
    if (result.isNull())
      return true;
    result = CheckAssignmentOperands(lhs, rhs, TokLoc, result);
    break;
  case BinaryOperator::AndAssign:
  case BinaryOperator::XorAssign:
  case BinaryOperator::OrAssign:
    result = CheckBitwiseOperands(lhs, rhs, TokLoc);
    if (result.isNull())
      return true;
    result = CheckAssignmentOperands(lhs, rhs, TokLoc, result);
    break;
  case BinaryOperator::Comma:
    result = CheckCommaOperands(lhs, rhs, TokLoc);
    break;
  }
  if (result.isNull())
    return true;
  return new BinaryOperator(lhs, rhs, Opc, result);
}

// Unary Operators.  'Tok' is the token for the operator.
Action::ExprResult Sema::ParseUnaryOp(SourceLocation OpLoc, tok::TokenKind Op,
                                      ExprTy *Input) {
  UnaryOperator::Opcode Opc = ConvertTokenKindToUnaryOpcode(Op);
  QualType resultType;
  switch (Opc) {
  default:
    assert(0 && "Unimplemented unary expr!");
  case UnaryOperator::PreInc:
  case UnaryOperator::PreDec:
    resultType = CheckIncrementDecrementOperand((Expr *)Input, OpLoc);
    break;
  case UnaryOperator::AddrOf: 
    resultType = CheckAddressOfOperand((Expr *)Input, OpLoc);
    break;
  case UnaryOperator::Deref: 
    resultType = CheckIndirectionOperand((Expr *)Input, OpLoc);
    break;
  case UnaryOperator::Plus:
  case UnaryOperator::Minus:
    resultType = UsualUnaryConversion(((Expr *)Input)->getType());
    if (!resultType->isArithmeticType())  // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr, resultType);
    break;
  case UnaryOperator::Not: // bitwise complement
    resultType = UsualUnaryConversion(((Expr *)Input)->getType());
    if (!resultType->isIntegerType())  // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr, resultType);
    break;
  case UnaryOperator::LNot: // logical negation
    resultType = UsualUnaryConversion(((Expr *)Input)->getType());
    if (!resultType->isScalarType()) // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr, resultType);
    break;
  case UnaryOperator::SizeOf:
    resultType = CheckSizeOfAlignOfOperand(((Expr *)Input)->getType(), OpLoc,
                                           true);
    break;
  case UnaryOperator::AlignOf:
    resultType = CheckSizeOfAlignOfOperand(((Expr *)Input)->getType(), OpLoc,
                                           false);
    break;
  }
  if (resultType.isNull())
    return true;
  return new UnaryOperator((Expr *)Input, Opc, resultType);
}
