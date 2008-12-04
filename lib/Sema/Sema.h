//===--- Sema.h - Semantic Analysis & AST Building --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Sema class, which performs semantic analysis and
// builds ASTs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_SEMA_H
#define LLVM_CLANG_AST_SEMA_H

#include "IdentifierResolver.h"
#include "CXXFieldCollector.h"
#include "SemaOverload.h"
#include "clang/Parse/Action.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/OwningPtr.h"
#include <vector>

namespace llvm {
  class APSInt;
}

namespace clang {
  class ASTContext;
  class ASTConsumer;
  class Preprocessor;
  class Decl;
  class DeclContext;
  class DeclSpec;
  class NamedDecl;
  class ScopedDecl;
  class Expr;
  class InitListExpr;
  class CallExpr;
  class VarDecl;
  class ParmVarDecl;
  class TypedefDecl;
  class FunctionDecl;
  class QualType;
  struct LangOptions;
  class Token;
  class IntegerLiteral;
  class StringLiteral;
  class ArrayType;
  class LabelStmt;
  class SwitchStmt;
  class ExtVectorType;
  class TypedefDecl;
  class ObjCInterfaceDecl;
  class ObjCCompatibleAliasDecl;
  class ObjCProtocolDecl;
  class ObjCImplementationDecl;
  class ObjCCategoryImplDecl;
  class ObjCCategoryDecl;
  class ObjCIvarDecl;
  class ObjCMethodDecl;
  class ObjCPropertyDecl;
  struct BlockSemaInfo;
  class BasePaths;

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
  PragmaPackStack(unsigned A) : Alignment(A) {}

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

/// Sema - This implements semantic analysis and AST building for C.
class Sema : public Action {
public:
  Preprocessor &PP;
  ASTContext &Context;
  ASTConsumer &Consumer;
  Diagnostic &Diags;
  SourceManager &SourceMgr;

  /// CurContext - This is the current declaration context of parsing.
  DeclContext *CurContext;

  /// PreDeclaratorDC - Keeps the declaration context before switching to the
  /// context of a declarator's nested-name-specifier.
  DeclContext *PreDeclaratorDC;

  /// CurBlock - If inside of a block definition, this contains a pointer to
  /// the active block object that represents it.
  BlockSemaInfo *CurBlock;

  /// PackContext - Manages the stack for #pragma pack. An alignment
  /// of 0 indicates default alignment.
  PragmaPackStack PackContext;

  /// LabelMap - This is a mapping from label identifiers to the LabelStmt for
  /// it (which acts like the label decl in some ways).  Forward referenced
  /// labels have a LabelStmt created for them with a null location & SubStmt.
  llvm::DenseMap<IdentifierInfo*, LabelStmt*> LabelMap;
  
  llvm::SmallVector<SwitchStmt*, 8> SwitchStack;
  
  /// ExtVectorDecls - This is a list all the extended vector types. This allows
  /// us to associate a raw vector type with one of the ext_vector type names.
  /// This is only necessary for issuing pretty diagnostics.
  llvm::SmallVector<TypedefDecl*, 24> ExtVectorDecls;

  /// ObjCImplementations - Keep track of all class @implementations
  /// so we can emit errors on duplicates.
  llvm::DenseMap<IdentifierInfo*, ObjCImplementationDecl*> ObjCImplementations;
  
  /// ObjCCategoryImpls - Maintain a list of category implementations so 
  /// we can check for duplicates and find local method declarations.
  llvm::SmallVector<ObjCCategoryImplDecl*, 8> ObjCCategoryImpls;
  
  /// ObjCProtocols - Keep track of all protocol declarations declared
  /// with @protocol keyword, so that we can emit errors on duplicates and
  /// find the declarations when needed.
  llvm::DenseMap<IdentifierInfo*, ObjCProtocolDecl*> ObjCProtocols;

  /// ObjCInterfaceDecls - Keep track of all class declarations declared
  /// with @interface, so that we can emit errors on duplicates and
  /// find the declarations when needed. 
  typedef llvm::DenseMap<const IdentifierInfo*, 
                         ObjCInterfaceDecl*> ObjCInterfaceDeclsTy;
  ObjCInterfaceDeclsTy ObjCInterfaceDecls;
    
  /// ObjCAliasDecls - Keep track of all class declarations declared
  /// with @compatibility_alias, so that we can emit errors on duplicates and
  /// find the declarations when needed. This construct is ancient and will
  /// likely never be seen. Nevertheless, it is here for compatibility.
  typedef llvm::DenseMap<const IdentifierInfo*, 
                         ObjCCompatibleAliasDecl*> ObjCAliasTy;
  ObjCAliasTy ObjCAliasDecls;

  /// FieldCollector - Collects CXXFieldDecls during parsing of C++ classes.
  llvm::OwningPtr<CXXFieldCollector> FieldCollector;

  IdentifierResolver IdResolver;

  // Enum values used by KnownFunctionIDs (see below).
  enum {
    id_NSLog,
    id_asprintf,
    id_fprintf,
    id_printf,
    id_snprintf,
    id_snprintf_chk,
    id_sprintf,
    id_sprintf_chk,
    id_vasprintf,
    id_vfprintf,    
    id_vsnprintf,
    id_vsnprintf_chk,
    id_vsprintf,
    id_vsprintf_chk,
    id_vprintf,
    id_num_known_functions
  };
  
  /// KnownFunctionIDs - This is a list of IdentifierInfo objects to a set
  /// of known functions used by the semantic analysis to do various
  /// kinds of checking (e.g. checking format string errors in printf calls).
  /// This list is populated upon the creation of a Sema object.    
  IdentifierInfo* KnownFunctionIDs[id_num_known_functions];

  /// Translation Unit Scope - useful to Objective-C actions that need
  /// to lookup file scope declarations in the "ordinary" C decl namespace.
  /// For example, user-defined classes, built-in "id" type, etc.
  Scope *TUScope;

  /// The C++ "std" namespace, where the standard library resides. Cached here
  /// by GetStdNamespace
  NamespaceDecl *StdNamespace;

  /// A flag to remember whether the implicit forms of operator new and delete
  /// have been declared.
  bool GlobalNewDeleteDeclared;
  
  /// ObjCMethodList - a linked list of methods with different signatures.
  struct ObjCMethodList {
    ObjCMethodDecl *Method;
    ObjCMethodList *Next;
    
    ObjCMethodList() {
      Method = 0; 
      Next = 0;
    }
    ObjCMethodList(ObjCMethodDecl *M, ObjCMethodList *C) {
      Method = M;
      Next = C;
    }
  };
  /// Instance/Factory Method Pools - allows efficient lookup when typechecking
  /// messages to "id". We need to maintain a list, since selectors can have
  /// differing signatures across classes. In Cocoa, this happens to be 
  /// extremely uncommon (only 1% of selectors are "overloaded").
  llvm::DenseMap<Selector, ObjCMethodList> InstanceMethodPool;
  llvm::DenseMap<Selector, ObjCMethodList> FactoryMethodPool;
public:
  Sema(Preprocessor &pp, ASTContext &ctxt, ASTConsumer &consumer);
  
  const LangOptions &getLangOptions() const;
  Diagnostic &getDiagnostics() const { return Diags; }
  SourceManager &getSourceManager() const { return SourceMgr; }

  /// The primitive diagnostic helpers.
  DiagnosticBuilder Diag(SourceLocation Loc, unsigned DiagID) {
    return Diags.Report(FullSourceLoc(Loc, SourceMgr), DiagID);
  }

  virtual void DeleteExpr(ExprTy *E);
  virtual void DeleteStmt(StmtTy *S);

  virtual void ActOnEndOfTranslationUnit();
  
  //===--------------------------------------------------------------------===//
  // Type Analysis / Processing: SemaType.cpp.
  //
  QualType ConvertDeclSpecToType(const DeclSpec &DS);
  void ProcessTypeAttributeList(QualType &Result, const AttributeList *AL);
  QualType GetTypeForDeclarator(Declarator &D, Scope *S, unsigned Skip = 0);
  DeclarationName GetNameForDeclarator(Declarator &D);

  QualType ObjCGetTypeForMethodDefinition(DeclTy *D);

  bool UnwrapSimilarPointerTypes(QualType& T1, QualType& T2);

  virtual TypeResult ActOnTypeName(Scope *S, Declarator &D);

