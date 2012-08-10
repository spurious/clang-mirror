//===- Calls.cpp - Wrapper for all function and method calls ------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file defines CallEvent and its subclasses, which represent path-
/// sensitive instances of different kinds of function and method calls
/// (C, C++, and Objective-C).
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/ParentMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"

using namespace clang;
using namespace ento;

QualType CallEvent::getResultType() const {
  QualType ResultTy = getDeclaredResultType();

  if (ResultTy.isNull())
    ResultTy = getOriginExpr()->getType();

  return ResultTy;
}

static bool isCallbackArg(SVal V, QualType T) {
  // If the parameter is 0, it's harmless.
  if (V.isZeroConstant())
    return false;

  // If a parameter is a block or a callback, assume it can modify pointer.
  if (T->isBlockPointerType() ||
      T->isFunctionPointerType() ||
      T->isObjCSelType())
    return true;

  // Check if a callback is passed inside a struct (for both, struct passed by
  // reference and by value). Dig just one level into the struct for now.

  if (isa<PointerType>(T) || isa<ReferenceType>(T))
    T = T->getPointeeType();

  if (const RecordType *RT = T->getAsStructureType()) {
    const RecordDecl *RD = RT->getDecl();
    for (RecordDecl::field_iterator I = RD->field_begin(), E = RD->field_end();
         I != E; ++I) {
      QualType FieldT = I->getType();
      if (FieldT->isBlockPointerType() || FieldT->isFunctionPointerType())
        return true;
    }
  }

  return false;
}

bool CallEvent::hasNonZeroCallbackArg() const {
  unsigned NumOfArgs = getNumArgs();

  // If calling using a function pointer, assume the function does not
  // have a callback. TODO: We could check the types of the arguments here.
  if (!getDecl())
    return false;

  unsigned Idx = 0;
  for (CallEvent::param_type_iterator I = param_type_begin(),
                                       E = param_type_end();
       I != E && Idx < NumOfArgs; ++I, ++Idx) {
    if (NumOfArgs <= Idx)
      break;

    if (isCallbackArg(getArgSVal(Idx), *I))
      return true;
  }
  
  return false;
}

/// \brief Returns true if a type is a pointer-to-const or reference-to-const
/// with no further indirection.
static bool isPointerToConst(QualType Ty) {
  QualType PointeeTy = Ty->getPointeeType();
  if (PointeeTy == QualType())
    return false;
  if (!PointeeTy.isConstQualified())
    return false;
  if (PointeeTy->isAnyPointerType())
    return false;
  return true;
}

// Try to retrieve the function declaration and find the function parameter
// types which are pointers/references to a non-pointer const.
// We will not invalidate the corresponding argument regions.
static void findPtrToConstParams(llvm::SmallSet<unsigned, 1> &PreserveArgs,
                                 const CallEvent &Call) {
  unsigned Idx = 0;
  for (CallEvent::param_type_iterator I = Call.param_type_begin(),
                                      E = Call.param_type_end();
       I != E; ++I, ++Idx) {
    if (isPointerToConst(*I))
      PreserveArgs.insert(Idx);
  }
}

