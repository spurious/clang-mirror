//===--- Scope.h - Scope interface ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Scope interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_SCOPE_H
#define LLVM_CLANG_PARSE_SCOPE_H

#include "clang/Parse/Action.h"
#include "llvm/ADT/SmallPtrSet.h"

namespace clang {

/// Scope - A scope is a transient data structure that is used while parsing the
/// program.  It assists with resolving identifiers to the appropriate
/// declaration.
///
class Scope {
public:
  /// ScopeFlags - These are bitfields that are or'd together when creating a
  /// scope, which defines the sorts of things the scope contains.
  enum ScopeFlags {
    /// FnScope - This indicates that the scope corresponds to a function, which
    /// means that labels are set here.
    FnScope       = 0x01,
    
    /// BreakScope - This is a while,do,switch,for, etc that can have break
    /// stmts embedded into it.
    BreakScope    = 0x02,
    
    /// ContinueScope - This is a while,do,for, which can have continue
    /// stmt embedded into it.
    ContinueScope = 0x04,
    
    /// DeclScope - This is a scope that can contain a declaration.  Some scopes
    /// just contain loop constructs but don't contain decls.
    DeclScope = 0x08,

    /// ControlScope - The controlling scope in a if/switch/while/for statement.
    ControlScope = 0x10,

    /// CXXClassScope - The scope of a C++ struct/union/class definition.
    CXXClassScope = 0x20,
    
    /// BlockScope - This is a scope that corresponds to a block object.
    /// Blocks serve as top-level scopes for some objects like labels, they
    /// also prevent things like break and continue.  BlockScopes have the
    /// other flags set as well.
    BlockScope = 0x40,

    /// TemplateParamScope - This is a scope that corresponds to the
    /// template parameters of a C++ template. Template parameter
    /// scope starts at the 'template' keyword and ends when the
    /// template declaration ends.
    TemplateParamScope = 0x80
  };
private:
  /// The parent scope for this scope.  This is null for the translation-unit
  /// scope.
  Scope *AnyParent;
  
  /// Depth - This is the depth of this scope.  The translation-unit scope has
  /// depth 0.
  unsigned Depth : 16;
  
  /// Flags - This contains a set of ScopeFlags, which indicates how the scope
  /// interrelates with other control flow statements.
  unsigned Flags : 8;
  
  /// FnParent - If this scope has a parent scope that is a function body, this
  /// pointer is non-null and points to it.  This is used for label processing.
  Scope *FnParent;
  
  /// BreakParent/ContinueParent - This is a direct link to the immediately
  /// preceeding BreakParent/ContinueParent if this scope is not one, or null if
  /// there is no containing break/continue scope.
  Scope *BreakParent, *ContinueParent;

  /// BlockParent - This is a direct link to the immediately containing
  /// BlockScope if this scope is not one, or null if there is none.
  Scope *BlockParent;

  /// TemplateParamParent - This is a direct link to the
  /// immediately containing template parameter scope. In the
  /// case of nested templates, template parameter scopes can have
  /// other template parameter scopes as parents.
  Scope *TemplateParamParent;

  /// DeclsInScope - This keeps track of all declarations in this scope.  When
  /// the declaration is added to the scope, it is set as the current
  /// declaration for the identifier in the IdentifierTable.  When the scope is
  /// popped, these declarations are removed from the IdentifierTable's notion
  /// of current declaration.  It is up to the current Action implementation to
  /// implement these semantics.
  typedef llvm::SmallPtrSet<Action::DeclTy*, 32> DeclSetTy;
  DeclSetTy DeclsInScope;
  
public:
  Scope(Scope *Parent, unsigned ScopeFlags) {
    Init(Parent, ScopeFlags);
  }

  /// getFlags - Return the flags for this scope.
  ///
  unsigned getFlags() const { return Flags; }

  /// isBlockScope - Return true if this scope does not correspond to a
  /// closure.
  bool isBlockScope() const { return Flags & BlockScope; }