  //===--------------------------------------------------------------------===//
  // Symbol table / Decl tracking callbacks: SemaDecl.cpp.
  //
  virtual TypeTy *isTypeName(IdentifierInfo &II, Scope *S,
                             const CXXScopeSpec *SS);
  virtual DeclTy *ActOnDeclarator(Scope *S, Declarator &D, DeclTy *LastInGroup);
  virtual DeclTy *ActOnParamDeclarator(Scope *S, Declarator &D);
  virtual void ActOnParamDefaultArgument(DeclTy *param, 
                                         SourceLocation EqualLoc,
                                         ExprTy *defarg);
  void AddInitializerToDecl(DeclTy *dcl, ExprTy *init);
  void ActOnUninitializedDecl(DeclTy *dcl);
  virtual DeclTy *FinalizeDeclaratorGroup(Scope *S, DeclTy *Group);

  virtual DeclTy *ActOnStartOfFunctionDef(Scope *S, Declarator &D);
  virtual DeclTy *ActOnStartOfFunctionDef(Scope *S, DeclTy *D);
  virtual void ObjCActOnStartOfMethodDef(Scope *S, DeclTy *D);
  
  virtual DeclTy *ActOnFinishFunctionBody(DeclTy *Decl, StmtTy *Body);
  virtual DeclTy *ActOnLinkageSpec(SourceLocation Loc, SourceLocation LBrace,
                                   SourceLocation RBrace, const char *Lang,
                                   unsigned StrSize, DeclTy *D);
  virtual DeclTy *ActOnFileScopeAsmDecl(SourceLocation Loc, ExprTy *expr);

  /// Scope actions.
  virtual void ActOnPopScope(SourceLocation Loc, Scope *S);
  virtual void ActOnTranslationUnitScope(SourceLocation Loc, Scope *S);

  /// ParsedFreeStandingDeclSpec - This method is invoked when a declspec with
  /// no declarator (e.g. "struct foo;") is parsed.
  virtual DeclTy *ParsedFreeStandingDeclSpec(Scope *S, DeclSpec &DS);  
  
  virtual DeclTy *ActOnTag(Scope *S, unsigned TagType, TagKind TK,
                           SourceLocation KWLoc, const CXXScopeSpec &SS,
                           IdentifierInfo *Name, SourceLocation NameLoc,
                           AttributeList *Attr);
  
  DeclTy* ActOnTagStruct(Scope *S, TagDecl::TagKind Kind, TagKind TK,
                         SourceLocation KWLoc, const CXXScopeSpec &SS,
                         IdentifierInfo *Name, SourceLocation NameLoc,
                         AttributeList *Attr);  
  
  virtual void ActOnDefs(Scope *S, SourceLocation DeclStart,
                         IdentifierInfo *ClassName,
                         llvm::SmallVectorImpl<DeclTy*> &Decls);
  virtual DeclTy *ActOnField(Scope *S, SourceLocation DeclStart,
                             Declarator &D, ExprTy *BitfieldWidth);
  
  virtual DeclTy *ActOnIvar(Scope *S, SourceLocation DeclStart,
                            Declarator &D, ExprTy *BitfieldWidth,
                            tok::ObjCKeywordKind visibility);

  // This is used for both record definitions and ObjC interface declarations.
  virtual void ActOnFields(Scope* S,
                           SourceLocation RecLoc, DeclTy *TagDecl,
                           DeclTy **Fields, unsigned NumFields,
                           SourceLocation LBrac, SourceLocation RBrac,
                           AttributeList *AttrList);
  virtual DeclTy *ActOnEnumConstant(Scope *S, DeclTy *EnumDecl,
                                    DeclTy *LastEnumConstant,
                                    SourceLocation IdLoc, IdentifierInfo *Id,
                                    SourceLocation EqualLoc, ExprTy *Val);
  virtual void ActOnEnumBody(SourceLocation EnumLoc, DeclTy *EnumDecl,
                             DeclTy **Elements, unsigned NumElements);

  DeclContext *getContainingDC(DeclContext *DC);

  /// Set the current declaration context until it gets popped.
  void PushDeclContext(DeclContext *DC);
  void PopDeclContext();
  
  /// CurFunctionDecl - If inside of a function body, this returns a pointer to
  /// the function decl for the function being parsed.
  FunctionDecl *getCurFunctionDecl() {
    return dyn_cast<FunctionDecl>(CurContext);
  }

  /// CurMethodDecl - If inside of a method body, this returns a pointer to
  /// the method decl for the method being parsed.
  ObjCMethodDecl *getCurMethodDecl();

  /// Add this decl to the scope shadowed decl chains.
  void PushOnScopeChains(NamedDecl *D, Scope *S);

  /// isDeclInScope - If 'Ctx' is a function/method, isDeclInScope returns true
  /// if 'D' is in Scope 'S', otherwise 'S' is ignored and isDeclInScope returns
  /// true if 'D' belongs to the given declaration context.
  bool isDeclInScope(Decl *D, DeclContext *Ctx, Scope *S = 0) {
    return IdResolver.isDeclInScope(D, Ctx, S);
  }

  /// Subroutines of ActOnDeclarator().
  TypedefDecl *ParseTypedefDecl(Scope *S, Declarator &D, QualType T,
                                ScopedDecl *LastDecl);
  TypedefDecl *MergeTypeDefDecl(TypedefDecl *New, Decl *Old);
  FunctionDecl *MergeFunctionDecl(FunctionDecl *New, Decl *Old, 
                                  bool &Redeclaration);
  VarDecl *MergeVarDecl(VarDecl *New, Decl *Old);
  FunctionDecl *MergeCXXFunctionDecl(FunctionDecl *New, FunctionDecl *Old);
  void CheckForFileScopedRedefinitions(Scope *S, VarDecl *VD);

  /// C++ Overloading.
  bool IsOverload(FunctionDecl *New, Decl* OldD, 
                  OverloadedFunctionDecl::function_iterator &MatchedDecl);
  ImplicitConversionSequence 
  TryImplicitConversion(Expr* From, QualType ToType,
                        bool SuppressUserConversions = false);
  bool IsStandardConversion(Expr *From, QualType ToType, 
                            StandardConversionSequence& SCS);
  bool IsIntegralPromotion(Expr *From, QualType FromType, QualType ToType);
  bool IsFloatingPointPromotion(QualType FromType, QualType ToType);
  bool IsPointerConversion(Expr *From, QualType FromType, QualType ToType,
                           QualType& ConvertedType);
  bool CheckPointerConversion(Expr *From, QualType ToType);
  bool IsQualificationConversion(QualType FromType, QualType ToType);
  bool IsUserDefinedConversion(Expr *From, QualType ToType, 
                               UserDefinedConversionSequence& User);

  ImplicitConversionSequence::CompareKind 
  CompareImplicitConversionSequences(const ImplicitConversionSequence& ICS1,
                                     const ImplicitConversionSequence& ICS2);

  ImplicitConversionSequence::CompareKind 
  CompareStandardConversionSequences(const StandardConversionSequence& SCS1,
                                     const StandardConversionSequence& SCS2);

  ImplicitConversionSequence::CompareKind 
  CompareQualificationConversions(const StandardConversionSequence& SCS1,
                                  const StandardConversionSequence& SCS2);

  ImplicitConversionSequence::CompareKind
  CompareDerivedToBaseConversions(const StandardConversionSequence& SCS1,
                                  const StandardConversionSequence& SCS2);

  ImplicitConversionSequence 
  TryCopyInitialization(Expr* From, QualType ToType,
                        bool SuppressUserConversions = false);
  bool PerformCopyInitialization(Expr *&From, QualType ToType, 
                                 const char *Flavor);

  ImplicitConversionSequence
  TryObjectArgumentInitialization(Expr *From, CXXMethodDecl *Method);
  bool PerformObjectArgumentInitialization(Expr *&From, CXXMethodDecl *Method);

  /// OverloadingResult - Capture the result of performing overload
  /// resolution.
  enum OverloadingResult {
    OR_Success,             ///< Overload resolution succeeded.
    OR_No_Viable_Function,  ///< No viable function found.
    OR_Ambiguous            ///< Ambiguous candidates found.
  };