ProgramStateRef CallEvent::invalidateRegions(unsigned BlockCount,
                                              ProgramStateRef Orig) const {
  ProgramStateRef Result = (Orig ? Orig : getState());

  SmallVector<const MemRegion *, 8> RegionsToInvalidate;
  getExtraInvalidatedRegions(RegionsToInvalidate);

  // Indexes of arguments whose values will be preserved by the call.
  llvm::SmallSet<unsigned, 1> PreserveArgs;
  if (!argumentsMayEscape())
    findPtrToConstParams(PreserveArgs, *this);

  for (unsigned Idx = 0, Count = getNumArgs(); Idx != Count; ++Idx) {
    if (PreserveArgs.count(Idx))
      continue;

    SVal V = getArgSVal(Idx);

    // If we are passing a location wrapped as an integer, unwrap it and
    // invalidate the values referred by the location.
    if (nonloc::LocAsInteger *Wrapped = dyn_cast<nonloc::LocAsInteger>(&V))
      V = Wrapped->getLoc();
    else if (!isa<Loc>(V))
      continue;

    if (const MemRegion *R = V.getAsRegion()) {
      // Invalidate the value of the variable passed by reference.

      // Are we dealing with an ElementRegion?  If the element type is
      // a basic integer type (e.g., char, int) and the underlying region
      // is a variable region then strip off the ElementRegion.
      // FIXME: We really need to think about this for the general case
      //   as sometimes we are reasoning about arrays and other times
      //   about (char*), etc., is just a form of passing raw bytes.
      //   e.g., void *p = alloca(); foo((char*)p);
      if (const ElementRegion *ER = dyn_cast<ElementRegion>(R)) {
        // Checking for 'integral type' is probably too promiscuous, but
        // we'll leave it in for now until we have a systematic way of
        // handling all of these cases.  Eventually we need to come up
        // with an interface to StoreManager so that this logic can be
        // appropriately delegated to the respective StoreManagers while
        // still allowing us to do checker-specific logic (e.g.,
        // invalidating reference counts), probably via callbacks.
        if (ER->getElementType()->isIntegralOrEnumerationType()) {
          const MemRegion *superReg = ER->getSuperRegion();
          if (isa<VarRegion>(superReg) || isa<FieldRegion>(superReg) ||
              isa<ObjCIvarRegion>(superReg))
            R = cast<TypedRegion>(superReg);
        }
        // FIXME: What about layers of ElementRegions?
      }

      // Mark this region for invalidation.  We batch invalidate regions
      // below for efficiency.
      RegionsToInvalidate.push_back(R);
    }
  }

  // Invalidate designated regions using the batch invalidation API.
  // NOTE: Even if RegionsToInvalidate is empty, we may still invalidate
  //  global variables.
  return Result->invalidateRegions(RegionsToInvalidate, getOriginExpr(),
                                   BlockCount, getLocationContext(),
                                   /*Symbols=*/0, this);
}

ProgramPoint CallEvent::getProgramPoint(bool IsPreVisit,
                                        const ProgramPointTag *Tag) const {
  if (const Expr *E = getOriginExpr()) {
    if (IsPreVisit)
      return PreStmt(E, getLocationContext(), Tag);
    return PostStmt(E, getLocationContext(), Tag);
  }

  const Decl *D = getDecl();
  assert(D && "Cannot get a program point without a statement or decl");  

  SourceLocation Loc = getSourceRange().getBegin();
  if (IsPreVisit)
    return PreImplicitCall(D, Loc, getLocationContext(), Tag);
  return PostImplicitCall(D, Loc, getLocationContext(), Tag);
}

SVal CallEvent::getArgSVal(unsigned Index) const {
  const Expr *ArgE = getArgExpr(Index);
  if (!ArgE)
    return UnknownVal();
  return getSVal(ArgE);
}

SourceRange CallEvent::getArgSourceRange(unsigned Index) const {
  const Expr *ArgE = getArgExpr(Index);
  if (!ArgE)
    return SourceRange();
  return ArgE->getSourceRange();
}

void CallEvent::dump(raw_ostream &Out) const {
  ASTContext &Ctx = getState()->getStateManager().getContext();
  if (const Expr *E = getOriginExpr()) {
    E->printPretty(Out, Ctx, 0, Ctx.getPrintingPolicy());
    Out << "\n";
    return;
  }

  if (const Decl *D = getDecl()) {
    Out << "Call to ";
    D->print(Out, Ctx.getPrintingPolicy());
    return;
  }

  // FIXME: a string representation of the kind would be nice.
  Out << "Unknown call (type " << getKind() << ")";
}


bool CallEvent::mayBeInlined(const Stmt *S) {
  // FIXME: Kill this.
  return isa<CallExpr>(S) || isa<ObjCMessageExpr>(S)
                          || isa<CXXConstructExpr>(S);
}

