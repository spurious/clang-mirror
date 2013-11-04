//== BasicObjCFoundationChecks.cpp - Simple Apple-Foundation checks -*- C++ -*--
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines BasicObjCFoundationChecks, a class that encapsulates
//  a set of simple checks to run on Objective-C code using Apple's Foundation
//  classes.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Analysis/DomainSpecific/CocoaConventions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {
class APIMisuse : public BugType {
public:
  APIMisuse(const char* name) : BugType(name, "API Misuse (Apple)") {}
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Utility functions.
//===----------------------------------------------------------------------===//

static StringRef GetReceiverInterfaceName(const ObjCMethodCall &msg) {
  if (const ObjCInterfaceDecl *ID = msg.getReceiverInterface())
    return ID->getIdentifier()->getName();
  return StringRef();
}

enum FoundationClass {
  FC_None,
  FC_NSArray,
  FC_NSDictionary,
  FC_NSEnumerator,
  FC_NSNull,
  FC_NSOrderedSet,
  FC_NSSet,
  FC_NSString
};

static FoundationClass findKnownClass(const ObjCInterfaceDecl *ID) {
  static llvm::StringMap<FoundationClass> Classes;
  if (Classes.empty()) {
    Classes["NSArray"] = FC_NSArray;
    Classes["NSDictionary"] = FC_NSDictionary;
    Classes["NSEnumerator"] = FC_NSEnumerator;
    Classes["NSNull"] = FC_NSNull;
    Classes["NSOrderedSet"] = FC_NSOrderedSet;
    Classes["NSSet"] = FC_NSSet;
    Classes["NSString"] = FC_NSString;
  }

  // FIXME: Should we cache this at all?
  FoundationClass result = Classes.lookup(ID->getIdentifier()->getName());
  if (result == FC_None)
    if (const ObjCInterfaceDecl *Super = ID->getSuperClass())
      return findKnownClass(Super);

  return result;
}

//===----------------------------------------------------------------------===//
// NilArgChecker - Check for prohibited nil arguments to ObjC method calls.
//===----------------------------------------------------------------------===//

namespace {
  class NilArgChecker : public Checker<check::PreObjCMessage,
                                       check::PostStmt<ObjCDictionaryLiteral>,
                                       check::PostStmt<ObjCArrayLiteral> > {
    mutable OwningPtr<APIMisuse> BT;

    void warnIfNilExpr(const Expr *E,
                       const char *Msg,
                       CheckerContext &C) const;

    void warnIfNilArg(CheckerContext &C,
                      const ObjCMethodCall &msg, unsigned Arg,
                      FoundationClass Class,
                      bool CanBeSubscript = false) const;

    void generateBugReport(ExplodedNode *N,
                           StringRef Msg,
                           SourceRange Range,
                           const Expr *Expr,
                           CheckerContext &C) const;

  public:
    void checkPreObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
    void checkPostStmt(const ObjCDictionaryLiteral *DL,
                       CheckerContext &C) const;
    void checkPostStmt(const ObjCArrayLiteral *AL,
                       CheckerContext &C) const;
  };
}

void NilArgChecker::warnIfNilExpr(const Expr *E,
                                  const char *Msg,
                                  CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  if (State->isNull(C.getSVal(E)).isConstrainedTrue()) {

    if (ExplodedNode *N = C.generateSink()) {
      generateBugReport(N, Msg, E->getSourceRange(), E, C);
    }
    
  }
}

void NilArgChecker::warnIfNilArg(CheckerContext &C,
                                 const ObjCMethodCall &msg,
                                 unsigned int Arg,
                                 FoundationClass Class,
                                 bool CanBeSubscript) const {
  // Check if the argument is nil.
  ProgramStateRef State = C.getState();
  if (!State->isNull(msg.getArgSVal(Arg)).isConstrainedTrue())
      return;
      
  if (ExplodedNode *N = C.generateSink()) {
    SmallString<128> sbuf;
    llvm::raw_svector_ostream os(sbuf);

    if (CanBeSubscript && msg.getMessageKind() == OCM_Subscript) {

      if (Class == FC_NSArray) {
        os << "Array element cannot be nil";
      } else if (Class == FC_NSDictionary) {
        if (Arg == 0) {
          os << "Value stored into '";
          os << GetReceiverInterfaceName(msg) << "' cannot be nil";
        } else {
          assert(Arg == 1);
          os << "'"<< GetReceiverInterfaceName(msg) << "' key cannot be nil";
        }
      } else
        llvm_unreachable("Missing foundation class for the subscript expr");

    } else {
      if (Class == FC_NSDictionary) {
        if (Arg == 0)
          os << "Value argument ";
        else {
          assert(Arg == 1);
          os << "Key argument ";
        }
        os << "to '" << msg.getSelector().getAsString() << "' cannot be nil";
      } else {
        os << "Argument to '" << GetReceiverInterfaceName(msg) << "' method '"
        << msg.getSelector().getAsString() << "' cannot be nil";
      }
    }
    
    generateBugReport(N, os.str(), msg.getArgSourceRange(Arg),
                      msg.getArgExpr(Arg), C);
  }
}

void NilArgChecker::generateBugReport(ExplodedNode *N,
                                      StringRef Msg,
                                      SourceRange Range,
                                      const Expr *E,
                                      CheckerContext &C) const {
  if (!BT)
    BT.reset(new APIMisuse("nil argument"));

  BugReport *R = new BugReport(*BT, Msg, N);
  R->addRange(Range);
  bugreporter::trackNullOrUndefValue(N, E, *R);
  C.emitReport(R);
}

void NilArgChecker::checkPreObjCMessage(const ObjCMethodCall &msg,
                                        CheckerContext &C) const {
  const ObjCInterfaceDecl *ID = msg.getReceiverInterface();
  if (!ID)
    return;

  FoundationClass Class = findKnownClass(ID);

  static const unsigned InvalidArgIndex = UINT_MAX;
  unsigned Arg = InvalidArgIndex;
  bool CanBeSubscript = false;
  
  if (Class == FC_NSString) {
    Selector S = msg.getSelector();
    
    if (S.isUnarySelector())
      return;
    
    // FIXME: This is going to be really slow doing these checks with
    //  lexical comparisons.
    
    std::string NameStr = S.getAsString();
    StringRef Name(NameStr);
    assert(!Name.empty());
    
    // FIXME: Checking for initWithFormat: will not work in most cases
    //  yet because [NSString alloc] returns id, not NSString*.  We will
    //  need support for tracking expected-type information in the analyzer
    //  to find these errors.
    if (Name == "caseInsensitiveCompare:" ||
        Name == "compare:" ||
        Name == "compare:options:" ||
        Name == "compare:options:range:" ||
        Name == "compare:options:range:locale:" ||
        Name == "componentsSeparatedByCharactersInSet:" ||
        Name == "initWithFormat:") {
      Arg = 0;
    }
  } else if (Class == FC_NSArray) {
    Selector S = msg.getSelector();

    if (S.isUnarySelector())
      return;

    if (S.getNameForSlot(0).equals("addObject")) {
      Arg = 0;
    } else if (S.getNameForSlot(0).equals("insertObject") &&
               S.getNameForSlot(1).equals("atIndex")) {
      Arg = 0;
    } else if (S.getNameForSlot(0).equals("replaceObjectAtIndex") &&
               S.getNameForSlot(1).equals("withObject")) {
      Arg = 1;
    } else if (S.getNameForSlot(0).equals("setObject") &&
               S.getNameForSlot(1).equals("atIndexedSubscript")) {
      Arg = 0;
      CanBeSubscript = true;
    } else if (S.getNameForSlot(0).equals("arrayByAddingObject")) {
      Arg = 0;
    }
  } else if (Class == FC_NSDictionary) {
    Selector S = msg.getSelector();

    if (S.isUnarySelector())
      return;

    if (S.getNameForSlot(0).equals("dictionaryWithObject") &&
        S.getNameForSlot(1).equals("forKey")) {
      Arg = 0;
      warnIfNilArg(C, msg, /* Arg */1, Class);
    } else if (S.getNameForSlot(0).equals("setObject") &&
               S.getNameForSlot(1).equals("forKey")) {
      Arg = 0;
      warnIfNilArg(C, msg, /* Arg */1, Class);
    } else if (S.getNameForSlot(0).equals("setObject") &&
               S.getNameForSlot(1).equals("forKeyedSubscript")) {
      CanBeSubscript = true;
      Arg = 0;
      warnIfNilArg(C, msg, /* Arg */1, Class, CanBeSubscript);
    } else if (S.getNameForSlot(0).equals("removeObjectForKey")) {
      Arg = 0;
    }
  }

  // If argument is '0', report a warning.
  if ((Arg != InvalidArgIndex))
    warnIfNilArg(C, msg, Arg, Class, CanBeSubscript);

}

void NilArgChecker::checkPostStmt(const ObjCArrayLiteral *AL,
                                  CheckerContext &C) const {
  unsigned NumOfElements = AL->getNumElements();
  for (unsigned i = 0; i < NumOfElements; ++i) {
    warnIfNilExpr(AL->getElement(i), "Array element cannot be nil", C);
  }
}

void NilArgChecker::checkPostStmt(const ObjCDictionaryLiteral *DL,
                                  CheckerContext &C) const {
  unsigned NumOfElements = DL->getNumElements();
  for (unsigned i = 0; i < NumOfElements; ++i) {
    ObjCDictionaryElement Element = DL->getKeyValueElement(i);
    warnIfNilExpr(Element.Key, "Dictionary key cannot be nil", C);
    warnIfNilExpr(Element.Value, "Dictionary value cannot be nil", C);
  }
}

//===----------------------------------------------------------------------===//
// Error reporting.
//===----------------------------------------------------------------------===//

namespace {
class CFNumberCreateChecker : public Checker< check::PreStmt<CallExpr> > {
  mutable OwningPtr<APIMisuse> BT;
  mutable IdentifierInfo* II;
public:
  CFNumberCreateChecker() : II(0) {}

  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;

private:
  void EmitError(const TypedRegion* R, const Expr *Ex,
                uint64_t SourceSize, uint64_t TargetSize, uint64_t NumberKind);
};
} // end anonymous namespace

enum CFNumberType {
  kCFNumberSInt8Type = 1,
  kCFNumberSInt16Type = 2,
  kCFNumberSInt32Type = 3,
  kCFNumberSInt64Type = 4,
  kCFNumberFloat32Type = 5,
  kCFNumberFloat64Type = 6,
  kCFNumberCharType = 7,
  kCFNumberShortType = 8,
  kCFNumberIntType = 9,
  kCFNumberLongType = 10,
  kCFNumberLongLongType = 11,
  kCFNumberFloatType = 12,
  kCFNumberDoubleType = 13,
  kCFNumberCFIndexType = 14,
  kCFNumberNSIntegerType = 15,
  kCFNumberCGFloatType = 16
};

static Optional<uint64_t> GetCFNumberSize(ASTContext &Ctx, uint64_t i) {
  static const unsigned char FixedSize[] = { 8, 16, 32, 64, 32, 64 };

  if (i < kCFNumberCharType)
    return FixedSize[i-1];

  QualType T;

  switch (i) {
    case kCFNumberCharType:     T = Ctx.CharTy;     break;
    case kCFNumberShortType:    T = Ctx.ShortTy;    break;
    case kCFNumberIntType:      T = Ctx.IntTy;      break;
    case kCFNumberLongType:     T = Ctx.LongTy;     break;
    case kCFNumberLongLongType: T = Ctx.LongLongTy; break;
    case kCFNumberFloatType:    T = Ctx.FloatTy;    break;
    case kCFNumberDoubleType:   T = Ctx.DoubleTy;   break;
    case kCFNumberCFIndexType:
    case kCFNumberNSIntegerType:
    case kCFNumberCGFloatType:
      // FIXME: We need a way to map from names to Type*.
    default:
      return None;
  }

  return Ctx.getTypeSize(T);
}

#if 0
static const char* GetCFNumberTypeStr(uint64_t i) {
  static const char* Names[] = {
    "kCFNumberSInt8Type",
    "kCFNumberSInt16Type",
    "kCFNumberSInt32Type",
    "kCFNumberSInt64Type",
    "kCFNumberFloat32Type",
    "kCFNumberFloat64Type",
    "kCFNumberCharType",
    "kCFNumberShortType",
    "kCFNumberIntType",
    "kCFNumberLongType",
    "kCFNumberLongLongType",
    "kCFNumberFloatType",
    "kCFNumberDoubleType",
    "kCFNumberCFIndexType",
    "kCFNumberNSIntegerType",
    "kCFNumberCGFloatType"
  };

  return i <= kCFNumberCGFloatType ? Names[i-1] : "Invalid CFNumberType";
}
#endif

void CFNumberCreateChecker::checkPreStmt(const CallExpr *CE,
                                         CheckerContext &C) const {
  ProgramStateRef state = C.getState();
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD)
    return;
  
