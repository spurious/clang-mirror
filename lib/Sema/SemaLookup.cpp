//===--------------------- SemaLookup.cpp - Name Lookup  ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements name lookup for C, C++, Objective-C, and
//  Objective-C++.
//
//===----------------------------------------------------------------------===//
#include "Sema.h"
#include "SemaInherit.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <set>
#include <vector>
#include <iterator>
#include <utility>
#include <algorithm>

using namespace clang;

typedef llvm::SmallVector<UsingDirectiveDecl*, 4> UsingDirectivesTy;
typedef llvm::DenseSet<NamespaceDecl*> NamespaceSet;
typedef llvm::SmallVector<Sema::LookupResult, 3> LookupResultsTy;

/// UsingDirAncestorCompare - Implements strict weak ordering of
/// UsingDirectives. It orders them by address of its common ancestor.
struct UsingDirAncestorCompare {

  /// @brief Compares UsingDirectiveDecl common ancestor with DeclContext.
  bool operator () (UsingDirectiveDecl *U, const DeclContext *Ctx) const {
    return U->getCommonAncestor() < Ctx;
  }

  /// @brief Compares UsingDirectiveDecl common ancestor with DeclContext.
  bool operator () (const DeclContext *Ctx, UsingDirectiveDecl *U) const {
    return Ctx < U->getCommonAncestor();
  }

  /// @brief Compares UsingDirectiveDecl common ancestors.
  bool operator () (UsingDirectiveDecl *U1, UsingDirectiveDecl *U2) const {
    return U1->getCommonAncestor() < U2->getCommonAncestor();
  }
};

/// AddNamespaceUsingDirectives - Adds all UsingDirectiveDecl's to heap UDirs
/// (ordered by common ancestors), found in namespace NS,
/// including all found (recursively) in their nominated namespaces.
void AddNamespaceUsingDirectives(DeclContext *NS,
                                 UsingDirectivesTy &UDirs,
                                 NamespaceSet &Visited) {
  DeclContext::udir_iterator I, End;

  for (llvm::tie(I, End) = NS->getUsingDirectives(); I !=End; ++I) {
    UDirs.push_back(*I);
    std::push_heap(UDirs.begin(), UDirs.end(), UsingDirAncestorCompare());
    NamespaceDecl *Nominated = (*I)->getNominatedNamespace();
    if (Visited.insert(Nominated).second)
      AddNamespaceUsingDirectives(Nominated, UDirs, /*ref*/ Visited);
  }
}

/// AddScopeUsingDirectives - Adds all UsingDirectiveDecl's found in Scope S,
/// including all found in the namespaces they nominate.
static void AddScopeUsingDirectives(Scope *S, UsingDirectivesTy &UDirs) {
  NamespaceSet VisitedNS;

  if (DeclContext *Ctx = static_cast<DeclContext*>(S->getEntity())) {

    if (NamespaceDecl *NS = dyn_cast<NamespaceDecl>(Ctx))
      VisitedNS.insert(NS);

    AddNamespaceUsingDirectives(Ctx, UDirs, /*ref*/ VisitedNS);

  } else {
    Scope::udir_iterator
      I = S->using_directives_begin(),
      End = S->using_directives_end();

    for (; I != End; ++I) {
      UsingDirectiveDecl * UD = static_cast<UsingDirectiveDecl*>(*I);
      UDirs.push_back(UD);
      std::push_heap(UDirs.begin(), UDirs.end(), UsingDirAncestorCompare());

      NamespaceDecl *Nominated = UD->getNominatedNamespace();
      if (!VisitedNS.count(Nominated)) {
        VisitedNS.insert(Nominated);
        AddNamespaceUsingDirectives(Nominated, UDirs, /*ref*/ VisitedNS);
      }
    }
  }
}

/// MaybeConstructOverloadSet - Name lookup has determined that the
/// elements in [I, IEnd) have the name that we are looking for, and
/// *I is a match for the namespace. This routine returns an
/// appropriate Decl for name lookup, which may either be *I or an
/// OverloadedFunctionDecl that represents the overloaded functions in
/// [I, IEnd). 
///
/// The existance of this routine is temporary; users of LookupResult
/// should be able to handle multiple results, to deal with cases of
/// ambiguity and overloaded functions without needing to create a
/// Decl node.
template<typename DeclIterator>
static NamedDecl *
MaybeConstructOverloadSet(ASTContext &Context, 
                          DeclIterator I, DeclIterator IEnd) {
  assert(I != IEnd && "Iterator range cannot be empty");
  assert(!isa<OverloadedFunctionDecl>(*I) && 
         "Cannot have an overloaded function");

  if (isa<FunctionDecl>(*I)) {
    // If we found a function, there might be more functions. If
    // so, collect them into an overload set.
    DeclIterator Last = I;
    OverloadedFunctionDecl *Ovl = 0;
    for (++Last; Last != IEnd && isa<FunctionDecl>(*Last); ++Last) {
      if (!Ovl) {
        // FIXME: We leak this overload set. Eventually, we want to
        // stop building the declarations for these overload sets, so
        // there will be nothing to leak.
        Ovl = OverloadedFunctionDecl::Create(Context, (*I)->getDeclContext(),
                                             (*I)->getDeclName());
        Ovl->addOverload(cast<FunctionDecl>(*I));
      }
      Ovl->addOverload(cast<FunctionDecl>(*Last));
    }
    
    // If we had more than one function, we built an overload
    // set. Return it.
    if (Ovl)
      return Ovl;
  }
  
  return *I;
}

