//===--- SemaDeclSpec.h - Declaration Specifier Semantic Analys -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines interfaces used for Declaration Specifiers and Declarators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_SEMADECLSPEC_H
#define LLVM_CLANG_PARSE_SEMADECLSPEC_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Parse/Action.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {
namespace clang {
  class LangOptions;
  class IdentifierInfo;
  
/// DeclSpec - This class captures information about "declaration specifiers",
/// which encompases storage-class-specifiers, type-specifiers, type-qualifiers,
/// and function-specifiers.
class DeclSpec {
public:
  // storage-class-specifier
  enum SCS {
    SCS_unspecified,
    SCS_typedef,
    SCS_extern,
    SCS_static,
    SCS_auto,
    SCS_register
  };
  
  // type-specifier
  enum TSW {
    TSW_unspecified,
    TSW_short,
    TSW_long,
    TSW_longlong
  };
  
  enum TSC {
    TSC_unspecified,
    TSC_imaginary,
    TSC_complex
  };
  
  enum TSS {
    TSS_unspecified,
    TSS_signed,
    TSS_unsigned
  };
  
  enum TST {
    TST_unspecified,
    TST_void,
    TST_char,
    TST_int,
    TST_float,
    TST_double,
    TST_bool,         // _Bool
    TST_decimal32,    // _Decimal32
    TST_decimal64,    // _Decimal64
    TST_decimal128,   // _Decimal128
    TST_enum,
    TST_union,
    TST_struct,
    TST_typedef
  };
  
  // type-qualifiers
  enum TQ {   // NOTE: These flags must be kept in sync with TypeRef::TQ.
    TQ_unspecified = 0,
    TQ_const       = 1,
    TQ_restrict    = 2,
    TQ_volatile    = 4
  };

  /// ParsedSpecifiers - Flags to query which specifiers were applied.  This is
  /// returned by getParsedSpecifiers.
  enum ParsedSpecifiers {
    PQ_None                  = 0,
    PQ_StorageClassSpecifier = 1,
    PQ_TypeSpecifier         = 2,
    PQ_TypeQualifier         = 4,
    PQ_FunctionSpecifier     = 8
  };
  
private:
    
  // storage-class-specifier
  SCS StorageClassSpec : 3;
  bool SCS_thread_specified : 1;

  // type-specifier
  TSW TypeSpecWidth : 2;
  TSC TypeSpecComplex : 2;
  TSS TypeSpecSign : 2;
  TST TypeSpecType : 4;
  
  // type-qualifiers
  unsigned TypeQualifiers : 3;  // Bitwise OR of TQ.
  
  // function-specifier
  bool FS_inline_specified : 1;
  
  // TypenameRep - If TypeSpecType == TST_typedef, this contains the
  // representation for the typedef.
  void *TypenameRep;  
  
  // attributes.
  // FIXME: implement declspec attributes.
  
  // SourceLocation info.  These are null if the item wasn't specified or if
  // the setting was synthesized.
  SourceLocation StorageClassSpecLoc, SCS_threadLoc;
  SourceLocation TSWLoc, TSCLoc, TSSLoc, TSTLoc;
  SourceLocation TQ_constLoc, TQ_restrictLoc, TQ_volatileLoc;
  SourceLocation FS_inlineLoc;
public:  
  
  DeclSpec()
    : StorageClassSpec(SCS_unspecified),
      SCS_thread_specified(false),
      TypeSpecWidth(TSW_unspecified),
      TypeSpecComplex(TSC_unspecified),
      TypeSpecSign(TSS_unspecified),
      TypeSpecType(TST_unspecified),
      TypeQualifiers(TSS_unspecified),
      FS_inline_specified(false),
      TypenameRep(0) {
  }
  
  // storage-class-specifier
  SCS getStorageClassSpec() const { return StorageClassSpec; }
  bool isThreadSpecified() const { return SCS_thread_specified; }
  
