//===-- DeclBase.h - Base Classes for representing declarations *- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Decl and DeclContext interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLBASE_H
#define LLVM_CLANG_AST_DECLBASE_H

#include "clang/AST/Attr.h"
#include "clang/AST/Type.h"
// FIXME: Layering violation
#include "clang/Parse/AccessSpecifier.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/PointerIntPair.h"

namespace clang {
class DeclContext;
class TranslationUnitDecl;
class NamespaceDecl;
class NamedDecl;
class FunctionDecl;
class CXXRecordDecl;
class EnumDecl;
class ObjCMethodDecl;
class ObjCInterfaceDecl;
class ObjCCategoryDecl;
class ObjCProtocolDecl;
class ObjCImplementationDecl;
class ObjCCategoryImplDecl;
class LinkageSpecDecl;
class BlockDecl;
class DeclarationName;

/// Decl - This represents one declaration (or definition), e.g. a variable, 
/// typedef, function, struct, etc.  
///
class Decl {
public:
  enum Kind {
    // This lists the concrete classes of Decl in order of the inheritance
    // hierarchy.  This allows us to do efficient classof tests based on the
    // enums below.   The commented out names are abstract class names.
    // [DeclContext] indicates that the class also inherits from DeclContext.
    
    // Decl
         TranslationUnit,  // [DeclContext]
    //   NamedDecl
           OverloadedFunction,
           Field,
             ObjCIvar,
             ObjCAtDefsField,
           Namespace,  // [DeclContext]
    //     TypeDecl
             Typedef,
    //       TagDecl // [DeclContext]
               Enum,  
               Record,
                 CXXRecord,  
 	     TemplateTypeParm,
    //     ValueDecl
             EnumConstant,
             Function,  // [DeclContext]
               CXXMethod,
                 CXXConstructor,
                 CXXDestructor,
                 CXXConversion,
             Var,
               ImplicitParam,
               CXXClassVar,
               ParmVar,
                 OriginalParmVar,
  	       NonTypeTemplateParm,
           ObjCMethod,  // [DeclContext]
           ObjCContainer, // [DeclContext]
             ObjCCategory,
             ObjCProtocol,
             ObjCInterface,
             ObjCCategoryImpl,  // [DeclContext]
             ObjCProperty,
             ObjCCompatibleAlias,
         LinkageSpec, // [DeclContext]
         ObjCPropertyImpl,
         ObjCImplementation, // [DeclContext]
         ObjCForwardProtocol,
         ObjCClass,
         FileScopeAsm,
         Block, // [DeclContext]
  
    // For each non-leaf class, we now define a mapping to the first/last member
    // of the class, to allow efficient classof.
    NamedFirst    = OverloadedFunction, NamedLast   = ObjCCompatibleAlias,
    ObjCContainerFirst = ObjCContainer, ObjCContainerLast = ObjCInterface,
    FieldFirst         = Field        , FieldLast     = ObjCAtDefsField,
    TypeFirst          = Typedef      , TypeLast      = TemplateTypeParm,
    TagFirst           = Enum         , TagLast       = CXXRecord,
    RecordFirst        = Record       , RecordLast    = CXXRecord,
    ValueFirst         = EnumConstant , ValueLast     = NonTypeTemplateParm,
    FunctionFirst      = Function     , FunctionLast  = CXXConversion,
    VarFirst           = Var          , VarLast       = NonTypeTemplateParm
  };

  /// IdentifierNamespace - According to C99 6.2.3, there are four namespaces,
  /// labels, tags, members and ordinary identifiers. These are meant
  /// as bitmasks, so that searches in C++ can look into the "tag" namespace
  /// during ordinary lookup.
  enum IdentifierNamespace {
    IDNS_Label = 0x1,
    IDNS_Tag = 0x2,
    IDNS_Member = 0x4,
    IDNS_Ordinary = 0x8
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
  
  /// NextDeclarator - If this decl was part of a multi-declarator declaration,
  /// such as "int X, Y, *Z;" this indicates Decl for the next declarator.
  Decl *NextDeclarator;
  