/// Merges together multiple LookupResults dealing with duplicated Decl's.
static Sema::LookupResult
MergeLookupResults(ASTContext &Context, LookupResultsTy &Results) {
  typedef Sema::LookupResult LResult;
  typedef llvm::SmallPtrSet<NamedDecl*, 4> DeclsSetTy;

  DeclsSetTy FoundDecls;
  OverloadedFunctionDecl *FoundOverloaded = 0;

  LookupResultsTy::iterator I = Results.begin(), End = Results.end();
  for (; I != End; ++I) {

    switch (I->getKind()) {
    case LResult::NotFound:
      assert(false &&
             "Should be always successful name lookup result here.");
      break;

    case LResult::AmbiguousReference:
      assert(false &&
             "Shouldn't get ambiguous reference here.");
      break;

    case LResult::Found:
      FoundDecls.insert(I->getAsDecl());
      break;

    case LResult::AmbiguousBaseSubobjectTypes:
    case LResult::AmbiguousBaseSubobjects: {
      assert(Results.size() == 1 && "Multiple LookupResults should be not case "
             "here, since using-directives can't occur at class scope.");
      return *I;
    }

    case LResult::FoundOverloaded:
      if (FoundOverloaded)
        // We have one spare OverloadedFunctionDecl already, so we store
        // its function decls.
        for (LResult::iterator
             FI = I->begin(), FEnd = I->end(); FI != FEnd; ++FI)
          FoundDecls.insert(*FI);
      else
        // First time we found OverloadedFunctionDecl, we want to conserve
        // it, and possibly add other found Decls later.
        FoundOverloaded = cast<OverloadedFunctionDecl>(I->getAsDecl());
      break;
    }
  }

  // Remove duplicated Decl pointing at same Decl, this might be case
  // for code like:
  //
  //    namespace A { int i; }
  //    namespace B { using namespace A; }
  //    namespace C { using namespace A; }
  //
  //    void foo() {
  //      using namespace B;
  //      using namespace C;
  //      ++i; // finds A::i, from both namespace B and C at global scope
  //    }
  //
  //  C++ [namespace.qual].p3:
  //    The same declaration found more than once is not an ambiguity
  //    (because it is still a unique declaration).
  //
  // FIXME: At this point happens too, because we are doing redundant lookups.
  //
  DeclsSetTy::iterator DI = FoundDecls.begin(), DEnd = FoundDecls.end();

  if (FoundOverloaded) {
    // We found overloaded functions result. We want to add any other
    // found decls, that are not already in FoundOverloaded, and are functions
    // or methods.
    OverloadedFunctionDecl::function_iterator 
      FI = FoundOverloaded->function_begin(),
      FEnd = FoundOverloaded->function_end();

    for (; FI < FEnd; ++FI) {
      if (FoundDecls.count(*FI))
        FoundDecls.erase(*FI);
    }

    DI = FoundDecls.begin(), DEnd = FoundDecls.end();

    for (; DI != DEnd; ++DI)
      if (FunctionDecl *Fun = dyn_cast<FunctionDecl>(*DI))
        FoundOverloaded->addOverload(Fun);

    return LResult::CreateLookupResult(Context, FoundOverloaded);
  } else if (std::size_t FoundLen = FoundDecls.size()) {
    // We might found multiple TagDecls pointing at same definition.
    if (TagDecl *R = dyn_cast<TagDecl>(*FoundDecls.begin())) {
      TagDecl *Canonical = Context.getCanonicalDecl(R);
      DeclsSetTy::iterator RI = FoundDecls.begin(), REnd = DEnd;
      for (;;) {
        ++RI;
        if (RI == REnd) {
          FoundLen = 1;
          break;
        }
        R = dyn_cast<TagDecl>(*RI);
        if (R && Canonical == Context.getCanonicalDecl(R)) { /* Skip */}
        else break;
      }
    }

    // We might find FunctionDecls in two (or more) distinct DeclContexts.
    //
    // C++ [basic.lookup].p1:
    // ... Name lookup may associate more than one declaration with
    // a name if it finds the name to be a function name; the declarations
    // are said to form a set of overloaded functions (13.1).
    // Overload resolution (13.3) takes place after name lookup has succeeded.
    //
    NamedDecl *D = MaybeConstructOverloadSet(Context, DI, DEnd);
    if ((FoundLen == 1) || isa<OverloadedFunctionDecl>(D))
      return LResult::CreateLookupResult(Context, D);

    // Found multiple Decls, it is ambiguous reference.
    return LResult::CreateLookupResult(Context, FoundDecls.begin(), FoundLen);
  }

  LResult Result = LResult::CreateLookupResult(Context, 0);
  return Result;
}

// Retrieve the set of identifier namespaces that correspond to a
// specific kind of name lookup.
inline unsigned 
getIdentifierNamespacesFromLookupNameKind(Sema::LookupNameKind NameKind, 
                                          bool CPlusPlus) {
  unsigned IDNS = 0;
  switch (NameKind) {
  case Sema::LookupOrdinaryName:
  case Sema::LookupOperatorName:
    IDNS = Decl::IDNS_Ordinary;
    if (CPlusPlus)
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Member;
    break;

  case Sema::LookupTagName:
    IDNS = Decl::IDNS_Tag;
    break;

  case Sema::LookupMemberName:
    IDNS = Decl::IDNS_Member;
    if (CPlusPlus)
      IDNS |= Decl::IDNS_Tag | Decl::IDNS_Ordinary;    
    break;

  case Sema::LookupNestedNameSpecifierName:
  case Sema::LookupNamespaceName:
    IDNS = Decl::IDNS_Ordinary | Decl::IDNS_Tag | Decl::IDNS_Member;
    break;
  }
  return IDNS;
}

Sema::LookupResult
Sema::LookupResult::CreateLookupResult(ASTContext &Context, NamedDecl *D) {
  LookupResult Result;
  Result.StoredKind = (D && isa<OverloadedFunctionDecl>(D))?
    OverloadedDeclSingleDecl : SingleDecl;
  Result.First = reinterpret_cast<uintptr_t>(D);
  Result.Last = 0;
  Result.Context = &Context;
  return Result;
}

/// @brief Moves the name-lookup results from Other to this LookupResult.
Sema::LookupResult
Sema::LookupResult::CreateLookupResult(ASTContext &Context, 
                                       IdentifierResolver::iterator F, 
                                       IdentifierResolver::iterator L) {
  LookupResult Result;
  Result.Context = &Context;

  if (F != L && isa<FunctionDecl>(*F)) {
    IdentifierResolver::iterator Next = F;
    ++Next;
    if (Next != L && isa<FunctionDecl>(*Next)) {
      Result.StoredKind = OverloadedDeclFromIdResolver;
      Result.First = F.getAsOpaqueValue();
      Result.Last = L.getAsOpaqueValue();
      return Result;
    }
  } 
    
  Result.StoredKind = SingleDecl;
  Result.First = reinterpret_cast<uintptr_t>(*F);
  Result.Last = 0;
  return Result;
}

Sema::LookupResult
Sema::LookupResult::CreateLookupResult(ASTContext &Context, 
                                       DeclContext::lookup_iterator F, 
                                       DeclContext::lookup_iterator L) {
  LookupResult Result;
  Result.Context = &Context;

  if (F != L && isa<FunctionDecl>(*F)) {
    DeclContext::lookup_iterator Next = F;
    ++Next;
    if (Next != L && isa<FunctionDecl>(*Next)) {
      Result.StoredKind = OverloadedDeclFromDeclContext;
      Result.First = reinterpret_cast<uintptr_t>(F);
      Result.Last = reinterpret_cast<uintptr_t>(L);
      return Result;
    }
  }
  
  Result.StoredKind = SingleDecl;
  Result.First = reinterpret_cast<uintptr_t>(*F);
  Result.Last = 0;
  return Result;
}

