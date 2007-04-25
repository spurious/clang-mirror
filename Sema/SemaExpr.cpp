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
using namespace llvm;
using namespace clang;

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
  case tok::char_constant:     // constant: character-constant
    // TODO: MOVE this to be some other callback.
  case tok::kw___func__:       // primary-expression: __func__ [C99 6.4.2.2]
  case tok::kw___FUNCTION__:   // primary-expression: __FUNCTION__ [GNU]
  case tok::kw___PRETTY_FUNCTION__:  // primary-expression: __P..Y_F..N__ [GNU]
    return 0;
  }
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
  return Val;
}


// Unary Operators.  'Tok' is the token for the operator.
Action::ExprResult Sema::ParseUnaryOp(SourceLocation OpLoc, tok::TokenKind Op,
                                      ExprTy *Input) {
  UnaryOperator::Opcode Opc;
  switch (Op) {
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
  case tok::kw___extension__: 
    return Input;
    //Opc = UnaryOperator::Extension;
    //break;
  }
  if (Opc == UnaryOperator::PreInc || Opc == UnaryOperator::PreDec)
    return CheckIncrementDecrementOperand((Expr *)Input, OpLoc, Opc);
  else if (Opc == UnaryOperator::AddrOf)
    return CheckAddressOfOperand((Expr *)Input, OpLoc);
  else if (Opc == UnaryOperator::Deref) 
    return CheckIndirectionOperand((Expr *)Input, OpLoc);
  else if (UnaryOperator::isArithmeticOp(Opc))
    return CheckArithmeticOperand((Expr *)Input, OpLoc, Opc);
  
  // will go away when all cases are handled...
  return new UnaryOperator((Expr *)Input, Opc, QualType());
}

Action::ExprResult Sema::
ParseSizeOfAlignOfTypeExpr(SourceLocation OpLoc, bool isSizeof, 
                           SourceLocation LParenLoc, TypeTy *Ty,
                           SourceLocation RParenLoc) {
  // If error parsing type, ignore.
  if (Ty == 0) return true;
  
  // Verify that this is a valid expression.
  QualType ArgTy = QualType::getFromOpaquePtr(Ty);
  
  if (isa<FunctionType>(ArgTy) && isSizeof) {
    // alignof(function) is allowed.
    Diag(OpLoc, diag::ext_sizeof_function_type);
    return new IntegerLiteral(1, Context.IntTy);
  } else if (ArgTy->isVoidType()) {
    Diag(OpLoc, diag::ext_sizeof_void_type, isSizeof ? "sizeof" : "__alignof");
  } else if (ArgTy->isIncompleteType()) {
    std::string TypeName;
    ArgTy->getAsString(TypeName);
    Diag(OpLoc, isSizeof ? diag::err_sizeof_incomplete_type : 
         diag::err_alignof_incomplete_type, TypeName);
    return new IntegerLiteral(0, Context.IntTy);
  }
  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return new SizeOfAlignOfTypeExpr(isSizeof, ArgTy, Context.getSizeType());
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
  return CheckIncrementDecrementOperand((Expr *)Input, OpLoc, Opc);
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
              ExprTy **Args, unsigned NumArgs,
              SourceLocation *CommaLocs, SourceLocation RParenLoc) {
  return new CallExpr((Expr*)Fn, (Expr**)Args, NumArgs);
}

Action::ExprResult Sema::
ParseCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
              SourceLocation RParenLoc, ExprTy *Op) {
  // If error parsing type, ignore.
  if (Ty == 0) return true;
  return new CastExpr(QualType::getFromOpaquePtr(Ty), (Expr*)Op);
}



