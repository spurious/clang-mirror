//===--- DeclBase.cpp - Declaration AST Node Implementation ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl and DeclContext classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/DenseMap.h"
using namespace clang;

//===----------------------------------------------------------------------===//
//  Statistics
//===----------------------------------------------------------------------===//

// temporary statistics gathering
static unsigned nFuncs = 0;
static unsigned nVars = 0;
static unsigned nParmVars = 0;
static unsigned nSUC = 0;
static unsigned nCXXSUC = 0;
static unsigned nEnumConst = 0;
static unsigned nEnumDecls = 0;
static unsigned nNamespaces = 0;
static unsigned nOverFuncs = 0;
static unsigned nTypedef = 0;
static unsigned nFieldDecls = 0;
static unsigned nInterfaceDecls = 0;
static unsigned nClassDecls = 0;
static unsigned nMethodDecls = 0;
static unsigned nProtocolDecls = 0;
static unsigned nForwardProtocolDecls = 0;
static unsigned nCategoryDecls = 0;
static unsigned nIvarDecls = 0;
static unsigned nAtDefsFieldDecls = 0;
static unsigned nObjCImplementationDecls = 0;
static unsigned nObjCCategoryImpl = 0;
static unsigned nObjCCompatibleAlias = 0;
static unsigned nObjCPropertyDecl = 0;
static unsigned nObjCPropertyImplDecl = 0;
static unsigned nLinkageSpecDecl = 0;
static unsigned nFileScopeAsmDecl = 0;
static unsigned nBlockDecls = 0;

static bool StatSwitch = false;

// This keeps track of all decl attributes. Since so few decls have attrs, we
// keep them in a hash map instead of wasting space in the Decl class.
typedef llvm::DenseMap<const Decl*, Attr*> DeclAttrMapTy;

static DeclAttrMapTy *DeclAttrs = 0;

const char *Decl::getDeclKindName() const {
  switch (DeclKind) {
  default: assert(0 && "Unknown decl kind!");
  case Namespace:           return "Namespace";
  case OverloadedFunction:  return "OverloadedFunction";
  case Typedef:             return "Typedef";
  case Function:            return "Function";
  case Var:                 return "Var";
  case ParmVar:             return "ParmVar";
  case EnumConstant:        return "EnumConstant";
  case ObjCIvar:            return "ObjCIvar";
  case ObjCInterface:       return "ObjCInterface";
  case ObjCImplementation:  return "ObjCImplementation";
  case ObjCClass:           return "ObjCClass";
  case ObjCMethod:          return "ObjCMethod";
  case ObjCProtocol:        return "ObjCProtocol";
  case ObjCProperty:        return "ObjCProperty";
  case ObjCPropertyImpl:    return "ObjCPropertyImpl";
  case ObjCForwardProtocol: return "ObjCForwardProtocol"; 
  case Record:              return "Record";
  case CXXRecord:           return "CXXRecord";
  case Enum:                return "Enum";
  case Block:               return "Block";
  }
}

bool Decl::CollectingStats(bool Enable) {
  if (Enable)
    StatSwitch = true;
  return StatSwitch;
}

