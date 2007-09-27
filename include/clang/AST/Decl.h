//===--- Decl.h - Classes for representing declarations ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Decl interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECL_H
#define LLVM_CLANG_AST_DECL_H

#include "clang/Basic/SourceLocation.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/APSInt.h"

namespace clang {
class IdentifierInfo;
class SelectorInfo;
class Expr;
class Stmt;
class FunctionDecl;
class AttributeList;
class ObjcIvarDecl;
class ObjcMethodDecl;
class ObjcProtocolDecl;
class ObjcCategoryDecl;

/// Decl - This represents one declaration (or definition), e.g. a variable, 
/// typedef, function, struct, etc.  
///
class Decl {
public:
  enum Kind {
    // Concrete sub-classes of ValueDecl
    Function, BlockVariable, FileVariable, ParmVariable, EnumConstant,
    // Concrete sub-classes of TypeDecl
    Typedef, Struct, Union, Class, Enum, ObjcInterface, ObjcClass, ObjcMethod,
    ObjcProtoMethod, ObjcProtocol, ObjcForwardProtocol, ObjcCategory,
    ObjcImplementation,
    // Concrete sub-class of Decl
    Field, ObjcIvar
  };

  /// IdentifierNamespace - According to C99 6.2.3, there are four namespaces,
  /// labels, tags, members and ordinary identifiers.
  enum IdentifierNamespace {
    IDNS_Label,
    IDNS_Tag,
    IDNS_Member,
    IDNS_Ordinary
  };
private:
  /// DeclKind - This indicates which class this is.
  Kind DeclKind   :  8;
  
  /// InvalidDecl - This indicates a semantic error occurred.
  unsigned int InvalidDecl :  1;

protected:
  Decl(Kind DK) : DeclKind(DK), InvalidDecl(0) {
    if (Decl::CollectingStats()) addDeclKind(DK);
  }
  virtual ~Decl();
  
public:
  
  Kind getKind() const { return DeclKind; }
  const char *getDeclKindName() const;
  
  /// setInvalidDecl - Indicates the Decl had a semantic error. This
  /// allows for graceful error recovery.
  void setInvalidDecl() { InvalidDecl = 1; }
  int isInvalidDecl() const { return InvalidDecl; }
  
  IdentifierNamespace getIdentifierNamespace() const {
    switch (DeclKind) {
    default: assert(0 && "Unknown decl kind!");
    case Typedef:
    case Function:
    case BlockVariable:
    case FileVariable:
    case ParmVariable:
    case EnumConstant:
    case ObjcInterface:
    case ObjcProtocol:
      return IDNS_Ordinary;
    case Struct:
    case Union:
    case Class:
    case Enum:
      return IDNS_Tag;
    }
  }
  // global temp stats (until we have a per-module visitor)
  static void addDeclKind(const Kind k);
  static bool CollectingStats(bool enable=false);
  static void PrintStats();
    
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *) { return true; }
};

/// ScopedDecl - Represent lexically scoped names, used for all ValueDecl's
/// and TypeDecl's.
class ScopedDecl : public Decl {
  /// Identifier - The identifier for this declaration (e.g. the name for the
  /// variable, the tag for a struct).
  IdentifierInfo *Identifier;
  
  /// Loc - The location that this decl.
  SourceLocation Loc;
  
  /// NextDeclarator - If this decl was part of a multi-declarator declaration,
  /// such as "int X, Y, *Z;" this indicates Decl for the next declarator.
  ScopedDecl *NextDeclarator;
  
  /// When this decl is in scope while parsing, the Next field contains a
  /// pointer to the shadowed decl of the same name.  When the scope is popped,
  /// Decls are relinked onto a containing decl object.
  ///
  ScopedDecl *Next;
protected:
  ScopedDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, ScopedDecl *PrevDecl) 
    : Decl(DK), Identifier(Id), Loc(L), NextDeclarator(PrevDecl), Next(0) {}
