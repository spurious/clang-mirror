//===--- Decl.h - Classes for representing declarations ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Decl interface and subclasses.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECL_H
#define LLVM_CLANG_AST_DECL_H

#include "clang/AST/Attr.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Bitcode/SerializationFwd.h"

namespace clang {
class Decl;
}

namespace clang {
class Expr;
class Stmt;
class StringLiteral;
class FunctionDecl;
class IdentifierInfo;

/// Decl - This represents one declaration (or definition), e.g. a variable, 
/// typedef, function, struct, etc.  
///
class Decl {
public:
  enum Kind {
    // This lists the concrete classes of Decl in order of the inheritance
    // hierarchy.  This allows us to do efficient classof tests based on the
    // enums below.   The commented out names are abstract class names.
    
    // Decl
    //   NamedDecl
           Field,
             ObjCIvar,
           ObjCCategory,
           ObjCCategoryImpl,
           ObjCImplementation,
           ObjCProtocol,
           PropertyDecl,
    //     ScopedDecl
             CompatibleAlias,
    //       TypeDecl
               ObjCInterface,
               Typedef,
    //         TagDecl
                 Enum,
    //           RecordDecl,
                   Struct,
                   Union,
                   Class,
    //       ValueDecl
               EnumConstant,
               Function,
    //         VarDecl
                 BlockVar,
                 FileVar,
                 ParmVar,
         ObjCMethod,
         ObjCClass,
         ObjCForwardProtocol,
 	 LinkageSpec,
   FileScopeAsm,
  
    // For each non-leaf class, we now define a mapping to the first/last member
    // of the class, to allow efficient classof.
    NamedFirst  = Field,         NamedLast  = ParmVar,
    FieldFirst  = Field,         FieldLast  = ObjCIvar,
    ScopedFirst = CompatibleAlias, ScopedLast = ParmVar,
    TypeFirst   = ObjCInterface, TypeLast   = Class,
    TagFirst    = Enum         , TagLast    = Class,
    RecordFirst = Struct       , RecordLast = Class,
    ValueFirst  = EnumConstant , ValueLast  = ParmVar,
    VarFirst    = BlockVar     , VarLast    = ParmVar
  };

  /// IdentifierNamespace - According to C99 6.2.3, there are four namespaces,
  /// labels, tags, members and ordinary identifiers.
  enum IdentifierNamespace {
    IDNS_Label,
    IDNS_Tag,
    IDNS_Member,
    IDNS_Ordinary
  };
  
  /// ObjCDeclQualifier - Qualifier used on types in method declarations
  /// for remote messaging. They are meant for the arguments though and
  /// applied to the Decls (ObjCMethodDecl and ParmVarDecl).
  enum ObjCDeclQualifier {
    OBJC_TQ_None = 0x0,
    OBJC_TQ_In = 0x1,
    OBJC_TQ_Inout = 0x2,
    OBJC_TQ_Out = 0x4,
    OBJC_TQ_Bycopy = 0x8,
    OBJC_TQ_Byref = 0x10,
    OBJC_TQ_Oneway = 0x20
  };
    
private:
  /// Loc - The location that this decl.
  SourceLocation Loc;
  
  /// DeclKind - This indicates which class this is.
  Kind DeclKind   :  8;
  
  /// InvalidDecl - This indicates a semantic error occurred.
  unsigned int InvalidDecl :  1;
  
  /// HasAttrs - This indicates whether the decl has attributes or not.
  unsigned int HasAttrs : 1;
protected:
  Decl(Kind DK, SourceLocation L) : Loc(L), DeclKind(DK), InvalidDecl(0),
    HasAttrs(false) {
    if (Decl::CollectingStats()) addDeclKind(DK);
  }
  
  virtual ~Decl();
  
public:
  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  Kind getKind() const { return DeclKind; }
  const char *getDeclKindName() const;
  
  void addAttr(Attr *attr);
  const Attr *getAttrs() const;

  template<typename T> const T *getAttr() const {
    for (const Attr *attr = getAttrs(); attr; attr = attr->getNext())
      if (const T *V = dyn_cast<T>(attr))
        return V;

    return 0;
  }
    