/// @brief Determine the result of name lookup.
Sema::LookupResult::LookupKind Sema::LookupResult::getKind() const {
  switch (StoredKind) {
  case SingleDecl:
    return (reinterpret_cast<Decl *>(First) != 0)? Found : NotFound;

  case OverloadedDeclSingleDecl:
  case OverloadedDeclFromIdResolver:
  case OverloadedDeclFromDeclContext:
    return FoundOverloaded;

  case AmbiguousLookupStoresBasePaths:
    return Last? AmbiguousBaseSubobjectTypes : AmbiguousBaseSubobjects;

  case AmbiguousLookupStoresDecls:
    return AmbiguousReference;
  }

  // We can't ever get here.
  return NotFound;
}

/// @brief Converts the result of name lookup into a single (possible
/// NULL) pointer to a declaration.
///
/// The resulting declaration will either be the declaration we found
/// (if only a single declaration was found), an
/// OverloadedFunctionDecl (if an overloaded function was found), or
/// NULL (if no declaration was found). This conversion must not be
/// used anywhere where name lookup could result in an ambiguity. 
///
/// The OverloadedFunctionDecl conversion is meant as a stop-gap
/// solution, since it causes the OverloadedFunctionDecl to be
/// leaked. FIXME: Eventually, there will be a better way to iterate
/// over the set of overloaded functions returned by name lookup.
NamedDecl *Sema::LookupResult::getAsDecl() const {
  switch (StoredKind) {
  case SingleDecl:
    return reinterpret_cast<NamedDecl *>(First);

  case OverloadedDeclFromIdResolver:
    return MaybeConstructOverloadSet(*Context,
                         IdentifierResolver::iterator::getFromOpaqueValue(First),
                         IdentifierResolver::iterator::getFromOpaqueValue(Last));

  case OverloadedDeclFromDeclContext:
    return MaybeConstructOverloadSet(*Context, 
                           reinterpret_cast<DeclContext::lookup_iterator>(First),
                           reinterpret_cast<DeclContext::lookup_iterator>(Last));

  case OverloadedDeclSingleDecl:
    return reinterpret_cast<OverloadedFunctionDecl*>(First);

  case AmbiguousLookupStoresDecls:
  case AmbiguousLookupStoresBasePaths:
    assert(false && 
           "Name lookup returned an ambiguity that could not be handled");
    break;
  }

  return 0;
}

/// @brief Retrieves the BasePaths structure describing an ambiguous
/// name lookup, or null.
BasePaths *Sema::LookupResult::getBasePaths() const {
  if (StoredKind == AmbiguousLookupStoresBasePaths)
      return reinterpret_cast<BasePaths *>(First);
  return 0;
}

Sema::LookupResult::iterator::reference 
Sema::LookupResult::iterator::operator*() const {
  switch (Result->StoredKind) {
  case SingleDecl:
    return reinterpret_cast<NamedDecl*>(Current);

  case OverloadedDeclSingleDecl:
    return *reinterpret_cast<NamedDecl**>(Current);

  case OverloadedDeclFromIdResolver:
    return *IdentifierResolver::iterator::getFromOpaqueValue(Current);

  case OverloadedDeclFromDeclContext:
    return *reinterpret_cast<DeclContext::lookup_iterator>(Current);
  
  case AmbiguousLookupStoresDecls:
  case AmbiguousLookupStoresBasePaths:
    assert(false && "Cannot look into ambiguous lookup results");
    break;
  }

  return 0;
}

Sema::LookupResult::iterator& Sema::LookupResult::iterator::operator++() {
  switch (Result->StoredKind) {
  case SingleDecl:
    Current = reinterpret_cast<uintptr_t>((NamedDecl*)0);
    break;

  case OverloadedDeclSingleDecl: {
    NamedDecl ** I = reinterpret_cast<NamedDecl**>(Current);
    ++I;
    Current = reinterpret_cast<uintptr_t>(I);
    break;
  }

  case OverloadedDeclFromIdResolver: {
    IdentifierResolver::iterator I 
      = IdentifierResolver::iterator::getFromOpaqueValue(Current);
    ++I;
    Current = I.getAsOpaqueValue();
    break;
  }

  case OverloadedDeclFromDeclContext: {
    DeclContext::lookup_iterator I 
      = reinterpret_cast<DeclContext::lookup_iterator>(Current);
    ++I;
    Current = reinterpret_cast<uintptr_t>(I);
    break;
  }

  case AmbiguousLookupStoresDecls:
  case AmbiguousLookupStoresBasePaths:
    assert(false && "Cannot look into ambiguous lookup results");
    break;
  }

  return *this;
}

Sema::LookupResult::iterator Sema::LookupResult::begin() {
  assert(!isAmbiguous() && "Lookup into an ambiguous result");
  if (StoredKind != OverloadedDeclSingleDecl)
    return iterator(this, First);
  OverloadedFunctionDecl * Ovl =
    reinterpret_cast<OverloadedFunctionDecl*>(First);
  return iterator(this, reinterpret_cast<uintptr_t>(&(*Ovl->function_begin())));
}

Sema::LookupResult::iterator Sema::LookupResult::end() {
  assert(!isAmbiguous() && "Lookup into an ambiguous result");
  if (StoredKind != OverloadedDeclSingleDecl)
    return iterator(this, Last);
  OverloadedFunctionDecl * Ovl =
    reinterpret_cast<OverloadedFunctionDecl*>(First);
  return iterator(this, reinterpret_cast<uintptr_t>(&(*Ovl->function_end())));
}

static bool isFunctionLocalScope(Scope *S) {
  if (DeclContext *Ctx = static_cast<DeclContext*>(S->getEntity()))
    return Ctx->isFunctionOrMethod();
  return true;
}

