//===- CIndexUSR.cpp - Clang-C Source Indexing Library --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the generation and use of USRs from CXEntities.
//
//===----------------------------------------------------------------------===//

#include "CIndexer.h"
#include "CXCursor.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::cxstring;

//===----------------------------------------------------------------------===//
// USR generation.
//===----------------------------------------------------------------------===//

namespace {
class USRGenerator : public DeclVisitor<USRGenerator> {
  llvm::raw_ostream &Out;
  bool IgnoreResults;
  ASTUnit *AU;
  bool generatedLoc;
public:
  USRGenerator(ASTUnit *au, llvm::raw_ostream &out)
    : Out(out), IgnoreResults(false), AU(au), generatedLoc(false) {}

  bool ignoreResults() const { return IgnoreResults; }

  // Visitation methods from generating USRs from AST elements.
  void VisitDeclContext(DeclContext *D);
  void VisitFieldDecl(FieldDecl *D);
  void VisitFunctionDecl(FunctionDecl *D);
  void VisitNamedDecl(NamedDecl *D);
  void VisitNamespaceDecl(NamespaceDecl *D);
  void VisitObjCClassDecl(ObjCClassDecl *CD);
  void VisitObjCContainerDecl(ObjCContainerDecl *CD);
  void VisitObjCForwardProtocolDecl(ObjCForwardProtocolDecl *P);
  void VisitObjCMethodDecl(ObjCMethodDecl *MD);
  void VisitObjCPropertyDecl(ObjCPropertyDecl *D);
  void VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
  void VisitTagDecl(TagDecl *D);
  void VisitTypedefDecl(TypedefDecl *D);
  void VisitVarDecl(VarDecl *D);
  void VisitLinkageSpecDecl(LinkageSpecDecl *D) {
    IgnoreResults = true;
    return;
  }

  /// Generate the string component containing the location of the
  ///  declaration.
  bool GenLoc(const Decl *D);

  /// String generation methods used both by the visitation methods
  /// and from other clients that want to directly generate USRs.  These
  /// methods do not construct complete USRs (which incorporate the parents
  /// of an AST element), but only the fragments concerning the AST element
  /// itself.

  /// Generate a USR fragment for a named declaration.  This does
  /// not include the USR component for the parent.
  void GenNamedDecl(llvm::StringRef name);

  /// Generate a USR for an Objective-C class.
  void GenObjCClass(llvm::StringRef cls);
  /// Generate a USR for an Objective-C class category.
  void GenObjCCategory(llvm::StringRef cls, llvm::StringRef cat);
  /// Generate a USR fragment for an Objective-C instance variable.  The
  /// complete USR can be created by concatenating the USR for the
  /// encompassing class with this USR fragment.
  void GenObjCIvar(llvm::StringRef ivar);
  /// Generate a USR fragment for an Objective-C method.
  void GenObjCMethod(llvm::StringRef sel, bool isInstanceMethod);
  /// Generate a USR fragment for an Objective-C property.
  void GenObjCProperty(llvm::StringRef prop);
  /// Generate a USR for an Objective-C protocol.
  void GenObjCProtocol(llvm::StringRef prot);

  void VisitType(QualType T);
};

class StringUSRGenerator {
private:
  llvm::SmallString<1024> StrBuf;
  llvm::raw_svector_ostream Out;
  USRGenerator UG;
public:
  StringUSRGenerator(const CXCursor *C = 0)
    : Out(StrBuf), UG(C ? cxcursor::getCursorASTUnit(*C) : 0, Out) {
    // Add the USR space prefix.
    Out << "c:";
  }

  llvm::StringRef str() {
    return Out.str();
  }

  USRGenerator* operator->() { return &UG; }