public:
  IdentifierInfo *getIdentifier() const { return Identifier; }
  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }
  const char *getName() const;

  ScopedDecl *getNext() const { return Next; }
  void setNext(ScopedDecl *N) { Next = N; }
  
  /// getNextDeclarator - If this decl was part of a multi-declarator
  /// declaration, such as "int X, Y, *Z;" this returns the decl for the next
  /// declarator.  Otherwise it returns null.
  ScopedDecl *getNextDeclarator() { return NextDeclarator; }
  const ScopedDecl *getNextDeclarator() const { return NextDeclarator; }
  void setNextDeclarator(ScopedDecl *N) { NextDeclarator = N; }
  
  // Implement isa/cast/dyncast/etc. - true for all ValueDecl's and TypeDecl's.
  static bool classof(const Decl *D) {
    return (D->getKind() >= Function && D->getKind() <= EnumConstant) || 
           (D->getKind() >= Typedef && D->getKind() <= Enum);
  }
  static bool classof(const ScopedDecl *D) { return true; }
};

/// ValueDecl - Represent the declaration of a variable (in which case it is 
/// an lvalue) a function (in which case it is a function designator) or
/// an enum constant. 
class ValueDecl : public ScopedDecl {
  QualType DeclType;
protected:
  ValueDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, QualType T,
            ScopedDecl *PrevDecl) : ScopedDecl(DK, L, Id, PrevDecl), DeclType(T) {}
public:
  QualType getType() const { return DeclType; }
  void setType(QualType newType) { DeclType = newType; }
  QualType getCanonicalType() const { return DeclType.getCanonicalType(); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() >= Function && D->getKind() <= EnumConstant;
  }
  static bool classof(const ValueDecl *D) { return true; }
};

/// VarDecl - An instance of this class is created to represent a variable
/// declaration or definition.
class VarDecl : public ValueDecl {
public:
  enum StorageClass {
    None, Extern, Static, Auto, Register
  };
  StorageClass getStorageClass() const { return SClass; }

  const Expr *getInit() const { return Init; }
  Expr *getInit() { return Init; }
  void setInit(Expr *I) { Init = I; }
  
  // hasAutoStorage - Returns true if either the implicit or explicit
  //  storage class of a variable is "auto."  In particular, variables
  //  declared within a function that lack a storage keyword are
  //  implicitly "auto", but are represented internally with a storage
  //  class of None.
  bool hasAutoStorage() {
    return (SClass == Auto || (SClass == None && getKind() != FileVariable));
  }

  // hasStaticStorage - Returns true if either the implicit or
  //  explicit storage class of a variable is "static."  In
  //  particular, variables declared within a file (outside of a
  //  function) that lack a storage keyword are implicitly "static,"
  //  but are represented internally with a storage class of "None".
  bool hasStaticStorage() {
    return (SClass == Static || (SClass == None && getKind() == FileVariable));
  }
      
  // hasLocalStorage - Returns true if a variable with function scope
  //  is a non-static local variable.
  bool hasLocalStorage() { return (hasAutoStorage() || SClass == Register); }

  // hasGlobalStorage - Returns true for all variables that do not
  //  have local storage.  This includs all global variables as well
  //  as static variables declared within a function.
  bool hasGlobalStorage() { return !hasAutoStorage(); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { 
    return D->getKind() >= BlockVariable && D->getKind() <= ParmVariable; 
  }
  static bool classof(const VarDecl *D) { return true; }
protected:
  VarDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, QualType T,
          StorageClass SC, ScopedDecl *PrevDecl)
    : ValueDecl(DK, L, Id, T, PrevDecl), Init(0) { SClass = SC; }
private:
  StorageClass SClass;
  Expr *Init;
};

/// BlockVarDecl - Represent a local variable declaration.
class BlockVarDecl : public VarDecl {
public:
  BlockVarDecl(SourceLocation L, IdentifierInfo *Id, QualType T, StorageClass S,
               ScopedDecl *PrevDecl)
    : VarDecl(BlockVariable, L, Id, T, S, PrevDecl) {}
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == BlockVariable; }
  static bool classof(const BlockVarDecl *D) { return true; }
};

/// FileVarDecl - Represent a file scoped variable declaration. This
/// will allow us to reason about external variable declarations and tentative 
/// definitions (C99 6.9.2p2) using our type system (without storing a
/// pointer to the decl's scope, which is transient).
class FileVarDecl : public VarDecl {
public:
  FileVarDecl(SourceLocation L, IdentifierInfo *Id, QualType T, StorageClass S,
              ScopedDecl *PrevDecl)
    : VarDecl(FileVariable, L, Id, T, S, PrevDecl) {}
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == FileVariable; }
  static bool classof(const FileVarDecl *D) { return true; }
};

