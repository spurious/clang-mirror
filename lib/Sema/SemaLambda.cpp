//===--- SemaLambda.cpp - Semantic Analysis for C++11 Lambdas -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ lambda expressions.
//
//===----------------------------------------------------------------------===//
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/AST/ExprCXX.h"
using namespace clang;
using namespace sema;

void Sema::ActOnStartOfLambdaDefinition(LambdaIntroducer &Intro,
                                        Declarator &ParamInfo,
                                        Scope *CurScope) {
  DeclContext *DC = CurContext;
  while (!(DC->isFunctionOrMethod() || DC->isRecord() || DC->isFileContext()))
    DC = DC->getParent();

  // Start constructing the lambda class.
  CXXRecordDecl *Class = CXXRecordDecl::Create(Context, TTK_Class, DC,
                                               Intro.Range.getBegin(),
                                               /*IdLoc=*/Intro.Range.getBegin(),
                                               /*Id=*/0);
  Class->startDefinition();
  Class->setLambda(true);
  CurContext->addDecl(Class);

  // Build the call operator; we don't really have all the relevant information
  // at this point, but we need something to attach child declarations to.
  QualType MethodTy;
  TypeSourceInfo *MethodTyInfo;
  bool ExplicitParams = true;
  SourceLocation EndLoc;
  if (ParamInfo.getNumTypeObjects() == 0) {
    // C++11 [expr.prim.lambda]p4:
    //   If a lambda-expression does not include a lambda-declarator, it is as 
    //   if the lambda-declarator were ().
    FunctionProtoType::ExtProtoInfo EPI;
    EPI.TypeQuals |= DeclSpec::TQ_const;
    MethodTy = Context.getFunctionType(Context.DependentTy,
                                       /*Args=*/0, /*NumArgs=*/0, EPI);
    MethodTyInfo = Context.getTrivialTypeSourceInfo(MethodTy);
    ExplicitParams = false;
    EndLoc = Intro.Range.getEnd();
  } else {
    assert(ParamInfo.isFunctionDeclarator() &&
           "lambda-declarator is a function");
    DeclaratorChunk::FunctionTypeInfo &FTI = ParamInfo.getFunctionTypeInfo();
    
    // C++11 [expr.prim.lambda]p5:
    //   This function call operator is declared const (9.3.1) if and only if 
    //   the lambda-expression's parameter-declaration-clause is not followed 
    //   by mutable. It is neither virtual nor declared volatile. [...]
    if (!FTI.hasMutableQualifier())
      FTI.TypeQuals |= DeclSpec::TQ_const;
    
    // C++11 [expr.prim.lambda]p5:
    //   [...] Default arguments (8.3.6) shall not be specified in the 
    //   parameter-declaration-clause of a lambda-declarator.
    CheckExtraCXXDefaultArguments(ParamInfo);
    
    MethodTyInfo = GetTypeForDeclarator(ParamInfo, CurScope);
    // FIXME: Can these asserts actually fail?
    assert(MethodTyInfo && "no type from lambda-declarator");
    MethodTy = MethodTyInfo->getType();
    assert(!MethodTy.isNull() && "no type from lambda declarator");
    EndLoc = ParamInfo.getSourceRange().getEnd();
  }
  
  // C++11 [expr.prim.lambda]p5:
  //   The closure type for a lambda-expression has a public inline function 
  //   call operator (13.5.4) whose parameters and return type are described by
  //   the lambda-expression's parameter-declaration-clause and 
  //   trailing-return-type respectively.
  DeclarationName MethodName
    = Context.DeclarationNames.getCXXOperatorName(OO_Call);
  DeclarationNameLoc MethodNameLoc;
  MethodNameLoc.CXXOperatorName.BeginOpNameLoc
    = Intro.Range.getBegin().getRawEncoding();
  MethodNameLoc.CXXOperatorName.EndOpNameLoc
    = Intro.Range.getEnd().getRawEncoding();
  CXXMethodDecl *Method
    = CXXMethodDecl::Create(Context, Class, EndLoc,
                            DeclarationNameInfo(MethodName, 
                                                Intro.Range.getBegin(),
                                                MethodNameLoc),
                            MethodTy, MethodTyInfo,
                            /*isStatic=*/false,
                            SC_None,
                            /*isInline=*/true,
                            /*isConstExpr=*/false,
                            EndLoc);
  Method->setAccess(AS_public);
  Class->addDecl(Method);
  Method->setLexicalDeclContext(DC); // FIXME: Minor hack.

  // Attributes on the lambda apply to the method.  
  ProcessDeclAttributes(CurScope, Method, ParamInfo);
  
  // Introduce the function call operator as the current declaration context.
  PushDeclContext(CurScope, Method);
    
  // Introduce the lambda scope.
  PushLambdaScope(Class, Method);
  LambdaScopeInfo *LSI = getCurLambda();
  if (Intro.Default == LCD_ByCopy)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByval;
  else if (Intro.Default == LCD_ByRef)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByref;
  LSI->IntroducerRange = Intro.Range;
  LSI->ExplicitParams = ExplicitParams;
  LSI->Mutable = (Method->getTypeQualifiers() & Qualifiers::Const) == 0;
 
  // Handle explicit captures.
  for (llvm::SmallVector<LambdaCapture, 4>::const_iterator
         C = Intro.Captures.begin(), 
         E = Intro.Captures.end(); 
       C != E; ++C) {
    if (C->Kind == LCK_This) {
      // C++11 [expr.prim.lambda]p8:
      //   An identifier or this shall not appear more than once in a 
      //   lambda-capture.
      if (LSI->isCXXThisCaptured()) {
        Diag(C->Loc, diag::err_capture_more_than_once) 
          << "'this'"
          << SourceRange(LSI->getCXXThisCapture().getLocation());
        continue;
      }

      // C++11 [expr.prim.lambda]p8:
      //   If a lambda-capture includes a capture-default that is =, the 
      //   lambda-capture shall not contain this [...].
      if (Intro.Default == LCD_ByCopy) {
        Diag(C->Loc, diag::err_this_capture_with_copy_default);
        continue;
      }

      // C++11 [expr.prim.lambda]p12:
      //   If this is captured by a local lambda expression, its nearest
      //   enclosing function shall be a non-static member function.
      QualType ThisCaptureType = getCurrentThisType();
      if (ThisCaptureType.isNull()) {
        Diag(C->Loc, diag::err_this_capture) << true;
        continue;
      }
      
      CheckCXXThisCapture(C->Loc, /*Explicit=*/true);
      continue;
    }

    assert(C->Id && "missing identifier for capture");

    // C++11 [expr.prim.lambda]p8:
    //   If a lambda-capture includes a capture-default that is &, the 
    //   identifiers in the lambda-capture shall not be preceded by &.
    //   If a lambda-capture includes a capture-default that is =, [...]
    //   each identifier it contains shall be preceded by &.
    if (C->Kind == LCK_ByRef && Intro.Default == LCD_ByRef) {
      Diag(C->Loc, diag::err_reference_capture_with_reference_default);
      continue;
    } else if (C->Kind == LCK_ByCopy && Intro.Default == LCD_ByCopy) {
      Diag(C->Loc, diag::err_copy_capture_with_copy_default);
      continue;
    }

    DeclarationNameInfo Name(C->Id, C->Loc);
    LookupResult R(*this, Name, LookupOrdinaryName);
    LookupName(R, CurScope);
    if (R.isAmbiguous())
      continue;
    if (R.empty()) {
      // FIXME: Disable corrections that would add qualification?
      CXXScopeSpec ScopeSpec;
      DeclFilterCCC<VarDecl> Validator;
      if (DiagnoseEmptyLookup(CurScope, ScopeSpec, R, Validator))
        continue;
    }

    // C++11 [expr.prim.lambda]p10:
    //   The identifiers in a capture-list are looked up using the usual rules
    //   for unqualified name lookup (3.4.1); each such lookup shall find a 
    //   variable with automatic storage duration declared in the reaching 
    //   scope of the local lambda expression.
    // FIXME: Check reaching scope. 
    VarDecl *Var = R.getAsSingle<VarDecl>();
    if (!Var) {
      Diag(C->Loc, diag::err_capture_does_not_name_variable) << C->Id;
      continue;
    }

    if (!Var->hasLocalStorage()) {
      Diag(C->Loc, diag::err_capture_non_automatic_variable) << C->Id;
      Diag(Var->getLocation(), diag::note_previous_decl) << C->Id;
      continue;
    }

    // C++11 [expr.prim.lambda]p8:
    //   An identifier or this shall not appear more than once in a 
    //   lambda-capture.
    if (LSI->isCaptured(Var)) {
      Diag(C->Loc, diag::err_capture_more_than_once) 
        << C->Id
        << SourceRange(LSI->getCapture(Var).getLocation());
      continue;
    }

    TryCaptureKind Kind = C->Kind == LCK_ByRef ? TryCapture_ExplicitByRef :
                                                 TryCapture_ExplicitByVal;
    TryCaptureVar(Var, C->Loc, Kind);
  }
  LSI->finishedExplicitCaptures();

  // Set the parameters on the decl, if specified.
  if (isa<FunctionProtoTypeLoc>(MethodTyInfo->getTypeLoc())) {
    FunctionProtoTypeLoc Proto =
    cast<FunctionProtoTypeLoc>(MethodTyInfo->getTypeLoc());
    Method->setParams(Proto.getParams());
    CheckParmsForFunctionDef(Method->param_begin(),
                             Method->param_end(),
                             /*CheckParameterNames=*/false);
    
    // Introduce our parameters into the function scope
    for (unsigned p = 0, NumParams = Method->getNumParams(); p < NumParams; ++p) {
      ParmVarDecl *Param = Method->getParamDecl(p);
      Param->setOwningFunction(Method);
      
      // If this has an identifier, add it to the scope stack.
      if (Param->getIdentifier()) {
        CheckShadow(CurScope, Param);
        
        PushOnScopeChains(Param, CurScope);
      }
    }
  }

  const FunctionType *Fn = MethodTy->getAs<FunctionType>();
  QualType RetTy = Fn->getResultType();
  if (RetTy != Context.DependentTy) {
    LSI->ReturnType = RetTy;
  } else {
    LSI->HasImplicitReturnType = true;
  }

  // FIXME: Check return type is complete, !isObjCObjectType
  
  // Enter a new evaluation context to insulate the block from any
  // cleanups from the enclosing full-expression.
  PushExpressionEvaluationContext(PotentiallyEvaluated);  
}

