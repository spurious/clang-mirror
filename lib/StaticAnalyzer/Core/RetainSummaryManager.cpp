//== RetainSummaryManager.cpp - Summaries for reference counting --*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines summaries implementation for retain counting, which
//  implements a reference count checker for Core Foundation, Cocoa
//  and OSObject (on Mac OS X).
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/RetainSummaryManager.h"
#include "clang/Analysis/DomainSpecific/CocoaConventions.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ParentMap.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang;
using namespace ento;

template <class T>
constexpr static bool isOneOf() {
  return false;
}

/// Helper function to check whether the class is one of the
/// rest of varargs.
template <class T, class P, class... ToCompare>
constexpr static bool isOneOf() {
  return std::is_same<T, P>::value || isOneOf<T, ToCompare...>();
}

template <class T> bool RetainSummaryManager::isAttrEnabled() {
  if (isOneOf<T, CFConsumedAttr, CFReturnsRetainedAttr,
              CFReturnsNotRetainedAttr, NSConsumedAttr, NSConsumesSelfAttr,
              NSReturnsAutoreleasedAttr, NSReturnsRetainedAttr,
              NSReturnsNotRetainedAttr>()) {
    return TrackObjCAndCFObjects;
  } else if (isOneOf<T, OSConsumedAttr, OSConsumesThisAttr,
                     OSReturnsNotRetainedAttr, OSReturnsRetainedAttr>()) {
    return TrackOSObjects;
  }
  llvm_unreachable("Unexpected attribute passed");
}

const RetainSummary *
RetainSummaryManager::getPersistentSummary(const RetainSummary &OldSumm) {
  // Unique "simple" summaries -- those without ArgEffects.
  if (OldSumm.isSimple()) {
    ::llvm::FoldingSetNodeID ID;
    OldSumm.Profile(ID);

    void *Pos;
    CachedSummaryNode *N = SimpleSummaries.FindNodeOrInsertPos(ID, Pos);

    if (!N) {
      N = (CachedSummaryNode *) BPAlloc.Allocate<CachedSummaryNode>();
      new (N) CachedSummaryNode(OldSumm);
      SimpleSummaries.InsertNode(N, Pos);
    }

    return &N->getValue();
  }

  RetainSummary *Summ = (RetainSummary *) BPAlloc.Allocate<RetainSummary>();
  new (Summ) RetainSummary(OldSumm);
  return Summ;
}

static bool isSubclass(const Decl *D,
                       StringRef ClassName) {
  using namespace ast_matchers;
  DeclarationMatcher SubclassM = cxxRecordDecl(isSameOrDerivedFrom(ClassName));
  return !(match(SubclassM, *D, D->getASTContext()).empty());
}

static bool isOSObjectSubclass(const Decl *D) {
  return isSubclass(D, "OSObject");
}

static bool isOSObjectDynamicCast(StringRef S) {
  return S == "safeMetaCast";
}

static bool isOSIteratorSubclass(const Decl *D) {
  return isSubclass(D, "OSIterator");
}

static bool hasRCAnnotation(const Decl *D, StringRef rcAnnotation) {
  for (const auto *Ann : D->specific_attrs<AnnotateAttr>()) {
    if (Ann->getAnnotation() == rcAnnotation)
      return true;
  }
  return false;
}

static bool isRetain(const FunctionDecl *FD, StringRef FName) {
  return FName.startswith_lower("retain") || FName.endswith_lower("retain");
}

static bool isRelease(const FunctionDecl *FD, StringRef FName) {
  return FName.startswith_lower("release") || FName.endswith_lower("release");
}

static bool isAutorelease(const FunctionDecl *FD, StringRef FName) {
  return FName.startswith_lower("autorelease") ||
         FName.endswith_lower("autorelease");
}

static bool isMakeCollectable(StringRef FName) {
  return FName.contains_lower("MakeCollectable");
}

/// A function is OSObject related if it is declared on a subclass
/// of OSObject, or any of the parameters is a subclass of an OSObject.
static bool isOSObjectRelated(const CXXMethodDecl *MD) {
  if (isOSObjectSubclass(MD->getParent()))
    return true;

  for (ParmVarDecl *Param : MD->parameters()) {
    QualType PT = Param->getType()->getPointeeType();
    if (!PT.isNull())
      if (CXXRecordDecl *RD = PT->getAsCXXRecordDecl())
        if (isOSObjectSubclass(RD))
          return true;
  }

  return false;
}