  /// NextDeclInScope - The next declaration within the same lexical
  /// DeclContext. These pointers form the linked list that is
  /// traversed via DeclContext's decls_begin()/decls_end().
  /// FIXME: If NextDeclarator is non-NULL, will it always be the same
  /// as NextDeclInScope? If so, we can use a
  /// PointerIntPair<Decl*, 1> to make Decl smaller.
  Decl *NextDeclInScope;

  friend class DeclContext;

  /// DeclCtx - Holds either a DeclContext* or a MultipleDC*.
  /// For declarations that don't contain C++ scope specifiers, it contains
  /// the DeclContext where the Decl was declared.
  /// For declarations with C++ scope specifiers, it contains a MultipleDC*
  /// with the context where it semantically belongs (SemanticDC) and the
  /// context where it was lexically declared (LexicalDC).
  /// e.g.:
  ///
  ///   namespace A {
  ///      void f(); // SemanticDC == LexicalDC == 'namespace A'
  ///   }
  ///   void A::f(); // SemanticDC == namespace 'A'
  ///                // LexicalDC == global namespace
  uintptr_t DeclCtx;

  struct MultipleDC {
    DeclContext *SemanticDC;
    DeclContext *LexicalDC;
  };

  inline bool isInSemaDC() const { return (DeclCtx & 0x1) == 0; }
  inline bool isOutOfSemaDC() const { return (DeclCtx & 0x1) != 0; }
  inline MultipleDC *getMultipleDC() const {
    return reinterpret_cast<MultipleDC*>(DeclCtx & ~0x1);
  }

  /// DeclKind - This indicates which class this is.
  Kind DeclKind   :  8;
  
  /// InvalidDecl - This indicates a semantic error occurred.
  unsigned int InvalidDecl :  1;
  
  /// HasAttrs - This indicates whether the decl has attributes or not.
  unsigned int HasAttrs : 1;

  /// Implicit - Whether this declaration was implicitly generated by
  /// the implementation rather than explicitly written by the user.
  bool Implicit : 1;

protected:
  /// Access - Used by C++ decls for the access specifier.
  // NOTE: VC++ treats enums as signed, avoid using the AccessSpecifier enum
  unsigned Access : 2;
  friend class CXXClassMemberWrapper;

  Decl(Kind DK, DeclContext *DC, SourceLocation L) 
    : Loc(L), NextDeclarator(0), NextDeclInScope(0), 
      DeclCtx(reinterpret_cast<uintptr_t>(DC)), DeclKind(DK), InvalidDecl(0),
      HasAttrs(false), Implicit(false) {
    if (Decl::CollectingStats()) addDeclKind(DK);
  }

  virtual ~Decl();

  /// setDeclContext - Set both the semantic and lexical DeclContext
  /// to DC.
  void setDeclContext(DeclContext *DC);

public:
  SourceLocation getLocation() const { return Loc; }
  void setLocation(SourceLocation L) { Loc = L; }

  Kind getKind() const { return DeclKind; }
  const char *getDeclKindName() const;
  
  const DeclContext *getDeclContext() const {
    if (isInSemaDC())
      return reinterpret_cast<DeclContext*>(DeclCtx);
    return getMultipleDC()->SemanticDC;
  }
  DeclContext *getDeclContext() {
    return const_cast<DeclContext*>(
                         const_cast<const Decl*>(this)->getDeclContext());
  }

  void setAccess(AccessSpecifier AS) { Access = AS; }
  AccessSpecifier getAccess() const { return AccessSpecifier(Access); }

  void addAttr(Attr *attr);
  const Attr *getAttrs() const;
  void swapAttrs(Decl *D);
  void invalidateAttrs();

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

  /// isImplicit - Indicates whether the declaration was implicitly
  /// generated by the implementation. If false, this declaration
  /// was written explicitly in the source code.
  bool isImplicit() const { return Implicit; }
  void setImplicit(bool I = true) { Implicit = I; }
  
