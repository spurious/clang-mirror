//===--- NestedNameSpecifier.cpp - C++ nested name specifiers -----*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the NestedNameSpecifier class, which represents
//  a C++ nested-name-specifier.
//
//===----------------------------------------------------------------------===//
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace clang;

NestedNameSpecifier *
NestedNameSpecifier::FindOrInsert(const ASTContext &Context,
                                  const NestedNameSpecifier &Mockup) {
  llvm::FoldingSetNodeID ID;
  Mockup.Profile(ID);

  void *InsertPos = 0;
  NestedNameSpecifier *NNS
    = Context.NestedNameSpecifiers.FindNodeOrInsertPos(ID, InsertPos);
  if (!NNS) {
    NNS = new (Context, 4) NestedNameSpecifier(Mockup);
    Context.NestedNameSpecifiers.InsertNode(NNS, InsertPos);
  }

  return NNS;
}

NestedNameSpecifier *
NestedNameSpecifier::Create(const ASTContext &Context,
                            NestedNameSpecifier *Prefix, IdentifierInfo *II) {
  assert(II && "Identifier cannot be NULL");
  assert((!Prefix || Prefix->isDependent()) && "Prefix must be dependent");

  NestedNameSpecifier Mockup;
  Mockup.Prefix.setPointer(Prefix);
  Mockup.Prefix.setInt(StoredIdentifier);
  Mockup.Specifier = II;
  return FindOrInsert(Context, Mockup);
}

NestedNameSpecifier *
NestedNameSpecifier::Create(const ASTContext &Context,
                            NestedNameSpecifier *Prefix, NamespaceDecl *NS) {
  assert(NS && "Namespace cannot be NULL");
  assert((!Prefix ||
          (Prefix->getAsType() == 0 && Prefix->getAsIdentifier() == 0)) &&
         "Broken nested name specifier");
  NestedNameSpecifier Mockup;
  Mockup.Prefix.setPointer(Prefix);
  Mockup.Prefix.setInt(StoredNamespaceOrAlias);
  Mockup.Specifier = NS;
  return FindOrInsert(Context, Mockup);
}

NestedNameSpecifier *
NestedNameSpecifier::Create(const ASTContext &Context,
                            NestedNameSpecifier *Prefix, 
                            NamespaceAliasDecl *Alias) {
  assert(Alias && "Namespace alias cannot be NULL");
  assert((!Prefix ||
          (Prefix->getAsType() == 0 && Prefix->getAsIdentifier() == 0)) &&
         "Broken nested name specifier");
  NestedNameSpecifier Mockup;
  Mockup.Prefix.setPointer(Prefix);
  Mockup.Prefix.setInt(StoredNamespaceOrAlias);
  Mockup.Specifier = Alias;
  return FindOrInsert(Context, Mockup);
}

NestedNameSpecifier *
NestedNameSpecifier::Create(const ASTContext &Context,
                            NestedNameSpecifier *Prefix,
                            bool Template, const Type *T) {
  assert(T && "Type cannot be NULL");
  NestedNameSpecifier Mockup;
  Mockup.Prefix.setPointer(Prefix);
  Mockup.Prefix.setInt(Template? StoredTypeSpecWithTemplate : StoredTypeSpec);
  Mockup.Specifier = const_cast<Type*>(T);
  return FindOrInsert(Context, Mockup);
}

NestedNameSpecifier *
NestedNameSpecifier::Create(const ASTContext &Context, IdentifierInfo *II) {
  assert(II && "Identifier cannot be NULL");
  NestedNameSpecifier Mockup;
  Mockup.Prefix.setPointer(0);
  Mockup.Prefix.setInt(StoredIdentifier);
  Mockup.Specifier = II;
  return FindOrInsert(Context, Mockup);
}

NestedNameSpecifier *
NestedNameSpecifier::GlobalSpecifier(const ASTContext &Context) {
  if (!Context.GlobalNestedNameSpecifier)
    Context.GlobalNestedNameSpecifier = new (Context, 4) NestedNameSpecifier();
  return Context.GlobalNestedNameSpecifier;
}

NestedNameSpecifier::SpecifierKind NestedNameSpecifier::getKind() const {
  if (Specifier == 0)
    return Global;

  switch (Prefix.getInt()) {
  case StoredIdentifier:
    return Identifier;

  case StoredNamespaceOrAlias:
    return isa<NamespaceDecl>(static_cast<NamedDecl *>(Specifier))? Namespace
                                                            : NamespaceAlias;

  case StoredTypeSpec:
    return TypeSpec;

  case StoredTypeSpecWithTemplate:
    return TypeSpecWithTemplate;
  }

  return Global;
}

/// \brief Retrieve the namespace stored in this nested name
/// specifier.
NamespaceDecl *NestedNameSpecifier::getAsNamespace() const {
  if (Prefix.getInt() == StoredNamespaceOrAlias)
    return dyn_cast<NamespaceDecl>(static_cast<NamedDecl *>(Specifier));

  return 0;
}

/// \brief Retrieve the namespace alias stored in this nested name
/// specifier.
NamespaceAliasDecl *NestedNameSpecifier::getAsNamespaceAlias() const {
  if (Prefix.getInt() == StoredNamespaceOrAlias)
    return dyn_cast<NamespaceAliasDecl>(static_cast<NamedDecl *>(Specifier));

  return 0;
}


/// \brief Whether this nested name specifier refers to a dependent
/// type or not.
bool NestedNameSpecifier::isDependent() const {
  switch (getKind()) {
  case Identifier:
    // Identifier specifiers always represent dependent types
    return true;

  case Namespace:
  case NamespaceAlias:
  case Global:
    return false;

  case TypeSpec:
  case TypeSpecWithTemplate:
    return getAsType()->isDependentType();
  }

  // Necessary to suppress a GCC warning.
  return false;
}

