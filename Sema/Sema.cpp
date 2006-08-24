//===--- Builder.cpp - AST Builder Implementation -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the actions class which builds an AST out of a parse
// stream.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Action.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Parse/Scope.h"
#include "clang/Lex/IdentifierTable.h"
#include "clang/Lex/LexerToken.h"
#include "llvm/Support/Visibility.h"
using namespace llvm;
using namespace clang;

/// ASTBuilder
namespace {
class VISIBILITY_HIDDEN ASTBuilder : public Action {
  /// FullLocInfo - If this is true, the ASTBuilder constructs AST Nodes that
  /// capture maximal location information for each source-language construct.
  bool FullLocInfo;
public:
  ASTBuilder(bool fullLocInfo) : FullLocInfo(fullLocInfo) {}
  //===--------------------------------------------------------------------===//
  // Symbol table tracking callbacks.
  //
  virtual bool isTypedefName(const IdentifierInfo &II, Scope *S) const;
  virtual void ParseDeclarator(SourceLocation Loc, Scope *S, Declarator &D,
                               ExprTy *Init);
  virtual void PopScope(SourceLocation Loc, Scope *S);
  
  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks.

  // Primary Expressions.
  virtual ExprTy *ParseSimplePrimaryExpr(const LexerToken &Tok);
  virtual ExprTy *ParseIntegerConstant(const LexerToken &Tok);
  virtual ExprTy *ParseFloatingConstant(const LexerToken &Tok);
  virtual ExprTy *ParseParenExpr(SourceLocation L, SourceLocation R,
                                 ExprTy *Val);
  
  // Binary/Unary Operators.  'Tok' is the token for the operator.
  virtual ExprTy *ParseUnaryOp(const LexerToken &Tok, ExprTy *Input);
  virtual ExprTy *ParsePostfixUnaryOp(const LexerToken &Tok, ExprTy *Input);
  
  virtual ExprTy *ParseArraySubscriptExpr(ExprTy *Base, SourceLocation LLoc,
                                          ExprTy *Idx, SourceLocation RLoc);
  virtual ExprTy *ParseMemberReferenceExpr(ExprTy *Base, SourceLocation OpLoc,
                                           tok::TokenKind OpKind,
                                           SourceLocation MemberLoc,
                                           IdentifierInfo &Member);
  
  /// ParseCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.
  virtual ExprTy *ParseCallExpr(ExprTy *Fn, SourceLocation LParenLoc,
                                ExprTy **Args, unsigned NumArgs,
                                SourceLocation *CommaLocs,
                                SourceLocation RParenLoc);
  
  
  virtual ExprTy *ParseBinOp(const LexerToken &Tok, ExprTy *LHS, ExprTy *RHS);
  
  /// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  virtual ExprTy *ParseConditionalOp(SourceLocation QuestionLoc, 
                                     SourceLocation ColonLoc,
                                     ExprTy *Cond, ExprTy *LHS, ExprTy *RHS);
};
} // end anonymous namespace


//===----------------------------------------------------------------------===//
// Symbol table tracking callbacks.
//===----------------------------------------------------------------------===//

bool ASTBuilder::isTypedefName(const IdentifierInfo &II, Scope *S) const {
  Decl *D = II.getFETokenInfo<Decl>();
  return D != 0 && D->getDeclSpecs().StorageClassSpec == DeclSpec::SCS_typedef;
}

void ASTBuilder::ParseDeclarator(SourceLocation Loc, Scope *S, Declarator &D,
                                 ExprTy *Init) {
  IdentifierInfo *II = D.getIdentifier();
  Decl *PrevDecl = II ? II->getFETokenInfo<Decl>() : 0;

  Decl *New = new Decl(II, D.getDeclSpec(), Loc, PrevDecl);
  
  // If this has an identifier, add it to the scope stack.
  if (II) {
    // If PrevDecl includes conflicting name here, emit a diagnostic.
    II->setFETokenInfo(New);
    S->AddDecl(II);
  }
}

void ASTBuilder::PopScope(SourceLocation Loc, Scope *S) {
  for (Scope::decl_iterator I = S->decl_begin(), E = S->decl_end();
       I != E; ++I) {
    IdentifierInfo &II = *static_cast<IdentifierInfo*>(*I);
    Decl *D = II.getFETokenInfo<Decl>();
    assert(D && "This decl didn't get pushed??");
    
    Decl *Next = D->getNext();

    // FIXME: Push the decl on the parent function list if in a function.
    delete D;
    
    II.setFETokenInfo(Next);
  }
}

//===--------------------------------------------------------------------===//
// Expression Parsing Callbacks.
//===--------------------------------------------------------------------===//

ASTBuilder::ExprTy *ASTBuilder::ParseSimplePrimaryExpr(const LexerToken &Tok) {
  switch (Tok.getKind()) {
  default:
    assert(0 && "Unknown simple primary expr!");
  case tok::identifier: {
    // Could be enum-constant or decl.
    //Tok.getIdentifierInfo()
    return new DeclExpr(*(Decl*)0);
  }
    
  case tok::char_constant:     // constant: character-constant
  case tok::kw___func__:       // primary-expression: __func__ [C99 6.4.2.2]
  case tok::kw___FUNCTION__:   // primary-expression: __FUNCTION__ [GNU]
  case tok::kw___PRETTY_FUNCTION__:  // primary-expression: __P..Y_F..N__ [GNU]
    assert(0 && "Unimp so far!");
    return 0;
  }
}

