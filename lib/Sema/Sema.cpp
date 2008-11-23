//===--- Sema.cpp - AST Builder and Semantic Analysis Implementation ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the actions class which performs semantic analysis and
// builds an AST out of a parse stream.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/Diagnostic.h"
using namespace clang;

/// ConvertQualTypeToStringFn - This function is used to pretty print the 
/// specified QualType as a string in diagnostics.
static void ConvertArgToStringFn(Diagnostic::ArgumentKind Kind, intptr_t QT,
                                      const char *Modifier, unsigned ML,
                                      const char *Argument, unsigned ArgLen,
                                      llvm::SmallVectorImpl<char> &Output) {
  assert(ML == 0 && ArgLen == 0 && "Invalid modifier for QualType argument");
  assert(Kind == Diagnostic::ak_qualtype);
  
  QualType Ty(QualType::getFromOpaquePtr(reinterpret_cast<void*>(QT)));
  
  // FIXME: Playing with std::string is really slow.
  std::string S = Ty.getAsString();
  Output.append(S.begin(), S.end());
}


static inline RecordDecl *CreateStructDecl(ASTContext &C, const char *Name) {
  if (C.getLangOptions().CPlusPlus)
    return CXXRecordDecl::Create(C, TagDecl::TK_struct, 
                                 C.getTranslationUnitDecl(),
                                 SourceLocation(), &C.Idents.get(Name));

  return RecordDecl::Create(C, TagDecl::TK_struct, 
                            C.getTranslationUnitDecl(),
                            SourceLocation(), &C.Idents.get(Name));
}

void Sema::ActOnTranslationUnitScope(SourceLocation Loc, Scope *S) {
  TUScope = S;
  PushDeclContext(Context.getTranslationUnitDecl());
  if (!PP.getLangOptions().ObjC1) return;
  
  // Synthesize "typedef struct objc_selector *SEL;"
  RecordDecl *SelTag = CreateStructDecl(Context, "objc_selector");
  PushOnScopeChains(SelTag, TUScope);
  
  QualType SelT = Context.getPointerType(Context.getTagDeclType(SelTag));
  TypedefDecl *SelTypedef = TypedefDecl::Create(Context, CurContext,
                                                SourceLocation(),
                                                &Context.Idents.get("SEL"),
                                                SelT, 0);
  PushOnScopeChains(SelTypedef, TUScope);
  Context.setObjCSelType(SelTypedef);

  // FIXME: Make sure these don't leak!
  RecordDecl *ClassTag = CreateStructDecl(Context, "objc_class");
  QualType ClassT = Context.getPointerType(Context.getTagDeclType(ClassTag));
  TypedefDecl *ClassTypedef = 
    TypedefDecl::Create(Context, CurContext, SourceLocation(),
                        &Context.Idents.get("Class"), ClassT, 0);
  PushOnScopeChains(ClassTag, TUScope);
  PushOnScopeChains(ClassTypedef, TUScope);
  Context.setObjCClassType(ClassTypedef);
  // Synthesize "@class Protocol;
  ObjCInterfaceDecl *ProtocolDecl =
    ObjCInterfaceDecl::Create(Context, SourceLocation(),
                              &Context.Idents.get("Protocol"), 
                              SourceLocation(), true);
  Context.setObjCProtoType(Context.getObjCInterfaceType(ProtocolDecl));
  PushOnScopeChains(ProtocolDecl, TUScope);
  
  // Synthesize "typedef struct objc_object { Class isa; } *id;"
  RecordDecl *ObjectTag = CreateStructDecl(Context, "objc_object");

  QualType ObjT = Context.getPointerType(Context.getTagDeclType(ObjectTag));
  PushOnScopeChains(ObjectTag, TUScope);
  TypedefDecl *IdTypedef = TypedefDecl::Create(Context, CurContext,
                                               SourceLocation(),
                                               &Context.Idents.get("id"),
                                               ObjT, 0);
  PushOnScopeChains(IdTypedef, TUScope);
  Context.setObjCIdType(IdTypedef);
}

