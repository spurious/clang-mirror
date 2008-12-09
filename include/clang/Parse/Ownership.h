//===--- Ownership.h - Parser Ownership Helpers -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file contains classes for managing ownership of Stmt and Expr nodes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_OWNERSHIP_H
#define LLVM_CLANG_PARSE_OWNERSHIP_H

namespace clang
{
  class DiagnosticBuilder;

  /// ActionBase - A small part split from Action because of the horrible
  /// definition order dependencies between Action and the smart pointers.
  class ActionBase {
  public:
    /// Out-of-line virtual destructor to provide home for this class.
    virtual ~ActionBase();

    // Types - Though these don't actually enforce strong typing, they document
    // what types are required to be identical for the actions.
    typedef void ExprTy;
    typedef void StmtTy;

    /// ActionResult - This structure is used while parsing/acting on
    /// expressions, stmts, etc.  It encapsulates both the object returned by
    /// the action, plus a sense of whether or not it is valid.
    template<unsigned UID>
    struct ActionResult {
      void *Val;
      bool isInvalid;

      ActionResult(bool Invalid = false) : Val(0), isInvalid(Invalid) {}
      template<typename ActualExprTy>
      ActionResult(ActualExprTy *val) : Val(val), isInvalid(false) {}
      ActionResult(const DiagnosticBuilder &) : Val(0), isInvalid(true) {}

      const ActionResult &operator=(void *RHS) {
        Val = RHS;
        isInvalid = false;
        return *this;
      }
    };

    /// Deletion callbacks - Since the parser doesn't know the concrete types of
    /// the AST nodes being generated, it must do callbacks to delete objects
    /// when recovering from errors. These are in ActionBase because the smart
    /// pointers need access to them.
    virtual void DeleteExpr(ExprTy *E) {}
    virtual void DeleteStmt(StmtTy *E) {}
  };

  /// ASTDestroyer - The type of an AST node destruction function pointer.
  typedef void (ActionBase::*ASTDestroyer)(void *);

  /// For the transition phase: translate from an ASTDestroyer to its
  /// ActionResult UID.
  template <ASTDestroyer Destroyer> struct DestroyerToUID;
  template <> struct DestroyerToUID<&ActionBase::DeleteExpr> {
    static const unsigned UID = 0;
  };
  template <> struct DestroyerToUID<&ActionBase::DeleteStmt> {
    static const unsigned UID = 1;
  };

  /// ASTOwningResult - A moveable smart pointer for AST nodes that also
  /// has an extra flag to indicate an additional success status.
  template <ASTDestroyer Destroyer> class ASTOwningResult;

  /// ASTOwningPtr - A moveable smart pointer for AST nodes.
  template <ASTDestroyer Destroyer> class ASTOwningPtr;

  /// ASTMultiPtr - A moveable smart pointer to multiple AST nodes. Only owns
  /// the individual pointers, not the array holding them.
  template <ASTDestroyer Destroyer> class ASTMultiPtr;

  /// Move emulation helper for ASTOwningResult
  template <ASTDestroyer Destroyer>
  class ASTResultMover
  {
    ASTOwningResult<Destroyer> &Moved;

  public:
    ASTResultMover(ASTOwningResult<Destroyer> &moved) : Moved(moved) {}

    ASTOwningResult<Destroyer> * operator ->() { return &Moved; }

    // For the transition phase.
    operator void*();

    // For the transition phase.
    operator ActionBase::ActionResult<DestroyerToUID<Destroyer>::UID>();
  };

  /// Move emulation helper for ASTOwningPtr
  template <ASTDestroyer Destroyer>
  class ASTPtrMover
  {
    ASTOwningPtr<Destroyer> &Moved;

  public:
    ASTPtrMover(ASTOwningPtr<Destroyer> &moved) : Moved(moved) {}

    ASTOwningPtr<Destroyer> * operator ->() { return &Moved; }

    operator void*();
  };

  /// Move emulation helper for ASTMultiPtr
  template <ASTDestroyer Destroyer>
  class ASTMultiMover
  {
    ASTMultiPtr<Destroyer> &Moved;