std::pair<bool, Sema::LookupResult>
Sema::CppLookupName(Scope *S, DeclarationName Name,
                    LookupNameKind NameKind, bool RedeclarationOnly) {
  assert(getLangOptions().CPlusPlus &&
         "Can perform only C++ lookup");
  unsigned IDNS 
    = getIdentifierNamespacesFromLookupNameKind(NameKind, /*CPlusPlus*/ true);
  Scope *Initial = S;
  IdentifierResolver::iterator 
    I = IdResolver.begin(Name),
    IEnd = IdResolver.end();

  // First we lookup local scope.
  // We don't consider using-dirctives, as per 7.3.4.p1 [namespace.udir]
  // ...During unqualified name lookup (3.4.1), the names appear as if
  // they were declared in the nearest enclosing namespace which contains
  // both the using-directive and the nominated namespace.
  // [Note: in this context, “contains” means “contains directly or
  // indirectly”. 
  //
  // For example:
  // namespace A { int i; }
  // void foo() {
  //   int i;
  //   {
  //     using namespace A;
  //     ++i; // finds local 'i', A::i appears at global scope
  //   }
  // }
  //
  for (; S && isFunctionLocalScope(S); S = S->getParent()) {
    // Check whether the IdResolver has anything in this scope.
    for (; I != IEnd && S->isDeclScope(*I); ++I) {
      if (isAcceptableLookupResult(*I, NameKind, IDNS)) {
        // We found something.  Look for anything else in our scope
        // with this same name and in an acceptable identifier
        // namespace, so that we can construct an overload set if we
        // need to.
        IdentifierResolver::iterator LastI = I;
        for (++LastI; LastI != IEnd; ++LastI) {
          if (!S->isDeclScope(*LastI))
            break;
        }
        LookupResult Result =
          LookupResult::CreateLookupResult(Context, I, LastI);
        return std::make_pair(true, Result);
      }
    }
    // NB: Icky, we need to look in function scope, but we need to check its
    // parent DeclContext (instead S->getParent()) for member name lookup,
    // in case it is out of line method definition. Like in:
    //
    // class C {
    //   int i;
    //   void foo();
    // };
    //
    // C::foo() {
    //   (void) i;
    // }
    //
    // FIXME: Maybe we should do member name lookup here instead?
    if (S->getEntity() && isFunctionLocalScope(S))
      break;
  }

  // Collect UsingDirectiveDecls in all scopes, and recursivly all
  // nominated namespaces by those using-directives.
  // UsingDirectives are pushed to heap, in common ancestor pointer
  // value order.
  // FIXME: Cache this sorted list in Scope structure, and maybe
  // DeclContext, so we don't build it for each lookup!
  UsingDirectivesTy UDirs;
  for (Scope *SC = Initial; SC; SC = SC->getParent())
    if (SC->getFlags() & Scope::DeclScope)
      AddScopeUsingDirectives(SC, UDirs);

  // Sort heapified UsingDirectiveDecls.
  std::sort_heap(UDirs.begin(), UDirs.end());

  // Lookup namespace scope, global scope, or possibly (CXX)Record DeclContext
  // for member name lookup.
  // Unqualified name lookup in C++ requires looking into scopes
  // that aren't strictly lexical, and therefore we walk through the
  // context as well as walking through the scopes.
  for (; S; S = S->getParent()) {
    LookupResultsTy LookupResults;
    bool LookedInCtx = false;

    // Check whether the IdResolver has anything in this scope.
    for (; I != IEnd && S->isDeclScope(*I); ++I) {
      if (isAcceptableLookupResult(*I, NameKind, IDNS)) {
        // We found something.  Look for anything else in our scope
        // with this same name and in an acceptable identifier
        // namespace, so that we can construct an overload set if we
        // need to.
        IdentifierResolver::iterator LastI = I;
        for (++LastI; LastI != IEnd; ++LastI) {
          if (!S->isDeclScope(*LastI))
            break;
        }
        
        // We store name lookup result, and continue trying to look into
        // associated context, and maybe namespaces nominated by
        // using-directives.
        LookupResults.push_back(
          LookupResult::CreateLookupResult(Context, I, LastI));
        break;
      }
    }

    // If there is an entity associated with this scope, it's a
    // DeclContext. We might need to perform qualified lookup into
    // it, or namespaces nominated by using-directives.
    DeclContext *Ctx = static_cast<DeclContext *>(S->getEntity());

    if (Ctx && isa<TranslationUnitDecl>(Ctx)) {
      UsingDirectivesTy::const_iterator UI, UEnd;
      // For each UsingDirectiveDecl, which common ancestor is equal
      // to Ctx, we preform qualified name lookup into namespace nominated
      // by it.
      llvm::tie(UI, UEnd) =
        std::equal_range(UDirs.begin(), UDirs.end(), Ctx,
                         UsingDirAncestorCompare());
      for (; UI != UEnd; ++UI) {
        // FIXME: We will have to ensure, that we won't consider
        // again using-directives during qualified name lookup!
        // (Once using-directives support for qualified name lookup gets
        // implemented).
        if (LookupResult R = LookupQualifiedName((*UI)->getNominatedNamespace(),
            Name, NameKind, RedeclarationOnly))
          LookupResults.push_back(R);
      }
      LookupResult Result;
      if ((Result = MergeLookupResults(Context, LookupResults)) ||
          RedeclarationOnly) {
        return std::make_pair(true, Result);
      }
    }

    // FIXME: We're performing redundant lookups here, where the
    // scope stack mirrors the semantic nested of classes and
    // namespaces. We can save some work by checking the lexical
    // scope against the semantic scope and avoiding any lookups
    // when they are the same.
    // FIXME: In some cases, we know that every name that could be
    // found by this qualified name lookup will also be on the
    // identifier chain. For example, inside a class without any
    // base classes, we never need to perform qualified lookup
    // because all of the members are on top of the identifier
    // chain. However, we cannot perform this optimization when the
    // lexical and semantic scopes don't line up, e.g., in an
    // out-of-line member definition.
    while (Ctx && Ctx->isFunctionOrMethod())
      Ctx = Ctx->getParent();
    while (Ctx && (Ctx->isNamespace() || Ctx->isRecord())) {
      LookedInCtx = true;
      // Look for declarations of this name in this scope.
      if (LookupResult R = LookupQualifiedName(Ctx, Name, NameKind,
                                               RedeclarationOnly))
        // We store that, to investigate futher, wheather reference 
        // to this Decl is no ambiguous.
        LookupResults.push_back(R);

      if (Ctx->isNamespace()) {
        // For each UsingDirectiveDecl, which common ancestor is equal
        // to Ctx, we preform qualified name lookup into namespace nominated
        // by it.
        UsingDirectivesTy::const_iterator UI, UEnd;
        llvm::tie(UI, UEnd) =
          std::equal_range(UDirs.begin(), UDirs.end(), Ctx,
                           UsingDirAncestorCompare());
        for (; UI != UEnd; ++UI) {
          // FIXME: We will have to ensure, that we won't consider
          // again using-directives during qualified name lookup!
          // (Once using-directives support for qualified name lookup gets
          // implemented).
          LookupResult R = LookupQualifiedName((*UI)->getNominatedNamespace(),
                                               Name, NameKind,
                                               RedeclarationOnly);
          if (R) 
            LookupResults.push_back(R);
        }
      }
      LookupResult Result;
      if ((Result = MergeLookupResults(Context, LookupResults)) ||
          (RedeclarationOnly && !Ctx->isTransparentContext())) {
        return std::make_pair(true, Result);
      }
      Ctx = Ctx->getParent();
    }

    if (!(LookedInCtx || LookupResults.empty())) {
      // We didn't Performed lookup in Scope entity, so we return
      // result form IdentifierResolver.
      assert((LookupResults.size() == 1) && "Wrong size!");
      return std::make_pair(true, LookupResults.front());
    }
  }
  return std::make_pair(false, LookupResult());
}