/// ParmVarDecl - Represent a parameter to a function.
class ParmVarDecl : public VarDecl {
public:
  ParmVarDecl(SourceLocation L, IdentifierInfo *Id, QualType T, StorageClass S,
              ScopedDecl *PrevDecl)
    : VarDecl(ParmVariable, L, Id, T, S, PrevDecl) {}
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == ParmVariable; }
  static bool classof(const ParmVarDecl *D) { return true; }
};

/// FunctionDecl - An instance of this class is created to represent a function
/// declaration or definition.
class FunctionDecl : public ValueDecl {
public:
  enum StorageClass {
    None, Extern, Static
  };
  FunctionDecl(SourceLocation L, IdentifierInfo *Id, QualType T,
               StorageClass S = None, bool isInline = false, 
               ScopedDecl *PrevDecl = 0)
    : ValueDecl(Function, L, Id, T, PrevDecl), 
      ParamInfo(0), Body(0), DeclChain(0), SClass(S), IsInline(isInline) {}
  virtual ~FunctionDecl();

  Stmt *getBody() const { return Body; }
  void setBody(Stmt *B) { Body = B; }
  
  ScopedDecl *getDeclChain() const { return DeclChain; }
  void setDeclChain(ScopedDecl *D) { DeclChain = D; }

  unsigned getNumParams() const;
  const ParmVarDecl *getParamDecl(unsigned i) const {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }
  ParmVarDecl *getParamDecl(unsigned i) {
    assert(i < getNumParams() && "Illegal param #");
    return ParamInfo[i];
  }
  void setParams(ParmVarDecl **NewParamInfo, unsigned NumParams);

  QualType getResultType() const { 
    return cast<FunctionType>(getType())->getResultType();
  }
  StorageClass getStorageClass() const { return SClass; }
  bool isInline() const { return IsInline; }
    
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == Function; }
  static bool classof(const FunctionDecl *D) { return true; }
private:
  /// ParamInfo - new[]'d array of pointers to VarDecls for the formal
  /// parameters of this function.  This is null if a prototype or if there are
  /// no formals.  TODO: we could allocate this space immediately after the
  /// FunctionDecl object to save an allocation like FunctionType does.
  ParmVarDecl **ParamInfo;
  
  Stmt *Body;  // Null if a prototype.
  
  /// DeclChain - Linked list of declarations that are defined inside this
  /// function.
  ScopedDecl *DeclChain;

  StorageClass SClass : 2;
  bool IsInline : 1;
};


/// FieldDecl - An instance of this class is created by Sema::ActOnField to 
/// represent a member of a struct/union/class.
class FieldDecl : public Decl {
  /// Identifier - The identifier for this declaration (e.g. the name for the
  /// variable, the tag for a struct).
  IdentifierInfo *Identifier;
  
  /// Loc - The location that this decl.
  SourceLocation Loc;

  QualType DeclType;  
public:
  FieldDecl(SourceLocation L, IdentifierInfo *Id, QualType T)
    : Decl(Field), Identifier(Id), Loc(L), DeclType(T) {}
  FieldDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, QualType T) 
    : Decl(DK), Identifier(Id), Loc(L), DeclType(T) {}

  IdentifierInfo *getIdentifier() const { return Identifier; }
  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }
  const char *getName() const;

  QualType getType() const { return DeclType; }
  QualType getCanonicalType() const { return DeclType.getCanonicalType(); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == Field || D->getKind() == ObjcIvar;
  }
  static bool classof(const FieldDecl *D) { return true; }
};

/// EnumConstantDecl - An instance of this object exists for each enum constant
/// that is defined.  For example, in "enum X {a,b}", each of a/b are
/// EnumConstantDecl's, X is an instance of EnumDecl, and the type of a/b is a
/// TagType for the X EnumDecl.
class EnumConstantDecl : public ValueDecl {
  Expr *Init; // an integer constant expression
  llvm::APSInt Val; // The value.
public:
  EnumConstantDecl(SourceLocation L, IdentifierInfo *Id, QualType T, Expr *E,
                   const llvm::APSInt &V, ScopedDecl *PrevDecl)
    : ValueDecl(EnumConstant, L, Id, T, PrevDecl), Init(E), Val(V) {}

  const Expr *getInitExpr() const { return Init; }
  Expr *getInitExpr() { return Init; }
  const llvm::APSInt &getInitVal() const { return Val; }

