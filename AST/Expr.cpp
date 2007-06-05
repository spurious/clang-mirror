//===--- Expr.cpp - Expression AST Node Implementation --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Expr class and subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Expr.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Lex/IdentifierTable.h"
using namespace llvm;
using namespace clang;

//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

StringLiteral::StringLiteral(const char *strData, unsigned byteLength, 
                             bool Wide, QualType t, SourceLocation firstLoc,
                             SourceLocation lastLoc) : 
  Expr(StringLiteralClass, t) {
  // OPTIMIZE: could allocate this appended to the StringLiteral.
  char *AStrData = new char[byteLength];
  memcpy(AStrData, strData, byteLength);
  StrData = AStrData;
  ByteLength = byteLength;
  IsWide = Wide;
  firstTokLoc = firstLoc;
  lastTokLoc = lastLoc;
}

StringLiteral::~StringLiteral() {
  delete[] StrData;
}

bool UnaryOperator::isPostfix(Opcode Op) {
  switch (Op) {
  case PostInc:
  case PostDec:
    return true;
  default:
    return false;
  }
}

/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "sizeof" or "[pre]++".
const char *UnaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
  default: assert(0 && "Unknown unary operator");
  case PostInc: return "++";
  case PostDec: return "--";
  case PreInc:  return "++";
  case PreDec:  return "--";
  case AddrOf:  return "&";
  case Deref:   return "*";
  case Plus:    return "+";
  case Minus:   return "-";
  case Not:     return "~";
  case LNot:    return "!";
  case Real:    return "__real";
  case Imag:    return "__imag";
  case SizeOf:  return "sizeof";
  case AlignOf: return "alignof";
  case Extension: return "__extension__";
  }
}

//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

CallExpr::CallExpr(Expr *fn, Expr **args, unsigned numargs, QualType t,
                   SourceLocation l)
  : Expr(CallExprClass, t), Fn(fn), NumArgs(numargs) {
  Args = new Expr*[numargs];
  for (unsigned i = 0; i != numargs; ++i)
    Args[i] = args[i];
  Loc = l;
}

/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "<<=".
const char *BinaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
  default: assert(0 && "Unknown binary operator");
  case Mul:       return "*";
  case Div:       return "/";
  case Rem:       return "%";
  case Add:       return "+";
  case Sub:       return "-";
  case Shl:       return "<<";
  case Shr:       return ">>";
  case LT:        return "<";
  case GT:        return ">";
  case LE:        return "<=";
  case GE:        return ">=";
  case EQ:        return "==";
  case NE:        return "!=";
  case And:       return "&";
  case Xor:       return "^";
  case Or:        return "|";
  case LAnd:      return "&&";
  case LOr:       return "||";
  case Assign:    return "=";
  case MulAssign: return "*=";
  case DivAssign: return "/=";
  case RemAssign: return "%=";
  case AddAssign: return "+=";
  case SubAssign: return "-=";
  case ShlAssign: return "<<=";
  case ShrAssign: return ">>=";
  case AndAssign: return "&=";
  case XorAssign: return "^=";
  case OrAssign:  return "|=";
  case Comma:     return ",";
  }
}

/// isLvalue - C99 6.3.2.1: an lvalue is an expression with an object type or an
/// incomplete type other than void. Nonarray expressions that can be lvalues:
///  - name, where name must be a variable
///  - e[i]
///  - (e), where e must be an lvalue
///  - e.name, where e must be an lvalue
///  - e->name
///  - *e, the type of e cannot be a function type
///  - string-constant
///
Expr::isLvalueResult Expr::isLvalue() {
  // first, check the type (C99 6.3.2.1)
  if (isa<FunctionType>(TR.getCanonicalType())) // from isObjectType()
    return LV_NotObjectType;
  if (TR->isIncompleteType() && TR->isVoidType())
    return LV_IncompleteVoidType;
  
  // the type looks fine, now check the expression
  switch (getStmtClass()) {
  case StringLiteralClass: // C99 6.5.1p4
  case ArraySubscriptExprClass: // C99 6.5.3p4 (e1[e2] == (*((e1)+(e2))))
    return LV_Valid;
  case DeclRefExprClass: // C99 6.5.1p2
    if (isa<VarDecl>(cast<DeclRefExpr>(this)->getDecl()))
      return LV_Valid;
    break;
  case MemberExprClass: // C99 6.5.2.3p4
    const MemberExpr *m = cast<MemberExpr>(this);
    return m->isArrow() ? LV_Valid : m->getBase()->isLvalue();
  case UnaryOperatorClass: // C99 6.5.3p4
    if (cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::Deref)
      return LV_Valid;
    break;
  case ParenExprClass: // C99 6.5.1p5
    return cast<ParenExpr>(this)->getSubExpr()->isLvalue();
  default:
    break;
  }
  return LV_InvalidExpression;
}