void Decl::PrintStats() {
  fprintf(stderr, "*** Decl Stats:\n");
  fprintf(stderr, "  %d decls total.\n", 
          int(nFuncs+nVars+nParmVars+nFieldDecls+nSUC+nCXXSUC+
              nEnumDecls+nEnumConst+nTypedef+nInterfaceDecls+nClassDecls+
              nMethodDecls+nProtocolDecls+nCategoryDecls+nIvarDecls+
              nAtDefsFieldDecls+nNamespaces+nOverFuncs));
  fprintf(stderr, "    %d namespace decls, %d each (%d bytes)\n", 
          nNamespaces, (int)sizeof(NamespaceDecl), 
          int(nNamespaces*sizeof(NamespaceDecl)));
  fprintf(stderr, "    %d overloaded function decls, %d each (%d bytes)\n", 
          nOverFuncs, (int)sizeof(OverloadedFunctionDecl), 
          int(nOverFuncs*sizeof(OverloadedFunctionDecl)));
  fprintf(stderr, "    %d function decls, %d each (%d bytes)\n", 
          nFuncs, (int)sizeof(FunctionDecl), int(nFuncs*sizeof(FunctionDecl)));
  fprintf(stderr, "    %d variable decls, %d each (%d bytes)\n", 
          nVars, (int)sizeof(VarDecl), 
          int(nVars*sizeof(VarDecl)));
  fprintf(stderr, "    %d parameter variable decls, %d each (%d bytes)\n", 
          nParmVars, (int)sizeof(ParmVarDecl),
          int(nParmVars*sizeof(ParmVarDecl)));
  fprintf(stderr, "    %d field decls, %d each (%d bytes)\n", 
          nFieldDecls, (int)sizeof(FieldDecl),
          int(nFieldDecls*sizeof(FieldDecl)));
  fprintf(stderr, "    %d @defs generated field decls, %d each (%d bytes)\n",
          nAtDefsFieldDecls, (int)sizeof(ObjCAtDefsFieldDecl),
          int(nAtDefsFieldDecls*sizeof(ObjCAtDefsFieldDecl)));
  fprintf(stderr, "    %d struct/union/class decls, %d each (%d bytes)\n", 
          nSUC, (int)sizeof(RecordDecl),
          int(nSUC*sizeof(RecordDecl)));
  fprintf(stderr, "    %d C++ struct/union/class decls, %d each (%d bytes)\n", 
          nCXXSUC, (int)sizeof(CXXRecordDecl),
          int(nCXXSUC*sizeof(CXXRecordDecl)));
  fprintf(stderr, "    %d enum decls, %d each (%d bytes)\n", 
          nEnumDecls, (int)sizeof(EnumDecl), 
          int(nEnumDecls*sizeof(EnumDecl)));
  fprintf(stderr, "    %d enum constant decls, %d each (%d bytes)\n", 
          nEnumConst, (int)sizeof(EnumConstantDecl),
          int(nEnumConst*sizeof(EnumConstantDecl)));
  fprintf(stderr, "    %d typedef decls, %d each (%d bytes)\n", 
          nTypedef, (int)sizeof(TypedefDecl),int(nTypedef*sizeof(TypedefDecl)));
  // Objective-C decls...
  fprintf(stderr, "    %d interface decls, %d each (%d bytes)\n", 
          nInterfaceDecls, (int)sizeof(ObjCInterfaceDecl),
          int(nInterfaceDecls*sizeof(ObjCInterfaceDecl)));
  fprintf(stderr, "    %d instance variable decls, %d each (%d bytes)\n", 
          nIvarDecls, (int)sizeof(ObjCIvarDecl),
          int(nIvarDecls*sizeof(ObjCIvarDecl)));
  fprintf(stderr, "    %d class decls, %d each (%d bytes)\n", 
          nClassDecls, (int)sizeof(ObjCClassDecl),
          int(nClassDecls*sizeof(ObjCClassDecl)));
  fprintf(stderr, "    %d method decls, %d each (%d bytes)\n", 
          nMethodDecls, (int)sizeof(ObjCMethodDecl),
          int(nMethodDecls*sizeof(ObjCMethodDecl)));
  fprintf(stderr, "    %d protocol decls, %d each (%d bytes)\n", 
          nProtocolDecls, (int)sizeof(ObjCProtocolDecl),
          int(nProtocolDecls*sizeof(ObjCProtocolDecl)));
  fprintf(stderr, "    %d forward protocol decls, %d each (%d bytes)\n", 
          nForwardProtocolDecls, (int)sizeof(ObjCForwardProtocolDecl),
          int(nForwardProtocolDecls*sizeof(ObjCForwardProtocolDecl)));
  fprintf(stderr, "    %d category decls, %d each (%d bytes)\n", 
          nCategoryDecls, (int)sizeof(ObjCCategoryDecl),
          int(nCategoryDecls*sizeof(ObjCCategoryDecl)));

  fprintf(stderr, "    %d class implementation decls, %d each (%d bytes)\n", 
          nObjCImplementationDecls, (int)sizeof(ObjCImplementationDecl),
          int(nObjCImplementationDecls*sizeof(ObjCImplementationDecl)));

  fprintf(stderr, "    %d class implementation decls, %d each (%d bytes)\n", 
          nObjCCategoryImpl, (int)sizeof(ObjCCategoryImplDecl),
          int(nObjCCategoryImpl*sizeof(ObjCCategoryImplDecl)));

  fprintf(stderr, "    %d compatibility alias decls, %d each (%d bytes)\n", 
          nObjCCompatibleAlias, (int)sizeof(ObjCCompatibleAliasDecl),
          int(nObjCCompatibleAlias*sizeof(ObjCCompatibleAliasDecl)));
  
  fprintf(stderr, "    %d property decls, %d each (%d bytes)\n", 
          nObjCPropertyDecl, (int)sizeof(ObjCPropertyDecl),
          int(nObjCPropertyDecl*sizeof(ObjCPropertyDecl)));
  
  fprintf(stderr, "    %d property implementation decls, %d each (%d bytes)\n", 
          nObjCPropertyImplDecl, (int)sizeof(ObjCPropertyImplDecl),
          int(nObjCPropertyImplDecl*sizeof(ObjCPropertyImplDecl)));
  
  fprintf(stderr, "Total bytes = %d\n", 
          int(nFuncs*sizeof(FunctionDecl)+
              nVars*sizeof(VarDecl)+nParmVars*sizeof(ParmVarDecl)+
              nFieldDecls*sizeof(FieldDecl)+nSUC*sizeof(RecordDecl)+
              nCXXSUC*sizeof(CXXRecordDecl)+
              nEnumDecls*sizeof(EnumDecl)+nEnumConst*sizeof(EnumConstantDecl)+
              nTypedef*sizeof(TypedefDecl)+
              nInterfaceDecls*sizeof(ObjCInterfaceDecl)+
              nIvarDecls*sizeof(ObjCIvarDecl)+
              nClassDecls*sizeof(ObjCClassDecl)+
              nMethodDecls*sizeof(ObjCMethodDecl)+
              nProtocolDecls*sizeof(ObjCProtocolDecl)+
              nForwardProtocolDecls*sizeof(ObjCForwardProtocolDecl)+
              nCategoryDecls*sizeof(ObjCCategoryDecl)+
              nObjCImplementationDecls*sizeof(ObjCImplementationDecl)+
              nObjCCategoryImpl*sizeof(ObjCCategoryImplDecl)+
              nObjCCompatibleAlias*sizeof(ObjCCompatibleAliasDecl)+
              nObjCPropertyDecl*sizeof(ObjCPropertyDecl)+
              nObjCPropertyImplDecl*sizeof(ObjCPropertyImplDecl)+
              nLinkageSpecDecl*sizeof(LinkageSpecDecl)+
              nFileScopeAsmDecl*sizeof(FileScopeAsmDecl)+
              nNamespaces*sizeof(NamespaceDecl)+
              nOverFuncs*sizeof(OverloadedFunctionDecl)));
    
}