  /// getParent - Return the scope that this is nested in.
  ///
  const Scope *getParent() const { return AnyParent; }
  Scope *getParent() { return AnyParent; }

  /// getFnParent - Return the closest scope that is a function body.
  ///
  const Scope *getFnParent() const { return FnParent; }
  Scope *getFnParent() { return FnParent; }
  
  /// getContinueParent - Return the closest scope that a continue statement
  /// would be affected by.  If the closest scope is a closure scope, we know 
  /// that there is no loop *inside* the closure.
  Scope *getContinueParent() {
    if (ContinueParent && !ContinueParent->isBlockScope())
      return ContinueParent;
    return 0;
  }

  const Scope *getContinueParent() const {
    return const_cast<Scope*>(this)->getContinueParent();
  }
  
  /// getBreakParent - Return the closest scope that a break statement
  /// would be affected by.  If the closest scope is a block scope, we know 
  /// that there is no loop *inside* the block.
  Scope *getBreakParent() {
    if (BreakParent && !BreakParent->isBlockScope())
      return BreakParent;
    return 0;
  }
  const Scope *getBreakParent() const {
    return const_cast<Scope*>(this)->getBreakParent();
  }
 
  Scope *getBlockParent() { return BlockParent; }
  const Scope *getBlockParent() const { return BlockParent; }  

  Scope *getTemplateParamParent() { return TemplateParamParent; }
  const Scope *getTemplateParamParent() const { return TemplateParamParent; }  
 
  typedef DeclSetTy::iterator decl_iterator;
  decl_iterator decl_begin() const { return DeclsInScope.begin(); }
  decl_iterator decl_end()   const { return DeclsInScope.end(); }
  bool decl_empty()          const { return DeclsInScope.empty(); }

  void AddDecl(Action::DeclTy *D) {
    DeclsInScope.insert(D);
  }

  /// isDeclScope - Return true if this is the scope that the specified decl is
  /// declared in.
  bool isDeclScope(Action::DeclTy *D) {
    return DeclsInScope.count(D) != 0;
  }

  /// isCXXClassScope - Return true if this scope is a C++ class scope.
  bool isCXXClassScope() const {
    return (getFlags() & Scope::CXXClassScope);
  }

  /// isInCXXInlineMethodScope - Return true if this scope is a C++ inline
  /// method scope or is inside one.
  bool isInCXXInlineMethodScope() const {
    if (const Scope *FnS = getFnParent()) {
      assert(FnS->getParent() && "TUScope not created?");
      return FnS->getParent()->isCXXClassScope();
    }
    return false;
  }
  
  /// isTemplateParamScope - Return true if this scope is a C++
  /// template parameter scope.
  bool isTemplateParamScope() const {
    return getFlags() & Scope::TemplateParamScope;
  }

  /// Init - This is used by the parser to implement scope caching.
  ///
  void Init(Scope *Parent, unsigned ScopeFlags) {
    AnyParent = Parent;
    Depth = AnyParent ? AnyParent->Depth+1 : 0;
    Flags = ScopeFlags;
    
    if (AnyParent) {
      FnParent       = AnyParent->FnParent;
      BreakParent    = AnyParent->BreakParent;
      ContinueParent = AnyParent->ContinueParent;
      BlockParent  = AnyParent->BlockParent;
      TemplateParamParent = AnyParent->TemplateParamParent;
    } else {
      FnParent = BreakParent = ContinueParent = BlockParent = 0;
      TemplateParamParent = 0;
    }
    
    // If this scope is a function or contains breaks/continues, remember it.
    if (Flags & FnScope)       	    FnParent = this;
    if (Flags & BreakScope)    	    BreakParent = this;
    if (Flags & ContinueScope) 	    ContinueParent = this;
    if (Flags & BlockScope)    	    BlockParent = this;
    if (Flags & TemplateParamScope) TemplateParamParent = this;
    DeclsInScope.clear();
  }
};
    
}  // end namespace clang

#endif