  ASTContext &Ctx = C.getASTContext();
  if (!II)
    II = &Ctx.Idents.get("CFNumberCreate");

  if (FD->getIdentifier() != II || CE->getNumArgs() != 3)
    return;

  // Get the value of the "theType" argument.
  const LocationContext *LCtx = C.getLocationContext();
  SVal TheTypeVal = state->getSVal(CE->getArg(1), LCtx);

  // FIXME: We really should allow ranges of valid theType values, and
  //   bifurcate the state appropriately.
  Optional<nonloc::ConcreteInt> V = TheTypeVal.getAs<nonloc::ConcreteInt>();
  if (!V)
    return;

  uint64_t NumberKind = V->getValue().getLimitedValue();
  Optional<uint64_t> OptTargetSize = GetCFNumberSize(Ctx, NumberKind);

  // FIXME: In some cases we can emit an error.
  if (!OptTargetSize)
    return;

  uint64_t TargetSize = *OptTargetSize;

  // Look at the value of the integer being passed by reference.  Essentially
  // we want to catch cases where the value passed in is not equal to the
  // size of the type being created.
  SVal TheValueExpr = state->getSVal(CE->getArg(2), LCtx);

  // FIXME: Eventually we should handle arbitrary locations.  We can do this
  //  by having an enhanced memory model that does low-level typing.
  Optional<loc::MemRegionVal> LV = TheValueExpr.getAs<loc::MemRegionVal>();
  if (!LV)
    return;