// Binary Operators.  'Tok' is the token for the operator.
Action::ExprResult Sema::ParseBinOp(SourceLocation TokLoc, tok::TokenKind Kind,
                                    ExprTy *LHS, ExprTy *RHS) {
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

  Expr *lhs = (Expr *)LHS, *rhs = (Expr*)RHS;
  
  if (BinaryOperator::isMultiplicativeOp(Opc)) 
    return CheckMultiplicativeOperands(lhs, rhs, TokLoc, Opc);
  else if (BinaryOperator::isAdditiveOp(Opc))
    return CheckAdditiveOperands(lhs, rhs, TokLoc, Opc);
  else if (BinaryOperator::isShiftOp(Opc))
    return CheckShiftOperands(lhs, rhs, TokLoc, Opc);
  else if (BinaryOperator::isRelationalOp(Opc))
    return CheckRelationalOperands(lhs, rhs, TokLoc, Opc);
  else if (BinaryOperator::isEqualityOp(Opc))
    return CheckEqualityOperands(lhs, rhs, TokLoc, Opc);
  else if (BinaryOperator::isBitwiseOp(Opc))
    return CheckBitwiseOperands(lhs, rhs, TokLoc, Opc);
  else if (BinaryOperator::isLogicalOp(Opc))
    return CheckLogicalOperands(lhs, rhs, TokLoc, Opc);
    
  assert(0 && "ParseBinOp() not handling all binary ops properly");
}

/// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
/// in the case of a the GNU conditional expr extension.
Action::ExprResult Sema::ParseConditionalOp(SourceLocation QuestionLoc, 
                                            SourceLocation ColonLoc,
                                            ExprTy *Cond, ExprTy *LHS,
                                            ExprTy *RHS) {
  return new ConditionalOperator((Expr*)Cond, (Expr*)LHS, (Expr*)RHS);
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

/// GetIntegerRank - Helper function for UsualArithmeticConversions().
static inline int GetIntegerRank(QualType t) {
  if (const BuiltinType *BT = dyn_cast<BuiltinType>(t.getCanonicalType())) {
    switch (BT->getKind()) {
    case BuiltinType::SChar:
    case BuiltinType::UChar:
      return 1;
    case BuiltinType::Short:
    case BuiltinType::UShort:
      return 2;
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return 3;
    case BuiltinType::Long:
    case BuiltinType::ULong:
      return 4;
    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
      return 5;
    default:
      assert(0 && "getFloatingPointRank(): not a floating type");
    }
  }
  return 0;
}

static inline QualType ConvertSignedWithGreaterRankThanUnsigned(
  QualType signedType, QualType unsignedType) {
  // FIXME: Need to check if the signed type can represent all values of the 
  // unsigned type. If it can, then the result is the signed type. If it can't, 
  // then the result is the unsigned version of the signed type.
  return signedType; 
}

/// GetFloatingRank - Helper function for UsualArithmeticConversions().
static inline int GetFloatingRank(QualType t) {
  if (const BuiltinType *BT = dyn_cast<BuiltinType>(t.getCanonicalType())) {
    switch (BT->getKind()) {
    case BuiltinType::Float:
    case BuiltinType::FloatComplex:
      return 1;
    case BuiltinType::Double:
    case BuiltinType::DoubleComplex:
      return 2;
    case BuiltinType::LongDouble:
    case BuiltinType::LongDoubleComplex:
      return 3;
    default:
      assert(0 && "getFloatingPointRank(): not a floating type");
    }
  }
  return 0;
}

/// ConvertFloatingRankToComplexType - Another helper for converting floats.
static inline QualType ConvertFloatingRankToComplexType(int rank, 
                                                        ASTContext &C) {
  switch (rank) {
  case 1:
    return C.FloatComplexTy;
  case 2:
    return C.DoubleComplexTy;
  case 3:
    return C.LongDoubleComplexTy;
  default:
    assert(0 && "convertRankToComplex(): illegal value for rank");
  }
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

    // the following code handles three different combinations:
    // complex/complex, complex/float, float/complex. When both operands 
    // are complex, the shorter operand is converted to the type of the longer,
    // and that is the type of the result. This corresponds to what is done
    // when combining two real floating-point operands. The fun begins when 
    // size promotion occur across type domains. GetFloatingRank &
    // ConvertFloatingRankToComplexType handle this without enumerating all
    // permutations. It also allows us to add new types without breakage.
    int lhsRank = GetFloatingRank(lhs);
    int rhsRank = GetFloatingRank(rhs);
    
    // From H&S 6.3.4: When one operand is complex and the other is a real
    // floating-point type, the less precise type is converted, within it's 
    // real or complex domain, to the precision of the other type. For example,
    // when combining a "long double" with a "double _Complex", the 
    // "double _Complex" is promoted to "long double _Complex".
    return ConvertFloatingRankToComplexType(std::max(lhsRank,rhsRank), Context);
  }
  // Now handle "real" floating types (i.e. float, double, long double).
  if (lhs->isRealFloatingType() || rhs->isRealFloatingType()) {
    // if we have an integer operand, the result is the real floating type.
    if (rhs->isIntegerType())
      return lhs;
    if (lhs->isIntegerType())
      return rhs;

    // we have two real floating types, float/complex combos were handled above.
    return GetFloatingRank(lhs) >= GetFloatingRank(rhs) ? lhs : rhs;
  }
  // Lastly, handle two integers (C99 6.3.1.8p1)
  bool t1Unsigned = lhs->isUnsignedIntegerType();
  bool t2Unsigned = rhs->isUnsignedIntegerType();
  
  if ((t1Unsigned && t2Unsigned) || (!t1Unsigned && !t2Unsigned))
    return GetIntegerRank(lhs) >= GetIntegerRank(rhs) ? lhs : rhs; 
  
  // We have two integer types with differing signs
  QualType unsignedType = t1Unsigned ? lhs : rhs;
  QualType signedType = t1Unsigned ? rhs : lhs;
  
  if (GetIntegerRank(unsignedType) >= GetIntegerRank(signedType))
    return unsignedType;
  else 
    return ConvertSignedWithGreaterRankThanUnsigned(signedType, unsignedType); 
}