/// isModifiableLvalue - C99 6.3.2.1: an lvalue that does not have array type,
/// does not have an incomplete type, does not have a const-qualified type, and
/// if it is a structure or union, does not have any member (including, 
/// recursively, any member or element of all contained aggregates or unions)
/// with a const-qualified type.
Expr::isModifiableLvalueResult Expr::isModifiableLvalue() {
  isLvalueResult lvalResult = isLvalue();
    
  switch (lvalResult) {
    case LV_Valid: break;
    case LV_NotObjectType: return MLV_NotObjectType;
    case LV_IncompleteVoidType: return MLV_IncompleteVoidType;
    case LV_InvalidExpression: return MLV_InvalidExpression;
  }
  if (TR.isConstQualified())
    return MLV_ConstQualified;
  if (TR->isArrayType())
    return MLV_ArrayType;
  if (TR->isIncompleteType())
    return MLV_IncompleteType;
    
  if (const RecordType *r = dyn_cast<RecordType>(TR.getCanonicalType())) {
    if (r->hasConstFields()) 
      return MLV_ConstQualified;
  }
  return MLV_Valid;    
}

/// isIntegerConstantExpr - this recursive routine will test if an expression is
/// an integer constant expression. Note: With the introduction of VLA's in
/// C99 the result of the sizeof operator is no longer always a constant
/// expression. The generalization of the wording to include any subexpression
/// that is not evaluated (C99 6.6p3) means that nonconstant subexpressions
/// can appear as operands to other operators (e.g. &&, ||, ?:). For instance,
/// "0 || f()" can be treated as a constant expression. In C90 this expression,
/// occurring in a context requiring a constant, would have been a constraint
/// violation. FIXME: This routine currently implements C90 semantics.
/// To properly implement C99 semantics this routine will need to evaluate
/// expressions involving operators previously mentioned.

