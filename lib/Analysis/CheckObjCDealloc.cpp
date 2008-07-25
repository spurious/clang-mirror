//==- CheckObjCDealloc.cpp - Check ObjC -dealloc implementation --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a CheckObjCDealloc, a checker that
//  analyzes an Objective-C class's implementation to determine if it
//  correctly implements -dealloc.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/LocalCheckers.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Analysis/PathSensitive/BugReporter.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/LangOptions.h"
#include <sstream>

using namespace clang;

static bool scan_dealloc(Stmt* S, Selector Dealloc) {  
  
  if (ObjCMessageExpr* ME = dyn_cast<ObjCMessageExpr>(S))
    if (ME->getSelector() == Dealloc)
      if (Expr* Receiver = ME->getReceiver()->IgnoreParenCasts())
        if (PreDefinedExpr* E = dyn_cast<PreDefinedExpr>(Receiver))
          if (E->getIdentType() == PreDefinedExpr::ObjCSuper)
            return true;

  // Recurse to children.

  for (Stmt::child_iterator I = S->child_begin(), E= S->child_end(); I!=E; ++I)
    if (*I && scan_dealloc(*I, Dealloc))
      return true;
  
  return false;
}

static bool isSEL(QualType T, IdentifierInfo* SelII) {
  if (const TypedefType* Ty = T->getAsTypedefType())
    return Ty->getDecl()->getIdentifier() == SelII;
  
  return false;
}

void clang::CheckObjCDealloc(ObjCImplementationDecl* D,
                             const LangOptions& LOpts, BugReporter& BR) {

  assert (LOpts.getGCMode() != LangOptions::GCOnly);
  
  ASTContext& Ctx = BR.getContext();
  ObjCInterfaceDecl* ID = D->getClassInterface();
    
  // Does the class contain any ivars that are pointers (or id<...>)?
  // If not, skip the check entirely.
  // NOTE: This is motivated by PR 2517:
  //        http://llvm.org/bugs/show_bug.cgi?id=2517
  
  bool containsPointerIvar = false;
  IdentifierInfo* SelII = &Ctx.Idents.get("SEL");
  
  for (ObjCInterfaceDecl::ivar_iterator I=ID->ivar_begin(), E=ID->ivar_end();
       I!=E; ++I) {
    
    ObjCIvarDecl* ID = *I;
    QualType T = ID->getType();
    
    if (!Ctx.isObjCObjectPointerType(T) ||
        ID->getAttr<IBOutletAttr>() || // Skip IBOutlets.
        isSEL(T, SelII)) // Skip SEL ivars.
      continue;
    
    containsPointerIvar = true;
    break;
  }
  
  if (!containsPointerIvar)
    return;
  
  // Determine if the class subclasses NSObject.
  IdentifierInfo* NSObjectII = &Ctx.Idents.get("NSObject");
  
  for ( ; ID ; ID = ID->getSuperClass())
    if (ID->getIdentifier() == NSObjectII)
      break;
  
  if (!ID)
    return;
  
  // Get the "dealloc" selector.
  IdentifierInfo* II = &Ctx.Idents.get("dealloc");
  Selector S = Ctx.Selectors.getSelector(0, &II);  
  ObjCMethodDecl* MD = 0;
  
  // Scan the instance methods for "dealloc".
  for (ObjCImplementationDecl::instmeth_iterator I = D->instmeth_begin(),
       E = D->instmeth_end(); I!=E; ++I) {
    
    if ((*I)->getSelector() == S) {
      MD = *I;
      break;
    }    
  }
  
  if (!MD) { // No dealloc found.
    
    const char* name = LOpts.getGCMode() == LangOptions::NonGC 
                       ? "missing -dealloc" 
                       : "missing -dealloc (Hybrid MM, non-GC)";
    
    std::ostringstream os;
    os << "Objective-C class '" << D->getName()
       << "' lacks a 'dealloc' instance method";
    
    BR.EmitBasicReport(name, os.str().c_str(), D->getLocStart());
    return;
  }
  
  // dealloc found.  Scan for missing [super dealloc].
  if (MD->getBody() && !scan_dealloc(MD->getBody(), S)) {
    
    const char* name = LOpts.getGCMode() == LangOptions::NonGC
                       ? "missing [super dealloc]"
                       : "missing [super dealloc] (Hybrid MM, non-GC)";
    
    std::ostringstream os;
    os << "The 'dealloc' instance method in Objective-C class '" << D->getName()
       << "' does not send a 'dealloc' message to its super class"
           " (missing [super dealloc])";
    
    BR.EmitBasicReport(name, os.str().c_str(), D->getLocStart());
    return;
  }   
}