Action::ExprResult Sema::CheckMultiplicativeOperands(
  Expr *lex, Expr *rex, SourceLocation loc, unsigned code) 
{
  QualType resType = UsualArithmeticConversions(lex->getType(), rex->getType());
  
  if ((BinaryOperator::Opcode)code == BinaryOperator::Rem) {
    if (!resType->isIntegerType())
      return Diag(loc, diag::err_typecheck_invalid_operands);
  } else { // *, /
    if (!resType->isArithmeticType())
      return Diag(loc, diag::err_typecheck_invalid_operands);
  }
  return new BinaryOperator(lex, rex, (BinaryOperator::Opcode)code, resType);
}

Action::ExprResult Sema::CheckAdditiveOperands( // C99 6.5.6
  Expr *lex, Expr *rex, SourceLocation loc, unsigned code) 
{
  return new BinaryOperator(lex, rex, (BinaryOperator::Opcode)code, Context.IntTy);
}

Action::ExprResult Sema::CheckShiftOperands( // C99 6.5.7
  Expr *lex, Expr *rex, SourceLocation loc, unsigned code)
{
  QualType resType = UsualArithmeticConversions(lex->getType(), rex->getType());
  
  if (!resType->isIntegerType())
    return Diag(loc, diag::err_typecheck_invalid_operands);

  return new BinaryOperator(lex, rex, (BinaryOperator::Opcode)code, resType);
}

Action::ExprResult Sema::CheckRelationalOperands( // C99 6.5.8
  Expr *lex, Expr *rex, SourceLocation loc, unsigned code)
{
  QualType lType = lex->getType(), rType = rex->getType();
  
  if (lType->isRealType() && rType->isRealType())
    ;
  else if (lType->isPointerType() &&  rType->isPointerType())
    ;
  else {
    // The following test is for GCC compatibility.
    if (lType->isIntegerType() || rType->isIntegerType())
      return Diag(loc, diag::err_typecheck_comparison_of_pointer_integer);
    return Diag(loc, diag::err_typecheck_invalid_operands);
  }
  return new BinaryOperator(lex, rex, (BinaryOperator::Opcode)code, 
                            Context.IntTy);
}

Action::ExprResult Sema::CheckEqualityOperands( // C99 6.5.9
  Expr *lex, Expr *rex, SourceLocation loc, unsigned code)
{
  QualType lType = lex->getType(), rType = rex->getType();
  
  if (lType->isArithmeticType() && rType->isArithmeticType())
    ;
  else if (lType->isPointerType() &&  rType->isPointerType())
    ;
  else {
    // The following test is for GCC compatibility.
    if (lType->isIntegerType() || rType->isIntegerType())
      return Diag(loc, diag::err_typecheck_comparison_of_pointer_integer);
    return Diag(loc, diag::err_typecheck_invalid_operands);
  }
  return new BinaryOperator(lex, rex, (BinaryOperator::Opcode)code, 
                            Context.IntTy);
}