  SourceLocation getStorageClassSpecLoc() const { return StorageClassSpecLoc; }
  SourceLocation getThreadSpecLoc() const { return SCS_threadLoc; }
  
  
  void ClearStorageClassSpecs() {
    StorageClassSpec     = DeclSpec::SCS_unspecified;
    SCS_thread_specified = false;
    StorageClassSpecLoc  = SourceLocation();
    SCS_threadLoc        = SourceLocation();
  }
  
  // type-specifier
  TSW getTypeSpecWidth() const { return TypeSpecWidth; }
  TSC getTypeSpecComplex() const { return TypeSpecComplex; }
  TSS getTypeSpecSign() const { return TypeSpecSign; }
  TST getTypeSpecType() const { return TypeSpecType; }
  void *getTypenameRep() const { return TypenameRep; }
  
  SourceLocation getTypeSpecWidthLoc() const { return TSWLoc; }
  SourceLocation getTypeSpecComplexLoc() const { return TSCLoc; }
  SourceLocation getTypeSpecSignLoc() const { return TSSLoc; }
  SourceLocation getTypeSpecTypeLoc() const { return TSTLoc; }
  
  // type-qualifiers

  /// getTypeQualifiers - Return a set of TQs.
  unsigned getTypeQualifiers() const { return TypeQualifiers; }
  SourceLocation getConstSpecLoc() const { return TQ_constLoc; }
  SourceLocation getRestrictSpecLoc() const { return TQ_restrictLoc; }
  SourceLocation getVolatileSpecLoc() const { return TQ_volatileLoc; }

  
  // function-specifier
  bool isInlineSpecified() const { return FS_inline_specified; }
  SourceLocation getInlineSpecLoc() const { return FS_inlineLoc; }
  void ClearFunctionSpecs() {
    FS_inline_specified = false;
    FS_inlineLoc = SourceLocation();
  }
  
  /// hasTypeSpecifier - Return true if any type-specifier has been found.
  bool hasTypeSpecifier() const {
    return getTypeSpecType() != DeclSpec::TST_unspecified ||
           getTypeSpecWidth() != DeclSpec::TSW_unspecified ||
           getTypeSpecComplex() != DeclSpec::TSC_unspecified ||
           getTypeSpecSign() != DeclSpec::TSS_unspecified;
  }
  
  
  /// getParsedSpecifiers - Return a bitmask of which flavors of specifiers this
  /// DeclSpec includes.
  ///
  unsigned getParsedSpecifiers() const;
  
  /// These methods set the specified attribute of the DeclSpec, but return true
  /// and ignore the request if invalid (e.g. "extern" then "auto" is
  /// specified).  The name of the previous specifier is returned in prevspec.
  bool SetStorageClassSpec(SCS S, SourceLocation Loc, const char *&PrevSpec);
  bool SetStorageClassSpecThread(SourceLocation Loc, const char *&PrevSpec);
  bool SetTypeSpecWidth(TSW W, SourceLocation Loc, const char *&PrevSpec);
  bool SetTypeSpecComplex(TSC C, SourceLocation Loc, const char *&PrevSpec);
  bool SetTypeSpecSign(TSS S, SourceLocation Loc, const char *&PrevSpec);
  bool SetTypeSpecType(TST T, SourceLocation Loc, const char *&PrevSpec,
                       void *TypenameRep = 0);
  
  bool SetTypeQual(TQ T, SourceLocation Loc, const char *&PrevSpec,
                   const LangOptions &Lang);
  
  bool SetFunctionSpecInline(SourceLocation Loc, const char *&PrevSpec);
  
  /// Finish - This does final analysis of the declspec, issuing diagnostics for
  /// things like "_Imaginary" (lacking an FP type).  After calling this method,
  /// DeclSpec is guaranteed self-consistent, even if an error occurred.
  void Finish(Diagnostic &D, const LangOptions &Lang);
};


/// DeclaratorChunk - One instance of this struct is used for each type in a
/// declarator that is parsed.
///
/// This is intended to be a small value object.
struct DeclaratorChunk {
  enum {
    Pointer, Array, Function
  } Kind;
  
  /// Loc - The place where this type was defined.
  SourceLocation Loc;
  