void Decl::addDeclKind(Kind k) {
  switch (k) {
  case Namespace:           nNamespaces++; break;
  case OverloadedFunction:  nOverFuncs++; break;
  case Typedef:             nTypedef++; break;
  case Function:            nFuncs++; break;
  case Var:                 nVars++; break;
  case ParmVar:             nParmVars++; break;
  case EnumConstant:        nEnumConst++; break;
  case Field:               nFieldDecls++; break;
  case Record:              nSUC++; break;
  case Enum:                nEnumDecls++; break;
  case ObjCInterface:       nInterfaceDecls++; break;
  case ObjCClass:           nClassDecls++; break;
  case ObjCMethod:          nMethodDecls++; break;
  case ObjCProtocol:        nProtocolDecls++; break;
  case ObjCForwardProtocol: nForwardProtocolDecls++; break;
  case ObjCCategory:        nCategoryDecls++; break;
  case ObjCIvar:            nIvarDecls++; break;
  case ObjCAtDefsField:     nAtDefsFieldDecls++; break;
  case ObjCImplementation:  nObjCImplementationDecls++; break;
  case ObjCCategoryImpl:    nObjCCategoryImpl++; break;
  case ObjCCompatibleAlias: nObjCCompatibleAlias++; break;
  case ObjCProperty:        nObjCPropertyDecl++; break;
  case ObjCPropertyImpl:    nObjCPropertyImplDecl++; break;
  case LinkageSpec:         nLinkageSpecDecl++; break;
  case FileScopeAsm:        nFileScopeAsmDecl++; break;
  case Block:               nBlockDecls++; break;
  case ImplicitParam:
  case TranslationUnit:     break;

  case CXXRecord:           nCXXSUC++; break;
  // FIXME: Statistics for C++ decls.
  case TemplateTypeParm:
  case NonTypeTemplateParm:
  case CXXMethod:
  case CXXConstructor:
  case CXXDestructor:
  case CXXConversion:
  case CXXClassVar:
    break;
  }
}

//===----------------------------------------------------------------------===//
// Decl Implementation
//===----------------------------------------------------------------------===//