Action::ExprResult Sema::CheckBitwiseOperands(
  Expr *lex, Expr *rex, SourceLocation loc, unsigned code) 
{
  QualType resType = UsualArithmeticConversions(lex->getType(), rex->getType());
  
  if (!resType->isIntegerType())
    return Diag(loc, diag::err_typecheck_invalid_operands);

  return new BinaryOperator(lex, rex, (BinaryOperator::Opcode)code, resType);
}

Action::ExprResult Sema::CheckLogicalOperands( // C99 6.5.[13,14]
  Expr *lex, Expr *rex, SourceLocation loc, unsigned code) 
{
  return new BinaryOperator(lex, rex, (BinaryOperator::Opcode)code);
}

Action::ExprResult
Sema::CheckIncrementDecrementOperand(Expr *op, SourceLocation OpLoc,
                                               unsigned OpCode) {
  QualType qType = op->getType();

  assert(!qType.isNull() && "no type for increment/decrement expression");

  QualType canonType = qType.getCanonicalType();

  // C99 6.5.2.4p1
  if (const PointerType *pt = dyn_cast<PointerType>(canonType)) {
    if (!pt->getPointeeType()->isObjectType()) // C99 6.5.2.4p2, 6.5.6p2
      return Diag(OpLoc, diag::err_typecheck_arithmetic_incomplete_type, qType);    
  } else if (!canonType->isRealType()) { 
    // FIXME: Allow Complex as a GCC extension.
    return Diag(OpLoc, diag::err_typecheck_illegal_increment_decrement, qType);    
  }
  // At this point, we know we have a real or pointer type. As a result, the
  // following predicate is overkill (i.e. it will check for types we know we
  // don't have in this context). Nevertheless, we model the C99 spec closely.
  if (!canonType.isModifiableLvalue())
    return Diag(OpLoc, diag::err_typecheck_not_modifiable, qType);

  return new UnaryOperator(op, (UnaryOperator::Opcode)OpCode, qType);
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
Action::ExprResult
Sema::CheckAddressOfOperand(Expr *op, SourceLocation OpLoc) {
  Decl *dcl = getPrimaryDeclaration(op);
  
  if (!op->isLvalue()) {
    if (dcl && isa<FunctionDecl>(dcl))
      ;  // C99 6.5.3.2p1: Allow function designators.
    else
      return Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof);      
  } else if (dcl) {
    // We have an lvalue with a decl. Make sure the decl is not declared 
    // with the register storage-class specifier.
    if (const VarDecl *vd = dyn_cast<VarDecl>(dcl)) {
      if (vd->getStorageClass() == VarDecl::Register)
        return Diag(OpLoc, diag::err_typecheck_address_of_register);
    } else 
      assert(0 && "Unknown/unexpected decl type");
    
    // FIXME: add check for bitfields!
  }
  // If the operand has type "type", the result has type "pointer to type".
  return new UnaryOperator(op, UnaryOperator::AddrOf, 
                           Context.getPointerType(op->getType()));
}

Action::ExprResult
Sema::CheckIndirectionOperand(Expr *op, SourceLocation OpLoc) {
  QualType qType = op->getType();

  assert(!qType.isNull() && "no type for * expression");

  QualType canonType = qType.getCanonicalType();
  
  return new UnaryOperator(op, UnaryOperator::Deref, QualType());
}

/// CheckArithmeticOperand - Check the arithmetic unary operators (C99 6.5.3.3).
Action::ExprResult
Sema::CheckArithmeticOperand(Expr *op, SourceLocation OpLoc, unsigned Opc) {
  QualType resultType = UsualUnaryConversion(op->getType());
  
  switch (Opc) {
  case UnaryOperator::Plus:
  case UnaryOperator::Minus:
    if (!resultType->isArithmeticType()) // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr, resultType);
    break;
  case UnaryOperator::Not: // bitwise complement
    if (!resultType->isIntegerType()) // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr, resultType);
    break;
  case UnaryOperator::LNot: // logical negation
    if (!resultType->isScalarType()) // C99 6.5.3.3p1
      return Diag(OpLoc, diag::err_typecheck_unary_expr, resultType);
    break;
  }
  return new UnaryOperator(op, (UnaryOperator::Opcode)Opc, resultType);
}