const RetainSummary *
RetainSummaryManager::getSummaryForOSObject(const FunctionDecl *FD,
                                            StringRef FName, QualType RetTy) {
  if (RetTy->isPointerType()) {
    const CXXRecordDecl *PD = RetTy->getPointeeType()->getAsCXXRecordDecl();
    if (PD && isOSObjectSubclass(PD)) {
      if (const IdentifierInfo *II = FD->getIdentifier()) {
        if (isOSObjectDynamicCast(II->getName()))
          return getDefaultSummary();

        // All objects returned with functions *not* starting with
        // get, or iterators, are returned at +1.
        if ((!II->getName().startswith("get") &&
             !II->getName().startswith("Get")) ||
            isOSIteratorSubclass(PD)) {
          return getOSSummaryCreateRule(FD);
        } else {
          return getOSSummaryGetRule(FD);
        }
      }
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
    const CXXRecordDecl *Parent = MD->getParent();
    if (TrackOSObjects && Parent && isOSObjectSubclass(Parent)) {
      if (FName == "release")
        return getOSSummaryReleaseRule(FD);

      if (FName == "retain")
        return getOSSummaryRetainRule(FD);

      if (FName == "free")
        return getOSSummaryFreeRule(FD);

      if (MD->getOverloadedOperator() == OO_New)
        return getOSSummaryCreateRule(MD);
    }
  }

  return nullptr;
}

const RetainSummary *RetainSummaryManager::getSummaryForObjCOrCFObject(
    const FunctionDecl *FD,
    StringRef FName,
    QualType RetTy,
    const FunctionType *FT,
    bool &AllowAnnotations) {

  ArgEffects ScratchArgs(AF.getEmptyMap());

  std::string RetTyName = RetTy.getAsString();
  if (FName == "pthread_create" || FName == "pthread_setspecific") {
    // Part of: <rdar://problem/7299394> and <rdar://problem/11282706>.
    // This will be addressed better with IPA.
    return getPersistentStopSummary();
  } else if(FName == "NSMakeCollectable") {
    // Handle: id NSMakeCollectable(CFTypeRef)
    AllowAnnotations = false;
    return RetTy->isObjCIdType() ? getUnarySummary(FT, cfmakecollectable)
                                 : getPersistentStopSummary();
  } else if (FName == "CMBufferQueueDequeueAndRetain" ||
             FName == "CMBufferQueueDequeueIfDataReadyAndRetain") {
    // Part of: <rdar://problem/39390714>.
    return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF),
                                ScratchArgs,
                                DoNothing,
                                DoNothing);
  } else if (FName == "CFPlugInInstanceCreate") {
    return getPersistentSummary(RetEffect::MakeNoRet(), ScratchArgs);
  } else if (FName == "IORegistryEntrySearchCFProperty" ||
             (RetTyName == "CFMutableDictionaryRef" &&
              (FName == "IOBSDNameMatching" || FName == "IOServiceMatching" ||
               FName == "IOServiceNameMatching" ||
               FName == "IORegistryEntryIDMatching" ||
               FName == "IOOpenFirmwarePathMatching"))) {
    // Part of <rdar://problem/6961230>. (IOKit)
    // This should be addressed using a API table.
    return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF),
                                ScratchArgs, DoNothing, DoNothing);
  } else if (FName == "IOServiceGetMatchingService" ||
             FName == "IOServiceGetMatchingServices") {
    // FIXES: <rdar://problem/6326900>
    // This should be addressed using a API table.  This strcmp is also
    // a little gross, but there is no need to super optimize here.
    ScratchArgs = AF.add(ScratchArgs, 1, DecRef);
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                DoNothing, DoNothing);
  } else if (FName == "IOServiceAddNotification" ||
             FName == "IOServiceAddMatchingNotification") {
    // Part of <rdar://problem/6961230>. (IOKit)
    // This should be addressed using a API table.
    ScratchArgs = AF.add(ScratchArgs, 2, DecRef);
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                DoNothing, DoNothing);
  } else if (FName == "CVPixelBufferCreateWithBytes") {
    // FIXES: <rdar://problem/7283567>
    // Eventually this can be improved by recognizing that the pixel
    // buffer passed to CVPixelBufferCreateWithBytes is released via
    // a callback and doing full IPA to make sure this is done correctly.
    // FIXME: This function has an out parameter that returns an
    // allocated object.
    ScratchArgs = AF.add(ScratchArgs, 7, StopTracking);
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                DoNothing, DoNothing);
  } else if (FName == "CGBitmapContextCreateWithData") {
    // FIXES: <rdar://problem/7358899>
    // Eventually this can be improved by recognizing that 'releaseInfo'
    // passed to CGBitmapContextCreateWithData is released via
    // a callback and doing full IPA to make sure this is done correctly.
    ScratchArgs = AF.add(ScratchArgs, 8, StopTracking);
    return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF),
                                ScratchArgs, DoNothing, DoNothing);
  } else if (FName == "CVPixelBufferCreateWithPlanarBytes") {
    // FIXES: <rdar://problem/7283567>
    // Eventually this can be improved by recognizing that the pixel
    // buffer passed to CVPixelBufferCreateWithPlanarBytes is released
    // via a callback and doing full IPA to make sure this is done
    // correctly.
    ScratchArgs = AF.add(ScratchArgs, 12, StopTracking);
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                DoNothing, DoNothing);
  } else if (FName == "VTCompressionSessionEncodeFrame") {
    // The context argument passed to VTCompressionSessionEncodeFrame()
    // is passed to the callback specified when creating the session
    // (e.g. with VTCompressionSessionCreate()) which can release it.
    // To account for this possibility, conservatively stop tracking
    // the context.
    ScratchArgs = AF.add(ScratchArgs, 5, StopTracking);
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                DoNothing, DoNothing);
  } else if (FName == "dispatch_set_context" ||
             FName == "xpc_connection_set_context") {
    // <rdar://problem/11059275> - The analyzer currently doesn't have
    // a good way to reason about the finalizer function for libdispatch.
    // If we pass a context object that is memory managed, stop tracking it.
    // <rdar://problem/13783514> - Same problem, but for XPC.
    // FIXME: this hack should possibly go away once we can handle
    // libdispatch and XPC finalizers.
    ScratchArgs = AF.add(ScratchArgs, 1, StopTracking);
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs,
                                DoNothing, DoNothing);
  } else if (FName.startswith("NSLog")) {
    return getDoNothingSummary();
  } else if (FName.startswith("NS") &&
             (FName.find("Insert") != StringRef::npos)) {
    // Whitelist NSXXInsertXX, for example NSMapInsertIfAbsent, since they can
    // be deallocated by NSMapRemove. (radar://11152419)
    ScratchArgs = AF.add(ScratchArgs, 1, StopTracking);
    ScratchArgs = AF.add(ScratchArgs, 2, StopTracking);
    return getPersistentSummary(RetEffect::MakeNoRet(),
                                ScratchArgs, DoNothing, DoNothing);
  }

  if (RetTy->isPointerType()) {

    // For CoreFoundation ('CF') types.
    if (cocoa::isRefType(RetTy, "CF", FName)) {
      if (isRetain(FD, FName)) {
        // CFRetain isn't supposed to be annotated. However, this may as
        // well be a user-made "safe" CFRetain function that is incorrectly
        // annotated as cf_returns_retained due to lack of better options.
        // We want to ignore such annotation.
        AllowAnnotations = false;

        return getUnarySummary(FT, cfretain);
      } else if (isAutorelease(FD, FName)) {
        // The headers use cf_consumed, but we can fully model CFAutorelease
        // ourselves.
        AllowAnnotations = false;

        return getUnarySummary(FT, cfautorelease);
      } else if (isMakeCollectable(FName)) {
        AllowAnnotations = false;
        return getUnarySummary(FT, cfmakecollectable);
      } else {
        return getCFCreateGetRuleSummary(FD);
      }
    }

    // For CoreGraphics ('CG') and CoreVideo ('CV') types.
    if (cocoa::isRefType(RetTy, "CG", FName) ||
        cocoa::isRefType(RetTy, "CV", FName)) {
      if (isRetain(FD, FName))
        return getUnarySummary(FT, cfretain);
      else
        return getCFCreateGetRuleSummary(FD);
    }

    // For all other CF-style types, use the Create/Get
    // rule for summaries but don't support Retain functions
    // with framework-specific prefixes.
    if (coreFoundation::isCFObjectRef(RetTy)) {
      return getCFCreateGetRuleSummary(FD);
    }

    if (FD->hasAttr<CFAuditedTransferAttr>()) {
      return getCFCreateGetRuleSummary(FD);
    }
  }

  // Check for release functions, the only kind of functions that we care
  // about that don't return a pointer type.
  if (FName.startswith("CG") || FName.startswith("CF")) {
    // Test for 'CGCF'.
    FName = FName.substr(FName.startswith("CGCF") ? 4 : 2);

    if (isRelease(FD, FName))
      return getUnarySummary(FT, cfrelease);
    else {
      assert(ScratchArgs.isEmpty());
      // Remaining CoreFoundation and CoreGraphics functions.
      // We use to assume that they all strictly followed the ownership idiom
      // and that ownership cannot be transferred.  While this is technically
      // correct, many methods allow a tracked object to escape.  For example:
      //
      //   CFMutableDictionaryRef x = CFDictionaryCreateMutable(...);
      //   CFDictionaryAddValue(y, key, x);
      //   CFRelease(x);
      //   ... it is okay to use 'x' since 'y' has a reference to it
      //
      // We handle this and similar cases with the follow heuristic.  If the
      // function name contains "InsertValue", "SetValue", "AddValue",
      // "AppendValue", or "SetAttribute", then we assume that arguments may
      // "escape."  This means that something else holds on to the object,
      // allowing it be used even after its local retain count drops to 0.
      ArgEffect E = (StrInStrNoCase(FName, "InsertValue") != StringRef::npos ||
                     StrInStrNoCase(FName, "AddValue") != StringRef::npos ||
                     StrInStrNoCase(FName, "SetValue") != StringRef::npos ||
                     StrInStrNoCase(FName, "AppendValue") != StringRef::npos ||
                     StrInStrNoCase(FName, "SetAttribute") != StringRef::npos)
                        ? MayEscape
                        : DoNothing;

      return getPersistentSummary(RetEffect::MakeNoRet(), ScratchArgs,
                                  DoNothing, E);
    }
  }

  return nullptr;
}