  /// setInvalidDecl - Indicates the Decl had a semantic error. This
  /// allows for graceful error recovery.
  void setInvalidDecl() { InvalidDecl = 1; }
  bool isInvalidDecl() const { return (bool) InvalidDecl; }
  
  IdentifierNamespace getIdentifierNamespace() const {
    switch (DeclKind) {
    default: assert(0 && "Unknown decl kind!");
    case Typedef:
    case Function:
    case BlockVar:
    case FileVar:
    case ParmVar:
    case EnumConstant:
    case ObjCInterface:
    case CompatibleAlias:
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
  
  /// Emit - Serialize this Decl to Bitcode.
  void Emit(llvm::Serializer& S) const;
    
  /// Create - Deserialize a Decl from Bitcode.
  static Decl* Create(llvm::Deserializer& D);

protected:
  /// EmitImpl - Provides the subclass-specific serialization logic for
  ///   serializing out a decl.
  virtual void EmitImpl(llvm::Serializer& S) const {
    // FIXME: This will eventually be a pure virtual function.
    assert (false && "Not implemented.");
  }
  
  void EmitInRec(llvm::Serializer& S) const;
  void ReadInRec(llvm::Deserializer& D);
};

/// NamedDecl - This represents a decl with an identifier for a name.  Many
/// decls have names, but not ObjCMethodDecl, @class, etc.
class NamedDecl : public Decl {
  /// Identifier - The identifier for this declaration (e.g. the name for the
  /// variable, the tag for a struct).
  IdentifierInfo *Identifier;
public:
  NamedDecl(Kind DK, SourceLocation L, IdentifierInfo *Id)
   : Decl(DK, L), Identifier(Id) {}
  
  IdentifierInfo *getIdentifier() const { return Identifier; }
  const char *getName() const;
    
  static bool classof(const Decl *D) {
    return D->getKind() >= NamedFirst && D->getKind() <= NamedLast;
  }
  static bool classof(const NamedDecl *D) { return true; }
  
protected:
  void EmitInRec(llvm::Serializer& S) const;
  void ReadInRec(llvm::Deserializer& D);
};

/// ScopedDecl - Represent lexically scoped names, used for all ValueDecl's
/// and TypeDecl's.
class ScopedDecl : public NamedDecl {
  /// NextDeclarator - If this decl was part of a multi-declarator declaration,
  /// such as "int X, Y, *Z;" this indicates Decl for the next declarator.
  ScopedDecl *NextDeclarator;
  
  /// When this decl is in scope while parsing, the Next field contains a
  /// pointer to the shadowed decl of the same name.  When the scope is popped,
  /// Decls are relinked onto a containing decl object.
  ///
  ScopedDecl *Next;

protected:
  ScopedDecl(Kind DK, SourceLocation L, IdentifierInfo *Id,ScopedDecl *PrevDecl)
    : NamedDecl(DK, L, Id), NextDeclarator(PrevDecl), Next(0) {}
  
public:
  ScopedDecl *getNext() const { return Next; }
  void setNext(ScopedDecl *N) { Next = N; }
  
  /// getNextDeclarator - If this decl was part of a multi-declarator
  /// declaration, such as "int X, Y, *Z;" this returns the decl for the next
  /// declarator.  Otherwise it returns null.
  ScopedDecl *getNextDeclarator() { return NextDeclarator; }
  const ScopedDecl *getNextDeclarator() const { return NextDeclarator; }
  void setNextDeclarator(ScopedDecl *N) { NextDeclarator = N; }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() >= ScopedFirst && D->getKind() <= ScopedLast;
  }
  static bool classof(const ScopedDecl *D) { return true; }
  
protected:
  void EmitInRec(llvm::Serializer& S) const;
  void ReadInRec(llvm::Deserializer& D);
  
  void EmitOutRec(llvm::Serializer& S) const;
  void ReadOutRec(llvm::Deserializer& D);
};

/// ValueDecl - Represent the declaration of a variable (in which case it is 
/// an lvalue) a function (in which case it is a function designator) or
/// an enum constant. 
class ValueDecl : public ScopedDecl {
  QualType DeclType;

protected:
  ValueDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, QualType T,
            ScopedDecl *PrevDecl) 
    : ScopedDecl(DK, L, Id, PrevDecl), DeclType(T) {}
public:
  QualType getType() const { return DeclType; }
  void setType(QualType newType) { DeclType = newType; }
  QualType getCanonicalType() const { return DeclType.getCanonicalType(); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() >= ValueFirst && D->getKind() <= ValueLast;
  }
  static bool classof(const ValueDecl *D) { return true; }
  
protected:
  void EmitInRec(llvm::Serializer& S) const;
  void ReadInRec(llvm::Deserializer& D);
};

/// VarDecl - An instance of this class is created to represent a variable
/// declaration or definition.
class VarDecl : public ValueDecl {
public:
  enum StorageClass {
    None, Extern, Static, Auto, Register, PrivateExtern
  };
  StorageClass getStorageClass() const { return (StorageClass)SClass; }