Sema::Sema(Preprocessor &pp, ASTContext &ctxt, ASTConsumer &consumer)
  : PP(pp), Context(ctxt), Consumer(consumer), Diags(PP.getDiagnostics()),
    SourceMgr(PP.getSourceManager()), CurContext(0), PreDeclaratorDC(0),
    CurBlock(0), PackContext(0), IdResolver(pp.getLangOptions()) {
  
  // Get IdentifierInfo objects for known functions for which we
  // do extra checking.  
  IdentifierTable &IT = PP.getIdentifierTable();  

  KnownFunctionIDs[id_printf]        = &IT.get("printf");
  KnownFunctionIDs[id_fprintf]       = &IT.get("fprintf");
  KnownFunctionIDs[id_sprintf]       = &IT.get("sprintf");
  KnownFunctionIDs[id_sprintf_chk]   = &IT.get("__builtin___sprintf_chk");
  KnownFunctionIDs[id_snprintf]      = &IT.get("snprintf");
  KnownFunctionIDs[id_snprintf_chk]  = &IT.get("__builtin___snprintf_chk");
  KnownFunctionIDs[id_asprintf]      = &IT.get("asprintf");
  KnownFunctionIDs[id_NSLog]         = &IT.get("NSLog");
  KnownFunctionIDs[id_vsnprintf]     = &IT.get("vsnprintf");
  KnownFunctionIDs[id_vasprintf]     = &IT.get("vasprintf");
  KnownFunctionIDs[id_vfprintf]      = &IT.get("vfprintf");
  KnownFunctionIDs[id_vsprintf]      = &IT.get("vsprintf");
  KnownFunctionIDs[id_vsprintf_chk]  = &IT.get("__builtin___vsprintf_chk");
  KnownFunctionIDs[id_vsnprintf]     = &IT.get("vsnprintf");
  KnownFunctionIDs[id_vsnprintf_chk] = &IT.get("__builtin___vsnprintf_chk");
  KnownFunctionIDs[id_vprintf]       = &IT.get("vprintf");

  StdNamespace = 0;
  TUScope = 0;
  if (getLangOptions().CPlusPlus)
    FieldCollector.reset(new CXXFieldCollector());
      
  // Tell diagnostics how to render things from the AST library.
  PP.getDiagnostics().SetArgToStringFn(ConvertArgToStringFn);
}

/// ImpCastExprToType - If Expr is not of type 'Type', insert an implicit cast. 
/// If there is already an implicit cast, merge into the existing one.
  /// If isLvalue, the result of the cast is an lvalue.
void Sema::ImpCastExprToType(Expr *&Expr, QualType Ty, bool isLvalue) {
  QualType ExprTy = Context.getCanonicalType(Expr->getType());
  QualType TypeTy = Context.getCanonicalType(Ty);
  
  if (ExprTy == TypeTy)
    return;
  
  if (Expr->getType().getTypePtr()->isPointerType() &&
      Ty.getTypePtr()->isPointerType()) {
    QualType ExprBaseType = 
      cast<PointerType>(ExprTy.getUnqualifiedType())->getPointeeType();
    QualType BaseType =
      cast<PointerType>(TypeTy.getUnqualifiedType())->getPointeeType();
    if (ExprBaseType.getAddressSpace() != BaseType.getAddressSpace()) {
      Diag(Expr->getExprLoc(), diag::err_implicit_pointer_address_space_cast)
        << Expr->getSourceRange();
    }
  }
  
  if (ImplicitCastExpr *ImpCast = dyn_cast<ImplicitCastExpr>(Expr)) {
    ImpCast->setType(Ty);
    ImpCast->setLvalueCast(isLvalue);
  } else 
    Expr = new ImplicitCastExpr(Ty, Expr, isLvalue);
}

void Sema::DeleteExpr(ExprTy *E) {
  delete static_cast<Expr*>(E);
}
void Sema::DeleteStmt(StmtTy *S) {
  delete static_cast<Stmt*>(S);
}

/// ActOnEndOfTranslationUnit - This is called at the very end of the
/// translation unit when EOF is reached and all but the top-level scope is
/// popped.
void Sema::ActOnEndOfTranslationUnit() {

}


//===----------------------------------------------------------------------===//
// Helper functions.
//===----------------------------------------------------------------------===//

const LangOptions &Sema::getLangOptions() const {
  return PP.getLangOptions();
}

ObjCMethodDecl *Sema::getCurMethodDecl() {
  DeclContext *DC = CurContext;
  while (isa<BlockDecl>(DC))
    DC = DC->getParent();
  return dyn_cast<ObjCMethodDecl>(DC);
}