  struct PointerTypeInfo {
    /// The type qualifiers: const/volatile/restrict.
    unsigned TypeQuals : 3;
  };
  struct ArrayTypeInfo {
    /// The type qualifiers for the array: const/volatile/restrict.
    unsigned TypeQuals : 3;
    
    /// True if this dimension included the 'static' keyword.
    bool hasStatic : 1;
    
    /// True if this dimension was [*].  In this case, NumElts is null.
    bool isStar : 1;
    
    /// This is the size of the array, or null if [] or [*] was specified.
    /// FIXME: make this be an expression* when we have expressions.
    void *NumElts;
  };
  
  /// ParamInfo - An array of paraminfo objects is allocated whenever a function
  /// declarator is parsed.  There are two interesting styles of arguments here:
  /// K&R-style identifier lists and parameter type lists.  K&R-style identifier
  /// lists will have information about the identifier, but no type information.
  /// Parameter type lists will have type info (if the actions module provides
  /// it), but may have null identifier info: e.g. for 'void foo(int X, int)'.
  struct ParamInfo {
    /// Ident - In a K&R 
    IdentifierInfo *Ident;
    SourceLocation IdentLoc;
    Action::TypeTy *TypeInfo;
    
    ParamInfo() {}
    ParamInfo(IdentifierInfo *ident, SourceLocation iloc, Action::TypeTy *typ)
      : Ident(ident), IdentLoc(iloc), TypeInfo(typ) {
    }
  };
  
  struct FunctionTypeInfo {
    /// hasPrototype - This is true if the function had at least one typed
    /// argument.  If the function is () or (a,b,c), then it has no prototype,
    /// and is treated as a K&R-style function.
    bool hasPrototype : 1;
    
    /// isVariadic - If this function has a prototype, and if that proto ends
    /// with ',...)', this is true.
    bool isVariadic : 1;

    /// NumArgs - This is the number of formal arguments provided for the
    /// declarator.
    unsigned NumArgs;

    /// ArgInfo - This is a pointer to a new[]'d array of ParamInfo objects that
    /// describe the arguments for this function declarator.  This is null if
    /// there are no arguments specified.
    ParamInfo *ArgInfo;
  };
  
  union {
    PointerTypeInfo Ptr;
    ArrayTypeInfo Arr;
    FunctionTypeInfo Fun;
  };
  
  
  /// getPointer - Return a DeclaratorChunk for a pointer.
  ///
  static DeclaratorChunk getPointer(unsigned TypeQuals, SourceLocation Loc) {
    DeclaratorChunk I;
    I.Kind          = Pointer;
    I.Loc           = Loc;
    I.Ptr.TypeQuals = TypeQuals;
    return I;
  }
  
  /// getArray - Return a DeclaratorChunk for an array.
  ///
  static DeclaratorChunk getArray(unsigned TypeQuals, bool isStatic,
                                  bool isStar, void *NumElts,
                                  SourceLocation Loc) {
    DeclaratorChunk I;
    I.Kind          = Array;
    I.Loc           = Loc;
    I.Arr.TypeQuals = TypeQuals;
    I.Arr.hasStatic = isStatic;
    I.Arr.isStar    = isStar;
    I.Arr.NumElts   = NumElts;
    return I;
  }
  