  const Expr *getInit() const { return Init; }
  Expr *getInit() { return Init; }
  void setInit(Expr *I) { Init = I; }
  
  // hasAutoStorage - Returns true if either the implicit or explicit
  //  storage class of a variable is "auto."  In particular, variables
  //  declared within a function that lack a storage keyword are
  //  implicitly "auto", but are represented internally with a storage
  //  class of None.
  bool hasAutoStorage() const {
    return getStorageClass() == Auto ||
          (getStorageClass() == None && getKind() != FileVar);
  }

  // hasStaticStorage - Returns true if either the implicit or
  //  explicit storage class of a variable is "static."  In
  //  particular, variables declared within a file (outside of a
  //  function) that lack a storage keyword are implicitly "static,"
  //  but are represented internally with a storage class of "None".
  bool hasStaticStorage() const {
    if (getStorageClass() == Static) return true;
    return getKind() == FileVar;
  }
      
  // hasLocalStorage - Returns true if a variable with function scope
  //  is a non-static local variable.
  bool hasLocalStorage() const {
    return hasAutoStorage() || getStorageClass() == Register;
  }

  // hasGlobalStorage - Returns true for all variables that do not
  //  have local storage.  This includs all global variables as well
  //  as static variables declared within a function.
  bool hasGlobalStorage() const { return !hasAutoStorage(); }
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() >= VarFirst && D->getKind() <= VarLast;
  }
  static bool classof(const VarDecl *D) { return true; }
protected:
  VarDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, QualType T,
          StorageClass SC, ScopedDecl *PrevDecl)
    : ValueDecl(DK, L, Id, T, PrevDecl), Init(0) { SClass = SC; }
private:
  Expr *Init;
  // FIXME: This can be packed into the bitfields in Decl.
  unsigned SClass : 3;
    
  friend class StmtIteratorBase;
  
protected:
  void EmitInRec(llvm::Serializer& S) const;
  void ReadInRec(llvm::Deserializer& D);
  
  void EmitOutRec(llvm::Serializer& S) const;
  void ReadOutRec(llvm::Deserializer& D);
  
  /// EmitImpl - Serialize this VarDecl. Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// ReadImpl - Deserialize this VarDecl. Called by subclasses.
  virtual void ReadImpl(llvm::Deserializer& S);
};

/// BlockVarDecl - Represent a local variable declaration.
class BlockVarDecl : public VarDecl {
public:
  BlockVarDecl(SourceLocation L, IdentifierInfo *Id, QualType T, StorageClass S,
               ScopedDecl *PrevDecl)
    : VarDecl(BlockVar, L, Id, T, S, PrevDecl) {}
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == BlockVar; }
  static bool classof(const BlockVarDecl *D) { return true; }  

protected:
  /// CreateImpl - Deserialize a BlockVarDecl.  Called by Decl::Create.
  static BlockVarDecl* CreateImpl(llvm::Deserializer& D);  

  friend Decl* Decl::Create(llvm::Deserializer& D);
};

/// FileVarDecl - Represent a file scoped variable declaration. This
/// will allow us to reason about external variable declarations and tentative 
/// definitions (C99 6.9.2p2) using our type system (without storing a
/// pointer to the decl's scope, which is transient).
class FileVarDecl : public VarDecl {
public:
  FileVarDecl(SourceLocation L, IdentifierInfo *Id, QualType T, StorageClass S,
              ScopedDecl *PrevDecl)
    : VarDecl(FileVar, L, Id, T, S, PrevDecl) {}
  
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == FileVar; }
  static bool classof(const FileVarDecl *D) { return true; }

