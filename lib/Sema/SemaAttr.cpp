//===--- SemaAttr.cpp - Semantic Analysis for Attributes ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements semantic analysis for non-trivial attributes and
// pragmas.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "Lookup.h"
#include "clang/AST/Expr.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// Pragma 'pack' and 'options align'
//===----------------------------------------------------------------------===//

namespace {
  /// PragmaPackStack - Simple class to wrap the stack used by #pragma
  /// pack.
  class PragmaPackStack {
    typedef std::vector< std::pair<unsigned, IdentifierInfo*> > stack_ty;

    /// Alignment - The current user specified alignment.
    unsigned Alignment;

    /// Stack - Entries in the #pragma pack stack, consisting of saved
    /// alignments and optional names.
    stack_ty Stack;

  public:
    PragmaPackStack() : Alignment(0) {}

    void setAlignment(unsigned A) { Alignment = A; }
    unsigned getAlignment() { return Alignment; }

    /// push - Push the current alignment onto the stack, optionally
    /// using the given \arg Name for the record, if non-zero.
    void push(IdentifierInfo *Name) {
      Stack.push_back(std::make_pair(Alignment, Name));
    }

    /// pop - Pop a record from the stack and restore the current
    /// alignment to the previous value. If \arg Name is non-zero then
    /// the first such named record is popped, otherwise the top record
    /// is popped. Returns true if the pop succeeded.
    bool pop(IdentifierInfo *Name);
  };
}  // end anonymous namespace.

bool PragmaPackStack::pop(IdentifierInfo *Name) {
  if (Stack.empty())
    return false;

  // If name is empty just pop top.
  if (!Name) {
    Alignment = Stack.back().first;
    Stack.pop_back();
    return true;
  }

  // Otherwise, find the named record.
  for (unsigned i = Stack.size(); i != 0; ) {
    --i;
    if (Stack[i].second == Name) {
      // Found it, pop up to and including this record.
      Alignment = Stack[i].first;
      Stack.erase(Stack.begin() + i, Stack.end());
      return true;
    }
  }

  return false;
}


/// FreePackedContext - Deallocate and null out PackContext.
void Sema::FreePackedContext() {
  delete static_cast<PragmaPackStack*>(PackContext);
  PackContext = 0;
}

void Sema::AddAlignmentAttributesForRecord(RecordDecl *RD) {
  // If there is no pack context, we don't need any attributes.
  if (!PackContext)
    return;

  PragmaPackStack *Stack = static_cast<PragmaPackStack*>(PackContext);

  // Otherwise, check to see if we need a max field alignment attribute.
  if (unsigned Alignment = Stack->getAlignment())
    RD->addAttr(::new (Context) MaxFieldAlignmentAttr(Alignment * 8));
}

void Sema::ActOnPragmaOptionsAlign(PragmaOptionsAlignKind Kind,
                                   SourceLocation PragmaLoc,
                                   SourceLocation KindLoc) {
  if (PackContext == 0)
    PackContext = new PragmaPackStack();

  PragmaPackStack *Context = static_cast<PragmaPackStack*>(PackContext);

  // Reset just pops the top of the stack.
  if (Kind == Action::POAK_Reset) {
    // Do the pop.
    if (!Context->pop(0)) {
      // If a name was specified then failure indicates the name
      // wasn't found. Otherwise failure indicates the stack was
      // empty.
      Diag(PragmaLoc, diag::warn_pragma_options_align_reset_failed)
        << "stack empty";
    }
    return;
  }

  // We don't support #pragma options align=power.
  switch (Kind) {
  case POAK_Natural:
    Context->push(0);
    Context->setAlignment(0);
    break;

  case POAK_Mac68k:
    // Check if the target supports this.
    if (!PP.getTargetInfo().hasAlignMac68kSupport()) {
      Diag(PragmaLoc, diag::err_pragma_options_align_mac68k_target_unsupported);
      return;
    } else {
      // Otherwise, just warn about it for now.
      Diag(PragmaLoc, diag::warn_pragma_options_align_unsupported_option)
        << KindLoc;
    }
    break;

  default:
    Diag(PragmaLoc, diag::warn_pragma_options_align_unsupported_option)
      << KindLoc;
    break;
  }
}