  const TypedValueRegion* R = dyn_cast<TypedValueRegion>(LV->stripCasts());
  if (!R)
    return;

  QualType T = Ctx.getCanonicalType(R->getValueType());

  // FIXME: If the pointee isn't an integer type, should we flag a warning?
  //  People can do weird stuff with pointers.

  if (!T->isIntegralOrEnumerationType())
    return;

  uint64_t SourceSize = Ctx.getTypeSize(T);

  // CHECK: is SourceSize == TargetSize
  if (SourceSize == TargetSize)
    return;

  // Generate an error.  Only generate a sink if 'SourceSize < TargetSize';
  // otherwise generate a regular node.
  //
  // FIXME: We can actually create an abstract "CFNumber" object that has
  //  the bits initialized to the provided values.
  //
  if (ExplodedNode *N = SourceSize < TargetSize ? C.generateSink() 
                                                : C.addTransition()) {
    SmallString<128> sbuf;
    llvm::raw_svector_ostream os(sbuf);
    
    os << (SourceSize == 8 ? "An " : "A ")
       << SourceSize << " bit integer is used to initialize a CFNumber "
                        "object that represents "
       << (TargetSize == 8 ? "an " : "a ")
       << TargetSize << " bit integer. ";
    
    if (SourceSize < TargetSize)
      os << (TargetSize - SourceSize)
      << " bits of the CFNumber value will be garbage." ;
    else
      os << (SourceSize - TargetSize)
      << " bits of the input integer will be lost.";

    if (!BT)
      BT.reset(new APIMisuse("Bad use of CFNumberCreate"));
    
    BugReport *report = new BugReport(*BT, os.str(), N);
    report->addRange(CE->getArg(2)->getSourceRange());
    C.emitReport(report);
  }
}