const RetainSummary *
RetainSummaryManager::generateSummary(const FunctionDecl *FD,
                                      bool &AllowAnnotations) {
  // We generate "stop" summaries for implicitly defined functions.
  if (FD->isImplicit())
    return getPersistentStopSummary();

  const IdentifierInfo *II = FD->getIdentifier();

  StringRef FName = II ? II->getName() : "";

  // Strip away preceding '_'.  Doing this here will effect all the checks
  // down below.
  FName = FName.substr(FName.find_first_not_of('_'));

  // Inspect the result type. Strip away any typedefs.
  const auto *FT = FD->getType()->getAs<FunctionType>();
  QualType RetTy = FT->getReturnType();

  if (TrackOSObjects)
    if (const RetainSummary *S = getSummaryForOSObject(FD, FName, RetTy))
      return S;

  if (TrackObjCAndCFObjects)
    if (const RetainSummary *S =
            getSummaryForObjCOrCFObject(FD, FName, RetTy, FT, AllowAnnotations))
      return S;

  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
    if (!(TrackOSObjects && isOSObjectRelated(MD)))
      return getPersistentSummary(RetEffect::MakeNoRet(),
                                  ArgEffects(AF.getEmptyMap()),
                                  DoNothing, StopTracking,
                                  DoNothing);

  return getDefaultSummary();
}

const RetainSummary *
RetainSummaryManager::getFunctionSummary(const FunctionDecl *FD) {
  // If we don't know what function we're calling, use our default summary.
  if (!FD)
    return getDefaultSummary();

  // Look up a summary in our cache of FunctionDecls -> Summaries.
  FuncSummariesTy::iterator I = FuncSummaries.find(FD);
  if (I != FuncSummaries.end())
    return I->second;

  // No summary?  Generate one.
  bool AllowAnnotations = true;
  const RetainSummary *S = generateSummary(FD, AllowAnnotations);

  // Annotations override defaults.
  if (AllowAnnotations)
    updateSummaryFromAnnotations(S, FD);

  FuncSummaries[FD] = S;
  return S;
}

//===----------------------------------------------------------------------===//
// Summary creation for functions (largely uses of Core Foundation).
//===----------------------------------------------------------------------===//