void Sema::ActOnPragmaPack(PragmaPackKind Kind, IdentifierInfo *Name,
                           ExprTy *alignment, SourceLocation PragmaLoc,
                           SourceLocation LParenLoc, SourceLocation RParenLoc) {
  Expr *Alignment = static_cast<Expr *>(alignment);

  // If specified then alignment must be a "small" power of two.
  unsigned AlignmentVal = 0;
  if (Alignment) {
    llvm::APSInt Val;

    // pack(0) is like pack(), which just works out since that is what
    // we use 0 for in PackAttr.
    if (Alignment->isTypeDependent() ||
        Alignment->isValueDependent() ||
        !Alignment->isIntegerConstantExpr(Val, Context) ||
        !(Val == 0 || Val.isPowerOf2()) ||
        Val.getZExtValue() > 16) {
      Diag(PragmaLoc, diag::warn_pragma_pack_invalid_alignment);
      Alignment->Destroy(Context);
      return; // Ignore
    }

    AlignmentVal = (unsigned) Val.getZExtValue();
  }

  if (PackContext == 0)
    PackContext = new PragmaPackStack();

  PragmaPackStack *Context = static_cast<PragmaPackStack*>(PackContext);

  switch (Kind) {
  case Action::PPK_Default: // pack([n])
    Context->setAlignment(AlignmentVal);
    break;

  case Action::PPK_Show: // pack(show)
    // Show the current alignment, making sure to show the right value
    // for the default.
    AlignmentVal = Context->getAlignment();
    // FIXME: This should come from the target.
    if (AlignmentVal == 0)
      AlignmentVal = 8;
    Diag(PragmaLoc, diag::warn_pragma_pack_show) << AlignmentVal;
    break;

  case Action::PPK_Push: // pack(push [, id] [, [n])
    Context->push(Name);
    // Set the new alignment if specified.
    if (Alignment)
      Context->setAlignment(AlignmentVal);
    break;

  case Action::PPK_Pop: // pack(pop [, id] [,  n])
    // MSDN, C/C++ Preprocessor Reference > Pragma Directives > pack:
    // "#pragma pack(pop, identifier, n) is undefined"
    if (Alignment && Name)
      Diag(PragmaLoc, diag::warn_pragma_pack_pop_identifer_and_alignment);

    // Do the pop.
    if (!Context->pop(Name)) {
      // If a name was specified then failure indicates the name
      // wasn't found. Otherwise failure indicates the stack was
      // empty.
      Diag(PragmaLoc, diag::warn_pragma_pack_pop_failed)
        << (Name ? "no record matching name" : "stack empty");

      // FIXME: Warn about popping named records as MSVC does.
    } else {
      // Pop succeeded, set the new alignment if specified.
      if (Alignment)
        Context->setAlignment(AlignmentVal);
    }
    break;

  default:
    assert(0 && "Invalid #pragma pack kind.");
  }
}

void Sema::ActOnPragmaUnused(const Token *Identifiers, unsigned NumIdentifiers,
                             Scope *curScope,
                             SourceLocation PragmaLoc,
                             SourceLocation LParenLoc,
                             SourceLocation RParenLoc) {

  for (unsigned i = 0; i < NumIdentifiers; ++i) {
    const Token &Tok = Identifiers[i];
    IdentifierInfo *Name = Tok.getIdentifierInfo();
    LookupResult Lookup(*this, Name, Tok.getLocation(), LookupOrdinaryName);
    LookupParsedName(Lookup, curScope, NULL, true);

    if (Lookup.empty()) {
      Diag(PragmaLoc, diag::warn_pragma_unused_undeclared_var)
        << Name << SourceRange(Tok.getLocation());
      continue;
    }

    VarDecl *VD = Lookup.getAsSingle<VarDecl>();
    if (!VD || !VD->hasLocalStorage()) {
      Diag(PragmaLoc, diag::warn_pragma_unused_expected_localvar)
        << Name << SourceRange(Tok.getLocation());
      continue;
    }

    VD->addAttr(::new (Context) UnusedAttr());
  }
}
