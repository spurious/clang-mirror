//===--- Action.h - Parser Action Interface ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Action and EmptyAction interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_ACTION_H
#define LLVM_CLANG_PARSE_ACTION_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TokenKinds.h"

namespace llvm {
namespace clang {
  // Semantic.
  class Declarator;
  // Parse.
  class Scope;
  class Action;
  // Lex.
  class IdentifierInfo;
  class LexerToken;

/// Action - As the parser reads the input file and recognizes the productions
/// of the grammar, it invokes methods on this class to turn the parsed input
/// into something useful: e.g. a parse tree.
///
/// The callback methods that this class provides are phrased as actions that
/// the parser has just done or is about to do when the method is called.  They
/// are not requests that the actions module do the specified action.
///
/// All of the methods here are optional except isTypedefName(), which must be
/// specified in order for the parse to complete accurately.  The EmptyAction
/// class does this bare-minimum of tracking to implement this functionality.
class Action {
public:
  /// Out-of-line virtual destructor to provide home for this class.
  virtual ~Action();
  
  // Types - Though these don't actually enforce strong typing, they document
  // what types are required to be identical for the actions.
  typedef void ExprTy;
  typedef void StmtTy;
  typedef void DeclTy;
  typedef void TypeTy;
  
  /// ActionResult - This structure is used while parsing/acting on expressions,
  /// stmts, etc.  It encapsulates both the object returned by the action, plus
  /// a sense of whether or not it is valid.
  template<unsigned UID>
  struct ActionResult {
    void *Val;
    bool isInvalid;
    
    ActionResult(bool Invalid = false) : Val(0), isInvalid(Invalid) {}
    template<typename ActualExprTy>
    ActionResult(ActualExprTy *val) : Val(val), isInvalid(false) {}
    
    const ActionResult &operator=(void *RHS) {
      Val = RHS;
      isInvalid = false;
      return *this;
    }
  };

  /// Expr/StmtResult - Provide a unique type to wrap ExprTy/StmtTy, etc,
  /// providing strong typing and allowing for failure.
  typedef ActionResult<0> ExprResult;
  typedef ActionResult<1> StmtResult;
  
  //===--------------------------------------------------------------------===//
  // Symbol Table Tracking Callbacks.
  //===--------------------------------------------------------------------===//
  
  /// isTypedefName - Return true if the specified identifier is a typedef name
  /// in the current scope.
  virtual bool isTypedefName(const IdentifierInfo &II, Scope *S) const = 0;
  
  /// ParseDeclarator - This callback is invoked when a declarator is parsed and
  /// 'Init' specifies the initializer if any.  This is for things like:
  /// "int X = 4" or "typedef int foo".
  ///
  /// LastInGroup is non-null for cases where one declspec has multiple
  /// declarators on it.  For example in 'int A, B', ParseDeclarator will be
  /// called with LastInGroup=A when invoked for B.
  virtual DeclTy *ParseDeclarator(Scope *S, Declarator &D,
                                  ExprTy *Init, DeclTy *LastInGroup) {
    return 0;
  }

  /// ParseFunctionDefinition - This is called when a function definition is
  /// parsed.  The declarator that is part of this is not passed to
  /// ParseDeclarator.
  virtual DeclTy *ParseFunctionDefinition(Scope *S, Declarator &D,
                                          // TODO: FORMAL ARG INFO.
                                          StmtTy *Body) {
    return 0;
  }

  /// PopScope - This callback is called immediately before the specified scope
  /// is popped and deleted.
  virtual void PopScope(SourceLocation Loc, Scope *S) {}
  
  //===--------------------------------------------------------------------===//
  // 'External Declaration' (Top Level) Parsing Callbacks.
  //===--------------------------------------------------------------------===//
  
  //===--------------------------------------------------------------------===//
  // Statement Parsing Callbacks.
  //===--------------------------------------------------------------------===//
  
  virtual StmtResult ParseCompoundStmt(SourceLocation L, SourceLocation R,
                                       StmtTy **Elts, unsigned NumElts) {
    return 0;
  }
  virtual StmtResult ParseExprStmt(ExprTy *Expr) {
    return 0;
  }
  
  /// ParseCaseStmt - Note that this handles the GNU 'case 1 ... 4' extension,
  /// which can specify an RHS value.
  virtual StmtResult ParseCaseStmt(SourceLocation CaseLoc, ExprTy *LHSVal,
                                   SourceLocation DotDotDotLoc, ExprTy *RHSVal,
                                   SourceLocation ColonLoc, StmtTy *SubStmt) {
    return 0;
  }
  virtual StmtResult ParseDefaultStmt(SourceLocation DefaultLoc,
                                      SourceLocation ColonLoc, StmtTy *SubStmt){
    return 0;
  }
  
  virtual StmtResult ParseLabelStmt(SourceLocation IdentLoc, IdentifierInfo *II,
                                    SourceLocation ColonLoc, StmtTy *SubStmt) {
    return 0;
  }
  
  virtual StmtResult ParseIfStmt(SourceLocation IfLoc, ExprTy *CondVal,
                                 StmtTy *ThenVal, SourceLocation ElseLoc,
                                 StmtTy *ElseVal) {
    return 0; 
  }
  