// Out-of-line virtual method providing a home for Decl.
Decl::~Decl() {
  if (!HasAttrs)
    return;
  
  DeclAttrMapTy::iterator it = DeclAttrs->find(this);
  assert(it != DeclAttrs->end() && "No attrs found but HasAttrs is true!");

  // release attributes.
  delete it->second;
  invalidateAttrs();
}

void Decl::addAttr(Attr *NewAttr) {
  if (!DeclAttrs)
    DeclAttrs = new DeclAttrMapTy();
  
  Attr *&ExistingAttr = (*DeclAttrs)[this];

  NewAttr->setNext(ExistingAttr);
  ExistingAttr = NewAttr;
  
  HasAttrs = true;
}

void Decl::invalidateAttrs() {
  if (!HasAttrs) return;

  HasAttrs = false;
  (*DeclAttrs)[this] = 0;
  DeclAttrs->erase(this);

  if (DeclAttrs->empty()) {
    delete DeclAttrs;
    DeclAttrs = 0;
  }
}

const Attr *Decl::getAttrs() const {
  if (!HasAttrs)
    return 0;
  
  return (*DeclAttrs)[this];
}

void Decl::swapAttrs(Decl *RHS) {
  bool HasLHSAttr = this->HasAttrs;
  bool HasRHSAttr = RHS->HasAttrs;
  
  // Usually, neither decl has attrs, nothing to do.
  if (!HasLHSAttr && !HasRHSAttr) return;
  
  // If 'this' has no attrs, swap the other way.
  if (!HasLHSAttr)
    return RHS->swapAttrs(this);
  
  // Handle the case when both decls have attrs.
  if (HasRHSAttr) {
    std::swap((*DeclAttrs)[this], (*DeclAttrs)[RHS]);
    return;
  }
  
  // Otherwise, LHS has an attr and RHS doesn't.
  (*DeclAttrs)[RHS] = (*DeclAttrs)[this];
  (*DeclAttrs).erase(this);
  this->HasAttrs = false;
  RHS->HasAttrs = true;
}


void Decl::Destroy(ASTContext& C) {

  if (ScopedDecl* SD = dyn_cast<ScopedDecl>(this)) {    

    // Observe the unrolled recursion.  By setting N->NextDeclarator = 0x0
    // within the loop, only the Destroy method for the first ScopedDecl
    // will deallocate all of the ScopedDecls in a chain.
    
    ScopedDecl* N = SD->getNextDeclarator();
    
    while (N) {
      ScopedDecl* Tmp = N->getNextDeclarator();
      N->NextDeclarator = 0x0;
      N->Destroy(C);
      N = Tmp;
    }
  }  
  
  this->~Decl();
  C.getAllocator().Deallocate((void *)this);
}

Decl *Decl::castFromDeclContext (const DeclContext *D) {
  return DeclContext::CastTo<Decl>(D);
}

DeclContext *Decl::castToDeclContext(const Decl *D) {
  return DeclContext::CastTo<DeclContext>(D);
}

//===----------------------------------------------------------------------===//
// DeclContext Implementation
//===----------------------------------------------------------------------===//

const DeclContext *DeclContext::getParent() const {
  if (const ScopedDecl *SD = dyn_cast<ScopedDecl>(this))
    return SD->getDeclContext();
  else if (const BlockDecl *BD = dyn_cast<BlockDecl>(this))
    return BD->getParentContext();
  else
    return NULL;
}

const DeclContext *DeclContext::getLexicalParent() const {
  if (const ScopedDecl *SD = dyn_cast<ScopedDecl>(this))
    return SD->getLexicalDeclContext();
  return getParent();
}

/// TwoNamedDecls - Stores up to two NamedDecls. The first
/// declaration, if any, is in the ordinary identifier namespace, and
/// corresponds to values (functions, variables, etc.). The second
/// declaration, if any, is in the tag identifier namespace, and
/// corresponds to tag types (classes, enums).
struct TwoNamedDecls {
  NamedDecl* Decls[2];
};

// FIXME: We really want to use a DenseSet here to eliminate the
// redundant storage of the declaration names, but (1) it doesn't give
// us the ability to search based on DeclarationName, (2) we really
// need something more like a DenseMultiSet, and (3) it's
// implemented in terms of DenseMap anyway.
typedef llvm::DenseMap<DeclarationName, TwoNamedDecls> StoredDeclsMap;