//===----------------------------------------------------------------------===//
// CFRetain/CFRelease/CFMakeCollectable checking for null arguments.
//===----------------------------------------------------------------------===//

namespace {
class CFRetainReleaseChecker : public Checker< check::PreStmt<CallExpr> > {
  mutable OwningPtr<APIMisuse> BT;
  mutable IdentifierInfo *Retain, *Release, *MakeCollectable;
public:
  CFRetainReleaseChecker(): Retain(0), Release(0), MakeCollectable(0) {}
  void checkPreStmt(const CallExpr *CE, CheckerContext &C) const;
};
} // end anonymous namespace


void CFRetainReleaseChecker::checkPreStmt(const CallExpr *CE,
                                          CheckerContext &C) const {
  // If the CallExpr doesn't have exactly 1 argument just give up checking.
  if (CE->getNumArgs() != 1)
    return;

  ProgramStateRef state = C.getState();
  const FunctionDecl *FD = C.getCalleeDecl(CE);
  if (!FD)
    return;
  
  if (!BT) {
    ASTContext &Ctx = C.getASTContext();
    Retain = &Ctx.Idents.get("CFRetain");
    Release = &Ctx.Idents.get("CFRelease");
    MakeCollectable = &Ctx.Idents.get("CFMakeCollectable");
    BT.reset(
      new APIMisuse("null passed to CFRetain/CFRelease/CFMakeCollectable"));
  }

  // Check if we called CFRetain/CFRelease/CFMakeCollectable.
  const IdentifierInfo *FuncII = FD->getIdentifier();
  if (!(FuncII == Retain || FuncII == Release || FuncII == MakeCollectable))
    return;

  // FIXME: The rest of this just checks that the argument is non-null.
  // It should probably be refactored and combined with NonNullParamChecker.

  // Get the argument's value.
  const Expr *Arg = CE->getArg(0);
  SVal ArgVal = state->getSVal(Arg, C.getLocationContext());
  Optional<DefinedSVal> DefArgVal = ArgVal.getAs<DefinedSVal>();
  if (!DefArgVal)
    return;

  // Get a NULL value.
  SValBuilder &svalBuilder = C.getSValBuilder();
  DefinedSVal zero =
      svalBuilder.makeZeroVal(Arg->getType()).castAs<DefinedSVal>();

  // Make an expression asserting that they're equal.
  DefinedOrUnknownSVal ArgIsNull = svalBuilder.evalEQ(state, zero, *DefArgVal);

  // Are they equal?
  ProgramStateRef stateTrue, stateFalse;
  llvm::tie(stateTrue, stateFalse) = state->assume(ArgIsNull);

  if (stateTrue && !stateFalse) {
    ExplodedNode *N = C.generateSink(stateTrue);
    if (!N)
      return;

    const char *description;
    if (FuncII == Retain)
      description = "Null pointer argument in call to CFRetain";
    else if (FuncII == Release)
      description = "Null pointer argument in call to CFRelease";
    else if (FuncII == MakeCollectable)
      description = "Null pointer argument in call to CFMakeCollectable";
    else
      llvm_unreachable("impossible case");

    BugReport *report = new BugReport(*BT, description, N);
    report->addRange(Arg->getSourceRange());
    bugreporter::trackNullOrUndefValue(N, Arg, *report);
    C.emitReport(report);
    return;
  }

  // From here on, we know the argument is non-null.
  C.addTransition(stateFalse);
}

//===----------------------------------------------------------------------===//
// Check for sending 'retain', 'release', or 'autorelease' directly to a Class.
//===----------------------------------------------------------------------===//

namespace {
class ClassReleaseChecker : public Checker<check::PreObjCMessage> {
  mutable Selector releaseS;
  mutable Selector retainS;
  mutable Selector autoreleaseS;
  mutable Selector drainS;
  mutable OwningPtr<BugType> BT;

public:
  void checkPreObjCMessage(const ObjCMethodCall &msg, CheckerContext &C) const;
};
}