  void AddOverloadCandidate(FunctionDecl *Function, 
                            Expr **Args, unsigned NumArgs,
                            OverloadCandidateSet& CandidateSet,
                            bool SuppressUserConversions = false);
  void AddMethodCandidate(CXXMethodDecl *Method,
                          Expr *Object, Expr **Args, unsigned NumArgs,
                          OverloadCandidateSet& CandidateSet,
                          bool SuppressUserConversions = true);
  void AddConversionCandidate(CXXConversionDecl *Conversion,
                              Expr *From, QualType ToType,
                              OverloadCandidateSet& CandidateSet);
  void AddSurrogateCandidate(CXXConversionDecl *Conversion,
                             const FunctionTypeProto *Proto,
                             Expr *Object, Expr **Args, unsigned NumArgs,
                             OverloadCandidateSet& CandidateSet);
  void AddOperatorCandidates(OverloadedOperatorKind Op, Scope *S,
                             Expr **Args, unsigned NumArgs,
                             OverloadCandidateSet& CandidateSet);
  void AddBuiltinCandidate(QualType ResultTy, QualType *ParamTys, 
                           Expr **Args, unsigned NumArgs,
                           OverloadCandidateSet& CandidateSet);
  void AddBuiltinOperatorCandidates(OverloadedOperatorKind Op, 
                                    Expr **Args, unsigned NumArgs, 
                                    OverloadCandidateSet& CandidateSet);
  void AddOverloadCandidates(const OverloadedFunctionDecl *Ovl, 
                             Expr **Args, unsigned NumArgs,
                             OverloadCandidateSet& CandidateSet,
                             bool SuppressUserConversions = false);
  bool isBetterOverloadCandidate(const OverloadCandidate& Cand1,
                                 const OverloadCandidate& Cand2);
  OverloadingResult BestViableFunction(OverloadCandidateSet& CandidateSet,
                                       OverloadCandidateSet::iterator& Best);
  void PrintOverloadCandidates(OverloadCandidateSet& CandidateSet,
                               bool OnlyViable);
                               
  FunctionDecl *ResolveAddressOfOverloadedFunction(Expr *From, QualType ToType,
                                                   bool Complain);
  void FixOverloadedFunctionReference(Expr *E, FunctionDecl *Fn);

  FunctionDecl *ResolveOverloadedCallFn(Expr *Fn, OverloadedFunctionDecl *Ovl,
                                        SourceLocation LParenLoc,
                                        Expr **Args, unsigned NumArgs,
                                        SourceLocation *CommaLocs, 
                                        SourceLocation RParenLoc);

  ExprResult 
  BuildCallToObjectOfClassType(Expr *Object, SourceLocation LParenLoc,
                               Expr **Args, unsigned NumArgs,
                               SourceLocation *CommaLocs, 
                               SourceLocation RParenLoc);

  ExprResult BuildOverloadedArrowExpr(Expr *Base, SourceLocation OpLoc,
                                      SourceLocation MemberLoc,
                                      IdentifierInfo &Member);
                                           
  /// Helpers for dealing with function parameters
  bool CheckParmsForFunctionDef(FunctionDecl *FD);
  void CheckCXXDefaultArguments(FunctionDecl *FD);
  void CheckExtraCXXDefaultArguments(Declarator &D);

  /// More parsing and symbol table subroutines...
  Decl *LookupDecl(DeclarationName Name, unsigned NSI, Scope *S,
                   const DeclContext *LookupCtx = 0,
                   bool enableLazyBuiltinCreation = true);
  ObjCInterfaceDecl *getObjCInterfaceDecl(IdentifierInfo *Id);
  ScopedDecl *LazilyCreateBuiltin(IdentifierInfo *II, unsigned ID, 
                                  Scope *S);
  ScopedDecl *ImplicitlyDefineFunction(SourceLocation Loc, IdentifierInfo &II,
                                 Scope *S);
  // Decl attributes - this routine is the top level dispatcher. 
  void ProcessDeclAttributes(Decl *D, const Declarator &PD);
  void ProcessDeclAttributeList(Decl *D, const AttributeList *AttrList);

  void WarnUndefinedMethod(SourceLocation ImpLoc, ObjCMethodDecl *method,
                           bool &IncompleteImpl);

  NamespaceDecl *GetStdNamespace();
                           
  /// CheckProtocolMethodDefs - This routine checks unimpletented
  /// methods declared in protocol, and those referenced by it.
  /// \param IDecl - Used for checking for methods which may have been
  /// inherited.
  void CheckProtocolMethodDefs(SourceLocation ImpLoc,
                               ObjCProtocolDecl *PDecl,
                               bool& IncompleteImpl,
                               const llvm::DenseSet<Selector> &InsMap,
                               const llvm::DenseSet<Selector> &ClsMap,
                               ObjCInterfaceDecl *IDecl);
  
  /// CheckImplementationIvars - This routine checks if the instance variables
  /// listed in the implelementation match those listed in the interface. 
  void CheckImplementationIvars(ObjCImplementationDecl *ImpDecl,
                                ObjCIvarDecl **Fields, unsigned nIvars,
                                SourceLocation Loc);
  
  /// ImplMethodsVsClassMethods - This is main routine to warn if any method
  /// remains unimplemented in the @implementation class.
  void ImplMethodsVsClassMethods(ObjCImplementationDecl* IMPDecl, 
                                 ObjCInterfaceDecl* IDecl);
  
  /// ImplCategoryMethodsVsIntfMethods - Checks that methods declared in the
  /// category interface is implemented in the category @implementation.
  void ImplCategoryMethodsVsIntfMethods(ObjCCategoryImplDecl *CatImplDecl,
                                        ObjCCategoryDecl *CatClassDecl);
  /// MatchTwoMethodDeclarations - Checks if two methods' type match and returns
  /// true, or false, accordingly.
  bool MatchTwoMethodDeclarations(const ObjCMethodDecl *Method, 
                                  const ObjCMethodDecl *PrevMethod,
                                  bool matchBasedOnSizeAndAlignment = false); 

  /// AddInstanceMethodToGlobalPool - All instance methods in a translation
  /// unit are added to a global pool. This allows us to efficiently associate
  /// a selector with a method declaraation for purposes of typechecking
  /// messages sent to "id" (where the class of the object is unknown).
  void AddInstanceMethodToGlobalPool(ObjCMethodDecl *Method);
  
  /// LookupInstanceMethodInGlobalPool - Returns the method and warns if
  /// there are multiple signatures.
  ObjCMethodDecl *LookupInstanceMethodInGlobalPool(Selector Sel, SourceRange R);
  
  /// AddFactoryMethodToGlobalPool - Same as above, but for factory methods.
  void AddFactoryMethodToGlobalPool(ObjCMethodDecl *Method);
  //===--------------------------------------------------------------------===//
  // Statement Parsing Callbacks: SemaStmt.cpp.
public:
  virtual StmtResult ActOnExprStmt(ExprTy *Expr);
  
  virtual StmtResult ActOnNullStmt(SourceLocation SemiLoc);
  virtual StmtResult ActOnCompoundStmt(SourceLocation L, SourceLocation R,
                                       StmtTy **Elts, unsigned NumElts,
                                       bool isStmtExpr);
  virtual StmtResult ActOnDeclStmt(DeclTy *Decl, SourceLocation StartLoc,
                                   SourceLocation EndLoc);
  virtual StmtResult ActOnCaseStmt(SourceLocation CaseLoc, ExprTy *LHSVal,
                                   SourceLocation DotDotDotLoc, ExprTy *RHSVal,
                                   SourceLocation ColonLoc, StmtTy *SubStmt);
  virtual StmtResult ActOnDefaultStmt(SourceLocation DefaultLoc,
                                      SourceLocation ColonLoc, StmtTy *SubStmt,
                                      Scope *CurScope);
  virtual StmtResult ActOnLabelStmt(SourceLocation IdentLoc, IdentifierInfo *II,
                                    SourceLocation ColonLoc, StmtTy *SubStmt);
  virtual StmtResult ActOnIfStmt(SourceLocation IfLoc, ExprTy *CondVal,
                                 StmtTy *ThenVal, SourceLocation ElseLoc,
                                 StmtTy *ElseVal);
  virtual StmtResult ActOnStartOfSwitchStmt(ExprTy *Cond);
  virtual StmtResult ActOnFinishSwitchStmt(SourceLocation SwitchLoc,
                                           StmtTy *Switch, ExprTy *Body);
  virtual StmtResult ActOnWhileStmt(SourceLocation WhileLoc, ExprTy *Cond,
                                    StmtTy *Body);
  virtual StmtResult ActOnDoStmt(SourceLocation DoLoc, StmtTy *Body,
                                 SourceLocation WhileLoc, ExprTy *Cond);
  
  virtual StmtResult ActOnForStmt(SourceLocation ForLoc, 
                                  SourceLocation LParenLoc, 
                                  StmtTy *First, ExprTy *Second, ExprTy *Third,
                                  SourceLocation RParenLoc, StmtTy *Body);
  virtual StmtResult ActOnObjCForCollectionStmt(SourceLocation ForColLoc, 
                                  SourceLocation LParenLoc, 
                                  StmtTy *First, ExprTy *Second,
                                  SourceLocation RParenLoc, StmtTy *Body);
  
