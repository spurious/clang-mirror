//===--- IdentifierTable.cpp - Hash table for identifier lookup -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the IdentifierInfo, IdentifierVisitor, and
// IdentifierTable interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/IdentifierTable.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Basic/LangOptions.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/DenseMap.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// Token Implementation
//===----------------------------------------------------------------------===//

/// isObjCAtKeyword - Return true if we have an ObjC keyword identifier. 
bool Token::isObjCAtKeyword(tok::ObjCKeywordKind objcKey) const {
  return getKind() == tok::identifier && 
         getIdentifierInfo()->getObjCKeywordID() == objcKey;
}

/// getObjCKeywordID - Return the ObjC keyword kind.
tok::ObjCKeywordKind Token::getObjCKeywordID() const {
  IdentifierInfo *specId = getIdentifierInfo();
  return specId ? specId->getObjCKeywordID() : tok::objc_not_keyword;
}

//===----------------------------------------------------------------------===//
// IdentifierInfo Implementation
//===----------------------------------------------------------------------===//

IdentifierInfo::IdentifierInfo() {
  Macro = 0;
  TokenID = tok::identifier;
  PPID = tok::pp_not_keyword;
  ObjCID = tok::objc_not_keyword;
  BuiltinID = 0;
  IsExtension = false;
  IsPoisoned = false;
  IsOtherTargetMacro = false;
  IsCPPOperatorKeyword = false;
  IsNonPortableBuiltin = false;
  FETokenInfo = 0;
}

IdentifierInfo::~IdentifierInfo() {
  delete Macro;
}

//===----------------------------------------------------------------------===//
// IdentifierTable Implementation
//===----------------------------------------------------------------------===//

IdentifierTable::IdentifierTable(const LangOptions &LangOpts)
  // Start with space for 8K identifiers.
  : HashTable(8192) {

  // Populate the identifier table with info about keywords for the current
  // language.
  AddKeywords(LangOpts);
}

//===----------------------------------------------------------------------===//
// Language Keyword Implementation
//===----------------------------------------------------------------------===//

/// AddKeyword - This method is used to associate a token ID with specific
/// identifiers because they are language keywords.  This causes the lexer to
/// automatically map matching identifiers to specialized token codes.
///
/// The C90/C99/CPP/CPP0x flags are set to 0 if the token should be
/// enabled in the specified langauge, set to 1 if it is an extension
/// in the specified language, and set to 2 if disabled in the
/// specified language.
static void AddKeyword(const char *Keyword, unsigned KWLen,
                       tok::TokenKind TokenCode,
                       int C90, int C99, int CXX, int CXX0x,
                       const LangOptions &LangOpts, IdentifierTable &Table) {
  int Flags = LangOpts.CPlusPlus ? (LangOpts.CPlusPlus0x? CXX0x : CXX)
                                 : (LangOpts.C99 ? C99 : C90);
  
  // Don't add this keyword if disabled in this language or if an extension
  // and extensions are disabled.
  if (Flags + LangOpts.NoExtensions >= 2) return;
  
  IdentifierInfo &Info = Table.get(Keyword, Keyword+KWLen);
  Info.setTokenID(TokenCode);
  Info.setIsExtensionToken(Flags == 1);
}

static void AddAlias(const char *Keyword, unsigned KWLen,
                     const char *AliaseeKeyword, unsigned AliaseeKWLen,
                     const LangOptions &LangOpts, IdentifierTable &Table) {
  IdentifierInfo &AliasInfo = Table.get(Keyword, Keyword+KWLen);
  IdentifierInfo &AliaseeInfo = Table.get(AliaseeKeyword,
                                          AliaseeKeyword+AliaseeKWLen);
  AliasInfo.setTokenID(AliaseeInfo.getTokenID());
  AliasInfo.setIsExtensionToken(AliaseeInfo.isExtensionToken());
}  

/// AddPPKeyword - Register a preprocessor keyword like "define" "undef" or 
/// "elif".
static void AddPPKeyword(tok::PPKeywordKind PPID, 
                         const char *Name, unsigned NameLen,
                         IdentifierTable &Table) {
  Table.get(Name, Name+NameLen).setPPKeywordID(PPID);
}

/// AddCXXOperatorKeyword - Register a C++ operator keyword alternative
/// representations.
static void AddCXXOperatorKeyword(const char *Keyword, unsigned KWLen,
                                  tok::TokenKind TokenCode,
                                  IdentifierTable &Table) {
  IdentifierInfo &Info = Table.get(Keyword, Keyword + KWLen);
  Info.setTokenID(TokenCode);
  Info.setIsCPlusplusOperatorKeyword();
}

/// AddObjCKeyword - Register an Objective-C @keyword like "class" "selector" or 
/// "property".
static void AddObjCKeyword(tok::ObjCKeywordKind ObjCID, 
                           const char *Name, unsigned NameLen,
                           IdentifierTable &Table) {
  Table.get(Name, Name+NameLen).setObjCKeywordID(ObjCID);
}