void ClassReleaseChecker::checkPreObjCMessage(const ObjCMethodCall &msg,
                                              CheckerContext &C) const {
  
  if (!BT) {
    BT.reset(new APIMisuse("message incorrectly sent to class instead of class "
                           "instance"));
  
    ASTContext &Ctx = C.getASTContext();
    releaseS = GetNullarySelector("release", Ctx);
    retainS = GetNullarySelector("retain", Ctx);
    autoreleaseS = GetNullarySelector("autorelease", Ctx);
    drainS = GetNullarySelector("drain", Ctx);
  }
  
  if (msg.isInstanceMessage())
    return;
  const ObjCInterfaceDecl *Class = msg.getReceiverInterface();
  assert(Class);

  Selector S = msg.getSelector();
  if (!(S == releaseS || S == retainS || S == autoreleaseS || S == drainS))
    return;
  
  if (ExplodedNode *N = C.addTransition()) {
    SmallString<200> buf;
    llvm::raw_svector_ostream os(buf);

    os << "The '" << S.getAsString() << "' message should be sent to instances "
          "of class '" << Class->getName()
       << "' and not the class directly";
  
    BugReport *report = new BugReport(*BT, os.str(), N);
    report->addRange(msg.getSourceRange());
    C.emitReport(report);
  }
}

//===----------------------------------------------------------------------===//
// Check for passing non-Objective-C types to variadic methods that expect
// only Objective-C types.
//===----------------------------------------------------------------------===//

namespace {
class VariadicMethodTypeChecker : public Checker<check::PreObjCMessage> {
  mutable Selector arrayWithObjectsS;
  mutable Selector dictionaryWithObjectsAndKeysS;
  mutable Selector setWithObjectsS;
  mutable Selector orderedSetWithObjectsS;
  mutable Selector initWithObjectsS;
  mutable Selector initWithObjectsAndKeysS;
  mutable OwningPtr<BugType> BT;

  bool isVariadicMessage(const ObjCMethodCall &msg) const;

public:
  void checkPreObjCMessage(const ObjCMethodCall &msg, CheckerContext &C) const;
};
}

/// isVariadicMessage - Returns whether the given message is a variadic message,
/// where all arguments must be Objective-C types.
bool
VariadicMethodTypeChecker::isVariadicMessage(const ObjCMethodCall &msg) const {
  const ObjCMethodDecl *MD = msg.getDecl();
  
  if (!MD || !MD->isVariadic() || isa<ObjCProtocolDecl>(MD->getDeclContext()))
    return false;
  
  Selector S = msg.getSelector();
  
  if (msg.isInstanceMessage()) {
    // FIXME: Ideally we'd look at the receiver interface here, but that's not
    // useful for init, because alloc returns 'id'. In theory, this could lead
    // to false positives, for example if there existed a class that had an
    // initWithObjects: implementation that does accept non-Objective-C pointer
    // types, but the chance of that happening is pretty small compared to the
    // gains that this analysis gives.
    const ObjCInterfaceDecl *Class = MD->getClassInterface();

    switch (findKnownClass(Class)) {
    case FC_NSArray:
    case FC_NSOrderedSet:
    case FC_NSSet:
      return S == initWithObjectsS;
    case FC_NSDictionary:
      return S == initWithObjectsAndKeysS;
    default:
      return false;
    }
  } else {
    const ObjCInterfaceDecl *Class = msg.getReceiverInterface();

    switch (findKnownClass(Class)) {
      case FC_NSArray:
        return S == arrayWithObjectsS;
      case FC_NSOrderedSet:
        return S == orderedSetWithObjectsS;
      case FC_NSSet:
        return S == setWithObjectsS;
      case FC_NSDictionary:
        return S == dictionaryWithObjectsAndKeysS;
      default:
        return false;
    }
  }
}

