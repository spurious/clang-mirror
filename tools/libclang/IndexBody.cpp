//===- CIndexHigh.cpp - Higher level API functions ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "IndexingContext.h"

#include "clang/AST/RecursiveASTVisitor.h"

using namespace clang;
using namespace cxindex;

namespace {

class BodyIndexer : public RecursiveASTVisitor<BodyIndexer> {
  IndexingContext &IndexCtx;
  const DeclContext *ParentDC;

public:
  BodyIndexer(IndexingContext &indexCtx, const DeclContext *DC)
    : IndexCtx(indexCtx), ParentDC(DC) { }
  
  bool shouldWalkTypesOfTypeLocs() const { return false; }

  bool TraverseTypeLoc(TypeLoc TL) {
    IndexCtx.indexTypeLoc(TL, 0, ParentDC);
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *E) {
    const NamedDecl *D = E->getDecl();
    if (!D)
      return true;
    if (D->getParentFunctionOrMethod())
      return true;
    
    IndexCtx.handleReference(D, E->getLocation(), 0, ParentDC, E);
    return true;
  }

  bool VisitMemberExpr(MemberExpr *E) {
    const NamedDecl *D = E->getMemberDecl();
    if (!D)
      return true;
    if (D->getParentFunctionOrMethod())
      return true;
    
    IndexCtx.handleReference(D, E->getMemberLoc(), 0, ParentDC, E);
    return true;
  }

  bool VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
    const NamedDecl *D = E->getDecl();
    if (!D)
      return true;
    if (D->getParentFunctionOrMethod())
      return true;
    
    IndexCtx.handleReference(D, E->getLocation(), 0, ParentDC, E);
    return true;
  }
};

} // anonymous namespace

void IndexingContext::indexBody(const Stmt *S, const DeclContext *DC) {
  BodyIndexer(*this, DC).TraverseStmt(const_cast<Stmt*>(S));
}