  public:
    ASTMultiMover(ASTMultiPtr<Destroyer> &moved) : Moved(moved) {}

    /// Reset the moved object's internal structures.
    void release();
  };

  template <ASTDestroyer Destroyer>
  class ASTOwningResult
  {
    ActionBase *Actions;
    void *Node;
    bool Invalid;

    friend class ASTResultMover<Destroyer>;
    friend class ASTOwningPtr<Destroyer>;

    ASTOwningResult(const ASTOwningResult&); // DO NOT IMPLEMENT
    ASTOwningResult& operator =(const ASTOwningResult&); // DO NOT IMPLEMENT

    void destroy() {
      if (Node) {
        assert(Actions && "Owning pointer without Action owns node.");
        (Actions->*Destroyer)(Node);
      }
    }

    void * take() {
      if (Invalid)
        return 0;
      return Node;
    }

  public:
    // For convenience and compatibility.
    ASTOwningResult(bool invalid = false)
      : Actions(0), Node(0), Invalid(invalid) {}
    // Same
    ASTOwningResult(const DiagnosticBuilder &)
      : Actions(0), Node(0), Invalid(true) {}
    explicit ASTOwningResult(ActionBase &actions, bool invalid = false)
      : Actions(&actions), Node(0), Invalid(invalid) {}
    ASTOwningResult(ActionBase &actions, void *node)
      : Actions(&actions), Node(node), Invalid(false) {}
    /// Move from another owning result
    ASTOwningResult(ASTResultMover<Destroyer> mover)
      : Actions(mover->Actions), Node(mover->take()), Invalid(mover->Invalid) {}
    /// Move from an owning pointer
    ASTOwningResult(ASTPtrMover<Destroyer> mover);

    /// Move assignment from another owning result
    ASTOwningResult & operator =(ASTResultMover<Destroyer> mover) {
      Actions = mover->Actions;
      Node = mover->take();
      Invalid = mover->Invalid;
      return *this;
    }

    /// Move assignment from an owning ptr
    ASTOwningResult & operator =(ASTPtrMover<Destroyer> mover);

    /// Assignment from a raw pointer. Takes ownership - beware!
    ASTOwningResult & operator =(void *raw)
    {
      assert((!raw || Actions) &&
             "Cannot have raw assignment when there's no Action");
      Node = raw;
      Invalid = false;
      return *this;
    }

    /// Assignment from an ActionResult. Takes ownership - beware!
    ASTOwningResult & operator =(
        const ActionBase::ActionResult<DestroyerToUID<Destroyer>::UID> &res) {
      assert((!res.Val || Actions) &&
             "Cannot assign from ActionResult when there's no Action");
      Node = res.Val;
      Invalid = res.isInvalid;
      return *this;
    }

    /// Access to the raw pointer.
    void * get() const { return Node; }

    bool isInvalid() const { return Invalid; }

    /// Does this point to a usable AST node? To be usable, the node must be
    /// valid and non-null.
    bool isUsable() const { return !Invalid && Node; }

    /// Move hook
    ASTResultMover<Destroyer> move() {
      return ASTResultMover<Destroyer>(*this);
    }
  };

  template <ASTDestroyer Destroyer>
  class ASTOwningPtr
  {
    ActionBase *Actions;
    void *Node;

    friend class ASTPtrMover<Destroyer>;
    friend class ASTOwningResult<Destroyer>;

    ASTOwningPtr(const ASTOwningPtr&); // DO NOT IMPLEMENT
    ASTOwningPtr& operator =(const ASTOwningPtr&); // DO NOT IMPLEMENT

    void destroy() {
      if (Node) {
        assert(Actions && "Owning pointer without Action owns node.");
        (Actions->*Destroyer)(Node);
      }
    }

  public:
    explicit ASTOwningPtr(ActionBase &actions)
      : Actions(&actions), Node(0) {}
    ASTOwningPtr(ActionBase &actions, void *node)
      : Actions(&actions), Node(node) {}
    /// Move from another owning pointer
    ASTOwningPtr(ASTPtrMover<Destroyer> mover)
      : Actions(mover->Actions), Node(mover->take()) {}
    /// Move from an owning result
    ASTOwningPtr(ASTResultMover<Destroyer> mover);