  IdentifierNamespace getIdentifierNamespace() const {
    switch (DeclKind) {
    default: 
      if (DeclKind >= FunctionFirst && DeclKind <= FunctionLast)
        return IDNS_Ordinary;
      assert(0 && "Unknown decl kind!");
    case OverloadedFunction:
    case Typedef:
    case EnumConstant:
    case Var:
    case CXXClassVar:
    case ImplicitParam:
    case ParmVar:
    case OriginalParmVar:
    case NonTypeTemplateParm:
    case ObjCMethod:
    case ObjCContainer:
    case ObjCCategory:
    case ObjCProtocol:
    case ObjCInterface:
    case ObjCCategoryImpl:
    case ObjCProperty:
    case ObjCCompatibleAlias:
      return IDNS_Ordinary;

    case Field:
    case ObjCAtDefsField:
    case ObjCIvar:
      return IDNS_Member;

    case Record:
    case CXXRecord:
    case Enum:
    case TemplateTypeParm:
      return IDNS_Tag;

    case Namespace:
      return IdentifierNamespace(IDNS_Tag | IDNS_Ordinary);
    }
  }
  
  bool isInIdentifierNamespace(unsigned NS) const {
    return getIdentifierNamespace() & NS;
  }
  
  /// getLexicalDeclContext - The declaration context where this Decl was
  /// lexically declared (LexicalDC). May be different from
  /// getDeclContext() (SemanticDC).
  /// e.g.:
  ///
  ///   namespace A {
  ///      void f(); // SemanticDC == LexicalDC == 'namespace A'
  ///   }
  ///   void A::f(); // SemanticDC == namespace 'A'
  ///                // LexicalDC == global namespace
  const DeclContext *getLexicalDeclContext() const {
    if (isInSemaDC())
      return reinterpret_cast<DeclContext*>(DeclCtx);
    return getMultipleDC()->LexicalDC;
  }
  DeclContext *getLexicalDeclContext() {
    return const_cast<DeclContext*>(
                  const_cast<const Decl*>(this)->getLexicalDeclContext());
  }

  void setLexicalDeclContext(DeclContext *DC);

  /// getNextDeclarator - If this decl was part of a multi-declarator
  /// declaration, such as "int X, Y, *Z;" this returns the decl for the next
  /// declarator.  Otherwise it returns null.
  Decl *getNextDeclarator() { return NextDeclarator; }
  const Decl *getNextDeclarator() const { return NextDeclarator; }
  void setNextDeclarator(Decl *N) { NextDeclarator = N; }
  
  // isDefinedOutsideFunctionOrMethod - This predicate returns true if this
  // scoped decl is defined outside the current function or method.  This is
  // roughly global variables and functions, but also handles enums (which could
  // be defined inside or outside a function etc).
  bool isDefinedOutsideFunctionOrMethod() const;

  // getBody - If this Decl represents a declaration for a body of code,
  //  such as a function or method definition, this method returns the top-level
  //  Stmt* of that body.  Otherwise this method returns null.  
  virtual Stmt* getBody() const { return 0; }
  
  // global temp stats (until we have a per-module visitor)
  static void addDeclKind(Kind k);
  static bool CollectingStats(bool Enable = false);
  static void PrintStats();
    
  /// isTemplateParameter - Determines whether this declartion is a
  /// template parameter.
  bool isTemplateParameter() const;

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Decl *) { return true; }
  static DeclContext *castToDeclContext(const Decl *);
  static Decl *castFromDeclContext(const DeclContext *);
  
  /// Emit - Serialize this Decl to Bitcode.
  void Emit(llvm::Serializer& S) const;
    
  /// Create - Deserialize a Decl from Bitcode.
  static Decl* Create(llvm::Deserializer& D, ASTContext& C);

