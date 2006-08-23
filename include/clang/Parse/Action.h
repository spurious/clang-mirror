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
  typedef void DeclTy;
  
  //===--------------------------------------------------------------------===//
  // Symbol Table Tracking Callbacks.
  //===--------------------------------------------------------------------===//
  
  /// isTypedefName - Return true if the specified identifier is a typedef name
  /// in the current scope.
  virtual bool isTypedefName(const IdentifierInfo &II, Scope *S) const = 0;
  
  /// ParseDeclarator - This callback is invoked when a declarator is parsed and
  /// 'Init' specifies the initializer if any.  This is for things like:
  /// "int X = 4" or "typedef int foo".
  virtual void ParseDeclarator(SourceLocation Loc, Scope *S, Declarator &D,
                               ExprTy *Init) {}
  
  /// PopScope - This callback is called immediately before the specified scope
  /// is popped and deleted.
  virtual void PopScope(SourceLocation Loc, Scope *S) {}
  
  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks.
  //===--------------------------------------------------------------------===//
  
  // Primary Expressions.
  virtual ExprTy *ParseIntegerConstant(const LexerToken &Tok) { return 0; }
  virtual ExprTy *ParseFloatingConstant(const LexerToken &Tok) { return 0; }

  virtual ExprTy *ParseParenExpr(SourceLocation L, SourceLocation R,
                                 ExprTy *Val) {
    return Val;
  }

  // Binary/Unary Operators.  'Tok' is the token for the operator.
  virtual ExprTy *ParseUnaryOp(const LexerToken &Tok, ExprTy *Input) {
    return 0;
  }
  virtual ExprTy *ParsePostfixUnaryOp(const LexerToken &Tok, ExprTy *Input) {
    return 0;
  }
  virtual ExprTy *ParseBinOp(const LexerToken &Tok, ExprTy *LHS, ExprTy *RHS) {
    return 0;
  }

  /// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  virtual ExprTy *ParseConditionalOp(SourceLocation QuestionLoc, 
                                     SourceLocation ColonLoc,
                                     ExprTy *Cond, ExprTy *LHS, ExprTy *RHS) {
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
  virtual void ParseDeclarator(SourceLocation Loc, Scope *S, Declarator &D,
                               ExprTy *Init);
  
  /// PopScope - When a scope is popped, if any typedefs are now out-of-scope,
  /// they are removed from the IdentifierInfo::FETokenInfo field.
  virtual void PopScope(SourceLocation Loc, Scope *S);
};
  
}  // end namespace clang
}  // end namespace llvm

#endif