void Sema::ActOnLambdaError(SourceLocation StartLoc, Scope *CurScope) {
  // Leave the expression-evaluation context.
  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();

  // Leave the context of the lambda.
  PopDeclContext();

  // Finalize the lambda.
  LambdaScopeInfo *LSI = getCurLambda();
  CXXRecordDecl *Class = LSI->Lambda;
  Class->setInvalidDecl();
  SmallVector<Decl*, 4> Fields(Class->field_begin(), Class->field_end());
  ActOnFields(0, Class->getLocation(), Class, Fields, 
              SourceLocation(), SourceLocation(), 0);
  CheckCompletedCXXClass(Class);

  PopFunctionScopeInfo();
}

ExprResult Sema::ActOnLambdaExpr(SourceLocation StartLoc,
                                 Stmt *Body, Scope *CurScope) {
  // Leave the expression-evaluation context.
  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();

  // Collect information from the lambda scope.
  llvm::SmallVector<LambdaExpr::Capture, 4> Captures;
  llvm::SmallVector<Expr *, 4> CaptureInits;
  LambdaCaptureDefault CaptureDefault;
  CXXRecordDecl *Class;
  SourceRange IntroducerRange;
  bool ExplicitParams;
  bool LambdaExprNeedsCleanups;
  {
    LambdaScopeInfo *LSI = getCurLambda();
    Class = LSI->Lambda;
    IntroducerRange = LSI->IntroducerRange;
    ExplicitParams = LSI->ExplicitParams;
    LambdaExprNeedsCleanups = LSI->ExprNeedsCleanups;

    // Translate captures.
    for (unsigned I = 0, N = LSI->Captures.size(); I != N; ++I) {
      LambdaScopeInfo::Capture From = LSI->Captures[I];
      assert(!From.isBlockCapture() && "Cannot capture __block variables");
      bool IsImplicit = I >= LSI->NumExplicitCaptures;

      // Handle 'this' capture.
      if (From.isThisCapture()) {
        Captures.push_back(LambdaExpr::Capture(From.getLocation(),
                                               IsImplicit,
                                               LCK_This));
        CaptureInits.push_back(new (Context) CXXThisExpr(From.getLocation(),
                                                         getCurrentThisType(),
                                                         /*isImplicit=*/true));
        continue;
      }

      VarDecl *Var = From.getVariable();
      // FIXME: Handle pack expansions.
      LambdaCaptureKind Kind = From.isCopyCapture()? LCK_ByCopy : LCK_ByRef;
      Captures.push_back(LambdaExpr::Capture(From.getLocation(), IsImplicit, 
                                             Kind, Var));
      CaptureInits.push_back(From.getCopyExpr());
    }

    switch (LSI->ImpCaptureStyle) {
    case CapturingScopeInfo::ImpCap_None:
      CaptureDefault = LCD_None;
      break;

    case CapturingScopeInfo::ImpCap_LambdaByval:
      CaptureDefault = LCD_ByCopy;
      break;

    case CapturingScopeInfo::ImpCap_LambdaByref:
      CaptureDefault = LCD_ByRef;
      break;

    case CapturingScopeInfo::ImpCap_Block:
      llvm_unreachable("block capture in lambda");
      break;
    }

    // Finalize the lambda class.
    SmallVector<Decl*, 4> Fields(Class->field_begin(), Class->field_end());
    ActOnFields(0, Class->getLocation(), Class, Fields, 
                SourceLocation(), SourceLocation(), 0);
    CheckCompletedCXXClass(Class);

    // C++ [expr.prim.lambda]p7:
    //   The lambda-expression's compound-statement yields the
    //   function-body (8.4) of the function call operator [...].
    ActOnFinishFunctionBody(LSI->CallOperator, Body, /*IsInstantation=*/false);
  }

  if (LambdaExprNeedsCleanups)
    ExprNeedsCleanups = true;

  LambdaExpr *Lambda = LambdaExpr::Create(Context, Class, IntroducerRange, 
                                          CaptureDefault, Captures, 
                                          ExplicitParams, CaptureInits, 
                                          Body->getLocEnd());

  // C++11 [expr.prim.lambda]p2:
  //   A lambda-expression shall not appear in an unevaluated operand
  //   (Clause 5).
  switch (ExprEvalContexts.back().Context) {
  case Unevaluated:
    // We don't actually diagnose this case immediately, because we
    // could be within a context where we might find out later that
    // the expression is potentially evaluated (e.g., for typeid).
    ExprEvalContexts.back().Lambdas.push_back(Lambda);
    break;

  case ConstantEvaluated:
  case PotentiallyEvaluated:
  case PotentiallyEvaluatedIfUsed:
    break;
  }

  return MaybeBindToTemporary(Lambda);
}