  void setInitExpr(Expr *E) { Init = E; }
  void setInitVal(llvm::APSInt &V) { Val = V; }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == EnumConstant;
  }
  static bool classof(const EnumConstantDecl *D) { return true; }
};


/// TypeDecl - Represents a declaration of a type.
///
class TypeDecl : public ScopedDecl {
  /// TypeForDecl - This indicates the Type object that represents this
  /// TypeDecl.  It is a cache maintained by ASTContext::getTypedefType and
  /// ASTContext::getTagDeclType.
  Type *TypeForDecl;
  friend class ASTContext;
protected:
  TypeDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, ScopedDecl *PrevDecl)
    : ScopedDecl(DK, L, Id, PrevDecl), TypeForDecl(0) {}
public:
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() >= Typedef && D->getKind() <= Enum;
  }
  static bool classof(const TypeDecl *D) { return true; }
};


class TypedefDecl : public TypeDecl {
  /// UnderlyingType - This is the type the typedef is set to.
  QualType UnderlyingType;
public:
  TypedefDecl(SourceLocation L, IdentifierInfo *Id, QualType T, ScopedDecl *PD) 
    : TypeDecl(Typedef, L, Id, PD), UnderlyingType(T) {}
  
  QualType getUnderlyingType() const { return UnderlyingType; }
  void setUnderlyingType(QualType newType) { UnderlyingType = newType; }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == Typedef; }
  static bool classof(const TypedefDecl *D) { return true; }
};


/// TagDecl - Represents the declaration of a struct/union/class/enum.
class TagDecl : public TypeDecl {
  /// IsDefinition - True if this is a definition ("struct foo {};"), false if
  /// it is a declaration ("struct foo;").
  bool IsDefinition : 1;
protected:
  TagDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, ScopedDecl *PrevDecl)
    : TypeDecl(DK, L, Id, PrevDecl) {
    IsDefinition = false;
  }
public:
  
  /// isDefinition - Return true if this decl has its body specified.
  bool isDefinition() const {
    return IsDefinition;
  }
  
  const char *getKindName() const {
    switch (getKind()) {
    default: assert(0 && "Unknown TagDecl!");
    case Struct: return "struct";
    case Union:  return "union";
    case Class:  return "class";
    case Enum:   return "enum";
    }
  }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() == Struct || D->getKind() == Union ||
           D->getKind() == Class || D->getKind() == Enum;
  }
  static bool classof(const TagDecl *D) { return true; }
protected:
  void setDefinition(bool V) { IsDefinition = V; }
};

/// EnumDecl - Represents an enum.  As an extension, we allow forward-declared
/// enums.
class EnumDecl : public TagDecl {
  /// ElementList - this is a linked list of EnumConstantDecl's which are linked
  /// together through their getNextDeclarator pointers.
  EnumConstantDecl *ElementList;
  
  /// IntegerType - This represent the integer type that the enum corresponds
  /// to for code generation purposes.  Note that the enumerator constants may
  /// have a different type than this does.
  QualType IntegerType;
public:
  EnumDecl(SourceLocation L, IdentifierInfo *Id, ScopedDecl *PrevDecl)
    : TagDecl(Enum, L, Id, PrevDecl) {
    ElementList = 0;
  }
  
  /// defineElements - When created, EnumDecl correspond to a forward declared
  /// enum.  This method is used to mark the decl as being defined, with the
  /// specified list of enums.
  void defineElements(EnumConstantDecl *ListHead, QualType NewType) {
    assert(!isDefinition() && "Cannot redefine enums!");
    ElementList = ListHead;
    setDefinition(true);
    
    IntegerType = NewType;
  }
  
  /// getIntegerType - Return the integer type this enum decl corresponds to.
  /// This returns a null qualtype for an enum forward definition.
  QualType getIntegerType() const { return IntegerType; }
  
  /// getEnumConstantList - Return the first EnumConstantDecl in the enum.
  ///
  EnumConstantDecl *getEnumConstantList() { return ElementList; }
  const EnumConstantDecl *getEnumConstantList() const { return ElementList; }
  
  static bool classof(const Decl *D) {
    return D->getKind() == Enum;
  }
  static bool classof(const EnumDecl *D) { return true; }
};