    /// Move assignment from another owning pointer
    ASTOwningPtr & operator =(ASTPtrMover<Destroyer> mover) {
      Actions = mover->Actions;
      Node = mover->take();
      return *this;
    }

    /// Move assignment from an owning result
    ASTOwningPtr & operator =(ASTResultMover<Destroyer> mover);

    /// Assignment from a raw pointer. Takes ownership - beware!
    ASTOwningPtr & operator =(void *raw)
    {
      assert((Actions || !raw) && "Cannot assign non-null raw without Action");
      Node = raw;
      return *this;
    }

    /// Access to the raw pointer.
    void * get() const { return Node; }

    /// Move hook
    ASTPtrMover<Destroyer> move() {
      return ASTPtrMover<Destroyer>(*this);
    }
  };

  template <ASTDestroyer Destroyer>
  class ASTMultiPtr
  {
    ActionBase &Actions;
    void **Nodes;
    unsigned Count;

    friend class ASTMultiMover<Destroyer>;

    ASTMultiPtr(const ASTMultiPtr&); // DO NOT IMPLEMENT
    // Reference member prevents copy assignment.

    void destroy() {
      assert((Count == 0 || Nodes) && "No nodes when count is not zero.");
      for (unsigned i = 0; i < Count; ++i) {
        if (Nodes[i])
          (Actions.*Destroyer)(Nodes[i]);
      }
    }

  public:
    explicit ASTMultiPtr(ActionBase &actions)
      : Actions(actions), Nodes(0), Count(0) {}
    ASTMultiPtr(ActionBase &actions, void **nodes, unsigned count)
      : Actions(actions), Nodes(nodes), Count(count) {}
    /// Move constructor
    ASTMultiPtr(ASTMultiMover<Destroyer> mover)
      : Actions(mover->Actions), Nodes(mover->Nodes), Count(mover->Count) {
      mover->Nodes = 0;
      mover->Count = 0;
    }

    /// Move assignment
    ASTMultiPtr & operator =(ASTMultiMover<Destroyer> mover) {
      Actions = mover->Actions;
      Nodes = mover->Nodes;
      Count = mover->Count;
      mover.release();
      return *this;
    }

    /// Access to the raw pointers.
    void ** get() const { return Nodes; }

    /// Access to the count.
    unsigned size() const { return Count; }

    /// Move hook
    ASTMultiMover<Destroyer> move() {
      return ASTMultiMover<Destroyer>(*this);
    }
  };

  // Out-of-line implementations due to definition dependencies

  template <ASTDestroyer Destroyer> inline
  ASTResultMover<Destroyer>::operator void*() {
    return Moved.take();
  }

  template <ASTDestroyer Destroyer> inline
  ASTPtrMover<Destroyer>::operator void*() {
    return Moved.take();
  }

  template <ASTDestroyer Destroyer> inline
  void ASTMultiMover<Destroyer>::release() {
    Moved.Nodes = 0;
    Moved.Count = 0;
  }

  template <ASTDestroyer Destroyer> inline
  ASTOwningResult<Destroyer>::ASTOwningResult(ASTPtrMover<Destroyer> mover)
    : Actions(mover->Actions), Node(mover->take()), Invalid(false) {}

  template <ASTDestroyer Destroyer> inline
  ASTOwningResult<Destroyer> &
  ASTOwningResult<Destroyer>::operator =(ASTPtrMover<Destroyer> mover) {
    Actions = mover->Actions;
    Node = mover->take();
    Invalid = false;
    return *this;
  }

  template <ASTDestroyer Destroyer> inline
  ASTOwningPtr<Destroyer>::ASTOwningPtr(ASTResultMover<Destroyer> mover)
    : Actions(mover->Actions), Node(mover->take()) {
  }

  template <ASTDestroyer Destroyer> inline
  ASTOwningPtr<Destroyer> &
  ASTOwningPtr<Destroyer>::operator =(ASTResultMover<Destroyer> mover) {
    Actions = mover->Actions;
    Node = mover->take();
    return *this;
  }
}

#endif