static void addParameterValuesToBindings(const StackFrameContext *CalleeCtx,
                                         CallEvent::BindingsTy &Bindings,
                                         SValBuilder &SVB,
                                         const CallEvent &Call,
                                         CallEvent::param_iterator I,
                                         CallEvent::param_iterator E) {
  MemRegionManager &MRMgr = SVB.getRegionManager();

  unsigned Idx = 0;
  for (; I != E; ++I, ++Idx) {
    const ParmVarDecl *ParamDecl = *I;
    assert(ParamDecl && "Formal parameter has no decl?");

    SVal ArgVal = Call.getArgSVal(Idx);
    if (!ArgVal.isUnknown()) {
      Loc ParamLoc = SVB.makeLoc(MRMgr.getVarRegion(ParamDecl, CalleeCtx));
      Bindings.push_back(std::make_pair(ParamLoc, ArgVal));
    }
  }

  // FIXME: Variadic arguments are not handled at all right now.
}


CallEvent::param_iterator AnyFunctionCall::param_begin() const {
  const FunctionDecl *D = getDecl();
  if (!D)
    return 0;

  return D->param_begin();
}

CallEvent::param_iterator AnyFunctionCall::param_end() const {
  const FunctionDecl *D = getDecl();
  if (!D)
    return 0;

  return D->param_end();
}

void AnyFunctionCall::getInitialStackFrameContents(
                                        const StackFrameContext *CalleeCtx,
                                        BindingsTy &Bindings) const {
  const FunctionDecl *D = cast<FunctionDecl>(CalleeCtx->getDecl());
  SValBuilder &SVB = getState()->getStateManager().getSValBuilder();
  addParameterValuesToBindings(CalleeCtx, Bindings, SVB, *this,
                               D->param_begin(), D->param_end());
}

QualType AnyFunctionCall::getDeclaredResultType() const {
  const FunctionDecl *D = getDecl();
  if (!D)
    return QualType();

  return D->getResultType();
}

bool AnyFunctionCall::argumentsMayEscape() const {
  if (hasNonZeroCallbackArg())
    return true;

  const FunctionDecl *D = getDecl();
  if (!D)
    return true;

  const IdentifierInfo *II = D->getIdentifier();
  if (!II)
    return true;

  // This set of "escaping" APIs is 

  // - 'int pthread_setspecific(ptheread_key k, const void *)' stores a
  //   value into thread local storage. The value can later be retrieved with
  //   'void *ptheread_getspecific(pthread_key)'. So even thought the
  //   parameter is 'const void *', the region escapes through the call.
  if (II->isStr("pthread_setspecific"))
    return true;

  // - xpc_connection_set_context stores a value which can be retrieved later
  //   with xpc_connection_get_context.
  if (II->isStr("xpc_connection_set_context"))
    return true;

  // - funopen - sets a buffer for future IO calls.
  if (II->isStr("funopen"))
    return true;

  StringRef FName = II->getName();

  // - CoreFoundation functions that end with "NoCopy" can free a passed-in
  //   buffer even if it is const.
  if (FName.endswith("NoCopy"))
    return true;

  // - NSXXInsertXX, for example NSMapInsertIfAbsent, since they can
  //   be deallocated by NSMapRemove.
  if (FName.startswith("NS") && (FName.find("Insert") != StringRef::npos))
    return true;

  // - Many CF containers allow objects to escape through custom
  //   allocators/deallocators upon container construction. (PR12101)
  if (FName.startswith("CF") || FName.startswith("CG")) {
    return StrInStrNoCase(FName, "InsertValue")  != StringRef::npos ||
           StrInStrNoCase(FName, "AddValue")     != StringRef::npos ||
           StrInStrNoCase(FName, "SetValue")     != StringRef::npos ||
           StrInStrNoCase(FName, "WithData")     != StringRef::npos ||
           StrInStrNoCase(FName, "AppendValue")  != StringRef::npos ||
           StrInStrNoCase(FName, "SetAttribute") != StringRef::npos;
  }

  return false;
}


const FunctionDecl *SimpleCall::getDecl() const {
  const FunctionDecl *D = getOriginExpr()->getDirectCallee();
  if (D)
    return D;

  return getSVal(getOriginExpr()->getCallee()).getAsFunctionDecl();
}