/// FIXME: Pass up a reason why! Invalid operation in i-c-e, division by zero,
/// comma, etc
bool Expr::isIntegerConstantExpr(APSInt &Result, SourceLocation *Loc,
                                 bool isEvaluated) const {
  switch (getStmtClass()) {
  default:
    if (Loc) *Loc = getLocStart();
    return false;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->
                     isIntegerConstantExpr(Result, Loc, isEvaluated);
  case IntegerLiteralClass:
    Result = cast<IntegerLiteral>(this)->getValue();
    break;
  case CharacterLiteralClass:
    // FIXME: This doesn't set the right width etc.
    Result.zextOrTrunc(32);  // FIXME: NOT RIGHT IN GENERAL.
    Result = cast<IntegerLiteral>(this)->getValue();
    break;
  case DeclRefExprClass:
    if (const EnumConstantDecl *D = 
          dyn_cast<EnumConstantDecl>(cast<DeclRefExpr>(this)->getDecl())) {
      D = D;
      // FIXME: Get the real assigned value and width.
      Result.zextOrTrunc(32);  // FIXME: NOT RIGHT IN GENERAL.
      Result = cast<IntegerLiteral>(this)->getValue();
      break;
    }
    if (Loc) *Loc = getLocStart();
    return false;
  case UnaryOperatorClass: {
    const UnaryOperator *Exp = cast<UnaryOperator>(this);
    
    // Get the operand value.  If this is sizeof/alignof, do not evalute the
    // operand.  This affects C99 6.6p3.
    if (Exp->isSizeOfAlignOfOp()) isEvaluated = false;
    if (!Exp->getSubExpr()->isIntegerConstantExpr(Result, Loc, isEvaluated))
      return false;

    switch (Exp->getOpcode()) {
    // Address, indirect, pre/post inc/dec, etc are not valid constant exprs.
    // See C99 6.6p3.
    default:
      if (Loc) *Loc = Exp->getOperatorLoc();
      return false;
    case UnaryOperator::Extension:
      return true;
    case UnaryOperator::SizeOf:
    case UnaryOperator::AlignOf:
      // sizeof(vla) is not a constantexpr: C99 6.5.3.4p2.
      if (!Exp->getSubExpr()->getType()->isConstantSizeType(Loc))
        return false;
      
      // FIXME: Evaluate sizeof/alignof.
      Result.zextOrTrunc(32);  // FIXME: NOT RIGHT IN GENERAL.
      Result = 1;  // FIXME: Obviously bogus
      break;
    case UnaryOperator::LNot: {
      bool Val = Result != 0;
      Result.zextOrTrunc(32);  // FIXME: NOT RIGHT IN GENERAL.
      Result = Val;
      break;
    }
    case UnaryOperator::Plus:
      // FIXME: Do usual unary promotions here!
      break;
    case UnaryOperator::Minus:
      // FIXME: Do usual unary promotions here!
      Result = -Result;
      break;
    case UnaryOperator::Not:
      // FIXME: Do usual unary promotions here!
      Result = ~Result;
      break;
    }
    break;
  }
  case SizeOfAlignOfTypeExprClass: {
    const SizeOfAlignOfTypeExpr *Exp = cast<SizeOfAlignOfTypeExpr>(this);
    // alignof always evaluates to a constant.
    if (Exp->isSizeOf() && !Exp->getArgumentType()->isConstantSizeType(Loc))
      return false;

    // FIXME: Evaluate sizeof/alignof.
    Result.zextOrTrunc(32);  // FIXME: NOT RIGHT IN GENERAL.
    Result = 1;  // FIXME: Obviously bogus
    break;
  }
  case BinaryOperatorClass: {
    const BinaryOperator *Exp = cast<BinaryOperator>(this);
    
    // The LHS of a constant expr is always evaluated and needed.
    if (!Exp->getLHS()->isIntegerConstantExpr(Result, Loc, isEvaluated))
      return false;
    
    APSInt RHS(Result);
    
    // The short-circuiting &&/|| operators don't necessarily evaluate their
    // RHS.  Make sure to pass isEvaluated down correctly.
    if (Exp->isLogicalOp()) {
      bool RHSEval;
      if (Exp->getOpcode() == BinaryOperator::LAnd)
        RHSEval = Result != 0;
      else {
        assert(Exp->getOpcode() == BinaryOperator::LOr &&"Unexpected logical");
        RHSEval = Result == 0;
      }
      
      if (!Exp->getRHS()->isIntegerConstantExpr(RHS, Loc,
                                                isEvaluated & RHSEval))
        return false;
    } else {
      if (!Exp->getRHS()->isIntegerConstantExpr(RHS, Loc, isEvaluated))
        return false;
    }
    
    // FIXME: These should all do the standard promotions, etc.
    switch (Exp->getOpcode()) {
    default:
      if (Loc) *Loc = getLocStart();
      return false;
    case BinaryOperator::Mul:
      Result *= RHS;
      break;
    case BinaryOperator::Div:
      if (RHS == 0) {
        if (!isEvaluated) break;
        if (Loc) *Loc = getLocStart();
        return false;
      }
      Result /= RHS;
      break;
    case BinaryOperator::Rem:
      if (RHS == 0) {
        if (!isEvaluated) break;
        if (Loc) *Loc = getLocStart();
        return false;
      }
      Result %= RHS;
      break;
    case BinaryOperator::Add: Result += RHS; break;
    case BinaryOperator::Sub: Result -= RHS; break;
    case BinaryOperator::Shl: Result <<= RHS.getLimitedValue(); break;
    case BinaryOperator::Shr: Result >>= RHS.getLimitedValue(); break;
    case BinaryOperator::LT:  Result = Result < RHS; break;
    case BinaryOperator::GT:  Result = Result > RHS; break;
    case BinaryOperator::LE:  Result = Result <= RHS; break;
    case BinaryOperator::GE:  Result = Result >= RHS; break;
    case BinaryOperator::EQ:  Result = Result == RHS; break;
    case BinaryOperator::NE:  Result = Result != RHS; break;
    case BinaryOperator::And: Result &= RHS; break;
    case BinaryOperator::Xor: Result ^= RHS; break;
    case BinaryOperator::Or:  Result |= RHS; break;
    case BinaryOperator::LAnd:
      Result = Result != 0 && RHS != 0;
      break;
    case BinaryOperator::LOr:
      Result = Result != 0 || RHS != 0;
      break;
      
    case BinaryOperator::Comma:
      // C99 6.6p3: "shall not contain assignment, ..., or comma operators,
      // *except* when they are contained within a subexpression that is not
      // evaluated".  Note that Assignment can never happen due to constraints
      // on the LHS subexpr, so we don't need to check it here.
      if (isEvaluated) {
        if (Loc) *Loc = getLocStart();
        return false;
      }
      
      // The result of the constant expr is the RHS.
      Result = RHS;
      return true;
    }
    
    assert(!Exp->isAssignmentOp() && "LHS can't be a constant expr!");
    break;
  }
  case CastExprClass: {
    const CastExpr *Exp = cast<CastExpr>(this);    
    // C99 6.6p6: shall only convert arithmetic types to integer types.
    if (!Exp->getSubExpr()->getType()->isArithmeticType() ||
        !Exp->getDestType()->isIntegerType()) {
      if (Loc) *Loc = Exp->getSubExpr()->getLocStart();
      return false;
    }
      
    // Handle simple integer->integer casts.
    if (Exp->getSubExpr()->getType()->isIntegerType()) {
      if (!Exp->getSubExpr()->isIntegerConstantExpr(Result, Loc, isEvaluated))
        return false;
      // FIXME: do the conversion on Result.
      break;
    }
    
    // Allow floating constants that are the immediate operands of casts or that
    // are parenthesized.
    const Expr *Operand = Exp->getSubExpr();
    while (const ParenExpr *PE = dyn_cast<ParenExpr>(Operand))
      Operand = PE->getSubExpr();
    
    if (const FloatingLiteral *FL = dyn_cast<FloatingLiteral>(Operand)) {
      // FIXME: Evaluate this correctly!
      Result = (int)FL->getValue();
      break;
    }
    if (Loc) *Loc = getLocStart();
    return false;
  }
  case ConditionalOperatorClass: {
    const ConditionalOperator *Exp = cast<ConditionalOperator>(this);
    
    if (!Exp->getCond()->isIntegerConstantExpr(Result, Loc, isEvaluated))
      return false;
    
    const Expr *TrueExp  = Exp->getLHS();
    const Expr *FalseExp = Exp->getRHS();
    if (Result == 0) std::swap(TrueExp, FalseExp);
    
    // Evaluate the false one first, discard the result.
    if (!FalseExp->isIntegerConstantExpr(Result, Loc, false))
      return false;
    // Evalute the true one, capture the result.
    if (!TrueExp->isIntegerConstantExpr(Result, Loc, isEvaluated))
      return false;
    // FIXME: promotions on result.
    break;
  }
  }

  // Cases that are valid constant exprs fall through to here.
  Result.setIsUnsigned(getType()->isUnsignedIntegerType());
  return true;
}