ASTBuilder::ExprTy *ASTBuilder::ParseIntegerConstant(const LexerToken &Tok) {
  return new IntegerConstant();
}
ASTBuilder::ExprTy *ASTBuilder::ParseFloatingConstant(const LexerToken &Tok) {
  return new FloatingConstant();
}

ASTBuilder::ExprTy *ASTBuilder::ParseParenExpr(SourceLocation L, 
                                               SourceLocation R,
                                               ExprTy *Val) {
  // FIXME: This is obviously just for testing.
  ((Expr*)Val)->dump();
  if (!FullLocInfo) return Val;
  
  return new ParenExpr(L, R, (Expr*)Val);
}

// Unary Operators.  'Tok' is the token for the operator.
ASTBuilder::ExprTy *ASTBuilder::ParseUnaryOp(const LexerToken &Tok, 
                                             ExprTy *Input) {
  UnaryOperator::Opcode Opc;
  switch (Tok.getKind()) {
  default: assert(0 && "Unknown unary op!");
  case tok::plusplus:   Opc = UnaryOperator::PreInc; break;
  case tok::minusminus: Opc = UnaryOperator::PreDec; break;
  case tok::amp:        Opc = UnaryOperator::AddrOf; break;
  case tok::star:       Opc = UnaryOperator::Deref; break;
  case tok::plus:       Opc = UnaryOperator::Plus; break;
  case tok::minus:      Opc = UnaryOperator::Minus; break;
  case tok::tilde:      Opc = UnaryOperator::Not; break;
  case tok::exclaim:    Opc = UnaryOperator::LNot; break;
  case tok::kw___real:  Opc = UnaryOperator::Real; break;
  case tok::kw___imag:  Opc = UnaryOperator::Imag; break;
  }

  if (!FullLocInfo)
    return new UnaryOperator((Expr*)Input, Opc);
  else
    return new UnaryOperatorLOC(Tok.getLocation(), (Expr*)Input, Opc);
}

ASTBuilder::ExprTy *ASTBuilder::ParsePostfixUnaryOp(const LexerToken &Tok,
                                                    ExprTy *Input) {
  UnaryOperator::Opcode Opc;
  switch (Tok.getKind()) {
  default: assert(0 && "Unknown unary op!");
  case tok::plusplus:   Opc = UnaryOperator::PostInc; break;
  case tok::minusminus: Opc = UnaryOperator::PostDec; break;
  }
  
  if (!FullLocInfo)
    return new UnaryOperator((Expr*)Input, Opc);
  else
    return new UnaryOperatorLOC(Tok.getLocation(), (Expr*)Input, Opc);
}

ASTBuilder::ExprTy *ASTBuilder::
ParseArraySubscriptExpr(ExprTy *Base, SourceLocation LLoc,
                        ExprTy *Idx, SourceLocation RLoc) {
  if (!FullLocInfo)
    return new ArraySubscriptExpr((Expr*)Base, (Expr*)Idx);
  else
    return new ArraySubscriptExprLOC((Expr*)Base, LLoc, (Expr*)Idx, RLoc);
}

ASTBuilder::ExprTy *ASTBuilder::
ParseMemberReferenceExpr(ExprTy *Base, SourceLocation OpLoc,
                         tok::TokenKind OpKind, SourceLocation MemberLoc,
                         IdentifierInfo &Member) {
  if (!FullLocInfo)
    return new MemberExpr((Expr*)Base, OpKind == tok::arrow, Member);
  else
    return new MemberExprLOC((Expr*)Base, OpLoc, OpKind == tok::arrow,
                             MemberLoc, Member);
}

/// ParseCallExpr - Handle a call to Fn with the specified array of arguments.
/// This provides the location of the left/right parens and a list of comma
/// locations.
ASTBuilder::ExprTy *ASTBuilder::
ParseCallExpr(ExprTy *Fn, SourceLocation LParenLoc,
              ExprTy **Args, unsigned NumArgs,
              SourceLocation *CommaLocs, SourceLocation RParenLoc) {
  if (!FullLocInfo)
    return new CallExpr((Expr*)Fn, (Expr**)Args, NumArgs);
  else
    return new CallExprLOC((Expr*)Fn, LParenLoc, (Expr**)Args, NumArgs,
                           CommaLocs, RParenLoc);
}


// Binary Operators.  'Tok' is the token for the operator.
ASTBuilder::ExprTy *ASTBuilder::ParseBinOp(const LexerToken &Tok, ExprTy *LHS,
                                           ExprTy *RHS) {
  BinaryOperator::Opcode Opc;
  switch (Tok.getKind()) {
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
  
  if (!FullLocInfo)
    return new BinaryOperator((Expr*)LHS, (Expr*)RHS, Opc);
  else
    return new BinaryOperatorLOC((Expr*)LHS, Tok.getLocation(), (Expr*)RHS,Opc);
}

/// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
/// in the case of a the GNU conditional expr extension.
ASTBuilder::ExprTy *ASTBuilder::ParseConditionalOp(SourceLocation QuestionLoc, 
                                                   SourceLocation ColonLoc,
                                                   ExprTy *Cond, ExprTy *LHS,
                                                   ExprTy *RHS) {
  if (!FullLocInfo)
    return new ConditionalOperator((Expr*)Cond, (Expr*)LHS, (Expr*)RHS);
  else
    return new ConditionalOperatorLOC((Expr*)Cond, QuestionLoc, (Expr*)LHS,
                                      ColonLoc, (Expr*)RHS);
}


/// Interface to the Builder.cpp file.
///
Action *CreateASTBuilderActions(bool FullLocInfo) {
  return new ASTBuilder(FullLocInfo);
}