static ArgEffect getStopTrackingHardEquivalent(ArgEffect E) {
  switch (E) {
  case DoNothing:
  case Autorelease:
  case DecRefBridgedTransferred:
  case IncRef:
  case IncRefMsg:
  case MakeCollectable:
  case UnretainedOutParameter:
  case RetainedOutParameter:
  case MayEscape:
  case StopTracking:
  case StopTrackingHard:
    return StopTrackingHard;
  case DecRef:
  case DecRefAndStopTrackingHard:
    return DecRefAndStopTrackingHard;
  case DecRefMsg:
  case DecRefMsgAndStopTrackingHard:
    return DecRefMsgAndStopTrackingHard;
  case Dealloc:
    return Dealloc;
  }

  llvm_unreachable("Unknown ArgEffect kind");
}

void RetainSummaryManager::updateSummaryForCall(const RetainSummary *&S,
                                                const CallEvent &Call) {
  if (Call.hasNonZeroCallbackArg()) {
    ArgEffect RecEffect =
      getStopTrackingHardEquivalent(S->getReceiverEffect());
    ArgEffect DefEffect =
      getStopTrackingHardEquivalent(S->getDefaultArgEffect());

    ArgEffects ScratchArgs(AF.getEmptyMap());
    ArgEffects CustomArgEffects = S->getArgEffects();
    for (ArgEffects::iterator I = CustomArgEffects.begin(),
                              E = CustomArgEffects.end();
         I != E; ++I) {
      ArgEffect Translated = getStopTrackingHardEquivalent(I->second);
      if (Translated != DefEffect)
        ScratchArgs = AF.add(ScratchArgs, I->first, Translated);
    }

    RetEffect RE = RetEffect::MakeNoRetHard();

    // Special cases where the callback argument CANNOT free the return value.
    // This can generally only happen if we know that the callback will only be
    // called when the return value is already being deallocated.
    if (const SimpleFunctionCall *FC = dyn_cast<SimpleFunctionCall>(&Call)) {
      if (IdentifierInfo *Name = FC->getDecl()->getIdentifier()) {
        // When the CGBitmapContext is deallocated, the callback here will free
        // the associated data buffer.
        // The callback in dispatch_data_create frees the buffer, but not
        // the data object.
        if (Name->isStr("CGBitmapContextCreateWithData") ||
            Name->isStr("dispatch_data_create"))
          RE = S->getRetEffect();
      }
    }

    S = getPersistentSummary(RE, ScratchArgs, RecEffect, DefEffect);
  }

  // Special case '[super init];' and '[self init];'
  //
  // Even though calling '[super init]' without assigning the result to self
  // and checking if the parent returns 'nil' is a bad pattern, it is common.
  // Additionally, our Self Init checker already warns about it. To avoid
  // overwhelming the user with messages from both checkers, we model the case
  // of '[super init]' in cases when it is not consumed by another expression
  // as if the call preserves the value of 'self'; essentially, assuming it can
  // never fail and return 'nil'.
  // Note, we don't want to just stop tracking the value since we want the
  // RetainCount checker to report leaks and use-after-free if SelfInit checker
  // is turned off.
  if (const ObjCMethodCall *MC = dyn_cast<ObjCMethodCall>(&Call)) {
    if (MC->getMethodFamily() == OMF_init && MC->isReceiverSelfOrSuper()) {

      // Check if the message is not consumed, we know it will not be used in
      // an assignment, ex: "self = [super init]".
      const Expr *ME = MC->getOriginExpr();
      const LocationContext *LCtx = MC->getLocationContext();
      ParentMap &PM = LCtx->getAnalysisDeclContext()->getParentMap();
      if (!PM.isConsumedExpr(ME)) {
        RetainSummaryTemplate ModifiableSummaryTemplate(S, *this);
        ModifiableSummaryTemplate->setReceiverEffect(DoNothing);
        ModifiableSummaryTemplate->setRetEffect(RetEffect::MakeNoRet());
      }
    }
  }
}

const RetainSummary *
RetainSummaryManager::getSummary(const CallEvent &Call,
                                 QualType ReceiverType) {
  const RetainSummary *Summ;
  switch (Call.getKind()) {
  case CE_Function:
  case CE_CXXMember:
  case CE_CXXMemberOperator:
  case CE_CXXConstructor:
  case CE_CXXAllocator:
    Summ = getFunctionSummary(cast_or_null<FunctionDecl>(Call.getDecl()));
    break;
  case CE_Block:
  case CE_CXXDestructor:
    // FIXME: These calls are currently unsupported.
    return getPersistentStopSummary();
  case CE_ObjCMessage: {
    const ObjCMethodCall &Msg = cast<ObjCMethodCall>(Call);
    if (Msg.isInstanceMessage())
      Summ = getInstanceMethodSummary(Msg, ReceiverType);
    else
      Summ = getClassMethodSummary(Msg);
    break;
  }
  }

  updateSummaryForCall(Summ, Call);

  assert(Summ && "Unknown call type?");
  return Summ;
}


const RetainSummary *
RetainSummaryManager::getCFCreateGetRuleSummary(const FunctionDecl *FD) {
  if (coreFoundation::followsCreateRule(FD))
    return getCFSummaryCreateRule(FD);

  return getCFSummaryGetRule(FD);
}

bool RetainSummaryManager::isTrustedReferenceCountImplementation(
    const FunctionDecl *FD) {
  return hasRCAnnotation(FD, "rc_ownership_trusted_implementation");
}