  /// Destroy - Call destructors and release memory.
  virtual void Destroy(ASTContext& C);

protected:
  /// EmitImpl - Provides the subclass-specific serialization logic for
  ///   serializing out a decl.
  virtual void EmitImpl(llvm::Serializer& S) const {
    // FIXME: This will eventually be a pure virtual function.
    assert (false && "Not implemented.");
  }
};

/// DeclContext - This is used only as base class of specific decl types that
/// can act as declaration contexts. These decls are:
///
///   TranslationUnitDecl
///   NamespaceDecl
///   FunctionDecl
///   RecordDecl/CXXRecordDecl
///   EnumDecl
///   ObjCMethodDecl
///   ObjCInterfaceDecl
///   LinkageSpecDecl
///   BlockDecl
class DeclContext {
  /// DeclKind - This indicates which class this is.
  Decl::Kind DeclKind   :  8;

  /// LookupPtrKind - Describes what kind of pointer LookupPtr
  /// actually is. 
  enum LookupPtrKind {
    /// LookupIsMap - Indicates that LookupPtr is actually a map.
    LookupIsMap = 7
  };

  /// LookupPtr - Pointer to a data structure used to lookup
  /// declarations within this context. If the context contains fewer
  /// than seven declarations, the number of declarations is provided
  /// in the 3 lowest-order bits and the upper bits are treated as a
  /// pointer to an array of NamedDecl pointers. If the context
  /// contains seven or more declarations, the upper bits are treated
  /// as a pointer to a DenseMap<DeclarationName, std::vector<NamedDecl*>>.
  /// FIXME: We need a better data structure for this.
  llvm::PointerIntPair<void*, 3> LookupPtr;

  /// FirstDecl - The first declaration stored within this declaration
  /// context.
  Decl *FirstDecl;

  /// LastDecl - The last declaration stored within this declaration
  /// context. FIXME: We could probably cache this value somewhere
  /// outside of the DeclContext, to reduce the size of DeclContext by
  /// another pointer.
  Decl *LastDecl;

  // Used in the CastTo template to get the DeclKind
  // from a Decl or a DeclContext. DeclContext doesn't have a getKind() method
  // to avoid 'ambiguous access' compiler errors.
  template<typename T> struct KindTrait {
    static Decl::Kind getKind(const T *D) { return D->getKind(); }
  };

  // Used only by the ToDecl and FromDecl methods
  template<typename To, typename From>
  static To *CastTo(const From *D) {
    Decl::Kind DK = KindTrait<From>::getKind(D);
    switch(DK) {
      case Decl::TranslationUnit:
        return static_cast<TranslationUnitDecl*>(const_cast<From*>(D));
      case Decl::Namespace:
        return static_cast<NamespaceDecl*>(const_cast<From*>(D));
      case Decl::Enum:
        return static_cast<EnumDecl*>(const_cast<From*>(D));
      case Decl::Record:
        return static_cast<RecordDecl*>(const_cast<From*>(D));
      case Decl::CXXRecord:
        return static_cast<CXXRecordDecl*>(const_cast<From*>(D));
      case Decl::ObjCMethod:
        return static_cast<ObjCMethodDecl*>(const_cast<From*>(D));
      case Decl::ObjCInterface:
        return static_cast<ObjCInterfaceDecl*>(const_cast<From*>(D));
      case Decl::ObjCCategory:
        return static_cast<ObjCCategoryDecl*>(const_cast<From*>(D));
      case Decl::ObjCProtocol:
        return static_cast<ObjCProtocolDecl*>(const_cast<From*>(D));
      case Decl::ObjCImplementation:
        return static_cast<ObjCImplementationDecl*>(const_cast<From*>(D));
      case Decl::ObjCCategoryImpl:
        return static_cast<ObjCCategoryImplDecl*>(const_cast<From*>(D));
      case Decl::LinkageSpec:
        return static_cast<LinkageSpecDecl*>(const_cast<From*>(D));
      case Decl::Block:
        return static_cast<BlockDecl*>(const_cast<From*>(D));
      default:
        if (DK >= Decl::FunctionFirst && DK <= Decl::FunctionLast)
          return static_cast<FunctionDecl*>(const_cast<From*>(D));

        assert(false && "a decl that inherits DeclContext isn't handled");
        return 0;
    }
  }

  /// isLookupMap - Determine if the lookup structure is a
  /// DenseMap. Othewise, it is an array.
  bool isLookupMap() const { return LookupPtr.getInt() == LookupIsMap; }