  /// getFunction - Return a DeclaratorChunk for a function.
  static DeclaratorChunk getFunction(bool hasProto, bool isVariadic,
                                     ParamInfo *ArgInfo, unsigned NumArgs,
                                     SourceLocation Loc) {
    DeclaratorChunk I;
    I.Kind             = Function;
    I.Loc              = Loc;
    I.Fun.hasPrototype = hasProto;
    I.Fun.isVariadic   = isVariadic;
    I.Fun.NumArgs      = NumArgs;
    I.Fun.ArgInfo      = 0;
    
    // new[] an argument array if needed.
    if (NumArgs) {
      I.Fun.ArgInfo = new DeclaratorChunk::ParamInfo[NumArgs];
      memcpy(I.Fun.ArgInfo, ArgInfo, sizeof(ArgInfo[0])*NumArgs);
    }
    return I;
  }
};


/// Declarator - Information about one declarator, including the parsed type
/// information and the identifier.  When the declarator is fully formed, this
/// is turned into the appropriate Decl object.
///
/// Declarators come in two types: normal declarators and abstract declarators.
/// Abstract declarators are used when parsing types, and don't have an
/// identifier.  Normal declarators do have ID's.
///
/// This is NOT intended to be a small value object: this should be a transient
/// object that lives on the stack.
class Declarator {
  const DeclSpec &DS;
  IdentifierInfo *Identifier;
  SourceLocation IdentifierLoc;
  
public:
  enum TheContext {
    FileContext,         // File scope declaration.
    PrototypeContext,    // Within a function prototype.
    KNRTypeListContext,  // K&R type definition list for formals.
    TypeNameContext,     // Abstract declarator for types.
    MemberContext,       // Struct/Union field.
    BlockContext,        // Declaration within a block in a function.
    ForContext           // Declaration within first part of a for loop.
  };
private:
  /// Context - Where we are parsing this declarator.
  ///
  TheContext Context;
  
  /// DeclTypeInfo - This holds each type that the declarator includes as it is
  /// parsed.  This is pushed from the identifier out, which means that element
  /// #0 will be the most closely bound to the identifier, and
  /// DeclTypeInfo.back() will be the least closely bound.
  SmallVector<DeclaratorChunk, 8> DeclTypeInfo;
  
public:
  Declarator(const DeclSpec &ds, TheContext C)
    : DS(ds), Identifier(0), Context(C) {
  }
  
  ~Declarator() {
    clear();
  }

  /// getDeclSpec - Return the declaration-specifier that this declarator was
  /// declared with.
  const DeclSpec &getDeclSpec() const { return DS; }
  
  TheContext getContext() const { return Context; }
  
  /// clear - Reset the contents of this Declarator.
  void clear() {
    Identifier = 0;
    IdentifierLoc = SourceLocation();
    
    for (unsigned i = 0, e = DeclTypeInfo.size(); i != e; ++i)
      if (DeclTypeInfo[i].Kind == DeclaratorChunk::Function)
        delete [] DeclTypeInfo[i].Fun.ArgInfo;
        
    DeclTypeInfo.clear();
  }
  
  /// mayOmitIdentifier - Return true if the identifier is either optional or
  /// not allowed.  This is true for typenames and prototypes.
  bool mayOmitIdentifier() const {
    return Context == TypeNameContext || Context == PrototypeContext;
  }

  /// mayHaveIdentifier - Return true if the identifier is either optional or
  /// required.  This is true for normal declarators and prototypes, but not
  /// typenames.
  bool mayHaveIdentifier() const {
    return Context != TypeNameContext;
  }
  
  /// isPastIdentifier - Return true if we have parsed beyond the point where
  /// the
  bool isPastIdentifier() const { return IdentifierLoc.isValid(); }
  
  IdentifierInfo *getIdentifier() const { return Identifier; }
  SourceLocation getIdentifierLoc() const { return IdentifierLoc; }
  
  void SetIdentifier(IdentifierInfo *ID, SourceLocation Loc) {
    Identifier = ID;
    IdentifierLoc = Loc;
  }
  
  void AddTypeInfo(const DeclaratorChunk &TI) {
    DeclTypeInfo.push_back(TI);
  }
  
  /// getNumTypeObjects() - Return the number of types applied to this
  /// declarator.
  unsigned getNumTypeObjects() const { return DeclTypeInfo.size(); }
  
  /// Return the specified TypeInfo from this declarator.  TypeInfo #0 is
  /// closest to the identifier.
  const DeclaratorChunk &getTypeObject(unsigned i) const {
    return DeclTypeInfo[i];
  }
  
  /// isFunctionDeclarator - Once this declarator is fully parsed and formed,
  /// this method returns true if the identifier is a function declarator.
  bool isFunctionDeclarator() const {
    return !DeclTypeInfo.empty() &&
           DeclTypeInfo[0].Kind == DeclaratorChunk::Function;
  }
};

  
}  // end namespace clang
}  // end namespace llvm

#endif