void VariadicMethodTypeChecker::checkPreObjCMessage(const ObjCMethodCall &msg,
                                                    CheckerContext &C) const {
  if (!BT) {
    BT.reset(new APIMisuse("Arguments passed to variadic method aren't all "
                           "Objective-C pointer types"));

    ASTContext &Ctx = C.getASTContext();
    arrayWithObjectsS = GetUnarySelector("arrayWithObjects", Ctx);
    dictionaryWithObjectsAndKeysS = 
      GetUnarySelector("dictionaryWithObjectsAndKeys", Ctx);
    setWithObjectsS = GetUnarySelector("setWithObjects", Ctx);
    orderedSetWithObjectsS = GetUnarySelector("orderedSetWithObjects", Ctx);

    initWithObjectsS = GetUnarySelector("initWithObjects", Ctx);
    initWithObjectsAndKeysS = GetUnarySelector("initWithObjectsAndKeys", Ctx);
  }

  if (!isVariadicMessage(msg))
      return;

  // We are not interested in the selector arguments since they have
  // well-defined types, so the compiler will issue a warning for them.
  unsigned variadicArgsBegin = msg.getSelector().getNumArgs();

  // We're not interested in the last argument since it has to be nil or the
  // compiler would have issued a warning for it elsewhere.
  unsigned variadicArgsEnd = msg.getNumArgs() - 1;

  if (variadicArgsEnd <= variadicArgsBegin)
    return;

  // Verify that all arguments have Objective-C types.
  Optional<ExplodedNode*> errorNode;
  ProgramStateRef state = C.getState();
  
  for (unsigned I = variadicArgsBegin; I != variadicArgsEnd; ++I) {
    QualType ArgTy = msg.getArgExpr(I)->getType();
    if (ArgTy->isObjCObjectPointerType())
      continue;

    // Block pointers are treaded as Objective-C pointers.
    if (ArgTy->isBlockPointerType())
      continue;

    // Ignore pointer constants.
    if (msg.getArgSVal(I).getAs<loc::ConcreteInt>())
      continue;
    
    // Ignore pointer types annotated with 'NSObject' attribute.
    if (C.getASTContext().isObjCNSObjectType(ArgTy))
      continue;
    
    // Ignore CF references, which can be toll-free bridged.
    if (coreFoundation::isCFObjectRef(ArgTy))
      continue;

    // Generate only one error node to use for all bug reports.
    if (!errorNode.hasValue())
      errorNode = C.addTransition();

    if (!errorNode.getValue())
      continue;

    SmallString<128> sbuf;
    llvm::raw_svector_ostream os(sbuf);

    StringRef TypeName = GetReceiverInterfaceName(msg);
    if (!TypeName.empty())
      os << "Argument to '" << TypeName << "' method '";
    else
      os << "Argument to method '";

    os << msg.getSelector().getAsString() 
       << "' should be an Objective-C pointer type, not '";
    ArgTy.print(os, C.getLangOpts());
    os << "'";

    BugReport *R = new BugReport(*BT, os.str(), errorNode.getValue());
    R->addRange(msg.getArgSourceRange(I));
    C.emitReport(R);
  }
}

//===----------------------------------------------------------------------===//
// Improves the modeling of loops over Cocoa collections.
//===----------------------------------------------------------------------===//

// The map from container symbol to the container count symbol.
// We currently will remember the last countainer count symbol encountered.
REGISTER_MAP_WITH_PROGRAMSTATE(ContainerCountMap, SymbolRef, SymbolRef)

namespace {
class ObjCLoopChecker
  : public Checker<check::PostStmt<ObjCForCollectionStmt>,
                   check::PostObjCMessage,
                   check::DeadSymbols,
                   check::PointerEscape > {
  mutable IdentifierInfo *CountSelectorII;

  bool isCollectionCountMethod(const ObjCMethodCall &M,
                               CheckerContext &C) const;

public:
  ObjCLoopChecker() : CountSelectorII(0) {}
  void checkPostStmt(const ObjCForCollectionStmt *FCS, CheckerContext &C) const;
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const;
};
}

static bool isKnownNonNilCollectionType(QualType T) {
  const ObjCObjectPointerType *PT = T->getAs<ObjCObjectPointerType>();
  if (!PT)
    return false;
  
  const ObjCInterfaceDecl *ID = PT->getInterfaceDecl();
  if (!ID)
    return false;

  switch (findKnownClass(ID)) {
  case FC_NSArray:
  case FC_NSDictionary:
  case FC_NSEnumerator:
  case FC_NSOrderedSet:
  case FC_NSSet:
    return true;
  default:
    return false;
  }
}

/// Assumes that the collection is non-nil.
///
/// If the collection is known to be nil, returns NULL to indicate an infeasible
/// path.
static ProgramStateRef checkCollectionNonNil(CheckerContext &C,
                                             ProgramStateRef State,
                                             const ObjCForCollectionStmt *FCS) {
  if (!State)
    return NULL;

  SVal CollectionVal = C.getSVal(FCS->getCollection());
  Optional<DefinedSVal> KnownCollection = CollectionVal.getAs<DefinedSVal>();
  if (!KnownCollection)
    return State;

  ProgramStateRef StNonNil, StNil;
  llvm::tie(StNonNil, StNil) = State->assume(*KnownCollection);
  if (StNil && !StNonNil) {
    // The collection is nil. This path is infeasible.
    return NULL;
  }

  return StNonNil;
}

/// Assumes that the collection elements are non-nil.
///
/// This only applies if the collection is one of those known not to contain
/// nil values.
static ProgramStateRef checkElementNonNil(CheckerContext &C,
                                          ProgramStateRef State,
                                          const ObjCForCollectionStmt *FCS) {
  if (!State)
    return NULL;

  // See if the collection is one where we /know/ the elements are non-nil.
  if (!isKnownNonNilCollectionType(FCS->getCollection()->getType()))
    return State;

  const LocationContext *LCtx = C.getLocationContext();
  const Stmt *Element = FCS->getElement();

  // FIXME: Copied from ExprEngineObjC.
  Optional<Loc> ElementLoc;
  if (const DeclStmt *DS = dyn_cast<DeclStmt>(Element)) {
    const VarDecl *ElemDecl = cast<VarDecl>(DS->getSingleDecl());
    assert(ElemDecl->getInit() == 0);
    ElementLoc = State->getLValue(ElemDecl, LCtx);
  } else {
    ElementLoc = State->getSVal(Element, LCtx).getAs<Loc>();
  }

  if (!ElementLoc)
    return State;

  // Go ahead and assume the value is non-nil.
  SVal Val = State->getSVal(*ElementLoc);
  return State->assume(Val.castAs<DefinedOrUnknownSVal>(), true);
}