void CXXInstanceCall::getExtraInvalidatedRegions(RegionList &Regions) const {
  if (const MemRegion *R = getCXXThisVal().getAsRegion())
    Regions.push_back(R);
}

static const CXXMethodDecl *devirtualize(const CXXMethodDecl *MD, SVal ThisVal){
  const MemRegion *R = ThisVal.getAsRegion();
  if (!R)
    return 0;

  const TypedValueRegion *TR = dyn_cast<TypedValueRegion>(R->StripCasts());
  if (!TR)
    return 0;

  const CXXRecordDecl *RD = TR->getValueType()->getAsCXXRecordDecl();
  if (!RD)
    return 0;

  const CXXMethodDecl *Result = MD->getCorrespondingMethodInClass(RD);
  const FunctionDecl *Definition;
  if (!Result->hasBody(Definition))
    return 0;

  return cast<CXXMethodDecl>(Definition);
}


RuntimeDefinition CXXInstanceCall::getRuntimeDefinition() const {
  const Decl *D = getDecl();
  if (!D)
    return RuntimeDefinition();

  const CXXMethodDecl *MD = cast<CXXMethodDecl>(D);
  if (!MD->isVirtual())
    return SimpleCall::getRuntimeDefinition();

  // If the method is virtual, see if we can find the actual implementation
  // based on context-sensitivity.
  // FIXME: Virtual method calls behave differently when an object is being
  // constructed or destructed. It's not as simple as "no devirtualization"
  // because a /partially/ constructed object can be referred to through a
  // base pointer. We'll eventually want to use DynamicTypeInfo here.
  if (const CXXMethodDecl *Devirtualized = devirtualize(MD, getCXXThisVal()))
    return RuntimeDefinition(Devirtualized);

  return RuntimeDefinition();
}

void CXXInstanceCall::getInitialStackFrameContents(
                                            const StackFrameContext *CalleeCtx,
                                            BindingsTy &Bindings) const {
  AnyFunctionCall::getInitialStackFrameContents(CalleeCtx, Bindings);

  // Handle the binding of 'this' in the new stack frame.
  // We need to make sure we have the proper layering of CXXBaseObjectRegions.
  SVal ThisVal = getCXXThisVal();
  if (!ThisVal.isUnknown()) {
    ProgramStateManager &StateMgr = getState()->getStateManager();
    SValBuilder &SVB = StateMgr.getSValBuilder();
    
    const CXXMethodDecl *MD = cast<CXXMethodDecl>(CalleeCtx->getDecl());
    Loc ThisLoc = SVB.getCXXThis(MD, CalleeCtx);

    if (const MemRegion *ThisReg = ThisVal.getAsRegion()) {
      const CXXRecordDecl *Class = MD->getParent();

      // We may be downcasting to call a devirtualized virtual method.
      // Search through the base casts we already have to see if we can just
      // strip them off.
      const CXXBaseObjectRegion *BaseReg;
      while ((BaseReg = dyn_cast<CXXBaseObjectRegion>(ThisReg))) {
        if (BaseReg->getDecl() == Class)
          break;
        ThisReg = BaseReg->getSuperRegion();
      }

      // Either we found the right base class, or we stripped all the casts to
      // the most derived type. Either one is good.
      ThisVal = loc::MemRegionVal(ThisReg);
    }

    Bindings.push_back(std::make_pair(ThisLoc, ThisVal));
  }
}



const Expr *CXXMemberCall::getCXXThisExpr() const {
  return getOriginExpr()->getImplicitObjectArgument();
}


const Expr *CXXMemberOperatorCall::getCXXThisExpr() const {
  return getOriginExpr()->getArg(0);
}


const BlockDataRegion *BlockCall::getBlockRegion() const {
  const Expr *Callee = getOriginExpr()->getCallee();
  const MemRegion *DataReg = getSVal(Callee).getAsRegion();

  return dyn_cast_or_null<BlockDataRegion>(DataReg);
}

CallEvent::param_iterator BlockCall::param_begin() const {
  const BlockDecl *D = getBlockDecl();
  if (!D)
    return 0;
  return D->param_begin();
}