/// isNullPointerConstant - C99 6.3.2.3p3 -  Return true if this is either an
/// integer constant expression with the value zero, or if this is one that is
/// cast to void*.
bool Expr::isNullPointerConstant() const {
  // Strip off a cast to void*, if it exists.
  if (const CastExpr *CE = dyn_cast<CastExpr>(this)) {
    // Check that it is a cast to void*.
    if (const PointerType *PT = dyn_cast<PointerType>(CE->getType())) {
      QualType Pointee = PT->getPointeeType();
      if (Pointee.getQualifiers() == 0 && Pointee->isVoidType() && // to void*
          CE->getSubExpr()->getType()->isIntegerType())            // from int.
        return CE->getSubExpr()->isNullPointerConstant();
    }
  } else if (const ParenExpr *PE = dyn_cast<ParenExpr>(this)) {
    // Accept ((void*)0) as a null pointer constant, as many other
    // implementations do.
    return PE->getSubExpr()->isNullPointerConstant();
  }
  
  // This expression must be an integer type.
  if (!getType()->isIntegerType())
    return false;
  
  // FIXME: If we have an integer constant expression, we need
  // to *evaluate* it and test for the value 0. The current code is too 
  // simplistic...it only allows for the integer literal "0". 
  // For example, the following is valid code:
  //
  //  void test1() { *(n ? p : (void *)(7-7)) = 1; }
  //
  if (const IntegerLiteral *C = dyn_cast<IntegerLiteral>(this))
    return C->getValue() == 0;
  return false;
}