  static Decl *getNextDeclInScope(Decl *D) { return D->NextDeclInScope; }

protected:
   DeclContext(Decl::Kind K) 
     : DeclKind(K), LookupPtr(), FirstDecl(0), LastDecl(0) { }

  void DestroyDecls(ASTContext &C);

public:
  ~DeclContext();

  Decl::Kind getDeclKind() const {
    return DeclKind;
  }
  const char *getDeclKindName() const;

  /// getParent - Returns the containing DeclContext if this is a Decl,
  /// else returns NULL.
  const DeclContext *getParent() const;
  DeclContext *getParent() {
    return const_cast<DeclContext*>(
                             const_cast<const DeclContext*>(this)->getParent());
  }

  /// getLexicalParent - Returns the containing lexical DeclContext. May be
  /// different from getParent, e.g.:
  ///
  ///   namespace A {
  ///      struct S;
  ///   }
  ///   struct A::S {}; // getParent() == namespace 'A'
  ///                   // getLexicalParent() == translation unit
  ///
  const DeclContext *getLexicalParent() const;
  DeclContext *getLexicalParent() {
    return const_cast<DeclContext*>(
                      const_cast<const DeclContext*>(this)->getLexicalParent());
  }

  bool isFunctionOrMethod() const {
    switch (DeclKind) {
      case Decl::Block:
      case Decl::ObjCMethod:
        return true;

      default:
       if (DeclKind >= Decl::FunctionFirst && DeclKind <= Decl::FunctionLast)
         return true;
        return false;
    }
  }

  bool isFileContext() const {
    return DeclKind == Decl::TranslationUnit || DeclKind == Decl::Namespace;
  }

  bool isRecord() const {
    return DeclKind == Decl::Record || DeclKind == Decl::CXXRecord;
  }

  bool isNamespace() const {
    return DeclKind == Decl::Namespace;
  }

  /// isTransparentContext - Determines whether this context is a
  /// "transparent" context, meaning that the members declared in this
  /// context are semantically declared in the nearest enclosing
  /// non-transparent (opaque) context but are lexically declared in
  /// this context. For example, consider the enumerators of an
  /// enumeration type: 
  /// @code
  /// enum E {
  ///   Val1 
  /// };
  /// @endcode
  /// Here, E is a transparent context, so its enumerator (Val1) will
  /// appear (semantically) that it is in the same context of E.
  /// Examples of transparent contexts include: enumerations (except for
  /// C++0x scoped enums), C++ linkage specifications, and C++0x
  /// inline namespaces.
  bool isTransparentContext() const;

  bool Encloses(DeclContext *DC) const {
    for (; DC; DC = DC->getParent())
      if (DC == this)
        return true;
    return false;
  }

  /// getPrimaryContext - There may be many different
  /// declarations of the same entity (including forward declarations
  /// of classes, multiple definitions of namespaces, etc.), each with
  /// a different set of declarations. This routine returns the
  /// "primary" DeclContext structure, which will contain the
  /// information needed to perform name lookup into this context.
  DeclContext *getPrimaryContext();

  /// getLookupContext - Retrieve the innermost non-transparent
  /// context of this context, which corresponds to the innermost
  /// location from which name lookup can find the entities in this
  /// context.
  DeclContext *getLookupContext() {
    return const_cast<DeclContext *>(
             const_cast<const DeclContext *>(this)->getLookupContext());
  }
  const DeclContext *getLookupContext() const;

  /// getNextContext - If this is a DeclContext that may have other
  /// DeclContexts that are semantically connected but syntactically
  /// different, such as C++ namespaces, this routine retrieves the
  /// next DeclContext in the link. Iteration through the chain of
  /// DeclContexts should begin at the primary DeclContext and
  /// continue until this function returns NULL. For example, given:
  /// @code
  /// namespace N {
  ///   int x;
  /// }
  /// namespace N {
  ///   int y;
  /// }
  /// @endcode
  /// The first occurrence of namespace N will be the primary
  /// DeclContext. Its getNextContext will return the second
  /// occurrence of namespace N.
  DeclContext *getNextContext();