/// RecordDecl - Represents a struct/union/class.
class RecordDecl : public TagDecl {
  /// HasFlexibleArrayMember - This is true if this struct ends with a flexible
  /// array member (e.g. int X[]) or if this union contains a struct that does.
  /// If so, this cannot be contained in arrays or other structs as a member.
  bool HasFlexibleArrayMember : 1;

  /// Members/NumMembers - This is a new[]'d array of pointers to Decls.
  FieldDecl **Members;   // Null if not defined.
  int NumMembers;   // -1 if not defined.
public:
  RecordDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, ScopedDecl*PrevDecl)
    : TagDecl(DK, L, Id, PrevDecl) {
    HasFlexibleArrayMember = false;
    assert(classof(static_cast<Decl*>(this)) && "Invalid Kind!");
    Members = 0;
    NumMembers = -1;
  }
  
  bool hasFlexibleArrayMember() const { return HasFlexibleArrayMember; }
  void setHasFlexibleArrayMember(bool V) { HasFlexibleArrayMember = V; }
  
  /// getNumMembers - Return the number of members, or -1 if this is a forward
  /// definition.
  int getNumMembers() const { return NumMembers; }
  const FieldDecl *getMember(unsigned i) const { return Members[i]; }
  FieldDecl *getMember(unsigned i) { return Members[i]; }

  /// defineBody - When created, RecordDecl's correspond to a forward declared
  /// record.  This method is used to mark the decl as being defined, with the
  /// specified contents.
  void defineBody(FieldDecl **Members, unsigned numMembers);

  /// getMember - If the member doesn't exist, or there are no members, this 
  /// function will return 0;
  FieldDecl *getMember(IdentifierInfo *name);

  static bool classof(const Decl *D) {
    return D->getKind() == Struct || D->getKind() == Union ||
           D->getKind() == Class;
  }
  static bool classof(const RecordDecl *D) { return true; }
};

class ObjcInterfaceDecl : public TypeDecl {
  
  /// Class's super class.
  ObjcInterfaceDecl *SuperClass;
  
  /// Protocols referenced in interface header declaration
  ObjcProtocolDecl **IntfRefProtocols;  // Null if none
  int NumIntfRefProtocols;  // -1 if none
  
  /// Ivars/NumIvars - This is a new[]'d array of pointers to Decls.
  ObjcIvarDecl **Ivars;   // Null if not defined.
  int NumIvars;   // -1 if not defined.
  
  /// instance methods
  ObjcMethodDecl **InsMethods;  // Null if not defined
  int NumInsMethods;  // -1 if not defined
  
  /// class methods
  ObjcMethodDecl **ClsMethods;  // Null if not defined
  int NumClsMethods;  // -1 if not defined
  
  /// List of categories defined for this class.
  ObjcCategoryDecl *ListCategories;
  
  bool isForwardDecl; // declared with @class.
public:
  ObjcInterfaceDecl(SourceLocation L, unsigned numRefProtos,
                    IdentifierInfo *Id, bool FD = false)
    : TypeDecl(ObjcInterface, L, Id, 0),
      SuperClass(0),
      IntfRefProtocols(0), NumIntfRefProtocols(-1),
      Ivars(0), NumIvars(-1),
      InsMethods(0), NumInsMethods(-1), ClsMethods(0), NumClsMethods(-1),
      ListCategories(0),
      isForwardDecl(FD) {
        AllocIntfRefProtocols(numRefProtos);
      }
    
  void AllocIntfRefProtocols(unsigned numRefProtos) {
    if (numRefProtos) {
      IntfRefProtocols = new ObjcProtocolDecl*[numRefProtos];
      memset(IntfRefProtocols, '\0',
             numRefProtos*sizeof(ObjcProtocolDecl*));
      NumIntfRefProtocols = numRefProtos;
    }
  }
  ObjcIvarDecl **getIntfDeclIvars() const { return Ivars; }
  int getIntfDeclNumIvars() const { return NumIvars; }
  
  void ObjcAddInstanceVariablesToClass(ObjcIvarDecl **ivars, 
				       unsigned numIvars);

  void ObjcAddMethods(ObjcMethodDecl **insMethods, unsigned numInsMembers,
                      ObjcMethodDecl **clsMethods, unsigned numClsMembers);
  
  bool getIsForwardDecl() const { return isForwardDecl; }
  void setIsForwardDecl(bool val) { isForwardDecl = val; }
  