CallEvent::param_iterator BlockCall::param_end() const {
  const BlockDecl *D = getBlockDecl();
  if (!D)
    return 0;
  return D->param_end();
}

void BlockCall::getExtraInvalidatedRegions(RegionList &Regions) const {
  // FIXME: This also needs to invalidate captured globals.
  if (const MemRegion *R = getBlockRegion())
    Regions.push_back(R);
}

void BlockCall::getInitialStackFrameContents(const StackFrameContext *CalleeCtx,
                                             BindingsTy &Bindings) const {
  const BlockDecl *D = cast<BlockDecl>(CalleeCtx->getDecl());
  SValBuilder &SVB = getState()->getStateManager().getSValBuilder();
  addParameterValuesToBindings(CalleeCtx, Bindings, SVB, *this,
                               D->param_begin(), D->param_end());
}


QualType BlockCall::getDeclaredResultType() const {
  const BlockDataRegion *BR = getBlockRegion();
  if (!BR)
    return QualType();
  QualType BlockTy = BR->getCodeRegion()->getLocationType();
  return cast<FunctionType>(BlockTy->getPointeeType())->getResultType();
}


SVal CXXConstructorCall::getCXXThisVal() const {
  if (Data)
    return loc::MemRegionVal(static_cast<const MemRegion *>(Data));
  return UnknownVal();
}

void CXXConstructorCall::getExtraInvalidatedRegions(RegionList &Regions) const {
  if (Data)
    Regions.push_back(static_cast<const MemRegion *>(Data));
}

void CXXConstructorCall::getInitialStackFrameContents(
                                             const StackFrameContext *CalleeCtx,
                                             BindingsTy &Bindings) const {
  AnyFunctionCall::getInitialStackFrameContents(CalleeCtx, Bindings);

  SVal ThisVal = getCXXThisVal();
  if (!ThisVal.isUnknown()) {
    SValBuilder &SVB = getState()->getStateManager().getSValBuilder();
    const CXXMethodDecl *MD = cast<CXXMethodDecl>(CalleeCtx->getDecl());
    Loc ThisLoc = SVB.getCXXThis(MD, CalleeCtx);
    Bindings.push_back(std::make_pair(ThisLoc, ThisVal));
  }
}



SVal CXXDestructorCall::getCXXThisVal() const {
  if (Data)
    return loc::MemRegionVal(static_cast<const MemRegion *>(Data));
  return UnknownVal();
}

void CXXDestructorCall::getExtraInvalidatedRegions(RegionList &Regions) const {
  if (Data)
    Regions.push_back(static_cast<const MemRegion *>(Data));
}

RuntimeDefinition CXXDestructorCall::getRuntimeDefinition() const {
  const Decl *D = AnyFunctionCall::getRuntimeDefinition().getDecl();
  if (!D)
    return RuntimeDefinition();

  const CXXMethodDecl *MD = cast<CXXMethodDecl>(D);
  if (!MD->isVirtual())
    return RuntimeDefinition(MD);

  // If the method is virtual, see if we can find the actual implementation
  // based on context-sensitivity.
  // FIXME: Virtual method calls behave differently when an object is being
  // constructed or destructed. It's not as simple as "no devirtualization"
  // because a /partially/ constructed object can be referred to through a
  // base pointer. We'll eventually want to use DynamicTypeInfo here.
  if (const CXXMethodDecl *Devirtualized = devirtualize(MD, getCXXThisVal()))
    return RuntimeDefinition(Devirtualized);

  return RuntimeDefinition();
}

void CXXDestructorCall::getInitialStackFrameContents(
                                             const StackFrameContext *CalleeCtx,
                                             BindingsTy &Bindings) const {
  AnyFunctionCall::getInitialStackFrameContents(CalleeCtx, Bindings);

  SVal ThisVal = getCXXThisVal();
  if (!ThisVal.isUnknown()) {
    SValBuilder &SVB = getState()->getStateManager().getSValBuilder();
    const CXXMethodDecl *MD = cast<CXXMethodDecl>(CalleeCtx->getDecl());
    Loc ThisLoc = SVB.getCXXThis(MD, CalleeCtx);
    Bindings.push_back(std::make_pair(ThisLoc, ThisVal));
  }
}