Optional<RetainSummaryManager::BehaviorSummary>
RetainSummaryManager::canEval(const CallExpr *CE, const FunctionDecl *FD,
                              bool &hasTrustedImplementationAnnotation) {

  IdentifierInfo *II = FD->getIdentifier();
  if (!II)
    return None;

  StringRef FName = II->getName();
  FName = FName.substr(FName.find_first_not_of('_'));

  QualType ResultTy = CE->getCallReturnType(Ctx);
  if (ResultTy->isObjCIdType()) {
    if (II->isStr("NSMakeCollectable"))
      return BehaviorSummary::Identity;
  } else if (ResultTy->isPointerType()) {
    // Handle: (CF|CG|CV)Retain
    //         CFAutorelease
    // It's okay to be a little sloppy here.
    if (FName == "CMBufferQueueDequeueAndRetain" ||
        FName == "CMBufferQueueDequeueIfDataReadyAndRetain") {
      // Part of: <rdar://problem/39390714>.
      // These are not retain. They just return something and retain it.
      return None;
    }
    if (cocoa::isRefType(ResultTy, "CF", FName) ||
        cocoa::isRefType(ResultTy, "CG", FName) ||
        cocoa::isRefType(ResultTy, "CV", FName))
      if (isRetain(FD, FName) || isAutorelease(FD, FName) ||
          isMakeCollectable(FName))
        return BehaviorSummary::Identity;

    // safeMetaCast is called by OSDynamicCast.
    // We assume that OSDynamicCast is either an identity (cast is OK,
    // the input was non-zero),
    // or that it returns zero (when the cast failed, or the input
    // was zero).
    if (TrackOSObjects && isOSObjectDynamicCast(FName)) {
      return BehaviorSummary::IdentityOrZero;
    }

    const FunctionDecl* FDD = FD->getDefinition();
    if (FDD && isTrustedReferenceCountImplementation(FDD)) {
      hasTrustedImplementationAnnotation = true;
      return BehaviorSummary::Identity;
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
    const CXXRecordDecl *Parent = MD->getParent();
    if (TrackOSObjects && Parent && isOSObjectSubclass(Parent))
      if (FName == "release" || FName == "retain")
        return BehaviorSummary::NoOp;
  }

  return None;
}

const RetainSummary *
RetainSummaryManager::getUnarySummary(const FunctionType* FT,
                                      UnaryFuncKind func) {

  // Unary functions have no arg effects by definition.
  ArgEffects ScratchArgs(AF.getEmptyMap());

  // Sanity check that this is *really* a unary function.  This can
  // happen if people do weird things.
  const FunctionProtoType* FTP = dyn_cast<FunctionProtoType>(FT);
  if (!FTP || FTP->getNumParams() != 1)
    return getPersistentStopSummary();

  ArgEffect Effect;
  switch (func) {
  case cfretain: Effect = IncRef; break;
  case cfrelease: Effect = DecRef; break;
  case cfautorelease: Effect = Autorelease; break;
  case cfmakecollectable: Effect = MakeCollectable; break;
  }

  ScratchArgs = AF.add(ScratchArgs, 0, Effect);
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              ScratchArgs,
                              DoNothing, DoNothing);
}

const RetainSummary *
RetainSummaryManager::getOSSummaryRetainRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              AF.getEmptyMap(),
                              /*ReceiverEff=*/DoNothing,
                              /*DefaultEff=*/DoNothing,
                              /*ThisEff=*/IncRef);
}

const RetainSummary *
RetainSummaryManager::getOSSummaryReleaseRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              AF.getEmptyMap(),
                              /*ReceiverEff=*/DoNothing,
                              /*DefaultEff=*/DoNothing,
                              /*ThisEff=*/DecRef);
}

const RetainSummary *
RetainSummaryManager::getOSSummaryFreeRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNoRet(),
                              AF.getEmptyMap(),
                              /*ReceiverEff=*/DoNothing,
                              /*DefaultEff=*/DoNothing,
                              /*ThisEff=*/Dealloc);
}

const RetainSummary *
RetainSummaryManager::getOSSummaryCreateRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeOwned(ObjKind::OS),
                              AF.getEmptyMap());
}

const RetainSummary *
RetainSummaryManager::getOSSummaryGetRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNotOwned(ObjKind::OS),
                              AF.getEmptyMap());
}

const RetainSummary *
RetainSummaryManager::getCFSummaryCreateRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF),
                              ArgEffects(AF.getEmptyMap()));
}

const RetainSummary *
RetainSummaryManager::getCFSummaryGetRule(const FunctionDecl *FD) {
  return getPersistentSummary(RetEffect::MakeNotOwned(ObjKind::CF),
                              ArgEffects(AF.getEmptyMap()),
                              DoNothing, DoNothing);
}




//===----------------------------------------------------------------------===//
// Summary creation for Selectors.
//===----------------------------------------------------------------------===//