  void setIntfRefProtocols(int idx, ObjcProtocolDecl *OID) {
    assert((idx < NumIntfRefProtocols) && "index out of range");
    IntfRefProtocols[idx] = OID;
  }
  
  ObjcInterfaceDecl *getSuperClass() const { return SuperClass; }
  void setSuperClass(ObjcInterfaceDecl * superCls) { SuperClass = superCls; }
  
  ObjcCategoryDecl* getListCategories() const { return ListCategories; }
  void setListCategories(ObjcCategoryDecl *category) { 
         ListCategories = category; 
  }
  
  static bool classof(const Decl *D) {
    return D->getKind() == ObjcInterface;
  }
  static bool classof(const ObjcInterfaceDecl *D) { return true; }
};

class ObjcIvarDecl : public FieldDecl {
public:
  ObjcIvarDecl(SourceLocation L, IdentifierInfo *Id, QualType T) 
    : FieldDecl(ObjcIvar, L, Id, T) {}
    
  enum AccessControl {
    None, Private, Protected, Public, Package
  };
  void setAccessControl(AccessControl ac) { DeclAccess = ac; }
  AccessControl getAccessControl() const { return DeclAccess; }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == ObjcIvar; }
  static bool classof(const ObjcIvarDecl *D) { return true; }
private:
  AccessControl DeclAccess : 3;
};

class ObjcClassDecl : public TypeDecl {
  ObjcInterfaceDecl **ForwardDecls;   // Null if not defined.
  int NumForwardDecls;               // -1 if not defined.
public:
  ObjcClassDecl(SourceLocation L, unsigned nElts)
    : TypeDecl(ObjcClass, L, 0, 0) { 
    if (nElts) {
      ForwardDecls = new ObjcInterfaceDecl*[nElts];
      memset(ForwardDecls, '\0', nElts*sizeof(ObjcInterfaceDecl*));
    }
    NumForwardDecls = nElts;
  }
  void setInterfaceDecl(int idx, ObjcInterfaceDecl *OID) {
    assert((idx < NumForwardDecls) && "index out of range");
    ForwardDecls[idx] = OID;
  }
  static bool classof(const Decl *D) {
    return D->getKind() == ObjcClass;
  }
  static bool classof(const ObjcClassDecl *D) { return true; }
};

/// ObjcMethodDecl - An instance of this class is created to represent an instance
/// or class method declaration.
class ObjcMethodDecl : public Decl {
public:
  enum ImplementationControl { None, Required, Optional };
private:
  // A unigue name for this method.
  SelectorInfo *Selector;
  
  // Type of this method.
  QualType MethodDeclType;
  /// ParamInfo - new[]'d array of pointers to VarDecls for the formal
  /// parameters of this Method.  This is null if there are no formals.  
  ParmVarDecl **ParamInfo;
  int NumMethodParams;  // -1 if no parameters
  
  /// List of attributes for this method declaration.
  AttributeList *MethodAttrs;

  /// instance (true) or class (false) method.
  bool IsInstance : 1;
  /// @required/@optional
  ImplementationControl DeclImplementation : 2;

public:
  ObjcMethodDecl(SourceLocation L, SelectorInfo *SelInfo, QualType T,
		 ParmVarDecl **paramInfo = 0, int numParams=-1,
		 AttributeList *M = 0, bool isInstance = true, 
		 Decl *PrevDecl = 0)
    : Decl(ObjcMethod), Selector(SelInfo), MethodDeclType(T), 
      ParamInfo(paramInfo), NumMethodParams(numParams),
      MethodAttrs(M), IsInstance(isInstance) {}
#if 0
  ObjcMethodDecl(Kind DK, SourceLocation L, IdentifierInfo &SelId, QualType T,
		 ParmVarDecl **paramInfo = 0, int numParams=-1,
		 AttributeList *M = 0, bool isInstance = true, 
		 Decl *PrevDecl = 0)
    : Decl(DK), Selector(SelId), MethodDeclType(T), 
      ParamInfo(paramInfo), NumMethodParams(numParams),
      MethodAttrs(M), IsInstance(isInstance) {}
#endif
  virtual ~ObjcMethodDecl();
  QualType getMethodType() const { return MethodDeclType; }
  unsigned getNumMethodParams() const { return NumMethodParams; }
  ParmVarDecl *getMethodParamDecl(unsigned i) {
    assert(i < getNumMethodParams() && "Illegal param #");
    return ParamInfo[i];
  }
  void setMethodParams(ParmVarDecl **NewParamInfo, unsigned NumParams);