protected:
  /// CreateImpl - Deserialize a FileVarDecl.  Called by Decl::Create.
  static FileVarDecl* CreateImpl(llvm::Deserializer& D);

  friend Decl* Decl::Create(llvm::Deserializer& D);
};

/// ParmVarDecl - Represent a parameter to a function.
class ParmVarDecl : public VarDecl {
public:
  ParmVarDecl(SourceLocation L, IdentifierInfo *Id, QualType T, StorageClass S,
              ScopedDecl *PrevDecl)
    : VarDecl(ParmVar, L, Id, T, S, PrevDecl), 
    objcDeclQualifier(OBJC_TQ_None) {}
  
  ObjCDeclQualifier getObjCDeclQualifier() const { return objcDeclQualifier; }
  void setObjCDeclQualifier(ObjCDeclQualifier QTVal) 
  { objcDeclQualifier = QTVal; }
    
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) { return D->getKind() == ParmVar; }
  static bool classof(const ParmVarDecl *D) { return true; }
  
private:
  /// FIXME: Also can be paced into the bitfields in Decl.
  /// in, inout, etc.
  ObjCDeclQualifier objcDeclQualifier : 6;
  
protected:
  /// EmitImpl - Serialize this ParmVarDecl. Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a ParmVarDecl.  Called by Decl::Create.
  static ParmVarDecl* CreateImpl(llvm::Deserializer& D);

  friend Decl* Decl::Create(llvm::Deserializer& D);
};

/// FunctionDecl - An instance of this class is created to represent a function
/// declaration or definition.
class FunctionDecl : public ValueDecl {
public:
  enum StorageClass {
    None, Extern, Static, PrivateExtern
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

  // Iterator access to formal parameters.
  unsigned param_size() const { return getNumParams(); }
  typedef ParmVarDecl **param_iterator;
  typedef ParmVarDecl * const *param_const_iterator;
  param_iterator param_begin() { return ParamInfo; }
  param_iterator param_end() { return ParamInfo+param_size(); }
  param_const_iterator param_begin() const { return ParamInfo; }
  param_const_iterator param_end() const { return ParamInfo+param_size(); }
  
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

protected:
  /// EmitImpl - Serialize this FunctionDecl.  Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a FunctionDecl.  Called by Decl::Create.
  static FunctionDecl* CreateImpl(llvm::Deserializer& D);
  
  friend Decl* Decl::Create(llvm::Deserializer& D);
};


/// FieldDecl - An instance of this class is created by Sema::ActOnField to 
/// represent a member of a struct/union/class.
class FieldDecl : public NamedDecl {
  QualType DeclType;  
  Expr *BitWidth;
public:
  FieldDecl(SourceLocation L, IdentifierInfo *Id, QualType T, 
            Expr *BW = NULL)
    : NamedDecl(Field, L, Id), DeclType(T), BitWidth(BW) {}
  FieldDecl(Kind DK, SourceLocation L, IdentifierInfo *Id, QualType T,
            Expr *BW = NULL)
    : NamedDecl(DK, L, Id), DeclType(T), BitWidth(BW) {}

  QualType getType() const { return DeclType; }
  QualType getCanonicalType() const { return DeclType.getCanonicalType(); }
  
  bool isBitField() const { return BitWidth != NULL; }
  Expr *getBitWidth() const { return BitWidth; }
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *D) {
    return D->getKind() >= FieldFirst && D->getKind() <= FieldLast;
  }
  static bool classof(const FieldDecl *D) { return true; }

protected:
  /// EmitImpl - Serialize this FieldDecl.  Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a FieldDecl.  Called by Decl::Create.
  static FieldDecl* CreateImpl(llvm::Deserializer& D);
  
  friend Decl* Decl::Create(llvm::Deserializer& D);  
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
  static bool classof(const Decl *D) { return D->getKind() == EnumConstant; }
  static bool classof(const EnumConstantDecl *D) { return true; }
  