Optional<RetEffect>
RetainSummaryManager::getRetEffectFromAnnotations(QualType RetTy,
                                                  const Decl *D) {
  if (TrackObjCAndCFObjects && cocoa::isCocoaObjectRef(RetTy)) {
    if (D->hasAttr<NSReturnsRetainedAttr>())
      return ObjCAllocRetE;

    if (D->hasAttr<NSReturnsNotRetainedAttr>() ||
        D->hasAttr<NSReturnsAutoreleasedAttr>())
      return RetEffect::MakeNotOwned(ObjKind::ObjC);

  } else if (!RetTy->isPointerType()) {
    return None;
  }

  if (hasEnabledAttr<CFReturnsRetainedAttr>(D)) {
    return RetEffect::MakeOwned(ObjKind::CF);
  } else if (hasEnabledAttr<OSReturnsRetainedAttr>(D)) {
    return RetEffect::MakeOwned(ObjKind::OS);
  } else if (hasRCAnnotation(D, "rc_ownership_returns_retained")) {
    return RetEffect::MakeOwned(ObjKind::Generalized);
  }

  if (hasEnabledAttr<CFReturnsNotRetainedAttr>(D)) {
    return RetEffect::MakeNotOwned(ObjKind::CF);
  } else if (hasEnabledAttr<OSReturnsNotRetainedAttr>(D)) {
    return RetEffect::MakeNotOwned(ObjKind::OS);
  } else if (hasRCAnnotation(D, "rc_ownership_returns_not_retained")) {
    return RetEffect::MakeNotOwned(ObjKind::Generalized);
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
    for (const auto *PD : MD->overridden_methods())
      if (auto RE = getRetEffectFromAnnotations(RetTy, PD))
        return RE;

  return None;
}

bool RetainSummaryManager::applyFunctionParamAnnotationEffect(
    const ParmVarDecl *pd, unsigned parm_idx, const FunctionDecl *FD,
    RetainSummaryTemplate &Template) {
  if (hasEnabledAttr<NSConsumedAttr>(pd)) {
    Template->addArg(AF, parm_idx, DecRefMsg);
    return true;
  } else if (hasEnabledAttr<CFConsumedAttr>(pd) ||
             hasEnabledAttr<OSConsumedAttr>(pd) ||
             hasRCAnnotation(pd, "rc_ownership_consumed")) {
    Template->addArg(AF, parm_idx, DecRef);
    return true;
  } else if (hasEnabledAttr<CFReturnsRetainedAttr>(pd) ||
             hasRCAnnotation(pd, "rc_ownership_returns_retained")) {
    QualType PointeeTy = pd->getType()->getPointeeType();
    if (!PointeeTy.isNull()) {
      if (coreFoundation::isCFObjectRef(PointeeTy)) {
        Template->addArg(AF, parm_idx, RetainedOutParameter);
        return true;
      }
    }
  } else if (hasEnabledAttr<CFReturnsNotRetainedAttr>(pd)) {
    QualType PointeeTy = pd->getType()->getPointeeType();
    if (!PointeeTy.isNull()) {
      if (coreFoundation::isCFObjectRef(PointeeTy)) {
        Template->addArg(AF, parm_idx, UnretainedOutParameter);
        return true;
      }
    }
  } else {
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD)) {
      for (const auto *OD : MD->overridden_methods()) {
        const ParmVarDecl *OP = OD->parameters()[parm_idx];
        if (applyFunctionParamAnnotationEffect(OP, parm_idx, OD, Template))
          return true;
      }
    }
  }

  return false;
}

void
RetainSummaryManager::updateSummaryFromAnnotations(const RetainSummary *&Summ,
                                                   const FunctionDecl *FD) {
  if (!FD)
    return;

  assert(Summ && "Must have a summary to add annotations to.");
  RetainSummaryTemplate Template(Summ, *this);

  // Effects on the parameters.
  unsigned parm_idx = 0;
  for (auto pi = FD->param_begin(),
         pe = FD->param_end(); pi != pe; ++pi, ++parm_idx) {
    const ParmVarDecl *pd = *pi;
    applyFunctionParamAnnotationEffect(pd, parm_idx, FD, Template);
  }

  QualType RetTy = FD->getReturnType();
  if (Optional<RetEffect> RetE = getRetEffectFromAnnotations(RetTy, FD))
    Template->setRetEffect(*RetE);

  if (hasEnabledAttr<OSConsumesThisAttr>(FD))
    Template->setThisEffect(DecRef);
}

void
RetainSummaryManager::updateSummaryFromAnnotations(const RetainSummary *&Summ,
                                                   const ObjCMethodDecl *MD) {
  if (!MD)
    return;

  assert(Summ && "Must have a valid summary to add annotations to");
  RetainSummaryTemplate Template(Summ, *this);

  // Effects on the receiver.
  if (MD->hasAttr<NSConsumesSelfAttr>())
    Template->setReceiverEffect(DecRefMsg);

  // Effects on the parameters.
  unsigned parm_idx = 0;
  for (auto pi=MD->param_begin(), pe=MD->param_end();
       pi != pe; ++pi, ++parm_idx) {
    const ParmVarDecl *pd = *pi;
    if (pd->hasAttr<NSConsumedAttr>()) {
      Template->addArg(AF, parm_idx, DecRefMsg);
    } else if (pd->hasAttr<CFConsumedAttr>() || pd->hasAttr<OSConsumedAttr>()) {
      Template->addArg(AF, parm_idx, DecRef);
    } else if (pd->hasAttr<CFReturnsRetainedAttr>()) {
      QualType PointeeTy = pd->getType()->getPointeeType();
      if (!PointeeTy.isNull())
        if (coreFoundation::isCFObjectRef(PointeeTy))
          Template->addArg(AF, parm_idx, RetainedOutParameter);
    } else if (pd->hasAttr<CFReturnsNotRetainedAttr>()) {
      QualType PointeeTy = pd->getType()->getPointeeType();
      if (!PointeeTy.isNull())
        if (coreFoundation::isCFObjectRef(PointeeTy))
          Template->addArg(AF, parm_idx, UnretainedOutParameter);
    }
  }

  QualType RetTy = MD->getReturnType();
  if (Optional<RetEffect> RetE = getRetEffectFromAnnotations(RetTy, MD))
    Template->setRetEffect(*RetE);
}