/// Returns NULL state if the collection is known to contain elements
/// (or is known not to contain elements if the Assumption parameter is false.)
static ProgramStateRef assumeCollectionNonEmpty(CheckerContext &C,
                                            ProgramStateRef State,
                                            const ObjCForCollectionStmt *FCS,
                                            bool Assumption = false) {
  if (!State)
    return NULL;

  SymbolRef CollectionS = C.getSVal(FCS->getCollection()).getAsSymbol();
  if (!CollectionS)
    return State;
  const SymbolRef *CountS = State->get<ContainerCountMap>(CollectionS);
  if (!CountS)
    return State;

  SValBuilder &SvalBuilder = C.getSValBuilder();
  SVal CountGreaterThanZeroVal =
    SvalBuilder.evalBinOp(State, BO_GT,
                          nonloc::SymbolVal(*CountS),
                          SvalBuilder.makeIntVal(0, (*CountS)->getType()),
                          SvalBuilder.getConditionType());
  Optional<DefinedSVal> CountGreaterThanZero =
    CountGreaterThanZeroVal.getAs<DefinedSVal>();
  if (!CountGreaterThanZero) {
    // The SValBuilder cannot construct a valid SVal for this condition.
    // This means we cannot properly reason about it.
    return State;
  }

  return State->assume(*CountGreaterThanZero, Assumption);
}

/// If the fist block edge is a back edge, we are reentering the loop.
static bool alreadyExecutedAtLeastOneLoopIteration(const ExplodedNode *N,
                                             const ObjCForCollectionStmt *FCS) {
  if (!N)
    return false;

  ProgramPoint P = N->getLocation();
  if (Optional<BlockEdge> BE = P.getAs<BlockEdge>()) {
    if (BE->getSrc()->getLoopTarget() == FCS)
      return true;
    return false;
  }

  // Keep looking for a block edge.
  for (ExplodedNode::const_pred_iterator I = N->pred_begin(),
                                         E = N->pred_end(); I != E; ++I) {
    if (alreadyExecutedAtLeastOneLoopIteration(*I, FCS))
      return true;
  }

  return false;
}

void ObjCLoopChecker::checkPostStmt(const ObjCForCollectionStmt *FCS,
                                    CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  // Check if this is the branch for the end of the loop.
  SVal CollectionSentinel = C.getSVal(FCS);
  if (CollectionSentinel.isZeroConstant()) {
    if (!alreadyExecutedAtLeastOneLoopIteration(C.getPredecessor(), FCS))
      State = assumeCollectionNonEmpty(C, State, FCS, /*Assumption*/false);

  // Otherwise, this is a branch that goes through the loop body.
  } else {
    State = checkCollectionNonNil(C, State, FCS);
    State = checkElementNonNil(C, State, FCS);
    State = assumeCollectionNonEmpty(C, State, FCS, /*Assumption*/true);
  }
  
  if (!State)
    C.generateSink();
  else if (State != C.getState())
    C.addTransition(State);
}

bool ObjCLoopChecker::isCollectionCountMethod(const ObjCMethodCall &M,
                                              CheckerContext &C) const {
  Selector S = M.getSelector();
  // Initialize the identifiers on first use.
  if (!CountSelectorII)
    CountSelectorII = &C.getASTContext().Idents.get("count");

  // If the method returns collection count, record the value.
  if (S.isUnarySelector() &&
      (S.getIdentifierInfoForSlot(0) == CountSelectorII))
    return true;
  
  return false;
}

void ObjCLoopChecker::checkPostObjCMessage(const ObjCMethodCall &M,
                                           CheckerContext &C) const {
  if (!M.isInstanceMessage())
    return;

  const ObjCInterfaceDecl *ClassID = M.getReceiverInterface();
  if (!ClassID)
    return;

  FoundationClass Class = findKnownClass(ClassID);
  if (Class != FC_NSDictionary &&
      Class != FC_NSArray &&
      Class != FC_NSSet &&
      Class != FC_NSOrderedSet)
    return;

  SymbolRef ContainerS = M.getReceiverSVal().getAsSymbol();
  if (!ContainerS)
    return;

  // If we are processing a call to "count", get the symbolic value returned by
  // a call to "count" and add it to the map.
  if (!isCollectionCountMethod(M, C))
    return;
  
  const Expr *MsgExpr = M.getOriginExpr();
  SymbolRef CountS = C.getSVal(MsgExpr).getAsSymbol();
  if (CountS) {
    ProgramStateRef State = C.getState();
    C.getSymbolManager().addSymbolDependency(ContainerS, CountS);
    State = State->set<ContainerCountMap>(ContainerS, CountS);
    C.addTransition(State);
  }
  return;
}