  friend class StmtIteratorBase;
  
protected:
  /// EmitImpl - Serialize this EnumConstantDecl.  Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a EnumConstantDecl.  Called by Decl::Create.
  static EnumConstantDecl* CreateImpl(llvm::Deserializer& D);
  
  friend Decl* Decl::Create(llvm::Deserializer& D);
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
    return D->getKind() >= TypeFirst && D->getKind() <= TypeLast;
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

protected:
  /// EmitImpl - Serialize this TypedefDecl.  Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a TypedefDecl.  Called by Decl::Create.
  static TypedefDecl* CreateImpl(llvm::Deserializer& D);
  
  friend Decl* Decl::Create(llvm::Deserializer& D);
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
    return D->getKind() >= TagFirst && D->getKind() <= TagLast;
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
	IntegerType = QualType();
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
  
  static bool classof(const Decl *D) { return D->getKind() == Enum; }
  static bool classof(const EnumDecl *D) { return true; }
  
protected:
  /// EmitImpl - Serialize this EnumDecl.  Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a EnumDecl.  Called by Decl::Create.
  static EnumDecl* CreateImpl(llvm::Deserializer& D);
  
  friend Decl* Decl::Create(llvm::Deserializer& D);
};


/// RecordDecl - Represents a struct/union/class.  For example:
///   struct X;                  // Forward declaration, no "body".
///   union Y { int A, B; };     // Has body with members A and B (FieldDecls).
/// This decl will be marked invalid if *any* members are invalid.
///
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
    return D->getKind() >= RecordFirst && D->getKind() <= RecordLast;
  }
  static bool classof(const RecordDecl *D) { return true; }

protected:
  /// EmitImpl - Serialize this RecordDecl.  Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a RecordDecl.  Called by Decl::Create.
  static RecordDecl* CreateImpl(Kind DK, llvm::Deserializer& D);
  
  friend Decl* Decl::Create(llvm::Deserializer& D);
};

class FileScopeAsmDecl : public Decl {
  StringLiteral *AsmString;
public:
  FileScopeAsmDecl(SourceLocation L, StringLiteral *asmstring)
    : Decl(FileScopeAsm, L), AsmString(asmstring) {}

  const StringLiteral *getAsmString() const { return AsmString; }
  StringLiteral *getAsmString() { return AsmString; }
  static bool classof(const Decl *D) {
    return D->getKind() == FileScopeAsm;
  }
  static bool classof(const FileScopeAsmDecl *D) { return true; }  
protected:
  /// EmitImpl - Serialize this FileScopeAsmDecl. Called by Decl::Emit.
  virtual void EmitImpl(llvm::Serializer& S) const;
  
  /// CreateImpl - Deserialize a FileScopeAsmDecl.  Called by Decl::Create.
  static FileScopeAsmDecl* CreateImpl(llvm::Deserializer& D);
  
  friend Decl* Decl::Create(llvm::Deserializer& D);
};

/// LinkageSpecDecl - This represents a linkage specification.  For example:
///   extern "C" void foo();
///
class LinkageSpecDecl : public Decl {
public:
  /// LanguageIDs - Used to represent the language in a linkage
  /// specification.  The values are part of the serialization abi for
  /// ASTs and cannot be changed without altering that abi.  To help
  /// ensure a stable abi for this, we choose the DW_LANG_ encodings
  /// from the dwarf standard.
  enum LanguageIDs { lang_c = /* DW_LANG_C */ 0x0002,
		     lang_cxx = /* DW_LANG_C_plus_plus */ 0x0004 };
private:
  /// Language - The language for this linkage specification.
  LanguageIDs Language;
  /// D - This is the Decl of the linkage specification.
  Decl *D;
public:
  LinkageSpecDecl(SourceLocation L, LanguageIDs lang, Decl *d)
   : Decl(LinkageSpec, L), Language(lang), D(d) {}
  
  LanguageIDs getLanguage() const { return Language; }
  const Decl *getDecl() const { return D; }
  Decl *getDecl() { return D; }
    
  static bool classof(const Decl *D) {
    return D->getKind() == LinkageSpec;
  }
  static bool classof(const LinkageSpecDecl *D) { return true; }
  
protected:
  void EmitInRec(llvm::Serializer& S) const;
  void ReadInRec(llvm::Deserializer& D);
};

}  // end namespace clang

#endif
