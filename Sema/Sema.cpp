//===--- Sema.cpp - AST Builder and Semantic Analysis Implementation ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the actions class which performs semantic analysis and
// builds an AST out of a parse stream.
//
//===----------------------------------------------------------------------===//

#include "Sema.h"
#include "clang/AST/ASTContext.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/Diagnostic.h"

using namespace clang;

void Sema::ActOnTranslationUnitScope(SourceLocation Loc, Scope *S) {
  TUScope = S;
}

/// GetObjcIdType - The following method assumes that "id" is imported
/// via <objc/objc.h>. This is the way GCC worked for almost 20 years.
/// In GCC 4.0, "id" is now a built-in type. Unfortunately, typedefs *cannot* be
/// redefined (even if they are identical). To allow a built-in types to coexist
/// with <objc/objc.h>, GCC has a special hack on decls (DECL_IN_SYSTEM_HEADER).
/// For now, we will *not* install id as a built-in. FIXME: reconsider this.
QualType Sema::GetObjcIdType(SourceLocation Loc) {
  assert(TUScope && "GetObjcIdType(): Top-level scope is null");
  if (!ObjcIdTypedef) {
    IdentifierInfo *IdIdent = &Context.Idents.get("id");
    ScopedDecl *IdDecl = LookupScopedDecl(IdIdent, Decl::IDNS_Ordinary, 
                                          SourceLocation(), TUScope);
    ObjcIdTypedef = dyn_cast_or_null<TypedefDecl>(IdDecl);
    if (!ObjcIdTypedef) {
      Diag(Loc, diag::err_missing_id_definition);
      return QualType();
    }
  }
  return Context.getTypedefType(ObjcIdTypedef);
}


Sema::Sema(Preprocessor &pp, ASTContext &ctxt, std::vector<Decl*> &prevInGroup)
  : PP(pp), Context(ctxt), CurFunctionDecl(0), LastInGroupList(prevInGroup) {
  
  // Get IdentifierInfo objects for known functions for which we
  // do extra checking.  
  IdentifierTable& IT = PP.getIdentifierTable();  

  KnownFunctionIDs[ id_printf  ] = &IT.get("printf");
  KnownFunctionIDs[ id_fprintf ] = &IT.get("fprintf");
  KnownFunctionIDs[ id_sprintf ] = &IT.get("sprintf");
  KnownFunctionIDs[ id_snprintf ] = &IT.get("snprintf");
  KnownFunctionIDs[ id_asprintf ] = &IT.get("asprintf");
  KnownFunctionIDs[ id_vsnprintf ] = &IT.get("vsnprintf");
  KnownFunctionIDs[ id_vasprintf ] = &IT.get("vasprintf");
  KnownFunctionIDs[ id_vfprintf ] = &IT.get("vfprintf");
  KnownFunctionIDs[ id_vsprintf ] = &IT.get("vsprintf");
  KnownFunctionIDs[ id_vprintf ] = &IT.get("vprintf");
  
  TUScope = 0;
  ObjcIdTypedef = 0;
}

void Sema::DeleteExpr(ExprTy *E) {
  delete static_cast<Expr*>(E);
}
void Sema::DeleteStmt(StmtTy *S) {
  delete static_cast<Stmt*>(S);
}

//===----------------------------------------------------------------------===//
// Helper functions.
//===----------------------------------------------------------------------===//

bool Sema::Diag(SourceLocation Loc, unsigned DiagID) {
  PP.getDiagnostics().Report(Loc, DiagID);
  return true;
}

bool Sema::Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg) {
  PP.getDiagnostics().Report(Loc, DiagID, &Msg, 1);
  return true;
}

bool Sema::Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg1,
                const std::string &Msg2) {
  std::string MsgArr[] = { Msg1, Msg2 };
  PP.getDiagnostics().Report(Loc, DiagID, MsgArr, 2);
  return true;
}

bool Sema::Diag(SourceLocation Loc, unsigned DiagID, SourceRange Range) {
  PP.getDiagnostics().Report(Loc, DiagID, 0, 0, &Range, 1);
  return true;
}

bool Sema::Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg,
                SourceRange Range) {
  PP.getDiagnostics().Report(Loc, DiagID, &Msg, 1, &Range, 1);
  return true;
}

bool Sema::Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg1,
                const std::string &Msg2, SourceRange Range) {
  std::string MsgArr[] = { Msg1, Msg2 };
  PP.getDiagnostics().Report(Loc, DiagID, MsgArr, 2, &Range, 1);
  return true;
}

bool Sema::Diag(SourceLocation Loc, unsigned DiagID,
                SourceRange R1, SourceRange R2) {
  SourceRange RangeArr[] = { R1, R2 };
  PP.getDiagnostics().Report(Loc, DiagID, 0, 0, RangeArr, 2);
  return true;
}

bool Sema::Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg,
                SourceRange R1, SourceRange R2) {
  SourceRange RangeArr[] = { R1, R2 };
  PP.getDiagnostics().Report(Loc, DiagID, &Msg, 1, RangeArr, 2);
  return true;
}

bool Sema::Diag(SourceLocation Range, unsigned DiagID, const std::string &Msg1,
                const std::string &Msg2, SourceRange R1, SourceRange R2) {
  std::string MsgArr[] = { Msg1, Msg2 };
  SourceRange RangeArr[] = { R1, R2 };
  PP.getDiagnostics().Report(Range, DiagID, MsgArr, 2, RangeArr, 2);
  return true;
}

const LangOptions &Sema::getLangOptions() const {
  return PP.getLangOptions();
}