/// @brief Perform unqualified name lookup starting from a given
/// scope.
///
/// Unqualified name lookup (C++ [basic.lookup.unqual], C99 6.2.1) is
/// used to find names within the current scope. For example, 'x' in
/// @code
/// int x;
/// int f() {
///   return x; // unqualified name look finds 'x' in the global scope
/// }
/// @endcode
///
/// Different lookup criteria can find different names. For example, a
/// particular scope can have both a struct and a function of the same
/// name, and each can be found by certain lookup criteria. For more
/// information about lookup criteria, see the documentation for the
/// class LookupCriteria.
///
/// @param S        The scope from which unqualified name lookup will
/// begin. If the lookup criteria permits, name lookup may also search
/// in the parent scopes.
///
/// @param Name     The name of the entity that we are searching for.
///
/// @param Criteria The criteria that this routine will use to
/// determine which names are visible and which names will be
/// found. Note that name lookup will find a name that is visible by
/// the given criteria, but the entity itself may not be semantically
/// correct or even the kind of entity expected based on the
/// lookup. For example, searching for a nested-name-specifier name
/// might result in an EnumDecl, which is visible but is not permitted
/// as a nested-name-specifier in C++03.
///
/// @returns The result of name lookup, which includes zero or more
/// declarations and possibly additional information used to diagnose
/// ambiguities.
Sema::LookupResult 
Sema::LookupName(Scope *S, DeclarationName Name, LookupNameKind NameKind,
                 bool RedeclarationOnly) {
  if (!Name) return LookupResult::CreateLookupResult(Context, 0);

  if (!getLangOptions().CPlusPlus) {
    // Unqualified name lookup in C/Objective-C is purely lexical, so
    // search in the declarations attached to the name.
    unsigned IDNS = 0;
    switch (NameKind) {
    case Sema::LookupOrdinaryName:
      IDNS = Decl::IDNS_Ordinary;
      break;

    case Sema::LookupTagName:
      IDNS = Decl::IDNS_Tag;
      break;

    case Sema::LookupMemberName:
      IDNS = Decl::IDNS_Member;
      break;

    case Sema::LookupOperatorName:
    case Sema::LookupNestedNameSpecifierName:
    case Sema::LookupNamespaceName:
      assert(false && "C does not perform these kinds of name lookup");
      break;
    }

    // Scan up the scope chain looking for a decl that matches this
    // identifier that is in the appropriate namespace.  This search
    // should not take long, as shadowing of names is uncommon, and
    // deep shadowing is extremely uncommon.
    for (IdentifierResolver::iterator I = IdResolver.begin(Name),
                                   IEnd = IdResolver.end(); 
         I != IEnd; ++I)
      if ((*I)->isInIdentifierNamespace(IDNS))
        return LookupResult::CreateLookupResult(Context, *I);
  } else {
    // Perform C++ unqualified name lookup.
    std::pair<bool, LookupResult> MaybeResult =
      CppLookupName(S, Name, NameKind, RedeclarationOnly);
    if (MaybeResult.first)
      return MaybeResult.second;
  }

  // If we didn't find a use of this identifier, and if the identifier
  // corresponds to a compiler builtin, create the decl object for the builtin
  // now, injecting it into translation unit scope, and return it.
  if (NameKind == LookupOrdinaryName) {
    IdentifierInfo *II = Name.getAsIdentifierInfo();
    if (II) {
      // If this is a builtin on this (or all) targets, create the decl.
      if (unsigned BuiltinID = II->getBuiltinID())
        return LookupResult::CreateLookupResult(Context,
                            LazilyCreateBuiltin((IdentifierInfo *)II, BuiltinID,
                                                S));
    }
    if (getLangOptions().ObjC1 && II) {
      // @interface and @compatibility_alias introduce typedef-like names.
      // Unlike typedef's, they can only be introduced at file-scope (and are 
      // therefore not scoped decls). They can, however, be shadowed by
      // other names in IDNS_Ordinary.
      ObjCInterfaceDeclsTy::iterator IDI = ObjCInterfaceDecls.find(II);
      if (IDI != ObjCInterfaceDecls.end())
        return LookupResult::CreateLookupResult(Context, IDI->second);
      ObjCAliasTy::iterator I = ObjCAliasDecls.find(II);
      if (I != ObjCAliasDecls.end())
        return LookupResult::CreateLookupResult(Context, 
                                                I->second->getClassInterface());
    }
  }
  return LookupResult::CreateLookupResult(Context, 0);
}

