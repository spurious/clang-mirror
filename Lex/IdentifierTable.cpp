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

char *MultiKeywordSelector::getName(llvm::SmallVectorImpl<char> &methodName) {
  methodName[0] = '\0';
  keyword_iterator KeyIter = keyword_begin();
  for (unsigned int i = 0; i < NumArgs; i++) {
    if (KeyIter[i]) {
      unsigned KeyLen = strlen(KeyIter[i]->getName());
      methodName.append(KeyIter[i]->getName(), KeyIter[i]->getName()+KeyLen);
    }
    methodName.push_back(':');
  }
  methodName.push_back('\0');
  return &methodName[0];
}

char *Selector::getName(llvm::SmallVectorImpl<char> &methodName) {
  methodName[0] = '\0';
  IdentifierInfo *II = getAsIdentifierInfo();
  if (II) {
    unsigned NameLen = strlen(II->getName());
    methodName.append(II->getName(), II->getName()+NameLen);
    if (getNumArgs() == 1)
      methodName.push_back(':');
    methodName.push_back('\0');
  } else { // We have a multiple keyword selector (no embedded flags).
    MultiKeywordSelector *SI = reinterpret_cast<MultiKeywordSelector *>(InfoPtr);
    SI->getName(methodName);
  }
  return &methodName[0];
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