bool NestedNameSpecifier::containsUnexpandedParameterPack() const {
  switch (getKind()) {
  case Identifier:
    return getPrefix() && getPrefix()->containsUnexpandedParameterPack();

  case Namespace:
  case NamespaceAlias:
  case Global:
    return false;

  case TypeSpec:
  case TypeSpecWithTemplate:
    return getAsType()->containsUnexpandedParameterPack();
  }

  // Necessary to suppress a GCC warning.
  return false;  
}

/// \brief Print this nested name specifier to the given output
/// stream.
void
NestedNameSpecifier::print(llvm::raw_ostream &OS,
                           const PrintingPolicy &Policy) const {
  if (getPrefix())
    getPrefix()->print(OS, Policy);

  switch (getKind()) {
  case Identifier:
    OS << getAsIdentifier()->getName();
    break;

  case Namespace:
    OS << getAsNamespace()->getName();
    break;

  case NamespaceAlias:
    OS << getAsNamespaceAlias()->getName();
    break;

  case Global:
    break;

  case TypeSpecWithTemplate:
    OS << "template ";
    // Fall through to print the type.

  case TypeSpec: {
    std::string TypeStr;
    const Type *T = getAsType();

    PrintingPolicy InnerPolicy(Policy);
    InnerPolicy.SuppressScope = true;

    // Nested-name-specifiers are intended to contain minimally-qualified
    // types. An actual ElaboratedType will not occur, since we'll store
    // just the type that is referred to in the nested-name-specifier (e.g.,
    // a TypedefType, TagType, etc.). However, when we are dealing with
    // dependent template-id types (e.g., Outer<T>::template Inner<U>),
    // the type requires its own nested-name-specifier for uniqueness, so we
    // suppress that nested-name-specifier during printing.
    assert(!isa<ElaboratedType>(T) &&
           "Elaborated type in nested-name-specifier");
    if (const TemplateSpecializationType *SpecType
          = dyn_cast<TemplateSpecializationType>(T)) {
      // Print the template name without its corresponding
      // nested-name-specifier.
      SpecType->getTemplateName().print(OS, InnerPolicy, true);

      // Print the template argument list.
      TypeStr = TemplateSpecializationType::PrintTemplateArgumentList(
                                                          SpecType->getArgs(),
                                                       SpecType->getNumArgs(),
                                                                 InnerPolicy);
    } else {
      // Print the type normally
      TypeStr = QualType(T, 0).getAsString(InnerPolicy);
    }
    OS << TypeStr;
    break;
  }
  }

  OS << "::";
}

void NestedNameSpecifier::dump(const LangOptions &LO) {
  print(llvm::errs(), PrintingPolicy(LO));
}

unsigned 
NestedNameSpecifierLoc::getLocalDataLength(NestedNameSpecifier *Qualifier) {
  assert(Qualifier && "Expected a non-NULL qualifier");

  // Location of the trailing '::'.
  unsigned Length = sizeof(unsigned);

  switch (Qualifier->getKind()) {
  case NestedNameSpecifier::Global:
    // Nothing more to add.
    break;

  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
    // The location of the identifier or namespace name.
    Length += sizeof(unsigned);
    break;

  case NestedNameSpecifier::TypeSpecWithTemplate:
  case NestedNameSpecifier::TypeSpec:
    // The "void*" that points at the TypeLoc data.
    // Note: the 'template' keyword is part of the TypeLoc.
    Length += sizeof(void *);
    break;
  }

  return Length;
}

unsigned 
NestedNameSpecifierLoc::getDataLength(NestedNameSpecifier *Qualifier) {
  unsigned Length = 0;
  for (; Qualifier; Qualifier = Qualifier->getPrefix())
    Length += getLocalDataLength(Qualifier);
  return Length;
}

namespace {
  /// \brief Load a (possibly unaligned) source location from a given address
  /// and offset.
  SourceLocation LoadSourceLocation(void *Data, unsigned Offset) {
    unsigned Raw;
    memcpy(&Raw, static_cast<char *>(Data) + Offset, sizeof(unsigned));
    return SourceLocation::getFromRawEncoding(Raw);
  }
  
  /// \brief Load a (possibly unaligned) pointer from a given address and
  /// offset.
  void *LoadPointer(void *Data, unsigned Offset) {
    void *Result;
    memcpy(&Result, static_cast<char *>(Data) + Offset, sizeof(void*));
    return Result;
  }
}

SourceRange NestedNameSpecifierLoc::getSourceRange() {
  NestedNameSpecifierLoc First = *this;
  while (NestedNameSpecifierLoc Prefix= First.getPrefix())
    First = Prefix;
  
  return SourceRange(First.getLocalSourceRange().getBegin(), 
                     getLocalSourceRange().getEnd());
}

SourceRange NestedNameSpecifierLoc::getLocalSourceRange() {
  unsigned Offset = getDataLength(Qualifier->getPrefix());
  switch (Qualifier->getKind()) {
  case NestedNameSpecifier::Global:
    return LoadSourceLocation(Data, Offset);

  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
    return SourceRange(LoadSourceLocation(Data, Offset),
                       LoadSourceLocation(Data, Offset + sizeof(unsigned)));

  case NestedNameSpecifier::TypeSpecWithTemplate:
  case NestedNameSpecifier::TypeSpec: {
    // The "void*" that points at the TypeLoc data.
    // Note: the 'template' keyword is part of the TypeLoc.
    void *TypeData = LoadPointer(Data, Offset);
    TypeLoc TL(Qualifier->getAsType(), TypeData);
    return SourceRange(TL.getBeginLoc(),
                       LoadSourceLocation(Data, Offset + sizeof(void*)));
  }
  }
  
  return SourceRange();
}