const RetainSummary *
RetainSummaryManager::getStandardMethodSummary(const ObjCMethodDecl *MD,
                                               Selector S, QualType RetTy) {
  // Any special effects?
  ArgEffect ReceiverEff = DoNothing;
  RetEffect ResultEff = RetEffect::MakeNoRet();

  // Check the method family, and apply any default annotations.
  switch (MD ? MD->getMethodFamily() : S.getMethodFamily()) {
    case OMF_None:
    case OMF_initialize:
    case OMF_performSelector:
      // Assume all Objective-C methods follow Cocoa Memory Management rules.
      // FIXME: Does the non-threaded performSelector family really belong here?
      // The selector could be, say, @selector(copy).
      if (cocoa::isCocoaObjectRef(RetTy))
        ResultEff = RetEffect::MakeNotOwned(ObjKind::ObjC);
      else if (coreFoundation::isCFObjectRef(RetTy)) {
        // ObjCMethodDecl currently doesn't consider CF objects as valid return
        // values for alloc, new, copy, or mutableCopy, so we have to
        // double-check with the selector. This is ugly, but there aren't that
        // many Objective-C methods that return CF objects, right?
        if (MD) {
          switch (S.getMethodFamily()) {
          case OMF_alloc:
          case OMF_new:
          case OMF_copy:
          case OMF_mutableCopy:
            ResultEff = RetEffect::MakeOwned(ObjKind::CF);
            break;
          default:
            ResultEff = RetEffect::MakeNotOwned(ObjKind::CF);
            break;
          }
        } else {
          ResultEff = RetEffect::MakeNotOwned(ObjKind::CF);
        }
      }
      break;
    case OMF_init:
      ResultEff = ObjCInitRetE;
      ReceiverEff = DecRefMsg;
      break;
    case OMF_alloc:
    case OMF_new:
    case OMF_copy:
    case OMF_mutableCopy:
      if (cocoa::isCocoaObjectRef(RetTy))
        ResultEff = ObjCAllocRetE;
      else if (coreFoundation::isCFObjectRef(RetTy))
        ResultEff = RetEffect::MakeOwned(ObjKind::CF);
      break;
    case OMF_autorelease:
      ReceiverEff = Autorelease;
      break;
    case OMF_retain:
      ReceiverEff = IncRefMsg;
      break;
    case OMF_release:
      ReceiverEff = DecRefMsg;
      break;
    case OMF_dealloc:
      ReceiverEff = Dealloc;
      break;
    case OMF_self:
      // -self is handled specially by the ExprEngine to propagate the receiver.
      break;
    case OMF_retainCount:
    case OMF_finalize:
      // These methods don't return objects.
      break;
  }

  // If one of the arguments in the selector has the keyword 'delegate' we
  // should stop tracking the reference count for the receiver.  This is
  // because the reference count is quite possibly handled by a delegate
  // method.
  if (S.isKeywordSelector()) {
    for (unsigned i = 0, e = S.getNumArgs(); i != e; ++i) {
      StringRef Slot = S.getNameForSlot(i);
      if (Slot.substr(Slot.size() - 8).equals_lower("delegate")) {
        if (ResultEff == ObjCInitRetE)
          ResultEff = RetEffect::MakeNoRetHard();
        else
          ReceiverEff = StopTrackingHard;
      }
    }
  }

  if (ReceiverEff == DoNothing &&
      ResultEff.getKind() == RetEffect::NoRet)
    return getDefaultSummary();

  return getPersistentSummary(ResultEff, ArgEffects(AF.getEmptyMap()),
                              ReceiverEff, MayEscape);
}

const RetainSummary *RetainSummaryManager::getInstanceMethodSummary(
    const ObjCMethodCall &Msg,
    QualType ReceiverType) {
  const ObjCInterfaceDecl *ReceiverClass = nullptr;

  // We do better tracking of the type of the object than the core ExprEngine.
  // See if we have its type in our private state.
  if (!ReceiverType.isNull())
    if (const auto *PT = ReceiverType->getAs<ObjCObjectPointerType>())
      ReceiverClass = PT->getInterfaceDecl();

  // If we don't know what kind of object this is, fall back to its static type.
  if (!ReceiverClass)
    ReceiverClass = Msg.getReceiverInterface();

  // FIXME: The receiver could be a reference to a class, meaning that
  //  we should use the class method.
  // id x = [NSObject class];
  // [x performSelector:... withObject:... afterDelay:...];
  Selector S = Msg.getSelector();
  const ObjCMethodDecl *Method = Msg.getDecl();
  if (!Method && ReceiverClass)
    Method = ReceiverClass->getInstanceMethod(S);

  return getMethodSummary(S, ReceiverClass, Method, Msg.getResultType(),
                          ObjCMethodSummaries);
}

const RetainSummary *
RetainSummaryManager::getMethodSummary(Selector S,
                                       const ObjCInterfaceDecl *ID,
                                       const ObjCMethodDecl *MD, QualType RetTy,
                                       ObjCMethodSummariesTy &CachedSummaries) {

  // Objective-C method summaries are only applicable to ObjC and CF objects.
  if (!TrackObjCAndCFObjects)
    return getDefaultSummary();

  // Look up a summary in our summary cache.
  const RetainSummary *Summ = CachedSummaries.find(ID, S);

  if (!Summ) {
    Summ = getStandardMethodSummary(MD, S, RetTy);

    // Annotations override defaults.
    updateSummaryFromAnnotations(Summ, MD);

    // Memoize the summary.
    CachedSummaries[ObjCSummaryKey(ID, S)] = Summ;
  }

  return Summ;
}

void RetainSummaryManager::InitializeClassMethodSummaries() {
  ArgEffects ScratchArgs = AF.getEmptyMap();

  // Create the [NSAssertionHandler currentHander] summary.
  addClassMethSummary("NSAssertionHandler", "currentHandler",
                getPersistentSummary(RetEffect::MakeNotOwned(ObjKind::ObjC),
                                     ScratchArgs));

  // Create the [NSAutoreleasePool addObject:] summary.
  ScratchArgs = AF.add(ScratchArgs, 0, Autorelease);
  addClassMethSummary("NSAutoreleasePool", "addObject",
                      getPersistentSummary(RetEffect::MakeNoRet(),
                                           ScratchArgs,
                                           DoNothing, Autorelease));
}