ProgramStateRef
ObjCLoopChecker::checkPointerEscape(ProgramStateRef State,
                                    const InvalidatedSymbols &Escaped,
                                    const CallEvent *Call,
                                    PointerEscapeKind Kind) const {
  // TODO: If we know that the call cannot change the collection count, there
  // is nothing to do, just return.

  // Remove the invalidated symbols form the collection count map.
  for (InvalidatedSymbols::const_iterator I = Escaped.begin(),
       E = Escaped.end();
       I != E; ++I) {
    SymbolRef Sym = *I;

    // The symbol escaped. Pessimistically, assume that the count could have
    // changed.
    State = State->remove<ContainerCountMap>(Sym);
  }
  return State;
}

void ObjCLoopChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                       CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  // Remove the dead symbols from the collection count map.
  ContainerCountMapTy Tracked = State->get<ContainerCountMap>();
  for (ContainerCountMapTy::iterator I = Tracked.begin(),
                                     E = Tracked.end(); I != E; ++I) {
    SymbolRef Sym = I->first;
    if (SymReaper.isDead(Sym))
      State = State->remove<ContainerCountMap>(Sym);
  }

  C.addTransition(State);
}

namespace {
/// \class ObjCNonNilReturnValueChecker
/// \brief The checker restricts the return values of APIs known to
/// never (or almost never) return 'nil'.
class ObjCNonNilReturnValueChecker
  : public Checker<check::PostObjCMessage> {
    mutable bool Initialized;
    mutable Selector ObjectAtIndex;
    mutable Selector ObjectAtIndexedSubscript;
    mutable Selector NullSelector;

public:
  ObjCNonNilReturnValueChecker() : Initialized(false) {}
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const;
};
}

static ProgramStateRef assumeExprIsNonNull(const Expr *NonNullExpr,
                                           ProgramStateRef State,
                                           CheckerContext &C) {
  SVal Val = State->getSVal(NonNullExpr, C.getLocationContext());
  if (Optional<DefinedOrUnknownSVal> DV = Val.getAs<DefinedOrUnknownSVal>())
    return State->assume(*DV, true);
  return State;
}

void ObjCNonNilReturnValueChecker::checkPostObjCMessage(const ObjCMethodCall &M,
                                                        CheckerContext &C)
                                                        const {
  ProgramStateRef State = C.getState();

  if (!Initialized) {
    ASTContext &Ctx = C.getASTContext();
    ObjectAtIndex = GetUnarySelector("objectAtIndex", Ctx);
    ObjectAtIndexedSubscript = GetUnarySelector("objectAtIndexedSubscript", Ctx);
    NullSelector = GetNullarySelector("null", Ctx);
  }

  // Check the receiver type.
  if (const ObjCInterfaceDecl *Interface = M.getReceiverInterface()) {

    // Assume that object returned from '[self init]' or '[super init]' is not
    // 'nil' if we are processing an inlined function/method.
    //
    // A defensive callee will (and should) check if the object returned by
    // '[super init]' is 'nil' before doing it's own initialization. However,
    // since 'nil' is rarely returned in practice, we should not warn when the
    // caller to the defensive constructor uses the object in contexts where
    // 'nil' is not accepted.
    if (!C.inTopFrame() && M.getDecl() &&
        M.getDecl()->getMethodFamily() == OMF_init &&
        M.isReceiverSelfOrSuper()) {
      State = assumeExprIsNonNull(M.getOriginExpr(), State, C);
    }

    FoundationClass Cl = findKnownClass(Interface);

    // Objects returned from
    // [NSArray|NSOrderedSet]::[ObjectAtIndex|ObjectAtIndexedSubscript]
    // are never 'nil'.
    if (Cl == FC_NSArray || Cl == FC_NSOrderedSet) {
      Selector Sel = M.getSelector();
      if (Sel == ObjectAtIndex || Sel == ObjectAtIndexedSubscript) {
        // Go ahead and assume the value is non-nil.
        State = assumeExprIsNonNull(M.getOriginExpr(), State, C);
      }
    }

    // Objects returned from [NSNull null] are not nil.
    if (Cl == FC_NSNull) {
      if (M.getSelector() == NullSelector) {
        // Go ahead and assume the value is non-nil.
        State = assumeExprIsNonNull(M.getOriginExpr(), State, C);
      }
    }
  }
  C.addTransition(State);
}

//===----------------------------------------------------------------------===//
// Check registration.
//===----------------------------------------------------------------------===//

void ento::registerNilArgChecker(CheckerManager &mgr) {
  mgr.registerChecker<NilArgChecker>();
}

void ento::registerCFNumberCreateChecker(CheckerManager &mgr) {
  mgr.registerChecker<CFNumberCreateChecker>();
}

void ento::registerCFRetainReleaseChecker(CheckerManager &mgr) {
  mgr.registerChecker<CFRetainReleaseChecker>();
}

void ento::registerClassReleaseChecker(CheckerManager &mgr) {
  mgr.registerChecker<ClassReleaseChecker>();
}

void ento::registerVariadicMethodTypeChecker(CheckerManager &mgr) {
  mgr.registerChecker<VariadicMethodTypeChecker>();
}

void ento::registerObjCLoopChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCLoopChecker>();
}

void ento::registerObjCNonNilReturnValueChecker(CheckerManager &mgr) {
  mgr.registerChecker<ObjCNonNilReturnValueChecker>();
}