CallEvent::param_iterator ObjCMethodCall::param_begin() const {
  const ObjCMethodDecl *D = getDecl();
  if (!D)
    return 0;

  return D->param_begin();
}

CallEvent::param_iterator ObjCMethodCall::param_end() const {
  const ObjCMethodDecl *D = getDecl();
  if (!D)
    return 0;

  return D->param_end();
}

void
ObjCMethodCall::getExtraInvalidatedRegions(RegionList &Regions) const {
  if (const MemRegion *R = getReceiverSVal().getAsRegion())
    Regions.push_back(R);
}

QualType ObjCMethodCall::getDeclaredResultType() const {
  const ObjCMethodDecl *D = getDecl();
  if (!D)
    return QualType();

  return D->getResultType();
}

SVal ObjCMethodCall::getReceiverSVal() const {
  // FIXME: Is this the best way to handle class receivers?
  if (!isInstanceMessage())
    return UnknownVal();
    
  if (const Expr *RecE = getOriginExpr()->getInstanceReceiver())
    return getSVal(RecE);

  // An instance message with no expression means we are sending to super.
  // In this case the object reference is the same as 'self'.
  const LocationContext *LCtx = getLocationContext();
  const ImplicitParamDecl *SelfDecl = LCtx->getSelfDecl();
  assert(SelfDecl && "No message receiver Expr, but not in an ObjC method");
  return getState()->getSVal(getState()->getRegion(SelfDecl, LCtx));
}

SourceRange ObjCMethodCall::getSourceRange() const {
  switch (getMessageKind()) {
  case OCM_Message:
    return getOriginExpr()->getSourceRange();
  case OCM_PropertyAccess:
  case OCM_Subscript:
    return getContainingPseudoObjectExpr()->getSourceRange();
  }
  llvm_unreachable("unknown message kind");
}

typedef llvm::PointerIntPair<const PseudoObjectExpr *, 2> ObjCMessageDataTy;

const PseudoObjectExpr *ObjCMethodCall::getContainingPseudoObjectExpr() const {
  assert(Data != 0 && "Lazy lookup not yet performed.");
  assert(getMessageKind() != OCM_Message && "Explicit message send.");
  return ObjCMessageDataTy::getFromOpaqueValue(Data).getPointer();
}

ObjCMessageKind ObjCMethodCall::getMessageKind() const {
  if (Data == 0) {
    ParentMap &PM = getLocationContext()->getParentMap();
    const Stmt *S = PM.getParent(getOriginExpr());
    if (const PseudoObjectExpr *POE = dyn_cast_or_null<PseudoObjectExpr>(S)) {
      const Expr *Syntactic = POE->getSyntacticForm();

      // This handles the funny case of assigning to the result of a getter.
      // This can happen if the getter returns a non-const reference.
      if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(Syntactic))
        Syntactic = BO->getLHS();

      ObjCMessageKind K;
      switch (Syntactic->getStmtClass()) {
      case Stmt::ObjCPropertyRefExprClass:
        K = OCM_PropertyAccess;
        break;
      case Stmt::ObjCSubscriptRefExprClass:
        K = OCM_Subscript;
        break;
      default:
        // FIXME: Can this ever happen?
        K = OCM_Message;
        break;
      }

      if (K != OCM_Message) {
        const_cast<ObjCMethodCall *>(this)->Data
          = ObjCMessageDataTy(POE, K).getOpaqueValue();
        assert(getMessageKind() == K);
        return K;
      }
    }
    
    const_cast<ObjCMethodCall *>(this)->Data
      = ObjCMessageDataTy(0, 1).getOpaqueValue();
    assert(getMessageKind() == OCM_Message);
    return OCM_Message;
  }

  ObjCMessageDataTy Info = ObjCMessageDataTy::getFromOpaqueValue(Data);
  if (!Info.getPointer())
    return OCM_Message;
  return static_cast<ObjCMessageKind>(Info.getInt());
}