void RetainSummaryManager::InitializeMethodSummaries() {

  ArgEffects ScratchArgs = AF.getEmptyMap();
  // Create the "init" selector.  It just acts as a pass-through for the
  // receiver.
  const RetainSummary *InitSumm = getPersistentSummary(ObjCInitRetE,
                                                       ScratchArgs,
                                                       DecRefMsg);
  addNSObjectMethSummary(GetNullarySelector("init", Ctx), InitSumm);

  // awakeAfterUsingCoder: behaves basically like an 'init' method.  It
  // claims the receiver and returns a retained object.
  addNSObjectMethSummary(GetUnarySelector("awakeAfterUsingCoder", Ctx),
                         InitSumm);

  // The next methods are allocators.
  const RetainSummary *AllocSumm = getPersistentSummary(ObjCAllocRetE,
                                                        ScratchArgs);
  const RetainSummary *CFAllocSumm =
    getPersistentSummary(RetEffect::MakeOwned(ObjKind::CF), ScratchArgs);

  // Create the "retain" selector.
  RetEffect NoRet = RetEffect::MakeNoRet();
  const RetainSummary *Summ = getPersistentSummary(NoRet, ScratchArgs, IncRefMsg);
  addNSObjectMethSummary(GetNullarySelector("retain", Ctx), Summ);

  // Create the "release" selector.
  Summ = getPersistentSummary(NoRet, ScratchArgs, DecRefMsg);
  addNSObjectMethSummary(GetNullarySelector("release", Ctx), Summ);

  // Create the -dealloc summary.
  Summ = getPersistentSummary(NoRet, ScratchArgs, Dealloc);
  addNSObjectMethSummary(GetNullarySelector("dealloc", Ctx), Summ);

  // Create the "autorelease" selector.
  Summ = getPersistentSummary(NoRet, ScratchArgs, Autorelease);
  addNSObjectMethSummary(GetNullarySelector("autorelease", Ctx), Summ);

  // For NSWindow, allocated objects are (initially) self-owned.
  // FIXME: For now we opt for false negatives with NSWindow, as these objects
  //  self-own themselves.  However, they only do this once they are displayed.
  //  Thus, we need to track an NSWindow's display status.
  //  This is tracked in <rdar://problem/6062711>.
  //  See also http://llvm.org/bugs/show_bug.cgi?id=3714.
  const RetainSummary *NoTrackYet = getPersistentSummary(
      RetEffect::MakeNoRet(), ScratchArgs, StopTracking, StopTracking);

  addClassMethSummary("NSWindow", "alloc", NoTrackYet);

  // For NSPanel (which subclasses NSWindow), allocated objects are not
  //  self-owned.
  // FIXME: For now we don't track NSPanels. object for the same reason
  //   as for NSWindow objects.
  addClassMethSummary("NSPanel", "alloc", NoTrackYet);

  // For NSNull, objects returned by +null are singletons that ignore
  // retain/release semantics.  Just don't track them.
  // <rdar://problem/12858915>
  addClassMethSummary("NSNull", "null", NoTrackYet);

  // Don't track allocated autorelease pools, as it is okay to prematurely
  // exit a method.
  addClassMethSummary("NSAutoreleasePool", "alloc", NoTrackYet);
  addClassMethSummary("NSAutoreleasePool", "allocWithZone", NoTrackYet, false);
  addClassMethSummary("NSAutoreleasePool", "new", NoTrackYet);

  // Create summaries QCRenderer/QCView -createSnapShotImageOfType:
  addInstMethSummary("QCRenderer", AllocSumm, "createSnapshotImageOfType");
  addInstMethSummary("QCView", AllocSumm, "createSnapshotImageOfType");

  // Create summaries for CIContext, 'createCGImage' and
  // 'createCGLayerWithSize'.  These objects are CF objects, and are not
  // automatically garbage collected.
  addInstMethSummary("CIContext", CFAllocSumm, "createCGImage", "fromRect");
  addInstMethSummary("CIContext", CFAllocSumm, "createCGImage", "fromRect",
                     "format", "colorSpace");
  addInstMethSummary("CIContext", CFAllocSumm, "createCGLayerWithSize", "info");
}

CallEffects CallEffects::getEffect(const ObjCMethodDecl *MD) {
  ASTContext &Ctx = MD->getASTContext();
  LangOptions L = Ctx.getLangOpts();
  RetainSummaryManager M(Ctx, L.ObjCAutoRefCount,
                         /*TrackNSAndCFObjects=*/true,
                         /*TrackOSObjects=*/false);
  const RetainSummary *S = M.getMethodSummary(MD);
  CallEffects CE(S->getRetEffect());
  CE.Receiver = S->getReceiverEffect();
  unsigned N = MD->param_size();
  for (unsigned i = 0; i < N; ++i) {
    CE.Args.push_back(S->getArg(i));
  }
  return CE;
}

CallEffects CallEffects::getEffect(const FunctionDecl *FD) {
  ASTContext &Ctx = FD->getASTContext();
  LangOptions L = Ctx.getLangOpts();
  RetainSummaryManager M(Ctx, L.ObjCAutoRefCount,
                         /*TrackNSAndCFObjects=*/true,
                         /*TrackOSObjects=*/false);
  const RetainSummary *S = M.getFunctionSummary(FD);
  CallEffects CE(S->getRetEffect());
  unsigned N = FD->param_size();
  for (unsigned i = 0; i < N; ++i) {
    CE.Args.push_back(S->getArg(i));
  }
  return CE;
}