  virtual StmtResult ActOnGotoStmt(SourceLocation GotoLoc,
                                   SourceLocation LabelLoc,
                                   IdentifierInfo *LabelII);
  virtual StmtResult ActOnIndirectGotoStmt(SourceLocation GotoLoc,
                                           SourceLocation StarLoc,
                                           ExprTy *DestExp);
  virtual StmtResult ActOnContinueStmt(SourceLocation ContinueLoc,
                                       Scope *CurScope);
  virtual StmtResult ActOnBreakStmt(SourceLocation GotoLoc, Scope *CurScope);
  
  virtual StmtResult ActOnReturnStmt(SourceLocation ReturnLoc,
                                     ExprTy *RetValExp);
  StmtResult ActOnBlockReturnStmt(SourceLocation ReturnLoc, Expr *RetValExp);
  
  virtual StmtResult ActOnAsmStmt(SourceLocation AsmLoc,
                                  bool IsSimple,
                                  bool IsVolatile,
                                  unsigned NumOutputs,
                                  unsigned NumInputs,
                                  std::string *Names,
                                  ExprTy **Constraints,
                                  ExprTy **Exprs,
                                  ExprTy *AsmString,
                                  unsigned NumClobbers,
                                  ExprTy **Clobbers,
                                  SourceLocation RParenLoc);
  
  virtual StmtResult ActOnObjCAtCatchStmt(SourceLocation AtLoc, 
                                          SourceLocation RParen, StmtTy *Parm, 
                                          StmtTy *Body, StmtTy *CatchList);
  
  virtual StmtResult ActOnObjCAtFinallyStmt(SourceLocation AtLoc, 
                                            StmtTy *Body);
  
  virtual StmtResult ActOnObjCAtTryStmt(SourceLocation AtLoc, 
                                        StmtTy *Try, 
                                        StmtTy *Catch, StmtTy *Finally);
  
  virtual StmtResult ActOnObjCAtThrowStmt(SourceLocation AtLoc, 
                                          StmtTy *Throw);
  virtual StmtResult ActOnObjCAtSynchronizedStmt(SourceLocation AtLoc, 
                                                 ExprTy *SynchExpr, 
                                                 StmtTy *SynchBody);
  
  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks: SemaExpr.cpp.

  // Primary Expressions.
  virtual ExprResult ActOnIdentifierExpr(Scope *S, SourceLocation Loc,
                                         IdentifierInfo &II,
                                         bool HasTrailingLParen,
                                         const CXXScopeSpec *SS = 0);
  virtual ExprResult ActOnCXXOperatorFunctionIdExpr(Scope *S, 
                                                    SourceLocation OperatorLoc,
                                                    OverloadedOperatorKind Op,
                                                    bool HasTrailingLParen,
                                                    const CXXScopeSpec &SS);
  virtual ExprResult ActOnCXXConversionFunctionExpr(Scope *S, 
                                                    SourceLocation OperatorLoc,
                                                    TypeTy *Ty,
                                                    bool HasTrailingLParen,
                                                    const CXXScopeSpec &SS);
  ExprResult ActOnDeclarationNameExpr(Scope *S, SourceLocation Loc,
                                      DeclarationName Name,
                                      bool HasTrailingLParen,
                                      const CXXScopeSpec *SS);
                                      

  virtual ExprResult ActOnPredefinedExpr(SourceLocation Loc,
                                         tok::TokenKind Kind);
  virtual ExprResult ActOnNumericConstant(const Token &);
  virtual ExprResult ActOnCharacterConstant(const Token &);
  virtual ExprResult ActOnParenExpr(SourceLocation L, SourceLocation R,
                                    ExprTy *Val);

  /// ActOnStringLiteral - The specified tokens were lexed as pasted string
  /// fragments (e.g. "foo" "bar" L"baz").
  virtual ExprResult ActOnStringLiteral(const Token *Toks, unsigned NumToks);
    
  // Binary/Unary Operators.  'Tok' is the token for the operator.
  virtual ExprResult ActOnUnaryOp(Scope *S, SourceLocation OpLoc, 
                                  tok::TokenKind Op, ExprTy *Input);
  virtual ExprResult 
    ActOnSizeOfAlignOfExpr(SourceLocation OpLoc, bool isSizeof, bool isType,
                           void *TyOrEx, const SourceRange &ArgRange);

  bool CheckSizeOfAlignOfOperand(QualType type, SourceLocation OpLoc,
                                 const SourceRange &R, bool isSizeof);
  
  virtual ExprResult ActOnPostfixUnaryOp(Scope *S, SourceLocation OpLoc, 
                                         tok::TokenKind Kind, ExprTy *Input);
  
  virtual ExprResult ActOnArraySubscriptExpr(Scope *S, ExprTy *Base, 
                                             SourceLocation LLoc, ExprTy *Idx,
                                             SourceLocation RLoc);
  virtual ExprResult ActOnMemberReferenceExpr(ExprTy *Base,SourceLocation OpLoc,
                                              tok::TokenKind OpKind,
                                              SourceLocation MemberLoc,
                                              IdentifierInfo &Member);
  
  /// ActOnCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.
  virtual ExprResult ActOnCallExpr(ExprTy *Fn, SourceLocation LParenLoc,
                                   ExprTy **Args, unsigned NumArgs,
                                   SourceLocation *CommaLocs,
                                   SourceLocation RParenLoc);
  
  virtual ExprResult ActOnCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
                                   SourceLocation RParenLoc, ExprTy *Op);
                                   
  virtual ExprResult ActOnCompoundLiteral(SourceLocation LParenLoc, TypeTy *Ty,
                                          SourceLocation RParenLoc, ExprTy *Op);
  
  virtual ExprResult ActOnInitList(SourceLocation LParenLoc, 
                                   ExprTy **InitList, unsigned NumInit,
                                   InitListDesignations &Designators,
                                   SourceLocation RParenLoc);
                                   
  virtual ExprResult ActOnBinOp(Scope *S, SourceLocation TokLoc, 
                                tok::TokenKind Kind,
                                ExprTy *LHS,ExprTy *RHS);
  ExprResult CreateBuiltinBinOp(SourceLocation TokLoc, 
                                unsigned Opc,
                                Expr *lhs, Expr *rhs);