  template <typename T>
  llvm::raw_svector_ostream &operator<<(const T &x) {
    Out << x;
    return Out;
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Generating USRs from ASTS.
//===----------------------------------------------------------------------===//

static bool InAnonymousNamespace(const Decl *D) {
  if (const NamespaceDecl *ND = dyn_cast<NamespaceDecl>(D->getDeclContext()))
    return ND->isAnonymousNamespace();
  return false;
}

static inline bool ShouldGenerateLocation(const NamedDecl *D) {
  return D->getLinkage() != ExternalLinkage && !InAnonymousNamespace(D);
}

void USRGenerator::VisitDeclContext(DeclContext *DC) {
  if (NamedDecl *D = dyn_cast<NamedDecl>(DC))
    Visit(D);
}

void USRGenerator::VisitFieldDecl(FieldDecl *D) {
  const std::string &s = D->getNameAsString();
  if (s.empty()) {
    // Bit fields can be anonymous.
    IgnoreResults = true;
    return;
  }
  VisitDeclContext(D->getDeclContext());
  Out << (isa<ObjCIvarDecl>(D) ? "@" : "@FI@") << s;
}

void USRGenerator::VisitFunctionDecl(FunctionDecl *D) {
  if (ShouldGenerateLocation(D) && GenLoc(D))
    return;

  VisitDeclContext(D->getDeclContext());
  Out << "@F@" << D->getNameAsString();

  ASTContext &Ctx = AU->getASTContext();
  if (!Ctx.getLangOptions().CPlusPlus || D->isExternC())
    return;

  // Mangle in type information for the arguments.
  for (FunctionDecl::param_iterator I = D->param_begin(), E = D->param_end();
       I != E; ++I) {
    Out << '#';
    if (ParmVarDecl *PD = *I)
      VisitType(PD->getType());
  }
  if (D->isVariadic())
    Out << '.';
}

void USRGenerator::VisitNamedDecl(NamedDecl *D) {
  VisitDeclContext(D->getDeclContext());
  const std::string &s = D->getNameAsString();
  // The string can be empty if the declaration has no name; e.g., it is
  // the ParmDecl with no name for declaration of a function pointer type, e.g.:
  //  	void  (*f)(void *);
  // In this case, don't generate a USR.
  if (s.empty())
    IgnoreResults = true;
  else
    GenNamedDecl(s);
}

void USRGenerator::VisitVarDecl(VarDecl *D) {
  // VarDecls can be declared 'extern' within a function or method body,
  // but their enclosing DeclContext is the function, not the TU.  We need
  // to check the storage class to correctly generate the USR.
  if (ShouldGenerateLocation(D) && GenLoc(D))
    return;

  VisitDeclContext(D->getDeclContext());

  // Variables always have simple names.
  llvm::StringRef s = D->getName();

  // The string can be empty if the declaration has no name; e.g., it is
  // the ParmDecl with no name for declaration of a function pointer type, e.g.:
  //  	void  (*f)(void *);
  // In this case, don't generate a USR.
  if (s.empty())
    IgnoreResults = true;
  else
    GenNamedDecl(s);
}

void USRGenerator::VisitNamespaceDecl(NamespaceDecl *D) {
  if (D->isAnonymousNamespace()) {
    Out << "@aN";
    return;
  }

  VisitDeclContext(D->getDeclContext());
  if (!IgnoreResults)
    Out << "@N@" << D->getName();
}

void USRGenerator::VisitObjCMethodDecl(ObjCMethodDecl *D) {
  Visit(cast<Decl>(D->getDeclContext()));
  GenObjCMethod(DeclarationName(D->getSelector()).getAsString(),
                D->isInstanceMethod());
}

void USRGenerator::VisitObjCClassDecl(ObjCClassDecl *D) {
  // FIXME: @class declarations can refer to multiple classes.  We need
  //  to be able to traverse these.
  IgnoreResults = true;
}

void USRGenerator::VisitObjCForwardProtocolDecl(ObjCForwardProtocolDecl *D) {
  // FIXME: @protocol declarations can refer to multiple protocols.  We need
  //  to be able to traverse these.
  IgnoreResults = true;
}

void USRGenerator::VisitObjCContainerDecl(ObjCContainerDecl *D) {
  switch (D->getKind()) {
    default:
      assert(false && "Invalid ObjC container.");
    case Decl::ObjCInterface:
    case Decl::ObjCImplementation:
      GenObjCClass(D->getName());
      break;
    case Decl::ObjCCategory: {
      ObjCCategoryDecl *CD = cast<ObjCCategoryDecl>(D);
      ObjCInterfaceDecl *ID = CD->getClassInterface();
      if (!ID) {
        // Handle invalid code where the @interface might not
        // have been specified.
        // FIXME: We should be able to generate this USR even if the
        // @interface isn't available.
        IgnoreResults = true;
        return;
      }
      GenObjCCategory(ID->getName(), CD->getName());
      break;
    }
    case Decl::ObjCCategoryImpl: {
      ObjCCategoryImplDecl *CD = cast<ObjCCategoryImplDecl>(D);
      ObjCInterfaceDecl *ID = CD->getClassInterface();
      if (!ID) {
        // Handle invalid code where the @interface might not
        // have been specified.
        // FIXME: We should be able to generate this USR even if the
        // @interface isn't available.
        IgnoreResults = true;
        return;
      }
      GenObjCCategory(ID->getName(), CD->getName());
      break;
    }
    case Decl::ObjCProtocol:
      GenObjCProtocol(cast<ObjCProtocolDecl>(D)->getName());
      break;
  }
}

void USRGenerator::VisitObjCPropertyDecl(ObjCPropertyDecl *D) {
  Visit(cast<Decl>(D->getDeclContext()));
  GenObjCProperty(D->getName());
}

void USRGenerator::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
  if (ObjCPropertyDecl *PD = D->getPropertyDecl()) {
    VisitObjCPropertyDecl(PD);
    return;
  }

  IgnoreResults = true;
}

void USRGenerator::VisitTagDecl(TagDecl *D) {
  // Add the location of the tag decl to handle resolution across
  // translation units.
  if (ShouldGenerateLocation(D) && GenLoc(D))
    return;

  D = D->getCanonicalDecl();
  VisitDeclContext(D->getDeclContext());

  switch (D->getTagKind()) {
    case TagDecl::TK_struct: Out << "@S"; break;
    case TagDecl::TK_class:  Out << "@C"; break;
    case TagDecl::TK_union:  Out << "@U"; break;
    case TagDecl::TK_enum:   Out << "@E"; break;
  }

  const std::string &s = D->getNameAsString();
  const TypedefDecl *TD = 0;
  if (s.empty()) {
    TD = D->getTypedefForAnonDecl();
    Out << (TD ? 'A' : 'a');
  }

  if (s.empty()) {
    if (TD)
      Out << '@' << TD;
  }
  else
    Out << '@' << s;
}

void USRGenerator::VisitTypedefDecl(TypedefDecl *D) {
  if (ShouldGenerateLocation(D) && GenLoc(D))
    return;
  DeclContext *DC = D->getDeclContext();
  if (NamedDecl *DCN = dyn_cast<NamedDecl>(DC))
    Visit(DCN);
  Out << "@T@";
  Out << D->getName();
}

bool USRGenerator::GenLoc(const Decl *D) {
  if (generatedLoc)
    return IgnoreResults;
  generatedLoc = true;

  const SourceManager &SM = AU->getSourceManager();
  SourceLocation L = D->getLocStart();
  if (L.isInvalid()) {
    IgnoreResults = true;
    return true;
  }
  L = SM.getInstantiationLoc(L);
  const std::pair<FileID, unsigned> &Decomposed = SM.getDecomposedLoc(L);
  const FileEntry *FE = SM.getFileEntryForID(Decomposed.first);
  if (FE) {
    llvm::sys::Path P(FE->getName());
    Out << P.getLast();
  }
  else {
    // This case really isn't interesting.
    IgnoreResults = true;
    return true;
  }
  Out << '@'
      << SM.getLineNumber(Decomposed.first, Decomposed.second) << ':'
      << SM.getColumnNumber(Decomposed.first, Decomposed.second);

  return IgnoreResults;
}

void USRGenerator::VisitType(QualType T) {
  // This method mangles in USR information for types.  It can possibly
  // just reuse the naming-mangling logic used by codegen, although the
  // requirements for USRs might not be the same.
  do {
    T = T.getTypePtr()->getCanonicalTypeInternal();
    Qualifiers Q = T.getQualifiers();
    if (Q.hasConst())
      Out << '1';
    if (Q.hasVolatile())
      Out << '2';
    if (Q.hasRestrict())
      Out << '3';

    // Mangle in ObjC GC qualifiers?

    if (const PointerType *PT = T->getAs<PointerType>()) {
      Out << '*';
      T = PT->getPointeeType();
      continue;
    }
    if (const ReferenceType *RT = T->getAs<ReferenceType>()) {
      Out << '&';
      T = RT->getPointeeType();
      continue;
    }
    if (const FunctionProtoType *FT = T->getAs<FunctionProtoType>()) {
      Out << 'F';
      VisitType(FT->getResultType());
      for (FunctionProtoType::arg_type_iterator
            I = FT->arg_type_begin(), E = FT->arg_type_end(); I!=E; ++I) {
        VisitType(*I);
      }
      if (FT->isVariadic())
        Out << '.';
      return;
    }
    if (const BlockPointerType *BT = T->getAs<BlockPointerType>()) {
      Out << 'B';
      T = BT->getPointeeType();
      continue;
    }
    if (const BuiltinType *BT = T->getAs<BuiltinType>()) {
      unsigned char c = '\0';
      switch (BT->getKind()) {
        case BuiltinType::Void:
          c = 'v'; break;
        case BuiltinType::Bool:
          c = 'b'; break;
        case BuiltinType::Char_U:
        case BuiltinType::UChar:
          c = 'c'; break;
        case BuiltinType::Char16:
          c = 'q'; break;
        case BuiltinType::Char32:
          c = 'w'; break;
        case BuiltinType::UShort:
          c = 's'; break;
        case BuiltinType::UInt:
          c = 'i'; break;
        case BuiltinType::ULong:
          c = 'l'; break;
        case BuiltinType::ULongLong:
          c = 'k'; break;
        case BuiltinType::UInt128:
          c = 'j'; break;
        case BuiltinType::Char_S:
        case BuiltinType::SChar:
          c = 'C'; break;
        case BuiltinType::WChar:
          c = 'W'; break;
        case BuiltinType::Short:
          c = 'S'; break;
        case BuiltinType::Int:
          c = 'I'; break;
        case BuiltinType::Long:
          c = 'L'; break;
        case BuiltinType::LongLong:
          c = 'K'; break;
        case BuiltinType::Int128:
          c = 'J'; break;
        case BuiltinType::Float:
          c = 'f'; break;
        case BuiltinType::Double:
          c = 'd'; break;
        case BuiltinType::LongDouble:
          c = 'D'; break;
        case BuiltinType::NullPtr:
          c = 'n'; break;
        case BuiltinType::Overload:
        case BuiltinType::Dependent:
        case BuiltinType::UndeducedAuto:
          IgnoreResults = true;
          return;
        case BuiltinType::ObjCId:
          c = 'o'; break;
        case BuiltinType::ObjCClass:
          c = 'O'; break;
        case BuiltinType::ObjCSel:
          c = 'e'; break;
      }
      Out << c;
      return;
    }
    if (const ComplexType *CT = T->getAs<ComplexType>()) {
      Out << '<';
      T = CT->getElementType();
      continue;
    }

    // Unhandled type.
    Out << ' ';
    break;
  } while (true);
}

//===----------------------------------------------------------------------===//
// General purpose USR generation methods.
//===----------------------------------------------------------------------===//

void USRGenerator::GenNamedDecl(llvm::StringRef name) {
  Out << "@" << name;
}

void USRGenerator::GenObjCClass(llvm::StringRef cls) {
  Out << "objc(cs)" << cls;
}

void USRGenerator::GenObjCCategory(llvm::StringRef cls, llvm::StringRef cat) {
  Out << "objc(cy)" << cls << '@' << cat;
}

void USRGenerator::GenObjCIvar(llvm::StringRef ivar) {
  GenNamedDecl(ivar);
}

void USRGenerator::GenObjCMethod(llvm::StringRef meth, bool isInstanceMethod) {
  Out << (isInstanceMethod ? "(im)" : "(cm)") << meth;
}

void USRGenerator::GenObjCProperty(llvm::StringRef prop) {
  Out << "(py)" << prop;
}

void USRGenerator::GenObjCProtocol(llvm::StringRef prot) {
  Out << "objc(pl)" << prot;
}

//===----------------------------------------------------------------------===//
// API hooks.
//===----------------------------------------------------------------------===//

static inline llvm::StringRef extractUSRSuffix(llvm::StringRef s) {
  return s.startswith("c:") ? s.substr(2) : "";
}

static CXString getDeclCursorUSR(const CXCursor &C) {
  Decl *D = cxcursor::getCursorDecl(C);

  // Don't generate USRs for things with invalid locations.
  if (!D || D->getLocStart().isInvalid())
    return createCXString("");

  // Check if the cursor has 'NoLinkage'.
  if (const NamedDecl *ND = dyn_cast<NamedDecl>(D))
    switch (ND->getLinkage()) {
      case ExternalLinkage:
        // Generate USRs for all entities with external linkage.
        break;
      case NoLinkage:
      case UniqueExternalLinkage:
        // We allow enums, typedefs, and structs that have no linkage to
        // have USRs that are anchored to the file they were defined in
        // (e.g., the header).  This is a little gross, but in principal
        // enums/anonymous structs/etc. defined in a common header file
        // are referred to across multiple translation units.
        if (isa<TagDecl>(ND) || isa<TypedefDecl>(ND) ||
            isa<EnumConstantDecl>(ND) || isa<FieldDecl>(ND) ||
            isa<VarDecl>(ND) || isa<NamespaceDecl>(ND))
          break;
        // Fall-through.
      case InternalLinkage:
        if (isa<FunctionDecl>(ND))
          break;
    }

  StringUSRGenerator SUG(&C);
  SUG->Visit(D);

  if (SUG->ignoreResults())
    return createCXString("");

  // For development testing.
  // assert(SUG.str().size() > 2);

    // Return a copy of the string that must be disposed by the caller.
  return createCXString(SUG.str(), true);
}

extern "C" {

CXString clang_getCursorUSR(CXCursor C) {
  const CXCursorKind &K = clang_getCursorKind(C);

  if (clang_isDeclaration(K))
      return getDeclCursorUSR(C);

  if (K == CXCursor_MacroDefinition) {
    StringUSRGenerator SUG(&C);
    SUG << "macro@"
        << cxcursor::getCursorMacroDefinition(C)->getName()->getNameStart();
    return createCXString(SUG.str(), true);
  }

  return createCXString("");
}

CXString clang_constructUSR_ObjCIvar(const char *name, CXString classUSR) {
  StringUSRGenerator SUG;
  SUG << extractUSRSuffix(clang_getCString(classUSR));
  SUG->GenObjCIvar(name);
  return createCXString(SUG.str(), true);
}

CXString clang_constructUSR_ObjCMethod(const char *name,
                                       unsigned isInstanceMethod,
                                       CXString classUSR) {
  StringUSRGenerator SUG;
  SUG << extractUSRSuffix(clang_getCString(classUSR));
  SUG->GenObjCMethod(name, isInstanceMethod);
  return createCXString(SUG.str(), true);
}

CXString clang_constructUSR_ObjCClass(const char *name) {
  StringUSRGenerator SUG;
  SUG->GenObjCClass(name);
  return createCXString(SUG.str(), true);
}

CXString clang_constructUSR_ObjCProtocol(const char *name) {
  StringUSRGenerator SUG;
  SUG->GenObjCProtocol(name);
  return createCXString(SUG.str(), true);
}

CXString clang_constructUSR_ObjCCategory(const char *class_name,
                                         const char *category_name) {
  StringUSRGenerator SUG;
  SUG->GenObjCCategory(class_name, category_name);
  return createCXString(SUG.str(), true);
}

CXString clang_constructUSR_ObjCProperty(const char *property,
                                         CXString classUSR) {
  StringUSRGenerator SUG;
  SUG << extractUSRSuffix(clang_getCString(classUSR));
  SUG->GenObjCProperty(property);
  return createCXString(SUG.str(), true);
}

} // end extern "C"