DeclContext::~DeclContext() {
  unsigned Size = LookupPtr.getInt();
  if (Size == LookupIsMap) {
    StoredDeclsMap *Map = static_cast<StoredDeclsMap*>(LookupPtr.getPointer());
    delete Map;
  } else {
    NamedDecl **Array = static_cast<NamedDecl**>(LookupPtr.getPointer());
    delete [] Array;
  }
}

void DeclContext::DestroyDecls(ASTContext &C) {
  for (decl_iterator D = Decls.begin(); D != Decls.end(); ++D) {
    if ((*D)->getLexicalDeclContext() == this)
      (*D)->Destroy(C);
  }
}

DeclContext *DeclContext::getPrimaryContext(ASTContext &Context) {
  switch (DeclKind) {
  case Decl::Block:
  case Decl::TranslationUnit:
    // There is only one DeclContext for these entities.
    return this;

  case Decl::Namespace:
    // The original namespace is our primary context.
    return static_cast<NamespaceDecl*>(this)->getOriginalNamespace();

  case Decl::Enum:
    // The declaration associated with the enumeration type is our
    // primary context.
    return Context.getTypeDeclType(static_cast<EnumDecl*>(this))
             ->getAsEnumType()->getDecl();

  case Decl::Record:
  case Decl::CXXRecord: {
    // The declaration associated with the type is be our primary
    // context. 
#if 0
    // FIXME: This is what we expect to do. However, it doesn't work
    // because ASTContext::setTagDefinition changes the result of
    // Context.getTypeDeclType, meaning that our "primary" declaration
    // of a RecordDecl/CXXRecordDecl will change, and we won't be able
    // to find any values inserted into the earlier "primary"
    // declaration. We need better tracking of redeclarations and
    // definitions.
    QualType Type = Context.getTypeDeclType(static_cast<RecordDecl*>(this));
    return Type->getAsRecordType()->getDecl();
#else
    // FIXME: This hack will work for now, because the declaration we
    // create when we're defining the record is the one we'll use as
    // the definition later.
    return this;
#endif
  }

  case Decl::ObjCMethod:
    return this;

  case Decl::ObjCInterface:
    // FIXME: Can Objective-C interfaces be forward-declared?
    return this;

  default:
    assert(DeclKind >= Decl::FunctionFirst && DeclKind <= Decl::FunctionLast &&
          "Unknown DeclContext kind");
    return this;
  }
}

DeclContext *DeclContext::getNextContext() {
  switch (DeclKind) {
  case Decl::Block:
  case Decl::TranslationUnit:
  case Decl::Enum:
  case Decl::Record:
  case Decl::CXXRecord:
  case Decl::ObjCMethod:
  case Decl::ObjCInterface:
    // There is only one DeclContext for these entities.
    return 0;

  case Decl::Namespace:
    // Return the next namespace
    return static_cast<NamespaceDecl*>(this)->getNextNamespace();

  default:
    assert(DeclKind >= Decl::FunctionFirst && DeclKind <= Decl::FunctionLast &&
          "Unknown DeclContext kind");
    return 0;
  }
}

void DeclContext::addDecl(ASTContext &Context, ScopedDecl *D, bool AllowLookup) {
  Decls.push_back(D);
  if (AllowLookup)
    D->getDeclContext()->insert(Context, D);
}

DeclContext::lookup_result 
DeclContext::lookup(ASTContext &Context, DeclarationName Name) {
  DeclContext *PrimaryContext = getPrimaryContext(Context);
  if (PrimaryContext != this)
    return PrimaryContext->lookup(Context, Name);

  /// If there is no lookup data structure, build one now by talking
  /// all of the linked DeclContexts (in declaration order!) and
  /// inserting their values.
  if (LookupPtr.getPointer() == 0) {
    for (DeclContext *DCtx = this; DCtx; DCtx = DCtx->getNextContext())
      for (decl_iterator D = DCtx->decls_begin(); D != DCtx->decls_end(); ++D)
        insertImpl(*D);
  }

  lookup_result Result(0, 0);
  if (isLookupMap()) {
    StoredDeclsMap *Map = static_cast<StoredDeclsMap*>(LookupPtr.getPointer());
    StoredDeclsMap::iterator Pos = Map->find(Name);
    if (Pos != Map->end()) {
      Result.first = Pos->second.Decls[0]? &Pos->second.Decls[0] 
                                        : &Pos->second.Decls[1];
      Result.second = Pos->second.Decls[1]? &Pos->second.Decls[2]
                                         : &Pos->second.Decls[1];
    }
    return Result;
  } 

  // We have a small array. Look into it.
  unsigned Size = LookupPtr.getInt();
  NamedDecl **Array = static_cast<NamedDecl**>(LookupPtr.getPointer());
  for (unsigned Idx = 0; Idx != Size; ++Idx)
    if (Array[Idx]->getDeclName() == Name) {
      Result.first = &Array[Idx];
      Result.second = Result.first + 1;
      if (Idx + 1 < Size && Array[Idx + 1]->getDeclName() == Name)
        ++Result.second;
      break;
    }

  return Result;
}