  /// ActOnConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  virtual ExprResult ActOnConditionalOp(SourceLocation QuestionLoc, 
                                        SourceLocation ColonLoc,
                                        ExprTy *Cond, ExprTy *LHS, ExprTy *RHS);

  /// ActOnAddrLabel - Parse the GNU address of label extension: "&&foo".
  virtual ExprResult ActOnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                                    IdentifierInfo *LabelII);
  
  virtual ExprResult ActOnStmtExpr(SourceLocation LPLoc, StmtTy *SubStmt,
                                   SourceLocation RPLoc); // "({..})"

  /// __builtin_offsetof(type, a.b[123][456].c)
  virtual ExprResult ActOnBuiltinOffsetOf(SourceLocation BuiltinLoc,
                                          SourceLocation TypeLoc, TypeTy *Arg1,
                                          OffsetOfComponent *CompPtr,
                                          unsigned NumComponents,
                                          SourceLocation RParenLoc);
    
  // __builtin_types_compatible_p(type1, type2)
  virtual ExprResult ActOnTypesCompatibleExpr(SourceLocation BuiltinLoc, 
                                              TypeTy *arg1, TypeTy *arg2,
                                              SourceLocation RPLoc);
                                              
  // __builtin_choose_expr(constExpr, expr1, expr2)
  virtual ExprResult ActOnChooseExpr(SourceLocation BuiltinLoc, 
                                     ExprTy *cond, ExprTy *expr1, ExprTy *expr2,
                                     SourceLocation RPLoc);
  
  // __builtin_overload(...)
  virtual ExprResult ActOnOverloadExpr(ExprTy **Args, unsigned NumArgs,
                                       SourceLocation *CommaLocs,
                                       SourceLocation BuiltinLoc, 
                                       SourceLocation RParenLoc);

  // __builtin_va_arg(expr, type)
  virtual ExprResult ActOnVAArg(SourceLocation BuiltinLoc,
                                ExprTy *expr, TypeTy *type,
                                SourceLocation RPLoc);

  // __null
  virtual ExprResult ActOnGNUNullExpr(SourceLocation TokenLoc);

  //===------------------------- "Block" Extension ------------------------===//

  /// ActOnBlockStart - This callback is invoked when a block literal is
  /// started.
  virtual void ActOnBlockStart(SourceLocation CaretLoc, Scope *CurScope);
  
  /// ActOnBlockArguments - This callback allows processing of block arguments.
  /// If there are no arguments, this is still invoked.
  virtual void ActOnBlockArguments(Declarator &ParamInfo);
  
  /// ActOnBlockError - If there is an error parsing a block, this callback
  /// is invoked to pop the information about the block from the action impl.
  virtual void ActOnBlockError(SourceLocation CaretLoc, Scope *CurScope);
  
  /// ActOnBlockStmtExpr - This is called when the body of a block statement
  /// literal was successfully completed.  ^(int x){...}
  virtual ExprResult ActOnBlockStmtExpr(SourceLocation CaretLoc, StmtTy *Body,
                                        Scope *CurScope);

  // Act on C++ namespaces
  virtual DeclTy *ActOnStartNamespaceDef(Scope *S, SourceLocation IdentLoc,
                                        IdentifierInfo *Ident,
                                        SourceLocation LBrace);
  virtual void ActOnFinishNamespaceDef(DeclTy *Dcl, SourceLocation RBrace);

  /// AddCXXDirectInitializerToDecl - This action is called immediately after 
  /// ActOnDeclarator, when a C++ direct initializer is present.
  /// e.g: "int x(1);"
  virtual void AddCXXDirectInitializerToDecl(DeclTy *Dcl,
                                             SourceLocation LParenLoc,
                                             ExprTy **Exprs, unsigned NumExprs,
                                             SourceLocation *CommaLocs,
                                             SourceLocation RParenLoc);

  /// InitializationKind - Represents which kind of C++ initialization
  /// [dcl.init] a routine is to perform.
  enum InitializationKind {
    IK_Direct, ///< Direct initialization
    IK_Copy,   ///< Copy initialization
    IK_Default ///< Default initialization
  };

  CXXConstructorDecl *
  PerformInitializationByConstructor(QualType ClassType,
                                     Expr **Args, unsigned NumArgs,
                                     SourceLocation Loc, SourceRange Range,
                                     DeclarationName InitEntity,
                                     InitializationKind Kind);

  /// ActOnCXXNamedCast - Parse {dynamic,static,reinterpret,const}_cast's.
  virtual ExprResult ActOnCXXNamedCast(SourceLocation OpLoc, tok::TokenKind Kind,
                                       SourceLocation LAngleBracketLoc, TypeTy *Ty,
                                       SourceLocation RAngleBracketLoc,
                                       SourceLocation LParenLoc, ExprTy *E,
                                       SourceLocation RParenLoc);

  /// ActOnCXXTypeidOfType - Parse typeid( type-id ).
  virtual ExprResult ActOnCXXTypeid(SourceLocation OpLoc,
                                    SourceLocation LParenLoc, bool isType,
                                    void *TyOrExpr, SourceLocation RParenLoc);

  //// ActOnCXXThis -  Parse 'this' pointer.
  virtual ExprResult ActOnCXXThis(SourceLocation ThisLoc);

  /// ActOnCXXBoolLiteral - Parse {true,false} literals.
  virtual ExprResult ActOnCXXBoolLiteral(SourceLocation OpLoc,
                                         tok::TokenKind Kind);
  
  //// ActOnCXXThrow -  Parse throw expressions.
  virtual ExprResult ActOnCXXThrow(SourceLocation OpLoc,
                                   ExprTy *expr);

  /// ActOnCXXTypeConstructExpr - Parse construction of a specified type.
  /// Can be interpreted either as function-style casting ("int(x)")
  /// or class type construction ("ClassType(x,y,z)")
  /// or creation of a value-initialized type ("int()").
  virtual ExprResult ActOnCXXTypeConstructExpr(SourceRange TypeRange,
                                               TypeTy *TypeRep,
                                               SourceLocation LParenLoc,
                                               ExprTy **Exprs,
                                               unsigned NumExprs,
                                               SourceLocation *CommaLocs,
                                               SourceLocation RParenLoc);

  /// ActOnCXXNew - Parsed a C++ 'new' expression.
  virtual ExprResult ActOnCXXNew(SourceLocation StartLoc, bool UseGlobal,
                                 SourceLocation PlacementLParen,
                                 ExprTy **PlacementArgs, unsigned NumPlaceArgs,
                                 SourceLocation PlacementRParen,
                                 bool ParenTypeId, Declarator &D,
                                 SourceLocation ConstructorLParen,
                                 ExprTy **ConstructorArgs, unsigned NumConsArgs,
                                 SourceLocation ConstructorRParen);
  bool CheckAllocatedType(QualType AllocType, const Declarator &D);
  bool FindAllocationFunctions(SourceLocation StartLoc, bool UseGlobal,
                               QualType AllocType, bool IsArray,
                               Expr **PlaceArgs, unsigned NumPlaceArgs,
                               FunctionDecl *&OperatorNew,
                               FunctionDecl *&OperatorDelete);
  bool FindAllocationOverload(SourceLocation StartLoc, DeclarationName Name,
                              Expr** Args, unsigned NumArgs, DeclContext *Ctx,
                              bool AllowMissing, FunctionDecl *&Operator);
  void DeclareGlobalNewDelete();
  void DeclareGlobalAllocationFunction(DeclarationName Name, QualType Return,
                                       QualType Argument);

  /// ActOnCXXDelete - Parsed a C++ 'delete' expression
  virtual ExprResult ActOnCXXDelete(SourceLocation StartLoc, bool UseGlobal,
                                    bool ArrayForm, ExprTy *Operand);

  /// ActOnCXXConditionDeclarationExpr - Parsed a condition declaration of a
  /// C++ if/switch/while/for statement.
  /// e.g: "if (int x = f()) {...}"
  virtual ExprResult ActOnCXXConditionDeclarationExpr(Scope *S,
                                                      SourceLocation StartLoc,
                                                      Declarator &D,
                                                      SourceLocation EqualLoc,
                                                      ExprTy *AssignExprVal);

  /// ActOnCXXGlobalScopeSpecifier - Return the object that represents the
  /// global scope ('::').
  virtual CXXScopeTy *ActOnCXXGlobalScopeSpecifier(Scope *S,
                                                   SourceLocation CCLoc);

  /// ActOnCXXNestedNameSpecifier - Called during parsing of a
  /// nested-name-specifier. e.g. for "foo::bar::" we parsed "foo::" and now
  /// we want to resolve "bar::". 'SS' is empty or the previously parsed
  /// nested-name part ("foo::"), 'IdLoc' is the source location of 'bar',
  /// 'CCLoc' is the location of '::' and 'II' is the identifier for 'bar'.
  /// Returns a CXXScopeTy* object representing the C++ scope.
  virtual CXXScopeTy *ActOnCXXNestedNameSpecifier(Scope *S,
                                                  const CXXScopeSpec &SS,
                                                  SourceLocation IdLoc,
                                                  SourceLocation CCLoc,
                                                  IdentifierInfo &II);

  /// ActOnCXXEnterDeclaratorScope - Called when a C++ scope specifier (global
  /// scope or nested-name-specifier) is parsed, part of a declarator-id.
  /// After this method is called, according to [C++ 3.4.3p3], names should be
  /// looked up in the declarator-id's scope, until the declarator is parsed and
  /// ActOnCXXExitDeclaratorScope is called.
  /// The 'SS' should be a non-empty valid CXXScopeSpec.
  virtual void ActOnCXXEnterDeclaratorScope(Scope *S, const CXXScopeSpec &SS);

  /// ActOnCXXExitDeclaratorScope - Called when a declarator that previously
  /// invoked ActOnCXXEnterDeclaratorScope(), is finished. 'SS' is the same
  /// CXXScopeSpec that was passed to ActOnCXXEnterDeclaratorScope as well.
  /// Used to indicate that names should revert to being looked up in the
  /// defining scope.
  virtual void ActOnCXXExitDeclaratorScope(const CXXScopeSpec &SS);

  // ParseObjCStringLiteral - Parse Objective-C string literals.
  virtual ExprResult ParseObjCStringLiteral(SourceLocation *AtLocs, 
                                            ExprTy **Strings,
                                            unsigned NumStrings);
  virtual ExprResult ParseObjCEncodeExpression(SourceLocation AtLoc,
                                               SourceLocation EncodeLoc,
                                               SourceLocation LParenLoc,
                                               TypeTy *Ty,
                                               SourceLocation RParenLoc);
  
  // ParseObjCSelectorExpression - Build selector expression for @selector
  virtual ExprResult ParseObjCSelectorExpression(Selector Sel,
                                                 SourceLocation AtLoc,
                                                 SourceLocation SelLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation RParenLoc);
  
  // ParseObjCProtocolExpression - Build protocol expression for @protocol
  virtual ExprResult ParseObjCProtocolExpression(IdentifierInfo * ProtocolName,
                                                 SourceLocation AtLoc,
                                                 SourceLocation ProtoLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation RParenLoc);

  //===--------------------------------------------------------------------===//
  // C++ Classes
  //
  virtual bool isCurrentClassName(const IdentifierInfo &II, Scope *S,
                                  const CXXScopeSpec *SS);
  
  virtual void ActOnStartCXXClassDef(Scope *S, DeclTy *TagDecl,
                                     SourceLocation LBrace);

  virtual DeclTy *ActOnCXXMemberDeclarator(Scope *S, AccessSpecifier AS,
                                           Declarator &D, ExprTy *BitfieldWidth,
                                           ExprTy *Init, DeclTy *LastInGroup);

  virtual MemInitResult ActOnMemInitializer(DeclTy *ConstructorD,
                                            Scope *S,
                                            IdentifierInfo *MemberOrBase,
                                            SourceLocation IdLoc,
                                            SourceLocation LParenLoc,
                                            ExprTy **Args, unsigned NumArgs,
                                            SourceLocation *CommaLocs,
                                            SourceLocation RParenLoc);

  void AddImplicitlyDeclaredMembersToClass(CXXRecordDecl *ClassDecl);

  virtual void ActOnFinishCXXMemberSpecification(Scope* S, SourceLocation RLoc,
                                                 DeclTy *TagDecl,
                                                 SourceLocation LBrac,
                                                 SourceLocation RBrac);

  virtual void ActOnFinishCXXClassDef(DeclTy *TagDecl);
  
  
  bool CheckConstructorDeclarator(Declarator &D, QualType &R,
                                  FunctionDecl::StorageClass& SC);
  bool CheckDestructorDeclarator(Declarator &D, QualType &R,
                                 FunctionDecl::StorageClass& SC);
  bool CheckConversionDeclarator(Declarator &D, QualType &R,
                                 FunctionDecl::StorageClass& SC);
  DeclTy *ActOnConstructorDeclarator(CXXConstructorDecl *Constructor);
  DeclTy *ActOnDestructorDeclarator(CXXDestructorDecl *Destructor);
  DeclTy *ActOnConversionDeclarator(CXXConversionDecl *Conversion);

  //===--------------------------------------------------------------------===//
  // C++ Derived Classes
  //

  /// ActOnBaseSpecifier - Parsed a base specifier
  virtual BaseResult ActOnBaseSpecifier(DeclTy *classdecl, 
                                        SourceRange SpecifierRange,
                                        bool Virtual, AccessSpecifier Access,
                                        TypeTy *basetype, SourceLocation BaseLoc);

  virtual void ActOnBaseSpecifiers(DeclTy *ClassDecl, BaseTy **Bases, 
                                   unsigned NumBases);

  bool IsDerivedFrom(QualType Derived, QualType Base);
  bool IsDerivedFrom(QualType Derived, QualType Base, BasePaths &Paths);

  bool CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                    SourceLocation Loc, SourceRange Range);

  //===--------------------------------------------------------------------===//
  // C++ Overloaded Operators [C++ 13.5]
  //

  bool CheckOverloadedOperatorDeclaration(FunctionDecl *FnDecl);

  // Objective-C declarations.
  virtual DeclTy *ActOnStartClassInterface(SourceLocation AtInterfaceLoc,
                                           IdentifierInfo *ClassName,
                                           SourceLocation ClassLoc,
                                           IdentifierInfo *SuperName,
                                           SourceLocation SuperLoc,
                                           DeclTy * const *ProtoRefs,
                                           unsigned NumProtoRefs,
                                           SourceLocation EndProtoLoc,
                                           AttributeList *AttrList);
  
  virtual DeclTy *ActOnCompatiblityAlias(
                    SourceLocation AtCompatibilityAliasLoc,
                    IdentifierInfo *AliasName,  SourceLocation AliasLocation,
                    IdentifierInfo *ClassName, SourceLocation ClassLocation);
                    
  virtual DeclTy *ActOnStartProtocolInterface(
                    SourceLocation AtProtoInterfaceLoc,
                    IdentifierInfo *ProtocolName, SourceLocation ProtocolLoc,
                    DeclTy * const *ProtoRefNames, unsigned NumProtoRefs,
                    SourceLocation EndProtoLoc,
                    AttributeList *AttrList);
  
  virtual DeclTy *ActOnStartCategoryInterface(SourceLocation AtInterfaceLoc,
                                              IdentifierInfo *ClassName,
                                              SourceLocation ClassLoc,
                                              IdentifierInfo *CategoryName,
                                              SourceLocation CategoryLoc,
                                              DeclTy * const *ProtoRefs,
                                              unsigned NumProtoRefs,
                                              SourceLocation EndProtoLoc);
  
  virtual DeclTy *ActOnStartClassImplementation(
                    SourceLocation AtClassImplLoc,
                    IdentifierInfo *ClassName, SourceLocation ClassLoc,
                    IdentifierInfo *SuperClassname, 
                    SourceLocation SuperClassLoc);
  
  virtual DeclTy *ActOnStartCategoryImplementation(
                                                  SourceLocation AtCatImplLoc,
                                                  IdentifierInfo *ClassName, 
                                                  SourceLocation ClassLoc,
                                                  IdentifierInfo *CatName,
                                                  SourceLocation CatLoc);
  
  virtual DeclTy *ActOnForwardClassDeclaration(SourceLocation Loc,
                                               IdentifierInfo **IdentList,
                                               unsigned NumElts);
  
  virtual DeclTy *ActOnForwardProtocolDeclaration(SourceLocation AtProtocolLoc,
                                            const IdentifierLocPair *IdentList,
                                                  unsigned NumElts);
  
  virtual void FindProtocolDeclaration(bool WarnOnDeclarations,
                                       const IdentifierLocPair *ProtocolId,
                                       unsigned NumProtocols,
                                   llvm::SmallVectorImpl<DeclTy *> &Protocols);
  
  /// Ensure attributes are consistent with type. 
  /// \param [in, out] Attributes The attributes to check; they will
  /// be modified to be consistent with \arg PropertyTy.
  void CheckObjCPropertyAttributes(QualType PropertyTy, 
                                   SourceLocation Loc,
                                   unsigned &Attributes);
  void diagnosePropertySetterGetterMismatch(ObjCPropertyDecl *property,
                                            const ObjCMethodDecl *GetterMethod,
                                            const ObjCMethodDecl *SetterMethod);
  void DiagnosePropertyMismatch(ObjCPropertyDecl *Property, 
                                ObjCPropertyDecl *SuperProperty,
                                const IdentifierInfo *Name);
  void ComparePropertiesInBaseAndSuper(ObjCInterfaceDecl *IDecl);
  
  void MergeProtocolPropertiesIntoClass(ObjCInterfaceDecl *IDecl,
                                        DeclTy *MergeProtocols);
  
  void MergeOneProtocolPropertiesIntoClass(ObjCInterfaceDecl *IDecl,
                                           ObjCProtocolDecl *PDecl);
  
  virtual void ActOnAtEnd(SourceLocation AtEndLoc, DeclTy *classDecl,
                      DeclTy **allMethods = 0, unsigned allNum = 0,
                      DeclTy **allProperties = 0, unsigned pNum = 0);
  
  virtual DeclTy *ActOnProperty(Scope *S, SourceLocation AtLoc,
                                FieldDeclarator &FD, ObjCDeclSpec &ODS,
                                Selector GetterSel, Selector SetterSel,
                                DeclTy *ClassCategory, bool *OverridingProperty,
                                tok::ObjCKeywordKind MethodImplKind);
  
  virtual DeclTy *ActOnPropertyImplDecl(SourceLocation AtLoc, 
                                        SourceLocation PropertyLoc,
                                        bool ImplKind, DeclTy *ClassImplDecl,
                                        IdentifierInfo *PropertyId,
                                        IdentifierInfo *PropertyIvar);
  
  virtual DeclTy *ActOnMethodDeclaration(
    SourceLocation BeginLoc, // location of the + or -.
    SourceLocation EndLoc,   // location of the ; or {.
    tok::TokenKind MethodType, 
    DeclTy *ClassDecl, ObjCDeclSpec &ReturnQT, TypeTy *ReturnType, 
    Selector Sel,
    // optional arguments. The number of types/arguments is obtained
    // from the Sel.getNumArgs().
    ObjCDeclSpec *ArgQT, TypeTy **ArgTypes, IdentifierInfo **ArgNames,
    AttributeList *AttrList, tok::ObjCKeywordKind MethodImplKind,
    bool isVariadic = false);

  // ActOnClassMessage - used for both unary and keyword messages.
  // ArgExprs is optional - if it is present, the number of expressions
  // is obtained from NumArgs.
  virtual ExprResult ActOnClassMessage(
    Scope *S,
    IdentifierInfo *receivingClassName, Selector Sel,
    SourceLocation lbrac, SourceLocation receiverLoc, SourceLocation rbrac, 
    ExprTy **ArgExprs, unsigned NumArgs);

  // ActOnInstanceMessage - used for both unary and keyword messages.
  // ArgExprs is optional - if it is present, the number of expressions
  // is obtained from NumArgs.
  virtual ExprResult ActOnInstanceMessage(
    ExprTy *receiver, Selector Sel,
    SourceLocation lbrac, SourceLocation rbrac, 
    ExprTy **ArgExprs, unsigned NumArgs);
  
  /// ActOnPragmaPack - Called on well formed #pragma pack(...).
  virtual void ActOnPragmaPack(PragmaPackKind Kind,
                               IdentifierInfo *Name,
                               ExprTy *Alignment,
                               SourceLocation PragmaLoc, 
                               SourceLocation LParenLoc,
                               SourceLocation RParenLoc);

  /// ImpCastExprToType - If Expr is not of type 'Type', insert an implicit
  /// cast.  If there is already an implicit cast, merge into the existing one.
  /// If isLvalue, the result of the cast is an lvalue.
  void ImpCastExprToType(Expr *&Expr, QualType Type, bool isLvalue = false);

  // UsualUnaryConversions - promotes integers (C99 6.3.1.1p2) and converts
  // functions and arrays to their respective pointers (C99 6.3.2.1).
  Expr *UsualUnaryConversions(Expr *&expr); 

  // DefaultFunctionArrayConversion - converts functions and arrays
  // to their respective pointers (C99 6.3.2.1). 
  void DefaultFunctionArrayConversion(Expr *&expr);
  
  // DefaultArgumentPromotion (C99 6.5.2.2p6). Used for function calls that
  // do not have a prototype. Integer promotions are performed on each 
  // argument, and arguments that have type float are promoted to double.
  void DefaultArgumentPromotion(Expr *&Expr);
  
  // UsualArithmeticConversions - performs the UsualUnaryConversions on it's
  // operands and then handles various conversions that are common to binary
  // operators (C99 6.3.1.8). If both operands aren't arithmetic, this
  // routine returns the first non-arithmetic type found. The client is 
  // responsible for emitting appropriate error diagnostics.
  QualType UsualArithmeticConversions(Expr *&lExpr, Expr *&rExpr,
                                      bool isCompAssign = false);
  
  /// UsualArithmeticConversionsType - handles the various conversions
  /// that are common to binary operators (C99 6.3.1.8, C++ [expr]p9)
  /// and returns the result type of that conversion.
  QualType UsualArithmeticConversionsType(QualType lhs, QualType rhs);
                                          

  /// AssignConvertType - All of the 'assignment' semantic checks return this
  /// enum to indicate whether the assignment was allowed.  These checks are
  /// done for simple assignments, as well as initialization, return from
  /// function, argument passing, etc.  The query is phrased in terms of a
  /// source and destination type.
  enum AssignConvertType {
    /// Compatible - the types are compatible according to the standard.
    Compatible,
    
    /// PointerToInt - The assignment converts a pointer to an int, which we
    /// accept as an extension.
    PointerToInt,
    
    /// IntToPointer - The assignment converts an int to a pointer, which we
    /// accept as an extension.
    IntToPointer,
    
    /// FunctionVoidPointer - The assignment is between a function pointer and
    /// void*, which the standard doesn't allow, but we accept as an extension.
    FunctionVoidPointer,

    /// IncompatiblePointer - The assignment is between two pointers types that
    /// are not compatible, but we accept them as an extension.
    IncompatiblePointer,
    
    /// CompatiblePointerDiscardsQualifiers - The assignment discards
    /// c/v/r qualifiers, which we accept as an extension.
    CompatiblePointerDiscardsQualifiers,

    /// IntToBlockPointer - The assignment converts an int to a block 
    /// pointer. We disallow this.
    IntToBlockPointer,

    /// IncompatibleBlockPointer - The assignment is between two block 
    /// pointers types that are not compatible.
    IncompatibleBlockPointer,
    
    /// IncompatibleObjCQualifiedId - The assignment is between a qualified
    /// id type and something else (that is incompatible with it). For example,
    /// "id <XXX>" = "Foo *", where "Foo *" doesn't implement the XXX protocol.
    IncompatibleObjCQualifiedId,
    
    /// Incompatible - We reject this conversion outright, it is invalid to
    /// represent it in the AST.
    Incompatible
  };
  
  /// DiagnoseAssignmentResult - Emit a diagnostic, if required, for the
  /// assignment conversion type specified by ConvTy.  This returns true if the
  /// conversion was invalid or false if the conversion was accepted.
  bool DiagnoseAssignmentResult(AssignConvertType ConvTy,
                                SourceLocation Loc,
                                QualType DstType, QualType SrcType,
                                Expr *SrcExpr, const char *Flavor);
  
  /// CheckAssignmentConstraints - Perform type checking for assignment, 
  /// argument passing, variable initialization, and function return values. 
  /// This routine is only used by the following two methods. C99 6.5.16.
  AssignConvertType CheckAssignmentConstraints(QualType lhs, QualType rhs);
  
  // CheckSingleAssignmentConstraints - Currently used by ActOnCallExpr,
  // CheckAssignmentOperands, and ActOnReturnStmt. Prior to type checking, 
  // this routine performs the default function/array converions.
  AssignConvertType CheckSingleAssignmentConstraints(QualType lhs, 
                                                     Expr *&rExpr);
  // CheckCompoundAssignmentConstraints - Type check without performing any 
  // conversions. For compound assignments, the "Check...Operands" methods 
  // perform the necessary conversions. 
  AssignConvertType CheckCompoundAssignmentConstraints(QualType lhs, 
                                                       QualType rhs);
  
  // Helper function for CheckAssignmentConstraints (C99 6.5.16.1p1)
  AssignConvertType CheckPointerTypesForAssignment(QualType lhsType, 
                                                   QualType rhsType);
                                                   
  // Helper function for CheckAssignmentConstraints involving two
  // blcok pointer types.
  AssignConvertType CheckBlockPointerTypesForAssignment(QualType lhsType, 
                                                        QualType rhsType);

  bool IsStringLiteralToNonConstPointerConversion(Expr *From, QualType ToType);

  bool PerformImplicitConversion(Expr *&From, QualType ToType);
  bool PerformImplicitConversion(Expr *&From, QualType ToType,
                                 const StandardConversionSequence& SCS);
  
  /// the following "Check" methods will return a valid/converted QualType
  /// or a null QualType (indicating an error diagnostic was issued).
    
  /// type checking binary operators (subroutines of ActOnBinOp).
  inline QualType InvalidOperands(SourceLocation l, Expr *&lex, Expr *&rex);
  inline QualType CheckMultiplyDivideOperands( // C99 6.5.5
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false); 
  inline QualType CheckRemainderOperands( // C99 6.5.5
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false); 
  inline QualType CheckAdditionOperands( // C99 6.5.6
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false);
  inline QualType CheckSubtractionOperands( // C99 6.5.6
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false);
  inline QualType CheckShiftOperands( // C99 6.5.7
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false);
  inline QualType CheckCompareOperands( // C99 6.5.8/9
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isRelational);
  inline QualType CheckBitwiseOperands( // C99 6.5.[10...12]
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false); 
  inline QualType CheckLogicalOperands( // C99 6.5.[13,14]
    Expr *&lex, Expr *&rex, SourceLocation OpLoc);
  // CheckAssignmentOperands is used for both simple and compound assignment.
  // For simple assignment, pass both expressions and a null converted type.
  // For compound assignment, pass both expressions and the converted type.
  inline QualType CheckAssignmentOperands( // C99 6.5.16.[1,2]
    Expr *lex, Expr *&rex, SourceLocation OpLoc, QualType convertedType);
  inline QualType CheckCommaOperands( // C99 6.5.17
    Expr *lex, Expr *&rex, SourceLocation OpLoc);
  inline QualType CheckConditionalOperands( // C99 6.5.15
    Expr *&cond, Expr *&lhs, Expr *&rhs, SourceLocation questionLoc);

  /// type checking for vector binary operators.
  inline QualType CheckVectorOperands(SourceLocation l, Expr *&lex, Expr *&rex);
  inline QualType CheckVectorCompareOperands(Expr *&lex, Expr *&rx,
                                             SourceLocation l, bool isRel);
  
  /// type checking unary operators (subroutines of ActOnUnaryOp).
  /// C99 6.5.3.1, 6.5.3.2, 6.5.3.4
  QualType CheckIncrementDecrementOperand(Expr *op, SourceLocation OpLoc);   
  QualType CheckAddressOfOperand(Expr *op, SourceLocation OpLoc);
  QualType CheckIndirectionOperand(Expr *op, SourceLocation OpLoc);
  QualType CheckRealImagOperand(Expr *&Op, SourceLocation OpLoc);
  
  /// type checking primary expressions.
  QualType CheckExtVectorComponent(QualType baseType, SourceLocation OpLoc,
                                   IdentifierInfo &Comp, SourceLocation CmpLoc);
  
  /// type checking declaration initializers (C99 6.7.8)
  friend class InitListChecker;
  bool CheckInitializerTypes(Expr *&simpleInit_or_initList, QualType &declType,
                             SourceLocation InitLoc,DeclarationName InitEntity);
  bool CheckSingleInitializer(Expr *&simpleInit, QualType declType);
  bool CheckForConstantInitializer(Expr *e, QualType t);
  bool CheckArithmeticConstantExpression(const Expr* e);
  bool CheckAddressConstantExpression(const Expr* e);
  bool CheckAddressConstantExpressionLValue(const Expr* e);
  void InitializerElementNotConstant(const Expr *e);
  
  StringLiteral *IsStringLiteralInit(Expr *Init, QualType DeclType);
  bool CheckStringLiteralInit(StringLiteral *strLiteral, QualType &DeclT);

  // type checking C++ declaration initializers (C++ [dcl.init]).

  /// ReferenceCompareResult - Expresses the result of comparing two
  /// types (cv1 T1 and cv2 T2) to determine their compatibility for the
  /// purposes of initialization by reference (C++ [dcl.init.ref]p4).
  enum ReferenceCompareResult {
    /// Ref_Incompatible - The two types are incompatible, so direct
    /// reference binding is not possible.
    Ref_Incompatible = 0,
    /// Ref_Related - The two types are reference-related, which means
    /// that their unqualified forms (T1 and T2) are either the same
    /// or T1 is a base class of T2.
    Ref_Related,
    /// Ref_Compatible_With_Added_Qualification - The two types are
    /// reference-compatible with added qualification, meaning that
    /// they are reference-compatible and the qualifiers on T1 (cv1)
    /// are greater than the qualifiers on T2 (cv2).
    Ref_Compatible_With_Added_Qualification,
    /// Ref_Compatible - The two types are reference-compatible and
    /// have equivalent qualifiers (cv1 == cv2).
    Ref_Compatible
  };

  ReferenceCompareResult CompareReferenceRelationship(QualType T1, QualType T2,
                                                      bool& DerivedToBase);

  bool CheckReferenceInit(Expr *&simpleInit_or_initList, QualType &declType,
                          ImplicitConversionSequence *ICS = 0,
                          bool SuppressUserConversions = false);

  /// CheckCastTypes - Check type constraints for casting between types.
  bool CheckCastTypes(SourceRange TyRange, QualType CastTy, Expr *&CastExpr);
  
  // CheckVectorCast - check type constraints for vectors. 
  // Since vectors are an extension, there are no C standard reference for this.
  // We allow casting between vectors and integer datatypes of the same size.
  // returns true if the cast is invalid
  bool CheckVectorCast(SourceRange R, QualType VectorTy, QualType Ty);
  
  /// CheckMessageArgumentTypes - Check types in an Obj-C message send. 
  /// \param Method - May be null.
  /// \param [out] ReturnType - The return type of the send.
  /// \return true iff there were any incompatible types.
  bool CheckMessageArgumentTypes(Expr **Args, unsigned NumArgs, Selector Sel,
                                 ObjCMethodDecl *Method, bool isClassMessage,
                                 SourceLocation lbrac, SourceLocation rbrac,
                                 QualType &ReturnType);  

  /// CheckCXXBooleanCondition - Returns true if conversion to bool is invalid.
  bool CheckCXXBooleanCondition(Expr *&CondExpr);
                    
  /// ConvertIntegerToTypeWarnOnOverflow - Convert the specified APInt to have
  /// the specified width and sign.  If an overflow occurs, detect it and emit
  /// the specified diagnostic.
  void ConvertIntegerToTypeWarnOnOverflow(llvm::APSInt &OldVal, 
                                          unsigned NewWidth, bool NewSign,
                                          SourceLocation Loc, unsigned DiagID);
  
  bool ObjCQualifiedIdTypesAreCompatible(QualType LHS, QualType RHS,
                                         bool ForCompare);

  /// Checks that the Objective-C declaration is declared in the global scope.
  /// Emits an error and marks the declaration as invalid if it's not declared
  /// in the global scope.
  bool CheckObjCDeclScope(Decl *D);

  void InitBuiltinVaListType();

  /// VerifyIntegerConstantExpression - verifies that an expression is an ICE,
  /// and reports the appropriate diagnostics. Returns false on success.
  /// Can optionally return the value of the expression.
  bool VerifyIntegerConstantExpression(const Expr*E, llvm::APSInt *Result = 0);

  //===--------------------------------------------------------------------===//
  // Extra semantic analysis beyond the C type system