bool ObjCMethodCall::canBeOverridenInSubclass(ObjCInterfaceDecl *IDecl,
                                             Selector Sel) const {
  assert(IDecl);
  const SourceManager &SM =
    getState()->getStateManager().getContext().getSourceManager();

  // If the class interface is declared inside the main file, assume it is not
  // subcassed. 
  // TODO: It could actually be subclassed if the subclass is private as well.
  // This is probably very rare.
  SourceLocation InterfLoc = IDecl->getEndOfDefinitionLoc();
  if (InterfLoc.isValid() && SM.isFromMainFile(InterfLoc))
    return false;


  // We assume that if the method is public (declared outside of main file) or
  // has a parent which publicly declares the method, the method could be
  // overridden in a subclass.

  // Find the first declaration in the class hierarchy that declares
  // the selector.
  ObjCMethodDecl *D = 0;
  while (true) {
    D = IDecl->lookupMethod(Sel, true);

    // Cannot find a public definition.
    if (!D)
      return false;

    // If outside the main file,
    if (D->getLocation().isValid() && !SM.isFromMainFile(D->getLocation()))
      return true;

    if (D->isOverriding()) {
      // Search in the superclass on the next iteration.
      IDecl = D->getClassInterface();
      if (!IDecl)
        return false;

      IDecl = IDecl->getSuperClass();
      if (!IDecl)
        return false;

      continue;
    }

    return false;
  };

  llvm_unreachable("The while loop should always terminate.");
}

RuntimeDefinition ObjCMethodCall::getRuntimeDefinition() const {
  const ObjCMessageExpr *E = getOriginExpr();
  assert(E);
  Selector Sel = E->getSelector();

  if (E->isInstanceMessage()) {

    // Find the the receiver type.
    const ObjCObjectPointerType *ReceiverT = 0;
    bool CanBeSubClassed = false;
    QualType SupersType = E->getSuperType();
    const MemRegion *Receiver = 0;

    if (!SupersType.isNull()) {
      // Super always means the type of immediate predecessor to the method
      // where the call occurs.
      ReceiverT = cast<ObjCObjectPointerType>(SupersType);
    } else {
      Receiver = getReceiverSVal().getAsRegion();
      if (!Receiver)
        return RuntimeDefinition();

      DynamicTypeInfo DTI = getState()->getDynamicTypeInfo(Receiver);
      QualType DynType = DTI.getType();
      CanBeSubClassed = DTI.canBeASubClass();
      ReceiverT = dyn_cast<ObjCObjectPointerType>(DynType);

      if (ReceiverT && CanBeSubClassed)
        if (ObjCInterfaceDecl *IDecl = ReceiverT->getInterfaceDecl())
          if (!canBeOverridenInSubclass(IDecl, Sel))
            CanBeSubClassed = false;
    }

    // Lookup the method implementation.
    if (ReceiverT)
      if (ObjCInterfaceDecl *IDecl = ReceiverT->getInterfaceDecl()) {
        const ObjCMethodDecl *MD = IDecl->lookupPrivateMethod(Sel);
        if (CanBeSubClassed)
          return RuntimeDefinition(MD, Receiver);
        else
          return RuntimeDefinition(MD, 0);
      }

  } else {
    // This is a class method.
    // If we have type info for the receiver class, we are calling via
    // class name.
    if (ObjCInterfaceDecl *IDecl = E->getReceiverInterface()) {
      // Find/Return the method implementation.
      return RuntimeDefinition(IDecl->lookupPrivateClassMethod(Sel));
    }
  }

  return RuntimeDefinition();
}

void ObjCMethodCall::getInitialStackFrameContents(
                                             const StackFrameContext *CalleeCtx,
                                             BindingsTy &Bindings) const {
  const ObjCMethodDecl *D = cast<ObjCMethodDecl>(CalleeCtx->getDecl());
  SValBuilder &SVB = getState()->getStateManager().getSValBuilder();
  addParameterValuesToBindings(CalleeCtx, Bindings, SVB, *this,
                               D->param_begin(), D->param_end());

  SVal SelfVal = getReceiverSVal();
  if (!SelfVal.isUnknown()) {
    const VarDecl *SelfD = CalleeCtx->getAnalysisDeclContext()->getSelfDecl();
    MemRegionManager &MRMgr = SVB.getRegionManager();
    Loc SelfLoc = SVB.makeLoc(MRMgr.getVarRegion(SelfD, CalleeCtx));
    Bindings.push_back(std::make_pair(SelfLoc, SelfVal));
  }
}