  AttributeList *getMethodAttrs() const {return MethodAttrs;}
  bool isInstance() const { return IsInstance; }
  // Related to protocols declared in  @protocol
  void setDeclImplementation(ImplementationControl ic)
         { DeclImplementation = ic; }
  ImplementationControl  getImplementationControl() const
                           { return DeclImplementation; }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { 
    return D->getKind() == ObjcMethod 
	   || D->getKind() == ObjcProtoMethod; 
  }
  static bool classof(const ObjcMethodDecl *D) { return true; }
};

class ObjcProtocolDecl : public TypeDecl {
  /// referenced protocols
  ObjcProtocolDecl **ReferencedProtocols;  // Null if none
  int NumReferencedProtocols;  // -1 if none
  
  /// protocol instance methods
  ObjcMethodDecl **ProtoInsMethods;  // Null if not defined
  int NumProtoInsMethods;  // -1 if not defined

  /// protocol class methods
  ObjcMethodDecl **ProtoClsMethods;  // Null if not defined
  int NumProtoClsMethods;  // -1 if not defined

  bool isForwardProtoDecl; // declared with @protocol.
public:
  ObjcProtocolDecl(SourceLocation L, unsigned numRefProtos,
                   IdentifierInfo *Id, bool FD = false)
    : TypeDecl(ObjcProtocol, L, Id, 0), 
      ReferencedProtocols(0), NumReferencedProtocols(-1),
      ProtoInsMethods(0), NumProtoInsMethods(-1), 
      ProtoClsMethods(0), NumProtoClsMethods(-1),
      isForwardProtoDecl(FD) {
        AllocReferencedProtocols(numRefProtos);
      }
  void AllocReferencedProtocols(unsigned numRefProtos) {
    if (numRefProtos) {
      ReferencedProtocols = new ObjcProtocolDecl*[numRefProtos];
      memset(ReferencedProtocols, '\0', 
             numRefProtos*sizeof(ObjcProtocolDecl*));
      NumReferencedProtocols = numRefProtos;
    }    
  }
  void ObjcAddProtoMethods(ObjcMethodDecl **insMethods, unsigned numInsMembers,
                           ObjcMethodDecl **clsMethods, unsigned numClsMembers);
  
  void setReferencedProtocols(int idx, ObjcProtocolDecl *OID) {
    assert((idx < NumReferencedProtocols) && "index out of range");
    ReferencedProtocols[idx] = OID;
  }
  
  
  bool getIsForwardProtoDecl() const { return isForwardProtoDecl; }
  void setIsForwardProtoDecl(bool val) { isForwardProtoDecl = val; }

  static bool classof(const Decl *D) {
    return D->getKind() == ObjcProtocol;
  }
  static bool classof(const ObjcProtocolDecl *D) { return true; }
};
  
class ObjcForwardProtocolDecl : public TypeDecl {
    ObjcProtocolDecl **ForwardProtocolDecls;   // Null if not defined.
    int NumForwardProtocolDecls;               // -1 if not defined.
  public:
    ObjcForwardProtocolDecl(SourceLocation L, unsigned nElts)
    : TypeDecl(ObjcForwardProtocol, L, 0, 0) { 
      if (nElts) {
        ForwardProtocolDecls = new ObjcProtocolDecl*[nElts];
        memset(ForwardProtocolDecls, '\0', nElts*sizeof(ObjcProtocolDecl*));
        NumForwardProtocolDecls = nElts;
      }
    }
    void setForwardProtocolDecl(int idx, ObjcProtocolDecl *OID) {
      assert((idx < NumForwardProtocolDecls) && "index out of range");
      ForwardProtocolDecls[idx] = OID;
    }
    static bool classof(const Decl *D) {
      return D->getKind() == ObjcForwardProtocol;
    }
    static bool classof(const ObjcForwardProtocolDecl *D) { return true; }
};

class ObjcCategoryDecl : public ScopedDecl {
  /// Interface belonging to this category
  ObjcInterfaceDecl *ClassInterface;
  
  /// Category name
  IdentifierInfo *ObjcCatName;
  