  /// decl_iterator - Iterates through the declarations stored
  /// within this context.
  class decl_iterator {
    /// Current - The current declaration.
    Decl *Current;

  public:
    typedef Decl*                     value_type;
    typedef Decl*                     reference;
    typedef Decl*                     pointer;
    typedef std::forward_iterator_tag iterator_category;
    typedef std::ptrdiff_t            difference_type;

    decl_iterator() : Current(0) { }
    explicit decl_iterator(Decl *C) : Current(C) { }

    reference operator*() const { return Current; }
    pointer operator->() const { return Current; }

    decl_iterator& operator++();

    decl_iterator operator++(int) {
      decl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }

    friend bool operator==(decl_iterator x, decl_iterator y) { 
      return x.Current == y.Current;
    }
    friend bool operator!=(decl_iterator x, decl_iterator y) { 
      return x.Current != y.Current;
    }
  };

  /// decls_begin/decls_end - Iterate over the declarations stored in
  /// this context. 
  decl_iterator decls_begin() const { return decl_iterator(FirstDecl); }
  decl_iterator decls_end()   const { return decl_iterator(); }

  /// specific_decl_iterator - Iterates over a subrange of
  /// declarations stored in a DeclContext, providing only those that
  /// are of type SpecificDecl (or a class derived from it) and,
  /// optionally, that meet some additional run-time criteria. This
  /// iterator is used, for example, to provide iteration over just
  /// the fields within a RecordDecl (with SpecificDecl = FieldDecl)
  /// or the instance methods within an Objective-C interface (with
  /// SpecificDecl = ObjCMethodDecl and using
  /// ObjCMethodDecl::isInstanceMethod as the run-time criteria). 
  template<typename SpecificDecl>
  class specific_decl_iterator {
    /// Current - The current, underlying declaration iterator, which
    /// will either be the same as End or will point to a declaration of
    /// type SpecificDecl.
    DeclContext::decl_iterator Current;
    
    /// End - One past the last declaration within the DeclContext.
    DeclContext::decl_iterator End;

    /// Acceptable - If non-NULL, points to a member function that
    /// will determine if a particular declaration of type
    /// SpecificDecl should be visited by the iteration.
    bool (SpecificDecl::*Acceptable)() const;

    /// SkipToNextDecl - Advances the current position up to the next
    /// declaration of type SpecificDecl that also meets the criteria
    /// required by Acceptable.
    void SkipToNextDecl() {
      while (Current != End && 
             (!isa<SpecificDecl>(*Current) ||
              (Acceptable && !(cast<SpecificDecl>(*Current)->*Acceptable)())))
        ++Current;
    }

  public:
    typedef SpecificDecl* value_type;
    typedef SpecificDecl* reference;
    typedef SpecificDecl* pointer;
    typedef std::iterator_traits<DeclContext::decl_iterator>::difference_type
      difference_type;
    typedef std::forward_iterator_tag iterator_category;

    specific_decl_iterator() : Current(), End(), Acceptable(0) { }

    /// specific_decl_iterator - Construct a new iterator over a
    /// subset of the declarations in [C, E). If A is non-NULL, it is
    /// a pointer to a member function of SpecificDecl that should
    /// return true for all of the SpecificDecl instances that will be
    /// in the subset of iterators. For example, if you want
    /// Objective-C instance methods, SpecificDecl will be
    /// ObjCMethodDecl and A will be &ObjCMethodDecl::isInstanceMethod.
    specific_decl_iterator(DeclContext::decl_iterator C, 
                           DeclContext::decl_iterator E,
                           bool (SpecificDecl::*A)() const = 0)
      : Current(C), End(E), Acceptable(A) {
      SkipToNextDecl();
    }

    reference operator*() const { return cast<SpecificDecl>(*Current); }
    pointer operator->() const { return cast<SpecificDecl>(*Current); }

    specific_decl_iterator& operator++() {
      ++Current;
      SkipToNextDecl();
      return *this;
    }