  virtual StmtResult ParseSwitchStmt(SourceLocation SwitchLoc, ExprTy *Cond,
                                     StmtTy *Body) {
    return 0;
  }
  virtual StmtResult ParseWhileStmt(SourceLocation WhileLoc, ExprTy *Cond,
                                     StmtTy *Body) {
    return 0;
  }
  virtual StmtResult ParseDoStmt(SourceLocation DoLoc, StmtTy *Body,
                                 SourceLocation WhileLoc, ExprTy *Cond) {
    return 0;
  }
  // PARSE FOR STMT.
  virtual StmtResult ParseGotoStmt(SourceLocation GotoLoc,
                                   SourceLocation LabelLoc,
                                   IdentifierInfo *LabelII) {
    return 0;
  }
  virtual StmtResult ParseContinueStmt(SourceLocation GotoLoc) {
    return 0;
  }
  virtual StmtResult ParseBreakStmt(SourceLocation GotoLoc) {
    return 0;
  }
  virtual StmtResult ParseIndirectGotoStmt(SourceLocation GotoLoc,
                                           SourceLocation StarLoc,
                                           ExprTy *DestExp) {
    return 0;
  }
  virtual StmtResult ParseReturnStmt(SourceLocation ReturnLoc,
                                           ExprTy *RetValExp) {
    return 0;
  }
  
  
  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks.
  //===--------------------------------------------------------------------===//
  
  // Primary Expressions.
  virtual ExprResult ParseSimplePrimaryExpr(const LexerToken &Tok) { return 0; }
  virtual ExprResult ParseIntegerConstant(const LexerToken &Tok) { return 0; }
  virtual ExprResult ParseFloatingConstant(const LexerToken &Tok) { return 0; }
  virtual ExprResult ParseParenExpr(SourceLocation L, SourceLocation R,
                                    ExprTy *Val) {
    return Val;  // Default impl returns operand.
  }
  
  /// ParseStringExpr - The (null terminated) string data is specified with
  /// StrData+StrLen.  isWide is true if this is a wide string. The Toks/NumToks
  /// array exposes the input tokens to provide location information.
  virtual ExprResult ParseStringExpr(const char *StrData, unsigned StrLen,
                                     bool isWide,
                                     const LexerToken *Toks, unsigned NumToks) {
    return 0;
  }

  // Postfix Expressions.
  virtual ExprResult ParsePostfixUnaryOp(const LexerToken &Tok, ExprTy *Input) {
    return 0;
  }
  virtual ExprResult ParseArraySubscriptExpr(ExprTy *Base, SourceLocation LLoc,
                                             ExprTy *Idx, SourceLocation RLoc) {
    return 0;
  }
  virtual ExprResult ParseMemberReferenceExpr(ExprTy *Base,SourceLocation OpLoc,
                                              tok::TokenKind OpKind,
                                              SourceLocation MemberLoc,
                                              IdentifierInfo &Member) {
    return 0;
  }
  
  /// ParseCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.  There are guaranteed to be one fewer commas than arguments,
  /// unless there are zero arguments.
  virtual ExprResult ParseCallExpr(ExprTy *Fn, SourceLocation LParenLoc,
                                   ExprTy **Args, unsigned NumArgs,
                                   SourceLocation *CommaLocs,
                                   SourceLocation RParenLoc) {
    return 0;
  }
  
  // Unary Operators.  'Tok' is the token for the operator.
  virtual ExprResult ParseUnaryOp(SourceLocation OpLoc, tok::TokenKind Op,
                                  ExprTy *Input) {
    return 0;
  }
  virtual ExprResult 
    ParseSizeOfAlignOfTypeExpr(SourceLocation OpLoc, bool isSizeof, 
                               SourceLocation LParenLoc, TypeTy *Ty,
                               SourceLocation RParenLoc) {
    return 0;
  }
  
  virtual ExprResult ParseCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
                                   SourceLocation RParenLoc, ExprTy *Op) {
    return 0;
  }
  
  virtual ExprResult ParseBinOp(const LexerToken &Tok,
                                ExprTy *LHS, ExprTy *RHS) {
    return 0;
  }

  /// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  virtual ExprResult ParseConditionalOp(SourceLocation QuestionLoc, 
                                        SourceLocation ColonLoc,
                                        ExprTy *Cond, ExprTy *LHS, ExprTy *RHS){
    return 0;
  }
};


/// EmptyAction - This is a simple (bare-minimum) implementation of the Action
/// class, which only keeps track of which typedefs are in-scope.  This class is
/// useful to subclass if clients want to implement some actions without having
/// to reimplement all of the scoping rules.
class EmptyAction : public Action {
public:
  /// isTypedefName - This looks at the IdentifierInfo::FETokenInfo field to
  /// determine whether the name is a typedef or not in this scope.
  virtual bool isTypedefName(const IdentifierInfo &II, Scope *S) const;
  
  /// ParseDeclarator - If this is a typedef declarator, we modify the
  /// IdentifierInfo::FETokenInfo field to keep track of this fact, until S is
  /// popped.
  virtual DeclTy *ParseDeclarator(Scope *S, Declarator &D, ExprTy *Init,
                                  DeclTy *LastInGroup);
  
  /// PopScope - When a scope is popped, if any typedefs are now out-of-scope,
  /// they are removed from the IdentifierInfo::FETokenInfo field.
  virtual void PopScope(SourceLocation Loc, Scope *S);
};
  
}  // end namespace clang
}  // end namespace llvm

#endif