  /// referenced protocols in this category
  ObjcProtocolDecl **CatReferencedProtocols;  // Null if none
  int NumCatReferencedProtocols;  // -1 if none
  
  /// category instance methods
  ObjcMethodDecl **CatInsMethods;  // Null if not defined
  int NumCatInsMethods;  // -1 if not defined

  /// category class methods
  ObjcMethodDecl **CatClsMethods;  // Null if not defined
  int NumCatClsMethods;  // -1 if not defined
  
  /// Next category belonging to this class
  ObjcCategoryDecl *NextClassCategory;

public:
  ObjcCategoryDecl(SourceLocation L, unsigned numRefProtocol, 
                   IdentifierInfo *Id)
    : ScopedDecl(ObjcCategory, L, Id, 0),
      ClassInterface(0), ObjcCatName(0),
      CatReferencedProtocols(0), NumCatReferencedProtocols(-1),
      CatInsMethods(0), NumCatInsMethods(-1),
      CatClsMethods(0), NumCatClsMethods(-1),
      NextClassCategory(0) {
        if (numRefProtocol) {
          CatReferencedProtocols = new ObjcProtocolDecl*[numRefProtocol];
          memset(CatReferencedProtocols, '\0', 
                 numRefProtocol*sizeof(ObjcProtocolDecl*));
          NumCatReferencedProtocols = numRefProtocol;
        }
      }

  ObjcInterfaceDecl *getClassInterface() const { return ClassInterface; }
  void setClassInterface(ObjcInterfaceDecl *IDecl) { ClassInterface = IDecl; }
  
  void setCatReferencedProtocols(int idx, ObjcProtocolDecl *OID) {
    assert((idx < NumCatReferencedProtocols) && "index out of range");
    CatReferencedProtocols[idx] = OID;
  }
  
  void ObjcAddCatMethods(ObjcMethodDecl **insMethods, unsigned numInsMembers,
                         ObjcMethodDecl **clsMethods, unsigned numClsMembers);
  
  IdentifierInfo *getCatName() const { return ObjcCatName; }
  void setCatName(IdentifierInfo *catName) { ObjcCatName = catName; }
  
  ObjcCategoryDecl *getNextClassCategory() const { return NextClassCategory; }
  void insertNextClassCategory() {
    NextClassCategory = ClassInterface->getListCategories();
    ClassInterface->setListCategories(this);
  }

  static bool classof(const Decl *D) {
    return D->getKind() == ObjcCategory;
  }
  static bool classof(const ObjcCategoryDecl *D) { return true; }
};
  
class ObjcImplementationDecl : public TypeDecl {
    
  /// Implementation Class's super class.
  ObjcInterfaceDecl *SuperClass;
    
  /// Optional Ivars/NumIvars - This is a new[]'d array of pointers to Decls.
  ObjcIvarDecl **Ivars;   // Null if not specified
  int NumIvars;   // -1 if not defined.
    
  /// implemented instance methods
  ObjcMethodDecl **InsMethods;  // Null if not defined
  int NumInsMethods;  // -1 if not defined
    
  /// implemented class methods
  ObjcMethodDecl **ClsMethods;  // Null if not defined
  int NumClsMethods;  // -1 if not defined
    
  public:
  ObjcImplementationDecl(SourceLocation L, IdentifierInfo *Id,
                         ObjcInterfaceDecl* superDecl)
    : TypeDecl(ObjcImplementation, L, Id, 0),
      SuperClass(superDecl),
      Ivars(0), NumIvars(-1),
      InsMethods(0), NumInsMethods(-1), ClsMethods(0), NumClsMethods(-1) {}
  
  void ObjcAddInstanceVariablesToClassImpl(ObjcIvarDecl **ivars, 
                                           unsigned numIvars);
    
  void ObjcAddMethods(ObjcMethodDecl **insMethods, unsigned numInsMembers,
                        ObjcMethodDecl **clsMethods, unsigned numClsMembers);
    
  ObjcInterfaceDecl *getImplSuperClass() const { return SuperClass; }
  
  void setImplSuperClass(ObjcInterfaceDecl * superCls) 
         { SuperClass = superCls; }
    
  static bool classof(const Decl *D) {
    return D->getKind() == ObjcImplementation;
  }
  static bool classof(const ObjcImplementationDecl *D) { return true; }
};
  

}  // end namespace clang
#endif