    specific_decl_iterator operator++(int) {
      specific_decl_iterator tmp(*this);
      ++(*this);
      return tmp;
    }
  
    friend bool
    operator==(const specific_decl_iterator& x, const specific_decl_iterator& y) {
      return x.Current == y.Current;
    }
  
    friend bool 
    operator!=(const specific_decl_iterator& x, const specific_decl_iterator& y) {
      return x.Current != y.Current;
    }
  };

  /// @brief Add the declaration D into this context.
  ///
  /// This routine should be invoked when the declaration D has first
  /// been declared, to place D into the context where it was
  /// (lexically) defined. Every declaration must be added to one
  /// (and only one!) context, where it can be visited via
  /// [decls_begin(), decls_end()). Once a declaration has been added
  /// to its lexical context, the corresponding DeclContext owns the
  /// declaration.
  ///
  /// If D is also a NamedDecl, it will be made visible within its
  /// semantic context via makeDeclVisibleInContext.
  void addDecl(Decl *D);

  /// lookup_iterator - An iterator that provides access to the results
  /// of looking up a name within this context.
  typedef NamedDecl **lookup_iterator;

  /// lookup_const_iterator - An iterator that provides non-mutable
  /// access to the results of lookup up a name within this context.
  typedef NamedDecl * const * lookup_const_iterator;

  typedef std::pair<lookup_iterator, lookup_iterator> lookup_result;
  typedef std::pair<lookup_const_iterator, lookup_const_iterator>
    lookup_const_result;

  /// lookup - Find the declarations (if any) with the given Name in
  /// this context. Returns a range of iterators that contains all of
  /// the declarations with this name, with object, function, member,
  /// and enumerator names preceding any tag name. Note that this
  /// routine will not look into parent contexts.
  lookup_result lookup(DeclarationName Name);
  lookup_const_result lookup(DeclarationName Name) const;

  /// @brief Makes a declaration visible within this context.
  ///
  /// This routine makes the declaration D visible to name lookup
  /// within this context and, if this is a transparent context,
  /// within its parent contexts up to the first enclosing
  /// non-transparent context. Making a declaration visible within a
  /// context does not transfer ownership of a declaration, and a
  /// declaration can be visible in many contexts that aren't its
  /// lexical context.
  ///
  /// If D is a redeclaration of an existing declaration that is
  /// visible from this context, as determined by
  /// NamedDecl::declarationReplaces, the previous declaration will be
  /// replaced with D.
  void makeDeclVisibleInContext(NamedDecl *D);

  static bool classof(const Decl *D) {
    switch (D->getKind()) {
      case Decl::TranslationUnit:
      case Decl::Namespace:
      case Decl::Enum:
      case Decl::Record:
      case Decl::CXXRecord:
      case Decl::ObjCMethod:
      case Decl::ObjCInterface:
      case Decl::ObjCCategory:
      case Decl::ObjCProtocol:
      case Decl::ObjCImplementation:
      case Decl::ObjCCategoryImpl:
      case Decl::LinkageSpec:
      case Decl::Block:
        return true;
      default:
        if (D->getKind() >= Decl::FunctionFirst &&
            D->getKind() <= Decl::FunctionLast)
          return true;
        return false;
    }
  }
  static bool classof(const DeclContext *D) { return true; }
  static bool classof(const TranslationUnitDecl *D) { return true; }
  static bool classof(const NamespaceDecl *D) { return true; }
  static bool classof(const FunctionDecl *D) { return true; }
  static bool classof(const RecordDecl *D) { return true; }
  static bool classof(const CXXRecordDecl *D) { return true; }
  static bool classof(const EnumDecl *D) { return true; }
  static bool classof(const ObjCMethodDecl *D) { return true; }
  static bool classof(const ObjCInterfaceDecl *D) { return true; }
  static bool classof(const ObjCCategoryDecl *D) { return true; }
  static bool classof(const ObjCProtocolDecl *D) { return true; }
  static bool classof(const ObjCImplementationDecl *D) { return true; }
  static bool classof(const ObjCCategoryImplDecl *D) { return true; }
  static bool classof(const LinkageSpecDecl *D) { return true; }
  static bool classof(const BlockDecl *D) { return true; }

private:
  void buildLookup(DeclContext *DCtx);
  void makeDeclVisibleInContextImpl(NamedDecl *D);