CallEventRef<SimpleCall>
CallEventManager::getSimpleCall(const CallExpr *CE, ProgramStateRef State,
                                const LocationContext *LCtx) {
  if (const CXXMemberCallExpr *MCE = dyn_cast<CXXMemberCallExpr>(CE))
    return create<CXXMemberCall>(MCE, State, LCtx);

  if (const CXXOperatorCallExpr *OpCE = dyn_cast<CXXOperatorCallExpr>(CE)) {
    const FunctionDecl *DirectCallee = OpCE->getDirectCallee();
    if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(DirectCallee))
      if (MD->isInstance())
        return create<CXXMemberOperatorCall>(OpCE, State, LCtx);

  } else if (CE->getCallee()->getType()->isBlockPointerType()) {
    return create<BlockCall>(CE, State, LCtx);
  }

  // Otherwise, it's a normal function call, static member function call, or
  // something we can't reason about.
  return create<FunctionCall>(CE, State, LCtx);
}


CallEventRef<>
CallEventManager::getCaller(const StackFrameContext *CalleeCtx,
                            ProgramStateRef State) {
  const LocationContext *ParentCtx = CalleeCtx->getParent();
  const LocationContext *CallerCtx = ParentCtx->getCurrentStackFrame();
  assert(CallerCtx && "This should not be used for top-level stack frames");

  const Stmt *CallSite = CalleeCtx->getCallSite();

  if (CallSite) {
    if (const CallExpr *CE = dyn_cast<CallExpr>(CallSite))
      return getSimpleCall(CE, State, CallerCtx);

    switch (CallSite->getStmtClass()) {
    case Stmt::CXXConstructExprClass: {
      SValBuilder &SVB = State->getStateManager().getSValBuilder();
      const CXXMethodDecl *Ctor = cast<CXXMethodDecl>(CalleeCtx->getDecl());
      Loc ThisPtr = SVB.getCXXThis(Ctor, CalleeCtx);
      SVal ThisVal = State->getSVal(ThisPtr);

      return getCXXConstructorCall(cast<CXXConstructExpr>(CallSite),
                                   ThisVal.getAsRegion(), State, CallerCtx);
    }
    case Stmt::CXXNewExprClass:
      return getCXXAllocatorCall(cast<CXXNewExpr>(CallSite), State, CallerCtx);
    case Stmt::ObjCMessageExprClass:
      return getObjCMethodCall(cast<ObjCMessageExpr>(CallSite),
                               State, CallerCtx);
    default:
      llvm_unreachable("This is not an inlineable statement.");
    }
  }

  // Fall back to the CFG. The only thing we haven't handled yet is
  // destructors, though this could change in the future.
  const CFGBlock *B = CalleeCtx->getCallSiteBlock();
  CFGElement E = (*B)[CalleeCtx->getIndex()];
  assert(isa<CFGImplicitDtor>(E) && "All other CFG elements should have exprs");
  assert(!isa<CFGTemporaryDtor>(E) && "We don't handle temporaries yet");

  SValBuilder &SVB = State->getStateManager().getSValBuilder();
  const CXXDestructorDecl *Dtor = cast<CXXDestructorDecl>(CalleeCtx->getDecl());
  Loc ThisPtr = SVB.getCXXThis(Dtor, CalleeCtx);
  SVal ThisVal = State->getSVal(ThisPtr);

  const Stmt *Trigger;
  if (const CFGAutomaticObjDtor *AutoDtor = dyn_cast<CFGAutomaticObjDtor>(&E))
    Trigger = AutoDtor->getTriggerStmt();
  else
    Trigger = Dtor->getBody();

  return getCXXDestructorCall(Dtor, Trigger, ThisVal.getAsRegion(),
                              State, CallerCtx);
}