DeclContext::lookup_const_result 
DeclContext::lookup(ASTContext &Context, DeclarationName Name) const {
  return const_cast<DeclContext*>(this)->lookup(Context, Name);
}

void DeclContext::insert(ASTContext &Context, NamedDecl *D) {
  DeclContext *PrimaryContext = getPrimaryContext(Context);
  if (PrimaryContext != this) {
    PrimaryContext->insert(Context, D);
    return;
  }

  // If we already have a lookup data structure, perform the insertion
  // into it. Otherwise, be lazy and don't build that structure until
  // someone asks for it.
  if (LookupPtr.getPointer())
    insertImpl(D);
}

void DeclContext::insertImpl(NamedDecl *D) {
  if (!isLookupMap()) {
    unsigned Size = LookupPtr.getInt();

    // The lookup data is stored as an array. Search through the array
    // to find the insertion location.
    NamedDecl **Array;
    if (Size == 0) {
      Array = new NamedDecl*[LookupIsMap - 1];
      LookupPtr.setPointer(Array);
    } else {
      Array = static_cast<NamedDecl **>(LookupPtr.getPointer());
    }

    // We always keep declarations of the same name next to each other
    // in the array, so that it is easy to return multiple results
    // from lookup(). There will be zero, one, or two declarations of
    // the same name.
    unsigned Match;
    for (Match = 0; Match != Size; ++Match)
      if (Array[Match]->getDeclName() == D->getDeclName())
        break;

    if (Match != Size) {
      // We found another declaration with the same name. If it's also
      // in the same identifier namespace, update the declaration in
      // place.
      Decl::IdentifierNamespace NS = D->getIdentifierNamespace();
      if (Array[Match]->getIdentifierNamespace() == NS) {
       Array[Match] = D;
       return;
      }
      if (Match + 1 < Size && Array[Match + 1]->getIdentifierNamespace() == NS) {
       Array[Match + 1] = D;
       return;
      }

      // If there is an existing declaration in the namespace of
      // ordinary identifiers, then it must precede the tag
      // declaration for C++ name lookup to operate properly. Therefore,
      // if our match is an ordinary name and the new name is in the
      // tag namespace, we'll insert the new declaration after it. 
      if (Match != Size && (NS == Decl::IDNS_Tag) && 
         (Array[Match]->getIdentifierNamespace() & Decl::IDNS_Ordinary))
       ++Match;
    }
       
    if (Size < LookupIsMap - 1) {
      // The new declaration will fit in the array. Insert the new
      // declaration at the position Match in the array. 
      for (unsigned Idx = Size; Idx > Match; --Idx)
       Array[Idx] = Array[Idx-1];
      
      Array[Match] = D;
      LookupPtr.setInt(Size + 1);
      return;
    }

    // We've reached capacity in this array. Create a map and copy in
    // all of the declarations that were stored in the array.
    StoredDeclsMap *Map = new StoredDeclsMap(16);
    LookupPtr.setPointer(Map);
    LookupPtr.setInt(LookupIsMap);
    for (unsigned Idx = 0; Idx != LookupIsMap - 1; ++Idx) 
      insertImpl(Array[Idx]);
    delete [] Array;

    // Fall through to perform insertion into the map.
  } 

  // Insert this declaration into the map.
  StoredDeclsMap *Map = static_cast<StoredDeclsMap*>(LookupPtr.getPointer());
  StoredDeclsMap::iterator Pos = Map->find(D->getDeclName());
  unsigned IndexOfD = D->getIdentifierNamespace() & Decl::IDNS_Ordinary? 0 : 1;

  if (Pos == Map->end()) {
    // Put this declaration into the appropriate slot.
    TwoNamedDecls Val;
    Val.Decls[IndexOfD] = D;
    Val.Decls[!IndexOfD] = 0;
    Map->insert(std::make_pair(D->getDeclName(),Val)).first;
  } else {
    Pos->second.Decls[IndexOfD] = D;
  }
}