  void EmitOutRec(llvm::Serializer& S) const;
  void ReadOutRec(llvm::Deserializer& D, ASTContext& C);

  friend class Decl;
};

template<> struct DeclContext::KindTrait<DeclContext> {
  static Decl::Kind getKind(const DeclContext *D) { return D->DeclKind; }
};

inline bool Decl::isTemplateParameter() const {
  return getKind() == TemplateTypeParm || getKind() == NonTypeTemplateParm;
}

inline bool Decl::isDefinedOutsideFunctionOrMethod() const {
  if (getDeclContext())
    return !getDeclContext()->getLookupContext()->isFunctionOrMethod();
  else
    return true;
}

inline DeclContext::decl_iterator& DeclContext::decl_iterator::operator++() {
  Current = getNextDeclInScope(Current);
  return *this;
}

} // end clang.

namespace llvm {

/// Implement a isa_impl_wrap specialization to check whether a DeclContext is
/// a specific Decl.
template<class ToTy>
struct isa_impl_wrap<ToTy,
                     const ::clang::DeclContext,const ::clang::DeclContext> {
  static bool doit(const ::clang::DeclContext &Val) {
    return ToTy::classof(::clang::Decl::castFromDeclContext(&Val));
  }
};
template<class ToTy>
struct isa_impl_wrap<ToTy, ::clang::DeclContext, ::clang::DeclContext>
  : public isa_impl_wrap<ToTy,
                      const ::clang::DeclContext,const ::clang::DeclContext> {};

/// Implement cast_convert_val for Decl -> DeclContext conversions.
template<class FromTy>
struct cast_convert_val< ::clang::DeclContext, FromTy, FromTy> {
  static ::clang::DeclContext &doit(const FromTy &Val) {
    return *FromTy::castToDeclContext(&Val);
  }
};

template<class FromTy>
struct cast_convert_val< ::clang::DeclContext, FromTy*, FromTy*> {
  static ::clang::DeclContext *doit(const FromTy *Val) {
    return FromTy::castToDeclContext(Val);
  }
};

template<class FromTy>
struct cast_convert_val< const ::clang::DeclContext, FromTy, FromTy> {
  static const ::clang::DeclContext &doit(const FromTy &Val) {
    return *FromTy::castToDeclContext(&Val);
  }
};

template<class FromTy>
struct cast_convert_val< const ::clang::DeclContext, FromTy*, FromTy*> {
  static const ::clang::DeclContext *doit(const FromTy *Val) {
    return FromTy::castToDeclContext(Val);
  }
};

/// Implement cast_convert_val for DeclContext -> Decl conversions.
template<class ToTy>
struct cast_convert_val<ToTy,
                        const ::clang::DeclContext,const ::clang::DeclContext> {
  static ToTy &doit(const ::clang::DeclContext &Val) {
    return *reinterpret_cast<ToTy*>(ToTy::castFromDeclContext(&Val));
  }
};
template<class ToTy>
struct cast_convert_val<ToTy, ::clang::DeclContext, ::clang::DeclContext>
  : public cast_convert_val<ToTy,
                      const ::clang::DeclContext,const ::clang::DeclContext> {};

template<class ToTy>
struct cast_convert_val<ToTy,
                     const ::clang::DeclContext*, const ::clang::DeclContext*> {
  static ToTy *doit(const ::clang::DeclContext *Val) {
    return reinterpret_cast<ToTy*>(ToTy::castFromDeclContext(Val));
  }
};
template<class ToTy>
struct cast_convert_val<ToTy, ::clang::DeclContext*, ::clang::DeclContext*>
  : public cast_convert_val<ToTy,
                    const ::clang::DeclContext*,const ::clang::DeclContext*> {};

} // end namespace llvm

#endif