/// @brief Perform qualified name lookup into a given context.
///
/// Qualified name lookup (C++ [basic.lookup.qual]) is used to find
/// names when the context of those names is explicit specified, e.g.,
/// "std::vector" or "x->member".
///
/// Different lookup criteria can find different names. For example, a
/// particular scope can have both a struct and a function of the same
/// name, and each can be found by certain lookup criteria. For more
/// information about lookup criteria, see the documentation for the
/// class LookupCriteria.
///
/// @param LookupCtx The context in which qualified name lookup will
/// search. If the lookup criteria permits, name lookup may also search
/// in the parent contexts or (for C++ classes) base classes.
///
/// @param Name     The name of the entity that we are searching for.
///
/// @param Criteria The criteria that this routine will use to
/// determine which names are visible and which names will be
/// found. Note that name lookup will find a name that is visible by
/// the given criteria, but the entity itself may not be semantically
/// correct or even the kind of entity expected based on the
/// lookup. For example, searching for a nested-name-specifier name
/// might result in an EnumDecl, which is visible but is not permitted
/// as a nested-name-specifier in C++03.
///
/// @returns The result of name lookup, which includes zero or more
/// declarations and possibly additional information used to diagnose
/// ambiguities.
Sema::LookupResult
Sema::LookupQualifiedName(DeclContext *LookupCtx, DeclarationName Name,
                          LookupNameKind NameKind, bool RedeclarationOnly) {
  assert(LookupCtx && "Sema::LookupQualifiedName requires a lookup context");
  
  if (!Name) return LookupResult::CreateLookupResult(Context, 0);

  // If we're performing qualified name lookup (e.g., lookup into a
  // struct), find fields as part of ordinary name lookup.
  unsigned IDNS
    = getIdentifierNamespacesFromLookupNameKind(NameKind, 
                                                getLangOptions().CPlusPlus);
  if (NameKind == LookupOrdinaryName)
    IDNS |= Decl::IDNS_Member;

  // Perform qualified name lookup into the LookupCtx.
  DeclContext::lookup_iterator I, E;
  for (llvm::tie(I, E) = LookupCtx->lookup(Name); I != E; ++I)
    if (isAcceptableLookupResult(*I, NameKind, IDNS))
      return LookupResult::CreateLookupResult(Context, I, E);

  // If this isn't a C++ class or we aren't allowed to look into base
  // classes, we're done.
  if (RedeclarationOnly || !isa<CXXRecordDecl>(LookupCtx))
    return LookupResult::CreateLookupResult(Context, 0);

  // Perform lookup into our base classes.
  BasePaths Paths;
  Paths.setOrigin(Context.getTypeDeclType(cast<RecordDecl>(LookupCtx)));

  // Look for this member in our base classes
  if (!LookupInBases(cast<CXXRecordDecl>(LookupCtx), 
                     MemberLookupCriteria(Name, NameKind, IDNS), Paths))
    return LookupResult::CreateLookupResult(Context, 0);

  // C++ [class.member.lookup]p2:
  //   [...] If the resulting set of declarations are not all from
  //   sub-objects of the same type, or the set has a nonstatic member
  //   and includes members from distinct sub-objects, there is an
  //   ambiguity and the program is ill-formed. Otherwise that set is
  //   the result of the lookup.
  // FIXME: support using declarations!
  QualType SubobjectType;
  int SubobjectNumber = 0;
  for (BasePaths::paths_iterator Path = Paths.begin(), PathEnd = Paths.end();
       Path != PathEnd; ++Path) {
    const BasePathElement &PathElement = Path->back();

    // Determine whether we're looking at a distinct sub-object or not.
    if (SubobjectType.isNull()) {
      // This is the first subobject we've looked at. Record it's type.
      SubobjectType = Context.getCanonicalType(PathElement.Base->getType());
      SubobjectNumber = PathElement.SubobjectNumber;
    } else if (SubobjectType 
                 != Context.getCanonicalType(PathElement.Base->getType())) {
      // We found members of the given name in two subobjects of
      // different types. This lookup is ambiguous.
      BasePaths *PathsOnHeap = new BasePaths;
      PathsOnHeap->swap(Paths);
      return LookupResult::CreateLookupResult(Context, PathsOnHeap, true);
    } else if (SubobjectNumber != PathElement.SubobjectNumber) {
      // We have a different subobject of the same type.

      // C++ [class.member.lookup]p5:
      //   A static member, a nested type or an enumerator defined in
      //   a base class T can unambiguously be found even if an object
      //   has more than one base class subobject of type T. 
      Decl *FirstDecl = *Path->Decls.first;
      if (isa<VarDecl>(FirstDecl) ||
          isa<TypeDecl>(FirstDecl) ||
          isa<EnumConstantDecl>(FirstDecl))
        continue;

      if (isa<CXXMethodDecl>(FirstDecl)) {
        // Determine whether all of the methods are static.
        bool AllMethodsAreStatic = true;
        for (DeclContext::lookup_iterator Func = Path->Decls.first;
             Func != Path->Decls.second; ++Func) {
          if (!isa<CXXMethodDecl>(*Func)) {
            assert(isa<TagDecl>(*Func) && "Non-function must be a tag decl");
            break;
          }

          if (!cast<CXXMethodDecl>(*Func)->isStatic()) {
            AllMethodsAreStatic = false;
            break;
          }
        }

        if (AllMethodsAreStatic)
          continue;
      }

      // We have found a nonstatic member name in multiple, distinct
      // subobjects. Name lookup is ambiguous.
      BasePaths *PathsOnHeap = new BasePaths;
      PathsOnHeap->swap(Paths);
      return LookupResult::CreateLookupResult(Context, PathsOnHeap, false);
    }
  }

  // Lookup in a base class succeeded; return these results.

  // If we found a function declaration, return an overload set.
  if (isa<FunctionDecl>(*Paths.front().Decls.first))
    return LookupResult::CreateLookupResult(Context, 
                        Paths.front().Decls.first, Paths.front().Decls.second);

  // We found a non-function declaration; return a single declaration.
  return LookupResult::CreateLookupResult(Context, *Paths.front().Decls.first);
}

/// @brief Performs name lookup for a name that was parsed in the
/// source code, and may contain a C++ scope specifier.
///
/// This routine is a convenience routine meant to be called from
/// contexts that receive a name and an optional C++ scope specifier
/// (e.g., "N::M::x"). It will then perform either qualified or
/// unqualified name lookup (with LookupQualifiedName or LookupName,
/// respectively) on the given name and return those results.
///
/// @param S        The scope from which unqualified name lookup will
/// begin.
/// 
/// @param SS       An optional C++ scope-specified, e.g., "::N::M".
///
/// @param Name     The name of the entity that name lookup will
/// search for.
///
/// @returns The result of qualified or unqualified name lookup.
Sema::LookupResult
Sema::LookupParsedName(Scope *S, const CXXScopeSpec *SS, 
                       DeclarationName Name, LookupNameKind NameKind,
                       bool RedeclarationOnly) {
  if (SS) {
    if (SS->isInvalid())
      return LookupResult::CreateLookupResult(Context, 0);

    if (SS->isSet())
      return LookupQualifiedName(static_cast<DeclContext *>(SS->getScopeRep()),
                                 Name, NameKind, RedeclarationOnly);
  }

  return LookupName(S, Name, NameKind, RedeclarationOnly);
}