private:
  Action::ExprResult CheckFunctionCall(FunctionDecl *FDecl, CallExpr *TheCall);
  bool CheckBuiltinCFStringArgument(Expr* Arg);
  bool SemaBuiltinVAStart(CallExpr *TheCall);
  bool SemaBuiltinUnorderedCompare(CallExpr *TheCall);
  bool SemaBuiltinStackAddress(CallExpr *TheCall);
  Action::ExprResult SemaBuiltinShuffleVector(CallExpr *TheCall);
  bool SemaBuiltinPrefetch(CallExpr *TheCall); 
  bool SemaBuiltinObjectSize(CallExpr *TheCall); 
  void CheckPrintfArguments(CallExpr *TheCall,
                            bool HasVAListArg, unsigned format_idx);
  void CheckReturnStackAddr(Expr *RetValExp, QualType lhsType,
                            SourceLocation ReturnLoc);
  void CheckFloatComparison(SourceLocation loc, Expr* lex, Expr* rex);
};

class InitListChecker {
  Sema *SemaRef;
  bool hadError;
  
  void CheckImplicitInitList(InitListExpr *ParentIList, QualType T, 
                             unsigned &Index);
  void CheckExplicitInitList(InitListExpr *IList, QualType &T,
                             unsigned &Index);

  void CheckListElementTypes(InitListExpr *IList, QualType &DeclType, 
                             unsigned &Index);
  void CheckSubElementType(InitListExpr *IList, QualType ElemType, 
                           unsigned &Index);
  // FIXME: Does DeclType need to be a reference type?
  void CheckScalarType(InitListExpr *IList, QualType &DeclType, 
                       unsigned &Index);
  void CheckVectorType(InitListExpr *IList, QualType DeclType, unsigned &Index);
  void CheckStructUnionTypes(InitListExpr *IList, QualType DeclType, 
                             unsigned &Index);
  void CheckArrayType(InitListExpr *IList, QualType &DeclType, unsigned &Index);
  
  int numArrayElements(QualType DeclType);
  int numStructUnionElements(QualType DeclType);
public:
  InitListChecker(Sema *S, InitListExpr *IL, QualType &T);
  bool HadError() { return hadError; }
};

/// BlockSemaInfo - When a block is being parsed, this contains information
/// about the block.  It is pointed to from Sema::CurBlock.
struct BlockSemaInfo {
  llvm::SmallVector<ParmVarDecl*, 8> Params;
  bool hasPrototype;
  bool isVariadic;
  
  BlockDecl *TheDecl;
  
  /// TheScope - This is the scope for the block itself, which contains
  /// arguments etc.
  Scope *TheScope;
  
  /// ReturnType - This will get set to block result type, by looking at
  /// return types, if any, in the block body.
  Type *ReturnType;
  
  /// PrevBlockInfo - If this is nested inside another block, this points
  /// to the outer block.
  BlockSemaInfo *PrevBlockInfo;
};


}  // end namespace clang

#endif