/// AddKeywords - Add all keywords to the symbol table.
///
void IdentifierTable::AddKeywords(const LangOptions &LangOpts) {
  enum {
    C90Shift = 0,
    EXTC90   = 1 << C90Shift,
    NOTC90   = 2 << C90Shift,
    C99Shift = 2,
    EXTC99   = 1 << C99Shift,
    NOTC99   = 2 << C99Shift,
    CPPShift = 4,
    EXTCPP   = 1 << CPPShift,
    NOTCPP   = 2 << CPPShift,
    CPP0xShift = 6,
    EXTCPP0x   = 1 << CPP0xShift,
    NOTCPP0x   = 2 << CPP0xShift,
    Mask     = 3
  };
  
  // Add keywords and tokens for the current language.
#define KEYWORD(NAME, FLAGS) \
  AddKeyword(#NAME, strlen(#NAME), tok::kw_ ## NAME,  \
             ((FLAGS) >> C90Shift) & Mask, \
             ((FLAGS) >> C99Shift) & Mask, \
             ((FLAGS) >> CPPShift) & Mask, \
             ((FLAGS) >> CPP0xShift) & Mask, LangOpts, *this);
#define ALIAS(NAME, TOK) \
  AddAlias(NAME, strlen(NAME), #TOK, strlen(#TOK), LangOpts, *this);
#define PPKEYWORD(NAME) \
  AddPPKeyword(tok::pp_##NAME, #NAME, strlen(#NAME), *this);
#define CXX_KEYWORD_OPERATOR(NAME, ALIAS) \
  if (LangOpts.CXXOperatorNames)          \
    AddCXXOperatorKeyword(#NAME, strlen(#NAME), tok::ALIAS, *this);
#define OBJC1_AT_KEYWORD(NAME) \
  if (LangOpts.ObjC1)          \
    AddObjCKeyword(tok::objc_##NAME, #NAME, strlen(#NAME), *this);
#define OBJC2_AT_KEYWORD(NAME) \
  if (LangOpts.ObjC2)          \
    AddObjCKeyword(tok::objc_##NAME, #NAME, strlen(#NAME), *this);
#include "clang/Basic/TokenKinds.def"
}


//===----------------------------------------------------------------------===//
// Stats Implementation
//===----------------------------------------------------------------------===//

/// PrintStats - Print statistics about how well the identifier table is doing
/// at hashing identifiers.
void IdentifierTable::PrintStats() const {
  unsigned NumBuckets = HashTable.getNumBuckets();
  unsigned NumIdentifiers = HashTable.getNumItems();
  unsigned NumEmptyBuckets = NumBuckets-NumIdentifiers;
  unsigned AverageIdentifierSize = 0;
  unsigned MaxIdentifierLength = 0;
  
  // TODO: Figure out maximum times an identifier had to probe for -stats.
  for (llvm::StringMap<IdentifierInfo, llvm::BumpPtrAllocator>::const_iterator
       I = HashTable.begin(), E = HashTable.end(); I != E; ++I) {
    unsigned IdLen = I->getKeyLength();
    AverageIdentifierSize += IdLen;
    if (MaxIdentifierLength < IdLen)
      MaxIdentifierLength = IdLen;
  }
  
  fprintf(stderr, "\n*** Identifier Table Stats:\n");
  fprintf(stderr, "# Identifiers:   %d\n", NumIdentifiers);
  fprintf(stderr, "# Empty Buckets: %d\n", NumEmptyBuckets);
  fprintf(stderr, "Hash density (#identifiers per bucket): %f\n",
          NumIdentifiers/(double)NumBuckets);
  fprintf(stderr, "Ave identifier length: %f\n",
          (AverageIdentifierSize/(double)NumIdentifiers));
  fprintf(stderr, "Max identifier length: %d\n", MaxIdentifierLength);
  
  // Compute statistics about the memory allocated for identifiers.
  HashTable.getAllocator().PrintStats();
}

//===----------------------------------------------------------------------===//
// SelectorTable Implementation
//===----------------------------------------------------------------------===//

unsigned llvm::DenseMapInfo<clang::Selector>::getHashValue(clang::Selector S) {
  return DenseMapInfo<void*>::getHashValue(S.getAsOpaquePtr());
}


/// MultiKeywordSelector - One of these variable length records is kept for each
/// selector containing more than one keyword. We use a folding set
/// to unique aggregate names (keyword selectors in ObjC parlance). Access to 
/// this class is provided strictly through Selector.
namespace clang {
class MultiKeywordSelector : public llvm::FoldingSetNode {
public:  
  unsigned NumArgs;

  // Constructor for keyword selectors.
  MultiKeywordSelector(unsigned nKeys, IdentifierInfo **IIV) {
    assert((nKeys > 1) && "not a multi-keyword selector");
    NumArgs = nKeys;
    // Fill in the trailing keyword array.
    IdentifierInfo **KeyInfo = reinterpret_cast<IdentifierInfo **>(this+1);
    for (unsigned i = 0; i != nKeys; ++i)
      KeyInfo[i] = IIV[i];
  }
  // getName - Derive the full selector name and return it.
  std::string getName() const;
    
  unsigned getNumArgs() const { return NumArgs; }
  
  typedef IdentifierInfo *const *keyword_iterator;
  keyword_iterator keyword_begin() const {
    return reinterpret_cast<keyword_iterator>(this+1);
  }
  keyword_iterator keyword_end() const { 
    return keyword_begin()+NumArgs; 
  }
  IdentifierInfo *getIdentifierInfoForSlot(unsigned i) const {
    assert(i < NumArgs && "getIdentifierInfoForSlot(): illegal index");
    return keyword_begin()[i];
  }
  static void Profile(llvm::FoldingSetNodeID &ID, 
                      keyword_iterator ArgTys, unsigned NumArgs) {
    ID.AddInteger(NumArgs);
    for (unsigned i = 0; i != NumArgs; ++i)
      ID.AddPointer(ArgTys[i]);
  }
  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, keyword_begin(), NumArgs);
  }
};
} // end namespace clang.

unsigned Selector::getNumArgs() const {
  unsigned IIF = getIdentifierInfoFlag();
  if (IIF == ZeroArg)
    return 0;
  if (IIF == OneArg)
    return 1;
  // We point to a MultiKeywordSelector (pointer doesn't contain any flags).
  MultiKeywordSelector *SI = reinterpret_cast<MultiKeywordSelector *>(InfoPtr);
  return SI->getNumArgs(); 
}

IdentifierInfo *Selector::getIdentifierInfoForSlot(unsigned argIndex) const {
  if (IdentifierInfo *II = getAsIdentifierInfo()) {
    assert(argIndex == 0 && "illegal keyword index");
    return II;
  }
  // We point to a MultiKeywordSelector (pointer doesn't contain any flags).
  MultiKeywordSelector *SI = reinterpret_cast<MultiKeywordSelector *>(InfoPtr);
  return SI->getIdentifierInfoForSlot(argIndex);
}

std::string MultiKeywordSelector::getName() const {
  std::string Result;
  unsigned Length = 0;
  for (keyword_iterator I = keyword_begin(), E = keyword_end(); I != E; ++I) {
    if (*I)
      Length += (*I)->getLength();
    ++Length;  // :
  }
  
  Result.reserve(Length);
  
  for (keyword_iterator I = keyword_begin(), E = keyword_end(); I != E; ++I) {
    if (*I)
      Result.insert(Result.end(), (*I)->getName(),
                    (*I)->getName()+(*I)->getLength());
    Result.push_back(':');
  }
  
  return Result;
}

std::string Selector::getName() const {
  if (IdentifierInfo *II = getAsIdentifierInfo()) {
    if (getNumArgs() == 0)
      return II->getName();
    
    std::string Res = II->getName();
    Res += ":";
    return Res;
  }
  
  // We have a multiple keyword selector (no embedded flags).
  return reinterpret_cast<MultiKeywordSelector *>(InfoPtr)->getName();
}


Selector SelectorTable::getSelector(unsigned nKeys, IdentifierInfo **IIV) {
  if (nKeys < 2)
    return Selector(IIV[0], nKeys);
  
  llvm::FoldingSet<MultiKeywordSelector> *SelTab;
  
  SelTab = static_cast<llvm::FoldingSet<MultiKeywordSelector> *>(Impl);
    
  // Unique selector, to guarantee there is one per name.
  llvm::FoldingSetNodeID ID;
  MultiKeywordSelector::Profile(ID, IIV, nKeys);

  void *InsertPos = 0;
  if (MultiKeywordSelector *SI = SelTab->FindNodeOrInsertPos(ID, InsertPos))
    return Selector(SI);
  
  // MultiKeywordSelector objects are not allocated with new because they have a
  // variable size array (for parameter types) at the end of them.
  MultiKeywordSelector *SI = 
    (MultiKeywordSelector*)malloc(sizeof(MultiKeywordSelector) + 
                                  nKeys*sizeof(IdentifierInfo *));
  new (SI) MultiKeywordSelector(nKeys, IIV);
  SelTab->InsertNode(SI, InsertPos);
  return Selector(SI);
}

SelectorTable::SelectorTable() {
  Impl = new llvm::FoldingSet<MultiKeywordSelector>;
}

SelectorTable::~SelectorTable() {
  delete static_cast<llvm::FoldingSet<MultiKeywordSelector> *>(Impl);
}