/// @brief Produce a diagnostic describing the ambiguity that resulted
/// from name lookup.
///
/// @param Result       The ambiguous name lookup result.
/// 
/// @param Name         The name of the entity that name lookup was
/// searching for.
///
/// @param NameLoc      The location of the name within the source code.
///
/// @param LookupRange  A source range that provides more
/// source-location information concerning the lookup itself. For
/// example, this range might highlight a nested-name-specifier that
/// precedes the name.
///
/// @returns true
bool Sema::DiagnoseAmbiguousLookup(LookupResult &Result, DeclarationName Name,
                                   SourceLocation NameLoc, 
                                   SourceRange LookupRange) {
  assert(Result.isAmbiguous() && "Lookup result must be ambiguous");

  if (BasePaths *Paths = Result.getBasePaths())
  {
    if (Result.getKind() == LookupResult::AmbiguousBaseSubobjects) {
      QualType SubobjectType = Paths->front().back().Base->getType();
      Diag(NameLoc, diag::err_ambiguous_member_multiple_subobjects)
        << Name << SubobjectType << getAmbiguousPathsDisplayString(*Paths)
        << LookupRange;

      DeclContext::lookup_iterator Found = Paths->front().Decls.first;
      while (isa<CXXMethodDecl>(*Found) && cast<CXXMethodDecl>(*Found)->isStatic())
        ++Found;

      Diag((*Found)->getLocation(), diag::note_ambiguous_member_found);

      return true;
    } 

    assert(Result.getKind() == LookupResult::AmbiguousBaseSubobjectTypes &&
           "Unhandled form of name lookup ambiguity");

    Diag(NameLoc, diag::err_ambiguous_member_multiple_subobject_types)
      << Name << LookupRange;

    std::set<Decl *> DeclsPrinted;
    for (BasePaths::paths_iterator Path = Paths->begin(), PathEnd = Paths->end();
         Path != PathEnd; ++Path) {
      Decl *D = *Path->Decls.first;
      if (DeclsPrinted.insert(D).second)
        Diag(D->getLocation(), diag::note_ambiguous_member_found);
    }

    delete Paths;
    return true;
  } else if (Result.getKind() == LookupResult::AmbiguousReference) {

    Diag(NameLoc, diag::err_ambiguous_reference) << Name << LookupRange;

    NamedDecl **DI = reinterpret_cast<NamedDecl **>(Result.First),
       **DEnd = reinterpret_cast<NamedDecl **>(Result.Last);

    for (; DI != DEnd; ++DI)
      Diag((*DI)->getLocation(), diag::note_ambiguous_candidate) << *DI;

    delete[] reinterpret_cast<NamedDecl **>(Result.First);

    return true;
  }

  assert(false && "Unhandled form of name lookup ambiguity");

  // We can't reach here.
  return true;
}

// \brief Add the associated classes and namespaces for
// argument-dependent lookup with an argument of class type 
// (C++ [basic.lookup.koenig]p2). 
static void 
addAssociatedClassesAndNamespaces(CXXRecordDecl *Class, 
                                  ASTContext &Context,
                            Sema::AssociatedNamespaceSet &AssociatedNamespaces,
                            Sema::AssociatedClassSet &AssociatedClasses) {
  // C++ [basic.lookup.koenig]p2:
  //   [...]
  //     -- If T is a class type (including unions), its associated
  //        classes are: the class itself; the class of which it is a
  //        member, if any; and its direct and indirect base
  //        classes. Its associated namespaces are the namespaces in
  //        which its associated classes are defined. 

  // Add the class of which it is a member, if any.
  DeclContext *Ctx = Class->getDeclContext();
  if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
    AssociatedClasses.insert(EnclosingClass);

  // Add the associated namespace for this class.
  while (Ctx->isRecord())
    Ctx = Ctx->getParent();
  if (NamespaceDecl *EnclosingNamespace = dyn_cast<NamespaceDecl>(Ctx))
    AssociatedNamespaces.insert(EnclosingNamespace);

  // Add the class itself. If we've already seen this class, we don't
  // need to visit base classes.
  if (!AssociatedClasses.insert(Class))
    return;

  // FIXME: Handle class template specializations

  // Add direct and indirect base classes along with their associated
  // namespaces.
  llvm::SmallVector<CXXRecordDecl *, 32> Bases;
  Bases.push_back(Class);
  while (!Bases.empty()) {
    // Pop this class off the stack.
    Class = Bases.back();
    Bases.pop_back();

    // Visit the base classes.
    for (CXXRecordDecl::base_class_iterator Base = Class->bases_begin(),
                                         BaseEnd = Class->bases_end();
         Base != BaseEnd; ++Base) {
      const RecordType *BaseType = Base->getType()->getAsRecordType();
      CXXRecordDecl *BaseDecl = cast<CXXRecordDecl>(BaseType->getDecl());
      if (AssociatedClasses.insert(BaseDecl)) {
        // Find the associated namespace for this base class.
        DeclContext *BaseCtx = BaseDecl->getDeclContext();
        while (BaseCtx->isRecord())
          BaseCtx = BaseCtx->getParent();
        if (NamespaceDecl *EnclosingNamespace = dyn_cast<NamespaceDecl>(BaseCtx))
          AssociatedNamespaces.insert(EnclosingNamespace);

        // Make sure we visit the bases of this base class.
        if (BaseDecl->bases_begin() != BaseDecl->bases_end())
          Bases.push_back(BaseDecl);
      }
    }
  }
}

// \brief Add the associated classes and namespaces for
// argument-dependent lookup with an argument of type T
// (C++ [basic.lookup.koenig]p2). 
static void 
addAssociatedClassesAndNamespaces(QualType T, 
                                  ASTContext &Context,
                            Sema::AssociatedNamespaceSet &AssociatedNamespaces,
                            Sema::AssociatedClassSet &AssociatedClasses) {
  // C++ [basic.lookup.koenig]p2:
  //
  //   For each argument type T in the function call, there is a set
  //   of zero or more associated namespaces and a set of zero or more
  //   associated classes to be considered. The sets of namespaces and
  //   classes is determined entirely by the types of the function
  //   arguments (and the namespace of any template template
  //   argument). Typedef names and using-declarations used to specify
  //   the types do not contribute to this set. The sets of namespaces
  //   and classes are determined in the following way:
  T = Context.getCanonicalType(T).getUnqualifiedType();

  //    -- If T is a pointer to U or an array of U, its associated
  //       namespaces and classes are those associated with U. 
  //
  // We handle this by unwrapping pointer and array types immediately,
  // to avoid unnecessary recursion.
  while (true) {
    if (const PointerType *Ptr = T->getAsPointerType())
      T = Ptr->getPointeeType();
    else if (const ArrayType *Ptr = Context.getAsArrayType(T))
      T = Ptr->getElementType();
    else 
      break;
  }

  //     -- If T is a fundamental type, its associated sets of
  //        namespaces and classes are both empty.
  if (T->getAsBuiltinType())
    return;

  //     -- If T is a class type (including unions), its associated
  //        classes are: the class itself; the class of which it is a
  //        member, if any; and its direct and indirect base
  //        classes. Its associated namespaces are the namespaces in
  //        which its associated classes are defined. 
  if (const CXXRecordType *ClassType 
        = dyn_cast_or_null<CXXRecordType>(T->getAsRecordType())) {
    addAssociatedClassesAndNamespaces(ClassType->getDecl(), 
                                      Context, AssociatedNamespaces, 
                                      AssociatedClasses);
    return;
  }

  //     -- If T is an enumeration type, its associated namespace is
  //        the namespace in which it is defined. If it is class
  //        member, its associated class is the member’s class; else
  //        it has no associated class. 
  if (const EnumType *EnumT = T->getAsEnumType()) {
    EnumDecl *Enum = EnumT->getDecl();

    DeclContext *Ctx = Enum->getDeclContext();
    if (CXXRecordDecl *EnclosingClass = dyn_cast<CXXRecordDecl>(Ctx))
      AssociatedClasses.insert(EnclosingClass);

    // Add the associated namespace for this class.
    while (Ctx->isRecord())
      Ctx = Ctx->getParent();
    if (NamespaceDecl *EnclosingNamespace = dyn_cast<NamespaceDecl>(Ctx))
      AssociatedNamespaces.insert(EnclosingNamespace);

    return;
  }

  //     -- If T is a function type, its associated namespaces and
  //        classes are those associated with the function parameter
  //        types and those associated with the return type.
  if (const FunctionType *FunctionType = T->getAsFunctionType()) {
    // Return type
    addAssociatedClassesAndNamespaces(FunctionType->getResultType(), 
                                      Context,
                                      AssociatedNamespaces, AssociatedClasses);

    const FunctionTypeProto *Proto = dyn_cast<FunctionTypeProto>(FunctionType);
    if (!Proto)
      return;

    // Argument types
    for (FunctionTypeProto::arg_type_iterator Arg = Proto->arg_type_begin(),
                                           ArgEnd = Proto->arg_type_end(); 
         Arg != ArgEnd; ++Arg)
      addAssociatedClassesAndNamespaces(*Arg, Context,
                                        AssociatedNamespaces, AssociatedClasses);
      
    return;
  }

  //     -- If T is a pointer to a member function of a class X, its
  //        associated namespaces and classes are those associated
  //        with the function parameter types and return type,
  //        together with those associated with X. 
  //
  //     -- If T is a pointer to a data member of class X, its
  //        associated namespaces and classes are those associated
  //        with the member type together with those associated with
  //        X. 
  if (const MemberPointerType *MemberPtr = T->getAsMemberPointerType()) {
    // Handle the type that the pointer to member points to.
    addAssociatedClassesAndNamespaces(MemberPtr->getPointeeType(),
                                      Context,
                                      AssociatedNamespaces, AssociatedClasses);

    // Handle the class type into which this points.
    if (const RecordType *Class = MemberPtr->getClass()->getAsRecordType())
      addAssociatedClassesAndNamespaces(cast<CXXRecordDecl>(Class->getDecl()),
                                        Context,
                                        AssociatedNamespaces, AssociatedClasses);

    return;
  }

  // FIXME: What about block pointers?
  // FIXME: What about Objective-C message sends?
}

/// \brief Find the associated classes and namespaces for
/// argument-dependent lookup for a call with the given set of
/// arguments.
///
/// This routine computes the sets of associated classes and associated
/// namespaces searched by argument-dependent lookup 
/// (C++ [basic.lookup.argdep]) for a given set of arguments.
void 
Sema::FindAssociatedClassesAndNamespaces(Expr **Args, unsigned NumArgs,
                                 AssociatedNamespaceSet &AssociatedNamespaces,
                                 AssociatedClassSet &AssociatedClasses) {
  AssociatedNamespaces.clear();
  AssociatedClasses.clear();

  // C++ [basic.lookup.koenig]p2:
  //   For each argument type T in the function call, there is a set
  //   of zero or more associated namespaces and a set of zero or more
  //   associated classes to be considered. The sets of namespaces and
  //   classes is determined entirely by the types of the function
  //   arguments (and the namespace of any template template
  //   argument). 
  for (unsigned ArgIdx = 0; ArgIdx != NumArgs; ++ArgIdx) {
    Expr *Arg = Args[ArgIdx];

    if (Arg->getType() != Context.OverloadTy) {
      addAssociatedClassesAndNamespaces(Arg->getType(), Context,
                                        AssociatedNamespaces, AssociatedClasses);
      continue;
    }

    // [...] In addition, if the argument is the name or address of a
    // set of overloaded functions and/or function templates, its
    // associated classes and namespaces are the union of those
    // associated with each of the members of the set: the namespace
    // in which the function or function template is defined and the
    // classes and namespaces associated with its (non-dependent)
    // parameter types and return type.
    DeclRefExpr *DRE = 0;
    if (UnaryOperator *unaryOp = dyn_cast<UnaryOperator>(Arg)) {
      if (unaryOp->getOpcode() == UnaryOperator::AddrOf)
        DRE = dyn_cast<DeclRefExpr>(unaryOp->getSubExpr());
    } else
      DRE = dyn_cast<DeclRefExpr>(Arg);
    if (!DRE)
      continue;

    OverloadedFunctionDecl *Ovl 
      = dyn_cast<OverloadedFunctionDecl>(DRE->getDecl());
    if (!Ovl)
      continue;

    for (OverloadedFunctionDecl::function_iterator Func = Ovl->function_begin(),
                                                FuncEnd = Ovl->function_end();
         Func != FuncEnd; ++Func) {
      FunctionDecl *FDecl = cast<FunctionDecl>(*Func);

      // Add the namespace in which this function was defined. Note
      // that, if this is a member function, we do *not* consider the
      // enclosing namespace of its class.
      DeclContext *Ctx = FDecl->getDeclContext();
      if (NamespaceDecl *EnclosingNamespace = dyn_cast<NamespaceDecl>(Ctx))
        AssociatedNamespaces.insert(EnclosingNamespace);

      // Add the classes and namespaces associated with the parameter
      // types and return type of this function.
      addAssociatedClassesAndNamespaces(FDecl->getType(), Context,
                                        AssociatedNamespaces, AssociatedClasses);
    }
  }
}
