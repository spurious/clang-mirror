//===--- Preprocess.cpp - C Language Family Preprocessor Implementation ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Preprocessor interface.
//
//===----------------------------------------------------------------------===//
//
// TODO: GCC Diagnostics emitted by the lexer:
//
// ERROR  : __VA_ARGS__ can only appear in the expansion of a C99 variadic macro
//
// Options to support:
//   -H       - Print the name of each header file used.
//   -C -CC   - Do not discard comments for cpp.
//   -d[MDNI] - Dump various things.
//   -fworking-directory - #line's with preprocessor's working dir.
//   -fpreprocessed
//   -dependency-file,-M,-MM,-MF,-MG,-MP,-MT,-MQ,-MD,-MMD
//   -W*
//   -w
//
// Messages to emit:
//   "Multiple include guards may be useful for:\n"
//
// TODO: Implement the include guard optimization.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Pragma.h"
#include "clang/Lex/ScratchBuffer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include <iostream>
using namespace llvm;
using namespace clang;

//===----------------------------------------------------------------------===//

Preprocessor::Preprocessor(Diagnostic &diags, const LangOptions &opts, 
                           FileManager &FM, SourceManager &SM) 
  : Diags(diags), Features(opts), FileMgr(FM), SourceMgr(SM),
    SystemDirIdx(0), NoCurDirSearch(false),
    CurLexer(0), CurDirLookup(0), CurMacroExpander(0) {
  ScratchBuf = new ScratchBuffer(SourceMgr);
      
  // Clear stats.
  NumDirectives = NumIncluded = NumDefined = NumUndefined = NumPragma = 0;
  NumIf = NumElse = NumEndif = 0;
  NumEnteredSourceFiles = NumMacroExpanded = NumFastMacroExpanded = 0;
  MaxIncludeStackDepth = 0;
  NumSkipped = 0;
    
  // Macro expansion is enabled.
  DisableMacroExpansion = false;
  SkippingContents = false;

  // There is no file-change handler yet.
  FileChangeHandler = 0;
  
  // Initialize the pragma handlers.
  PragmaHandlers = new PragmaNamespace(0);
  RegisterBuiltinPragmas();
  
  // Initialize builtin macros like __LINE__ and friends.
  RegisterBuiltinMacros();
}

Preprocessor::~Preprocessor() {
  // Free any active lexers.
  delete CurLexer;
  
  while (!IncludeMacroStack.empty()) {
    delete IncludeMacroStack.back().TheLexer;
    delete IncludeMacroStack.back().TheMacroExpander;
    IncludeMacroStack.pop_back();
  }
  
  // Release pragma information.
  delete PragmaHandlers;

  // Delete the scratch buffer info.
  delete ScratchBuf;
}

/// getFileInfo - Return the PerFileInfo structure for the specified
/// FileEntry.
Preprocessor::PerFileInfo &Preprocessor::getFileInfo(const FileEntry *FE) {
  if (FE->getUID() >= FileInfo.size())
    FileInfo.resize(FE->getUID()+1);
  return FileInfo[FE->getUID()];
}  


/// AddKeywords - Add all keywords to the symbol table.
///
void Preprocessor::AddKeywords() {
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
    Mask     = 3
  };

  // Add keywords and tokens for the current language.
#define KEYWORD(NAME, FLAGS) \
  AddKeyword(#NAME+1, tok::kw##NAME,     \
             (FLAGS >> C90Shift) & Mask, \
             (FLAGS >> C99Shift) & Mask, \
             (FLAGS >> CPPShift) & Mask);
#define ALIAS(NAME, TOK) \
  AddKeyword(NAME, tok::kw_ ## TOK, 0, 0, 0);
#include "clang/Basic/TokenKinds.def"
}

/// Diag - Forwarding function for diagnostics.  This emits a diagnostic at
/// the specified LexerToken's location, translating the token's start
/// position in the current buffer into a SourcePosition object for rendering.
void Preprocessor::Diag(SourceLocation Loc, unsigned DiagID, 
                        const std::string &Msg) {
  // If we are in a '#if 0' block, don't emit any diagnostics for notes,
  // warnings or extensions.
  if (isSkipping() && Diagnostic::isNoteWarningOrExtension(DiagID))
    return;
  
  Diags.Report(Loc, DiagID, Msg);
}
void Preprocessor::Diag(const LexerToken &Tok, unsigned DiagID,
                        const std::string &Msg) {
  // If we are in a '#if 0' block, don't emit any diagnostics for notes,
  // warnings or extensions.
  if (isSkipping() && Diagnostic::isNoteWarningOrExtension(DiagID))
    return;
  
  Diag(Tok.getLocation(), DiagID, Msg);
}


void Preprocessor::DumpToken(const LexerToken &Tok, bool DumpFlags) const {
  std::cerr << tok::getTokenName(Tok.getKind()) << " '"
            << getSpelling(Tok) << "'";
  
  if (!DumpFlags) return;
  std::cerr << "\t";
  if (Tok.isAtStartOfLine())
    std::cerr << " [StartOfLine]";
  if (Tok.hasLeadingSpace())
    std::cerr << " [LeadingSpace]";
  if (Tok.needsCleaning()) {
    const char *Start = SourceMgr.getCharacterData(Tok.getLocation());
    std::cerr << " [UnClean='" << std::string(Start, Start+Tok.getLength())
              << "']";
  }
}

void Preprocessor::DumpMacro(const MacroInfo &MI) const {
  std::cerr << "MACRO: ";
  for (unsigned i = 0, e = MI.getNumTokens(); i != e; ++i) {
    DumpToken(MI.getReplacementToken(i));
    std::cerr << "  ";
  }
  std::cerr << "\n";
}

void Preprocessor::PrintStats() {
  std::cerr << "\n*** Preprocessor Stats:\n";
  std::cerr << FileInfo.size() << " files tracked.\n";
  unsigned NumOnceOnlyFiles = 0, MaxNumIncludes = 0, NumSingleIncludedFiles = 0;
  for (unsigned i = 0, e = FileInfo.size(); i != e; ++i) {
    NumOnceOnlyFiles += FileInfo[i].isImport;
    if (MaxNumIncludes < FileInfo[i].NumIncludes)
      MaxNumIncludes = FileInfo[i].NumIncludes;
    NumSingleIncludedFiles += FileInfo[i].NumIncludes == 1;
  }
  std::cerr << "  " << NumOnceOnlyFiles << " #import/#pragma once files.\n";
  std::cerr << "  " << NumSingleIncludedFiles << " included exactly once.\n";
  std::cerr << "  " << MaxNumIncludes << " max times a file is included.\n";
  
  std::cerr << NumDirectives << " directives found:\n";
  std::cerr << "  " << NumDefined << " #define.\n";
  std::cerr << "  " << NumUndefined << " #undef.\n";
  std::cerr << "  " << NumIncluded << " #include/#include_next/#import.\n";
  std::cerr << "    " << NumEnteredSourceFiles << " source files entered.\n";
  std::cerr << "    " << MaxIncludeStackDepth << " max include stack depth\n";
  std::cerr << "  " << NumIf << " #if/#ifndef/#ifdef.\n";
  std::cerr << "  " << NumElse << " #else/#elif.\n";
  std::cerr << "  " << NumEndif << " #endif.\n";
  std::cerr << "  " << NumPragma << " #pragma.\n";
  std::cerr << NumSkipped << " #if/#ifndef#ifdef regions skipped\n";

  std::cerr << NumMacroExpanded << " macros expanded, "
            << NumFastMacroExpanded << " on the fast path.\n";
}

//===----------------------------------------------------------------------===//
// Token Spelling
//===----------------------------------------------------------------------===//


/// getSpelling() - Return the 'spelling' of this token.  The spelling of a
/// token are the characters used to represent the token in the source file
/// after trigraph expansion and escaped-newline folding.  In particular, this
/// wants to get the true, uncanonicalized, spelling of things like digraphs
/// UCNs, etc.
std::string Preprocessor::getSpelling(const LexerToken &Tok) const {
  assert((int)Tok.getLength() >= 0 && "Token character range is bogus!");
  
  // If this token contains nothing interesting, return it directly.
  const char *TokStart = SourceMgr.getCharacterData(Tok.getLocation());
  assert(TokStart && "Token has invalid location!");
  if (!Tok.needsCleaning())
    return std::string(TokStart, TokStart+Tok.getLength());
  
  // Otherwise, hard case, relex the characters into the string.
  std::string Result;
  Result.reserve(Tok.getLength());
  
  for (const char *Ptr = TokStart, *End = TokStart+Tok.getLength();
       Ptr != End; ) {
    unsigned CharSize;
    Result.push_back(Lexer::getCharAndSizeNoWarn(Ptr, CharSize, Features));
    Ptr += CharSize;
  }
  assert(Result.size() != unsigned(Tok.getLength()) &&
         "NeedsCleaning flag set on something that didn't need cleaning!");
  return Result;
}

/// getSpelling - This method is used to get the spelling of a token into a
/// preallocated buffer, instead of as an std::string.  The caller is required
/// to allocate enough space for the token, which is guaranteed to be at least
/// Tok.getLength() bytes long.  The actual length of the token is returned.
unsigned Preprocessor::getSpelling(const LexerToken &Tok, char *Buffer) const {
  assert((int)Tok.getLength() >= 0 && "Token character range is bogus!");
  
  const char *TokStart = SourceMgr.getCharacterData(Tok.getLocation());
  assert(TokStart && "Token has invalid location!");

  // If this token contains nothing interesting, return it directly.
  if (!Tok.needsCleaning()) {
    unsigned Size = Tok.getLength();
    memcpy(Buffer, TokStart, Size);
    return Size;
  }
  // Otherwise, hard case, relex the characters into the string.
  std::string Result;
  Result.reserve(Tok.getLength());
  
  char *OutBuf = Buffer;
  for (const char *Ptr = TokStart, *End = TokStart+Tok.getLength();
       Ptr != End; ) {
    unsigned CharSize;
    *OutBuf++ = Lexer::getCharAndSizeNoWarn(Ptr, CharSize, Features);
    Ptr += CharSize;
  }
  assert(unsigned(OutBuf-Buffer) != Tok.getLength() &&
         "NeedsCleaning flag set on something that didn't need cleaning!");
  
  return OutBuf-Buffer;
}

//===----------------------------------------------------------------------===//
// Source File Location Methods.
//===----------------------------------------------------------------------===//


/// LookupFile - Given a "foo" or <foo> reference, look up the indicated file,
/// return null on failure.  isAngled indicates whether the file reference is
/// for system #include's or not (i.e. using <> instead of "").
const FileEntry *Preprocessor::LookupFile(const std::string &Filename, 
                                          bool isAngled,
                                          const DirectoryLookup *FromDir,
                                          const DirectoryLookup *&CurDir) {
  assert(CurLexer && "Cannot enter a #include inside a macro expansion!");
  CurDir = 0;
  
  // If 'Filename' is absolute, check to see if it exists and no searching.
  // FIXME: Portability.  This should be a sys::Path interface, this doesn't
  // handle things like C:\foo.txt right, nor win32 \\network\device\blah.
  if (Filename[0] == '/') {
    // If this was an #include_next "/absolute/file", fail.
    if (FromDir) return 0;

    // Otherwise, just return the file.
    return FileMgr.getFile(Filename);
  }
  
  // Step #0, unless disabled, check to see if the file is in the #includer's
  // directory.  This search is not done for <> headers.
  if (!isAngled && !FromDir && !NoCurDirSearch) {
    const FileEntry *CurFE = 
      SourceMgr.getFileEntryForFileID(CurLexer->getCurFileID());
    if (CurFE) {
      // Concatenate the requested file onto the directory.
      // FIXME: Portability.  Should be in sys::Path.
      if (const FileEntry *FE = 
            FileMgr.getFile(CurFE->getDir()->getName()+"/"+Filename)) {
        if (CurDirLookup)
          CurDir = CurDirLookup;
        else
          CurDir = 0;
        
        // This file is a system header or C++ unfriendly if the old file is.
        getFileInfo(FE).DirInfo = getFileInfo(CurFE).DirInfo;
        return FE;
      }
    }
  }
  
  // If this is a system #include, ignore the user #include locs.
  unsigned i = isAngled ? SystemDirIdx : 0;

  // If this is a #include_next request, start searching after the directory the
  // file was found in.
  if (FromDir)
    i = FromDir-&SearchDirs[0];
  
  // Check each directory in sequence to see if it contains this file.
  for (; i != SearchDirs.size(); ++i) {
    // Concatenate the requested file onto the directory.
    // FIXME: Portability.  Adding file to dir should be in sys::Path.
    std::string SearchDir = SearchDirs[i].getDir()->getName()+"/"+Filename;
    if (const FileEntry *FE = FileMgr.getFile(SearchDir)) {
      CurDir = &SearchDirs[i];
      
      // This file is a system header or C++ unfriendly if the dir is.
      getFileInfo(FE).DirInfo = CurDir->getDirCharacteristic();
      return FE;
    }
  }
  
  // Otherwise, didn't find it.
  return 0;
}

/// isInPrimaryFile - Return true if we're in the top-level file, not in a
/// #include.
bool Preprocessor::isInPrimaryFile() const {
  unsigned NumLexersFound = 0;
  if (CurLexer && !CurLexer->Is_PragmaLexer)
    ++NumLexersFound;
  
  /// If there are any stacked lexers, we're in a #include.
  for (unsigned i = 0, e = IncludeMacroStack.size(); i != e; ++i)
    if (IncludeMacroStack[i].TheLexer) {
      if (!IncludeMacroStack[i].TheLexer->Is_PragmaLexer)
        if (++NumLexersFound > 1)
          return false;
    }
  return NumLexersFound < 2;
}

/// getCurrentLexer - Return the current file lexer being lexed from.  Note
/// that this ignores any potentially active macro expansions and _Pragma
/// expansions going on at the time.
Lexer *Preprocessor::getCurrentFileLexer() const {
  if (CurLexer && !CurLexer->Is_PragmaLexer) return CurLexer;
  
  // Look for a stacked lexer.
  for (unsigned i = IncludeMacroStack.size(); i != 0; --i) {
    Lexer *L = IncludeMacroStack[i].TheLexer;
    if (L && !L->Is_PragmaLexer) // Ignore macro & _Pragma expansions.
      return L;
  }
  return 0;
}


/// EnterSourceFile - Add a source file to the top of the include stack and
/// start lexing tokens from it instead of the current buffer.  Return true
/// on failure.
void Preprocessor::EnterSourceFile(unsigned FileID,
                                   const DirectoryLookup *CurDir) {
  assert(CurMacroExpander == 0 && "Cannot #include a file inside a macro!");
  ++NumEnteredSourceFiles;
  
  if (MaxIncludeStackDepth < IncludeMacroStack.size())
    MaxIncludeStackDepth = IncludeMacroStack.size();

  const SourceBuffer *Buffer = SourceMgr.getBuffer(FileID);
  Lexer *TheLexer = new Lexer(Buffer, FileID, *this);
  EnterSourceFileWithLexer(TheLexer, CurDir);
}  
  
/// EnterSourceFile - Add a source file to the top of the include stack and
/// start lexing tokens from it instead of the current buffer.
void Preprocessor::EnterSourceFileWithLexer(Lexer *TheLexer, 
                                            const DirectoryLookup *CurDir) {
    
  // Add the current lexer to the include stack.
  if (CurLexer || CurMacroExpander)
    IncludeMacroStack.push_back(IncludeStackInfo(CurLexer, CurDirLookup,
                                                 CurMacroExpander));
  
  CurLexer = TheLexer;
  CurDirLookup = CurDir;
  CurMacroExpander = 0;
  
  // Notify the client, if desired, that we are in a new source file.
  if (FileChangeHandler && !CurLexer->Is_PragmaLexer) {
    DirectoryLookup::DirType FileType = DirectoryLookup::NormalHeaderDir;
    
    // Get the file entry for the current file.
    if (const FileEntry *FE = 
          SourceMgr.getFileEntryForFileID(CurLexer->getCurFileID()))
      FileType = getFileInfo(FE).DirInfo;
    
    FileChangeHandler(SourceLocation(CurLexer->getCurFileID(), 0),
                      EnterFile, FileType);
  }
}



/// EnterMacro - Add a Macro to the top of the include stack and start lexing
/// tokens from it instead of the current buffer.
void Preprocessor::EnterMacro(LexerToken &Tok) {
  IdentifierTokenInfo *Identifier = Tok.getIdentifierInfo();
  MacroInfo &MI = *Identifier->getMacroInfo();
  IncludeMacroStack.push_back(IncludeStackInfo(CurLexer, CurDirLookup,
                                               CurMacroExpander));
  CurLexer     = 0;
  CurDirLookup = 0;
  
  // TODO: Figure out arguments.
  
  // Mark the macro as currently disabled, so that it is not recursively
  // expanded.
  MI.DisableMacro();
  CurMacroExpander = new MacroExpander(Tok, *this);
}

//===----------------------------------------------------------------------===//
// Macro Expansion Handling.
//===----------------------------------------------------------------------===//

/// RegisterBuiltinMacro - Register the specified identifier in the identifier
/// table and mark it as a builtin macro to be expanded.
IdentifierTokenInfo *Preprocessor::RegisterBuiltinMacro(const char *Name) {
  // Get the identifier.
  IdentifierTokenInfo *Id = getIdentifierInfo(Name);
  
  // Mark it as being a macro that is builtin.
  MacroInfo *MI = new MacroInfo(SourceLocation());
  MI->setIsBuiltinMacro();
  Id->setMacroInfo(MI);
  return Id;
}


/// RegisterBuiltinMacros - Register builtin macros, such as __LINE__ with the
/// identifier table.
void Preprocessor::RegisterBuiltinMacros() {
  Ident__LINE__ = RegisterBuiltinMacro("__LINE__");
  Ident__FILE__ = RegisterBuiltinMacro("__FILE__");
  Ident__DATE__ = RegisterBuiltinMacro("__DATE__");
  Ident__TIME__ = RegisterBuiltinMacro("__TIME__");
  Ident_Pragma  = RegisterBuiltinMacro("_Pragma");
  
  // GCC Extensions.
  Ident__BASE_FILE__     = RegisterBuiltinMacro("__BASE_FILE__");
  Ident__INCLUDE_LEVEL__ = RegisterBuiltinMacro("__INCLUDE_LEVEL__");
  Ident__TIMESTAMP__     = RegisterBuiltinMacro("__TIMESTAMP__");
  
  // FIXME: implement them all:
//Pseudo #defines.
  // __STDC__    1 if !stdc_0_in_system_headers and "std"
  // __STDC_VERSION__
  // __STDC_HOSTED__
  // __OBJC__
}


/// HandleMacroExpandedIdentifier - If an identifier token is read that is to be
/// expanded as a macro, handle it and return the next token as 'Identifier'.
void Preprocessor::HandleMacroExpandedIdentifier(LexerToken &Identifier, 
                                                 MacroInfo *MI) {
  ++NumMacroExpanded;

  // If this is a builtin macro, like __LINE__ or _Pragma, handle it specially.
  if (MI->isBuiltinMacro())
    return ExpandBuiltinMacro(Identifier);
  
  // If we started lexing a macro, enter the macro expansion body.
  // FIXME: Read/Validate the argument list here!
  
  
  // If this macro expands to no tokens, don't bother to push it onto the
  // expansion stack, only to take it right back off.
  if (MI->getNumTokens() == 0) {
    // Ignore this macro use, just return the next token in the current
    // buffer.
    bool HadLeadingSpace = Identifier.hasLeadingSpace();
    bool IsAtStartOfLine = Identifier.isAtStartOfLine();
    
    Lex(Identifier);
    
    // If the identifier isn't on some OTHER line, inherit the leading
    // whitespace/first-on-a-line property of this token.  This handles
    // stuff like "! XX," -> "! ," and "   XX," -> "    ,", when XX is
    // empty.
    if (!Identifier.isAtStartOfLine()) {
      if (IsAtStartOfLine) Identifier.SetFlag(LexerToken::StartOfLine);
      if (HadLeadingSpace) Identifier.SetFlag(LexerToken::LeadingSpace);
    }
    ++NumFastMacroExpanded;
    return;
    
  } else if (MI->getNumTokens() == 1 &&
             // Don't handle identifiers if they need recursive expansion.
             (MI->getReplacementToken(0).getIdentifierInfo() == 0 ||
              !MI->getReplacementToken(0).getIdentifierInfo()->getMacroInfo())){
    // FIXME: Function-style macros only if no arguments?
    
    // Otherwise, if this macro expands into a single trivially-expanded
    // token: expand it now.  This handles common cases like 
    // "#define VAL 42".
    
    // Propagate the isAtStartOfLine/hasLeadingSpace markers of the macro
    // identifier to the expanded token.
    bool isAtStartOfLine = Identifier.isAtStartOfLine();
    bool hasLeadingSpace = Identifier.hasLeadingSpace();
    
    // Remember where the token is instantiated.
    SourceLocation InstantiateLoc = Identifier.getLocation();
    
    // Replace the result token.
    Identifier = MI->getReplacementToken(0);
    
    // Restore the StartOfLine/LeadingSpace markers.
    Identifier.SetFlagValue(LexerToken::StartOfLine , isAtStartOfLine);
    Identifier.SetFlagValue(LexerToken::LeadingSpace, hasLeadingSpace);
    
    // Update the tokens location to include both its logical and physical
    // locations.
    SourceLocation Loc =
      SourceMgr.getInstantiationLoc(Identifier.getLocation(), InstantiateLoc);
    Identifier.SetLocation(Loc);
    
    // Since this is not an identifier token, it can't be macro expanded, so
    // we're done.
    ++NumFastMacroExpanded;
    return;
  }
  
  // Start expanding the macro (FIXME, pass arguments).
  EnterMacro(Identifier);
  
  // Now that the macro is at the top of the include stack, ask the
  // preprocessor to read the next token from it.
  return Lex(Identifier);
}

/// ComputeDATE_TIME - Compute the current time, enter it into the specified
/// scratch buffer, then return DATELoc/TIMELoc locations with the position of
/// the identifier tokens inserted.
static void ComputeDATE_TIME(SourceLocation &DATELoc, SourceLocation &TIMELoc,
                             ScratchBuffer *ScratchBuf) {
  time_t TT = time(0);
  struct tm *TM = localtime(&TT);
  
  static const char * const Months[] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
  };
  
  char TmpBuffer[100];
  sprintf(TmpBuffer, "\"%s %2d %4d\"", Months[TM->tm_mon], TM->tm_mday, 
          TM->tm_year+1900);
  DATELoc = ScratchBuf->getToken(TmpBuffer, strlen(TmpBuffer));

  sprintf(TmpBuffer, "\"%02d:%02d:%02d\"", TM->tm_hour, TM->tm_min, TM->tm_sec);
  TIMELoc = ScratchBuf->getToken(TmpBuffer, strlen(TmpBuffer));
}

/// ExpandBuiltinMacro - If an identifier token is read that is to be expanded
/// as a builtin macro, handle it and return the next token as 'Tok'.
void Preprocessor::ExpandBuiltinMacro(LexerToken &Tok) {
  // Figure out which token this is.
  IdentifierTokenInfo *ITI = Tok.getIdentifierInfo();
  assert(ITI && "Can't be a macro without id info!");
  
  // If this is an _Pragma directive, expand it, invoke the pragma handler, then
  // lex the token after it.
  if (ITI == Ident_Pragma)
    return Handle_Pragma(Tok);
  
  char TmpBuffer[100];

  // Set up the return result.
  Tok.SetIdentifierInfo(0);
  Tok.ClearFlag(LexerToken::NeedsCleaning);
  
  if (ITI == Ident__LINE__) {
    // __LINE__ expands to a simple numeric value.
    sprintf(TmpBuffer, "%u", SourceMgr.getLineNumber(Tok.getLocation()));
    unsigned Length = strlen(TmpBuffer);
    Tok.SetKind(tok::numeric_constant);
    Tok.SetLength(Length);
    Tok.SetLocation(ScratchBuf->getToken(TmpBuffer, Length, Tok.getLocation()));
  } else if (ITI == Ident__FILE__ || ITI == Ident__BASE_FILE__) {
    SourceLocation Loc = Tok.getLocation();
    if (ITI == Ident__BASE_FILE__) {
      Diag(Tok, diag::ext_pp_base_file);
      SourceLocation NextLoc = SourceMgr.getIncludeLoc(Loc.getFileID());
      while (NextLoc.getFileID() != 0) {
        Loc = NextLoc;
        NextLoc = SourceMgr.getIncludeLoc(Loc.getFileID());
      }
    }
    
    // Escape this filename.  Turn '\' -> '\\' '"' -> '\"'
    std::string FN = SourceMgr.getSourceName(Loc);
    for (unsigned i = 0, e = FN.size(); i != e; ++i)
      if (FN[i] == '\\' || FN[i] == '"') {
        FN.insert(FN.begin()+i, '\\');
        ++i; ++e;
      }
    
    // Add quotes.
    FN = '"' + FN + '"';
    Tok.SetKind(tok::string_literal);
    Tok.SetLength(FN.size());
    Tok.SetLocation(ScratchBuf->getToken(&FN[0], FN.size(), Tok.getLocation()));
  } else if (ITI == Ident__DATE__) {
    if (!DATELoc.isValid())
      ComputeDATE_TIME(DATELoc, TIMELoc, ScratchBuf);
    Tok.SetKind(tok::string_literal);
    Tok.SetLength(strlen("\"Mmm dd yyyy\""));
    Tok.SetLocation(SourceMgr.getInstantiationLoc(DATELoc, Tok.getLocation()));
  } else if (ITI == Ident__TIME__) {
    if (!TIMELoc.isValid())
      ComputeDATE_TIME(DATELoc, TIMELoc, ScratchBuf);
    Tok.SetKind(tok::string_literal);
    Tok.SetLength(strlen("\"hh:mm:ss\""));
    Tok.SetLocation(SourceMgr.getInstantiationLoc(TIMELoc, Tok.getLocation()));
  } else if (ITI == Ident__INCLUDE_LEVEL__) {
    Diag(Tok, diag::ext_pp_include_level);

    // Compute the include depth of this token.
    unsigned Depth = 0;
    SourceLocation Loc = SourceMgr.getIncludeLoc(Tok.getLocation().getFileID());
    for (; Loc.getFileID() != 0; ++Depth)
      Loc = SourceMgr.getIncludeLoc(Loc.getFileID());
    
    // __INCLUDE_LEVEL__ expands to a simple numeric value.
    sprintf(TmpBuffer, "%u", Depth);
    unsigned Length = strlen(TmpBuffer);
    Tok.SetKind(tok::numeric_constant);
    Tok.SetLength(Length);
    Tok.SetLocation(ScratchBuf->getToken(TmpBuffer, Length, Tok.getLocation()));
  } else if (ITI == Ident__TIMESTAMP__) {
    // MSVC, ICC, GCC, VisualAge C++ extension.  The generated string should be
    // of the form "Ddd Mmm dd hh::mm::ss yyyy", which is returned by asctime.
    Diag(Tok, diag::ext_pp_timestamp);

    // Get the file that we are lexing out of.  If we're currently lexing from
    // a macro, dig into the include stack.
    const FileEntry *CurFile = 0;
    Lexer *TheLexer = getCurrentFileLexer();
    
    if (TheLexer)
      CurFile = SourceMgr.getFileEntryForFileID(TheLexer->getCurFileID());
    
    // If this file is older than the file it depends on, emit a diagnostic.
    const char *Result;
    if (CurFile) {
      time_t TT = CurFile->getModificationTime();
      struct tm *TM = localtime(&TT);
      Result = asctime(TM);
    } else {
      Result = "??? ??? ?? ??:??:?? ????\n";
    }
    TmpBuffer[0] = '"';
    strcpy(TmpBuffer+1, Result);
    unsigned Len = strlen(TmpBuffer);
    TmpBuffer[Len-1] = '"';  // Replace the newline with a quote.
    Tok.SetKind(tok::string_literal);
    Tok.SetLength(Len);
    Tok.SetLocation(ScratchBuf->getToken(TmpBuffer, Len, Tok.getLocation()));
  } else {
    assert(0 && "Unknown identifier!");
  }  
}

//===----------------------------------------------------------------------===//
// Lexer Event Handling.
//===----------------------------------------------------------------------===//

/// HandleIdentifier - This callback is invoked when the lexer reads an
/// identifier.  This callback looks up the identifier in the map and/or
/// potentially macro expands it or turns it into a named token (like 'for').
void Preprocessor::HandleIdentifier(LexerToken &Identifier) {
  if (Identifier.getIdentifierInfo() == 0) {
    // If we are skipping tokens (because we are in a #if 0 block), there will
    // be no identifier info, just return the token.
    assert(isSkipping() && "Token isn't an identifier?");
    return;
  }
  IdentifierTokenInfo &ITI = *Identifier.getIdentifierInfo();

  // If this identifier was poisoned, and if it was not produced from a macro
  // expansion, emit an error.
  if (ITI.isPoisoned() && CurLexer)
    Diag(Identifier, diag::err_pp_used_poisoned_id);
  
  if (MacroInfo *MI = ITI.getMacroInfo())
    if (MI->isEnabled() && !DisableMacroExpansion)
      return HandleMacroExpandedIdentifier(Identifier, MI);

  // Change the kind of this identifier to the appropriate token kind, e.g.
  // turning "for" into a keyword.
  Identifier.SetKind(ITI.getTokenID());
    
  // If this is an extension token, diagnose its use.
  if (ITI.isExtensionToken()) Diag(Identifier, diag::ext_token_used);
}

/// HandleEndOfFile - This callback is invoked when the lexer hits the end of
/// the current file.  This either returns the EOF token or pops a level off
/// the include stack and keeps going.
void Preprocessor::HandleEndOfFile(LexerToken &Result, bool isEndOfMacro) {
  assert(!CurMacroExpander &&
         "Ending a file when currently in a macro!");
  
  // If we are in a #if 0 block skipping tokens, and we see the end of the file,
  // this is an error condition.  Just return the EOF token up to
  // SkipExcludedConditionalBlock.  The Lexer will have already have issued
  // errors for the unterminated #if's on the conditional stack.
  if (isSkipping()) {
    Result.StartToken();
    CurLexer->BufferPtr = CurLexer->BufferEnd;
    CurLexer->FormTokenWithChars(Result, CurLexer->BufferEnd);
    Result.SetKind(tok::eof);
    return;
  }
  
  // If this is a #include'd file, pop it off the include stack and continue
  // lexing the #includer file.
  if (!IncludeMacroStack.empty()) {
    // We're done with the #included file.
    delete CurLexer;
    CurLexer         = IncludeMacroStack.back().TheLexer;
    CurDirLookup     = IncludeMacroStack.back().TheDirLookup;
    CurMacroExpander = IncludeMacroStack.back().TheMacroExpander;
    IncludeMacroStack.pop_back();

    // Notify the client, if desired, that we are in a new source file.
    if (FileChangeHandler && !isEndOfMacro && CurLexer) {
      DirectoryLookup::DirType FileType = DirectoryLookup::NormalHeaderDir;
      
      // Get the file entry for the current file.
      if (const FileEntry *FE = 
            SourceMgr.getFileEntryForFileID(CurLexer->getCurFileID()))
        FileType = getFileInfo(FE).DirInfo;

      FileChangeHandler(CurLexer->getSourceLocation(CurLexer->BufferPtr),
                        ExitFile, FileType);
    }
    
    return Lex(Result);
  }
  
  Result.StartToken();
  CurLexer->BufferPtr = CurLexer->BufferEnd;
  CurLexer->FormTokenWithChars(Result, CurLexer->BufferEnd);
  Result.SetKind(tok::eof);
  
  // We're done with the #included file.
  delete CurLexer;
  CurLexer = 0;
}

/// HandleEndOfMacro - This callback is invoked when the lexer hits the end of
/// the current macro line.
void Preprocessor::HandleEndOfMacro(LexerToken &Result) {
  assert(CurMacroExpander && !CurLexer &&
         "Ending a macro when currently in a #include file!");

  // Mark macro not ignored now that it is no longer being expanded.
  CurMacroExpander->getMacro().EnableMacro();
  delete CurMacroExpander;

  // Handle this like a #include file being popped off the stack.
  CurMacroExpander = 0;
  return HandleEndOfFile(Result, true);
}


//===----------------------------------------------------------------------===//
// Utility Methods for Preprocessor Directive Handling.
//===----------------------------------------------------------------------===//

/// DiscardUntilEndOfDirective - Read and discard all tokens remaining on the
/// current line until the tok::eom token is found.
void Preprocessor::DiscardUntilEndOfDirective() {
  LexerToken Tmp;
  do {
    LexUnexpandedToken(Tmp);
  } while (Tmp.getKind() != tok::eom);
}

/// ReadMacroName - Lex and validate a macro name, which occurs after a
/// #define or #undef.  This sets the token kind to eom and discards the rest
/// of the macro line if the macro name is invalid.
void Preprocessor::ReadMacroName(LexerToken &MacroNameTok) {
  // Read the token, don't allow macro expansion on it.
  LexUnexpandedToken(MacroNameTok);
  
  // Missing macro name?
  if (MacroNameTok.getKind() == tok::eom)
    return Diag(MacroNameTok, diag::err_pp_missing_macro_name);
  
  if (MacroNameTok.getIdentifierInfo() == 0) {
    Diag(MacroNameTok, diag::err_pp_macro_not_identifier);
    // Fall through on error.
  } else if (0) {
    // FIXME: C++.  Error if defining a C++ named operator.
    
  } else if (0) {
    // FIXME: Error if defining "defined" in C99 6.10.8.4.
  } else {
    // Okay, we got a good identifier node.  Return it.
    return;
  }
  
  
  // Invalid macro name, read and discard the rest of the line.  Then set the
  // token kind to tok::eom.
  MacroNameTok.SetKind(tok::eom);
  return DiscardUntilEndOfDirective();
}

/// CheckEndOfDirective - Ensure that the next token is a tok::eom token.  If
/// not, emit a diagnostic and consume up until the eom.
void Preprocessor::CheckEndOfDirective(const char *DirType) {
  LexerToken Tmp;
  Lex(Tmp);
  // There should be no tokens after the directive, but we allow them as an
  // extension.
  if (Tmp.getKind() != tok::eom) {
    Diag(Tmp, diag::ext_pp_extra_tokens_at_eol, DirType);
    DiscardUntilEndOfDirective();
  }
}



/// SkipExcludedConditionalBlock - We just read a #if or related directive and
/// decided that the subsequent tokens are in the #if'd out portion of the
/// file.  Lex the rest of the file, until we see an #endif.  If
/// FoundNonSkipPortion is true, then we have already emitted code for part of
/// this #if directive, so #else/#elif blocks should never be entered. If ElseOk
/// is true, then #else directives are ok, if not, then we have already seen one
/// so a #else directive is a duplicate.  When this returns, the caller can lex
/// the first valid token.
void Preprocessor::SkipExcludedConditionalBlock(SourceLocation IfTokenLoc,
                                                bool FoundNonSkipPortion,
                                                bool FoundElse) {
  ++NumSkipped;
  assert(CurMacroExpander == 0 && CurLexer &&
         "Lexing a macro, not a file?");

  CurLexer->pushConditionalLevel(IfTokenLoc, /*isSkipping*/false,
                                 FoundNonSkipPortion, FoundElse);
  
  // Know that we are going to be skipping tokens.  Set this flag to indicate
  // this, which has a couple of effects:
  //  1. If EOF of the current lexer is found, the include stack isn't popped.
  //  2. Identifier information is not looked up for identifier tokens.  As an
  //     effect of this, implicit macro expansion is naturally disabled.
  //  3. "#" tokens at the start of a line are treated as normal tokens, not
  //     implicitly transformed by the lexer.
  //  4. All notes, warnings, and extension messages are disabled.
  //
  SkippingContents = true;
  LexerToken Tok;
  while (1) {
    CurLexer->Lex(Tok);
    
    // If this is the end of the buffer, we have an error.  The lexer will have
    // already handled this error condition, so just return and let the caller
    // lex after this #include.
    if (Tok.getKind() == tok::eof) break;
    
    // If this token is not a preprocessor directive, just skip it.
    if (Tok.getKind() != tok::hash || !Tok.isAtStartOfLine())
      continue;
      
    // We just parsed a # character at the start of a line, so we're in
    // directive mode.  Tell the lexer this so any newlines we see will be
    // converted into an EOM token (this terminates the macro).
    CurLexer->ParsingPreprocessorDirective = true;
    
    // Read the next token, the directive flavor.
    LexUnexpandedToken(Tok);
    
    // If this isn't an identifier directive (e.g. is "# 1\n" or "#\n", or
    // something bogus), skip it.
    if (Tok.getKind() != tok::identifier) {
      CurLexer->ParsingPreprocessorDirective = false;
      continue;
    }

    // If the first letter isn't i or e, it isn't intesting to us.  We know that
    // this is safe in the face of spelling differences, because there is no way
    // to spell an i/e in a strange way that is another letter.  Skipping this
    // allows us to avoid looking up the identifier info for #define/#undef and
    // other common directives.
    const char *RawCharData = SourceMgr.getCharacterData(Tok.getLocation());
    char FirstChar = RawCharData[0];
    if (FirstChar >= 'a' && FirstChar <= 'z' && 
        FirstChar != 'i' && FirstChar != 'e') {
      CurLexer->ParsingPreprocessorDirective = false;
      continue;
    }
    
    // Get the identifier name without trigraphs or embedded newlines.  Note
    // that we can't use Tok.getIdentifierInfo() because its lookup is disabled
    // when skipping.
    // TODO: could do this with zero copies in the no-clean case by using
    // strncmp below.
    char Directive[20];
    unsigned IdLen;
    if (!Tok.needsCleaning() && Tok.getLength() < 20) {
      IdLen = Tok.getLength();
      memcpy(Directive, RawCharData, IdLen);
      Directive[IdLen] = 0;
    } else {
      std::string DirectiveStr = getSpelling(Tok);
      IdLen = DirectiveStr.size();
      if (IdLen >= 20) {
        CurLexer->ParsingPreprocessorDirective = false;
        continue;
      }
      memcpy(Directive, &DirectiveStr[0], IdLen);
      Directive[IdLen] = 0;
    }
    
    if (FirstChar == 'i' && Directive[1] == 'f') {
      if ((IdLen == 2) ||   // "if"
          (IdLen == 5 && !strcmp(Directive+2, "def")) ||   // "ifdef"
          (IdLen == 6 && !strcmp(Directive+2, "ndef"))) {  // "ifndef"
        // We know the entire #if/#ifdef/#ifndef block will be skipped, don't
        // bother parsing the condition.
        DiscardUntilEndOfDirective();
        CurLexer->pushConditionalLevel(Tok.getLocation(), /*wasskipping*/true,
                                       /*foundnonskip*/false,
                                       /*fnddelse*/false);
      }
    } else if (FirstChar == 'e') {
      if (IdLen == 5 && !strcmp(Directive+1, "ndif")) {  // "endif"
        CheckEndOfDirective("#endif");
        PPConditionalInfo CondInfo;
        CondInfo.WasSkipping = true; // Silence bogus warning.
        bool InCond = CurLexer->popConditionalLevel(CondInfo);
        assert(!InCond && "Can't be skipping if not in a conditional!");
        
        // If we popped the outermost skipping block, we're done skipping!
        if (!CondInfo.WasSkipping)
          break;
      } else if (IdLen == 4 && !strcmp(Directive+1, "lse")) { // "else".
        // #else directive in a skipping conditional.  If not in some other
        // skipping conditional, and if #else hasn't already been seen, enter it
        // as a non-skipping conditional.
        CheckEndOfDirective("#else");
        PPConditionalInfo &CondInfo = CurLexer->peekConditionalLevel();
        
        // If this is a #else with a #else before it, report the error.
        if (CondInfo.FoundElse) Diag(Tok, diag::pp_err_else_after_else);
        
        // Note that we've seen a #else in this conditional.
        CondInfo.FoundElse = true;
        
        // If the conditional is at the top level, and the #if block wasn't
        // entered, enter the #else block now.
        if (!CondInfo.WasSkipping && !CondInfo.FoundNonSkip) {
          CondInfo.FoundNonSkip = true;
          break;
        }
      } else if (IdLen == 4 && !strcmp(Directive+1, "lif")) {  // "elif".
        PPConditionalInfo &CondInfo = CurLexer->peekConditionalLevel();

        bool ShouldEnter;
        // If this is in a skipping block or if we're already handled this #if
        // block, don't bother parsing the condition.
        if (CondInfo.WasSkipping || CondInfo.FoundNonSkip) {
          DiscardUntilEndOfDirective();
          ShouldEnter = false;
        } else {
          // Restore the value of SkippingContents so that identifiers are
          // looked up, etc, inside the #elif expression.
          assert(SkippingContents && "We have to be skipping here!");
          SkippingContents = false;
          ShouldEnter = EvaluateDirectiveExpression();
          SkippingContents = true;
        }
        
        // If this is a #elif with a #else before it, report the error.
        if (CondInfo.FoundElse) Diag(Tok, diag::pp_err_elif_after_else);
        
        // If this condition is true, enter it!
        if (ShouldEnter) {
          CondInfo.FoundNonSkip = true;
          break;
        }
      }
    }
    
    CurLexer->ParsingPreprocessorDirective = false;
  }

  // Finally, if we are out of the conditional (saw an #endif or ran off the end
  // of the file, just stop skipping and return to lexing whatever came after
  // the #if block.
  SkippingContents = false;
}

//===----------------------------------------------------------------------===//
// Preprocessor Directive Handling.
//===----------------------------------------------------------------------===//

/// HandleDirective - This callback is invoked when the lexer sees a # token
/// at the start of a line.  This consumes the directive, modifies the 
/// lexer/preprocessor state, and advances the lexer(s) so that the next token
/// read is the correct one.
void Preprocessor::HandleDirective(LexerToken &Result) {
  // FIXME: Traditional: # with whitespace before it not recognized by K&R?
  
  // We just parsed a # character at the start of a line, so we're in directive
  // mode.  Tell the lexer this so any newlines we see will be converted into an
  // EOM token (this terminates the macro).
  CurLexer->ParsingPreprocessorDirective = true;
  
  ++NumDirectives;
  
  // Read the next token, the directive flavor.
  LexUnexpandedToken(Result);
  
  switch (Result.getKind()) {
  default: break;
  case tok::eom:
    return;   // null directive.

#if 0
  case tok::numeric_constant:
    // FIXME: implement # 7 line numbers!
    break;
#endif
  case tok::kw_else:
    return HandleElseDirective(Result);
  case tok::kw_if:
    return HandleIfDirective(Result);
  case tok::identifier:
    // Get the identifier name without trigraphs or embedded newlines.
    const char *Directive = Result.getIdentifierInfo()->getName();
    bool isExtension = false;
    switch (Result.getIdentifierInfo()->getNameLength()) {
    case 4:
      if (Directive[0] == 'l' && !strcmp(Directive, "line"))
        ;  // FIXME: implement #line
      if (Directive[0] == 'e' && !strcmp(Directive, "elif"))
        return HandleElifDirective(Result);
      if (Directive[0] == 's' && !strcmp(Directive, "sccs")) {
        isExtension = true;  // FIXME: implement #sccs
        // SCCS is the same as #ident.
      }
      break;
    case 5:
      if (Directive[0] == 'e' && !strcmp(Directive, "endif"))
        return HandleEndifDirective(Result);
      if (Directive[0] == 'i' && !strcmp(Directive, "ifdef"))
        return HandleIfdefDirective(Result, false);
      if (Directive[0] == 'u' && !strcmp(Directive, "undef"))
        return HandleUndefDirective(Result);
      if (Directive[0] == 'e' && !strcmp(Directive, "error"))
        return HandleUserDiagnosticDirective(Result, false);
      if (Directive[0] == 'i' && !strcmp(Directive, "ident"))
        isExtension = true;  // FIXME: implement #ident
      break;
    case 6:
      if (Directive[0] == 'd' && !strcmp(Directive, "define"))
        return HandleDefineDirective(Result);
      if (Directive[0] == 'i' && !strcmp(Directive, "ifndef"))
        return HandleIfdefDirective(Result, true);
      if (Directive[0] == 'i' && !strcmp(Directive, "import"))
        return HandleImportDirective(Result);
      if (Directive[0] == 'p' && !strcmp(Directive, "pragma"))
        return HandlePragmaDirective();
      if (Directive[0] == 'a' && !strcmp(Directive, "assert"))
        isExtension = true;  // FIXME: implement #assert
      break;
    case 7:
      if (Directive[0] == 'i' && !strcmp(Directive, "include"))
        return HandleIncludeDirective(Result);            // Handle #include.
      if (Directive[0] == 'w' && !strcmp(Directive, "warning")) {
        Diag(Result, diag::ext_pp_warning_directive);
        return HandleUserDiagnosticDirective(Result, true);
      }
      break;
    case 8:
      if (Directive[0] == 'u' && !strcmp(Directive, "unassert")) {
        isExtension = true;  // FIXME: implement #unassert
      }
      break;
    case 12:
      if (Directive[0] == 'i' && !strcmp(Directive, "include_next"))
        return HandleIncludeNextDirective(Result);      // Handle #include_next.
      break;
    }
    break;
  }
  
  // If we reached here, the preprocessing token is not valid!
  Diag(Result, diag::err_pp_invalid_directive);
  
  // Read the rest of the PP line.
  do {
    Lex(Result);
  } while (Result.getKind() != tok::eom);
  
  // Okay, we're done parsing the directive.
}

void Preprocessor::HandleUserDiagnosticDirective(LexerToken &Result, 
                                                 bool isWarning) {
  // Read the rest of the line raw.  We do this because we don't want macros
  // to be expanded and we don't require that the tokens be valid preprocessing
  // tokens.  For example, this is allowed: "#warning `   'foo".  GCC does
  // collapse multiple consequtive white space between tokens, but this isn't
  // specified by the standard.
  std::string Message = CurLexer->ReadToEndOfLine();

  unsigned DiagID = isWarning ? diag::pp_hash_warning : diag::err_pp_hash_error;
  return Diag(Result, DiagID, Message);
}

//===----------------------------------------------------------------------===//
// Preprocessor Include Directive Handling.
//===----------------------------------------------------------------------===//

/// HandleIncludeDirective - The "#include" tokens have just been read, read the
/// file to be included from the lexer, then include it!  This is a common
/// routine with functionality shared between #include, #include_next and
/// #import.
void Preprocessor::HandleIncludeDirective(LexerToken &IncludeTok,
                                          const DirectoryLookup *LookupFrom,
                                          bool isImport) {
  ++NumIncluded;
  LexerToken FilenameTok;
  std::string Filename = CurLexer->LexIncludeFilename(FilenameTok);
  
  // If the token kind is EOM, the error has already been diagnosed.
  if (FilenameTok.getKind() == tok::eom)
    return;
  
  // Verify that there is nothing after the filename, other than EOM.  Use the
  // preprocessor to lex this in case lexing the filename entered a macro.
  CheckEndOfDirective("#include");

  // Check that we don't have infinite #include recursion.
  if (IncludeMacroStack.size() == MaxAllowedIncludeStackDepth-1)
    return Diag(FilenameTok, diag::err_pp_include_too_deep);
  
  // Find out whether the filename is <x> or "x".
  bool isAngled = Filename[0] == '<';
  
  // Remove the quotes.
  Filename = std::string(Filename.begin()+1, Filename.end()-1);
  
  // Search include directories.
  const DirectoryLookup *CurDir;
  const FileEntry *File = LookupFile(Filename, isAngled, LookupFrom, CurDir);
  if (File == 0)
    return Diag(FilenameTok, diag::err_pp_file_not_found);
  
  // Get information about this file.
  PerFileInfo &FileInfo = getFileInfo(File);
  
  // If this is a #import directive, check that we have not already imported
  // this header.
  if (isImport) {
    // If this has already been imported, don't import it again.
    FileInfo.isImport = true;
    
    // Has this already been #import'ed or #include'd?
    if (FileInfo.NumIncludes) return;
  } else {
    // Otherwise, if this is a #include of a file that was previously #import'd
    // or if this is the second #include of a #pragma once file, ignore it.
    if (FileInfo.isImport)
      return;
  }

  // Look up the file, create a File ID for it.
  unsigned FileID = 
    SourceMgr.createFileID(File, FilenameTok.getLocation());
  if (FileID == 0)
    return Diag(FilenameTok, diag::err_pp_file_not_found);

  // Finally, if all is good, enter the new file!
  EnterSourceFile(FileID, CurDir);

  // Increment the number of times this file has been included.
  ++FileInfo.NumIncludes;
}

/// HandleIncludeNextDirective - Implements #include_next.
///
void Preprocessor::HandleIncludeNextDirective(LexerToken &IncludeNextTok) {
  Diag(IncludeNextTok, diag::ext_pp_include_next_directive);
  
  // #include_next is like #include, except that we start searching after
  // the current found directory.  If we can't do this, issue a
  // diagnostic.
  const DirectoryLookup *Lookup = CurDirLookup;
  if (isInPrimaryFile()) {
    Lookup = 0;
    Diag(IncludeNextTok, diag::pp_include_next_in_primary);
  } else if (Lookup == 0) {
    Diag(IncludeNextTok, diag::pp_include_next_absolute_path);
  } else {
    // Start looking up in the next directory.
    ++Lookup;
  }
  
  return HandleIncludeDirective(IncludeNextTok, Lookup);
}

/// HandleImportDirective - Implements #import.
///
void Preprocessor::HandleImportDirective(LexerToken &ImportTok) {
  Diag(ImportTok, diag::ext_pp_import_directive);
  
  return HandleIncludeDirective(ImportTok, 0, true);
}

//===----------------------------------------------------------------------===//
// Preprocessor Macro Directive Handling.
//===----------------------------------------------------------------------===//

/// HandleDefineDirective - Implements #define.  This consumes the entire macro
/// line then lets the caller lex the next real token.
///
void Preprocessor::HandleDefineDirective(LexerToken &DefineTok) {
  ++NumDefined;
  LexerToken MacroNameTok;
  ReadMacroName(MacroNameTok);
  
  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.getKind() == tok::eom)
    return;
  
  MacroInfo *MI = new MacroInfo(MacroNameTok.getLocation());
  
  LexerToken Tok;
  LexUnexpandedToken(Tok);
  
  if (Tok.getKind() == tok::eom) {
    // If there is no body to this macro, we have no special handling here.
  } else if (Tok.getKind() == tok::l_paren && !Tok.hasLeadingSpace()) {
    // This is a function-like macro definition.
    //assert(0 && "Function-like macros not implemented!");
    return DiscardUntilEndOfDirective();

  } else if (!Tok.hasLeadingSpace()) {
    // C99 requires whitespace between the macro definition and the body.  Emit
    // a diagnostic for something like "#define X+".
    if (Features.C99) {
      Diag(Tok, diag::ext_c99_whitespace_required_after_macro_name);
    } else {
      // FIXME: C90/C++ do not get this diagnostic, but it does get a similar
      // one in some cases!
    }
  } else {
    // This is a normal token with leading space.  Clear the leading space
    // marker on the first token to get proper expansion.
    Tok.ClearFlag(LexerToken::LeadingSpace);
  }
  
  // Read the rest of the macro body.
  while (Tok.getKind() != tok::eom) {
    MI->AddTokenToBody(Tok);
    
    // FIXME: Read macro body.  See create_iso_definition.
    
    // Get the next token of the macro.
    LexUnexpandedToken(Tok);
  }
  
  // Finally, if this identifier already had a macro defined for it, verify that
  // the macro bodies are identical and free the old definition.
  if (MacroInfo *OtherMI = MacroNameTok.getIdentifierInfo()->getMacroInfo()) {
    if (OtherMI->isBuiltinMacro())       // C99 6.10.8.4
      Diag(MacroNameTok, diag::pp_redef_builtin_macro);
    
    
    // FIXME: Verify the definition is the same.
    // Macros must be identical.  This means all tokes and whitespace separation
    // must be the same.
    delete OtherMI;
  }
  
  MacroNameTok.getIdentifierInfo()->setMacroInfo(MI);
}


/// HandleUndefDirective - Implements #undef.
///
void Preprocessor::HandleUndefDirective(LexerToken &UndefTok) {
  ++NumUndefined;
  LexerToken MacroNameTok;
  ReadMacroName(MacroNameTok);
  
  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.getKind() == tok::eom)
    return;
  
  // Check to see if this is the last token on the #undef line.
  CheckEndOfDirective("#undef");
  
  // Okay, we finally have a valid identifier to undef.
  MacroInfo *MI = MacroNameTok.getIdentifierInfo()->getMacroInfo();
  
  // If the macro is not defined, this is a noop undef, just return.
  if (MI == 0) return;

  if (MI->isBuiltinMacro())       // C99 6.10.8.4
    Diag(MacroNameTok, diag::pp_undef_builtin_macro);
  
#if 0 // FIXME: implement warn_unused_macros.
  if (CPP_OPTION (pfile, warn_unused_macros))
    _cpp_warn_if_unused_macro (pfile, node, NULL);
#endif
  
  // Free macro definition.
  delete MI;
  MacroNameTok.getIdentifierInfo()->setMacroInfo(0);
}


//===----------------------------------------------------------------------===//
// Preprocessor Conditional Directive Handling.
//===----------------------------------------------------------------------===//

/// HandleIfdefDirective - Implements the #ifdef/#ifndef directive.  isIfndef is
/// true when this is a #ifndef directive.
///
void Preprocessor::HandleIfdefDirective(LexerToken &Result, bool isIfndef) {
  ++NumIf;
  LexerToken DirectiveTok = Result;
  
  LexerToken MacroNameTok;
  ReadMacroName(MacroNameTok);
  
  // Error reading macro name?  If so, diagnostic already issued.
  if (MacroNameTok.getKind() == tok::eom)
    return;
  
  // Check to see if this is the last token on the #if[n]def line.
  CheckEndOfDirective("#ifdef");
  
  // Should we include the stuff contained by this directive?
  if (!MacroNameTok.getIdentifierInfo()->getMacroInfo() == isIfndef) {
    // Yes, remember that we are inside a conditional, then lex the next token.
    CurLexer->pushConditionalLevel(DirectiveTok.getLocation(), /*wasskip*/false,
                                   /*foundnonskip*/true, /*foundelse*/false);
  } else {
    // No, skip the contents of this block and return the first token after it.
    SkipExcludedConditionalBlock(DirectiveTok.getLocation(),
                                 /*Foundnonskip*/false, 
                                 /*FoundElse*/false);
  }
}

/// HandleIfDirective - Implements the #if directive.
///
void Preprocessor::HandleIfDirective(LexerToken &IfToken) {
  ++NumIf;
  bool ConditionalTrue = EvaluateDirectiveExpression();
  
  // Should we include the stuff contained by this directive?
  if (ConditionalTrue) {
    // Yes, remember that we are inside a conditional, then lex the next token.
    CurLexer->pushConditionalLevel(IfToken.getLocation(), /*wasskip*/false,
                                   /*foundnonskip*/true, /*foundelse*/false);
  } else {
    // No, skip the contents of this block and return the first token after it.
    SkipExcludedConditionalBlock(IfToken.getLocation(), /*Foundnonskip*/false, 
                                 /*FoundElse*/false);
  }
}

/// HandleEndifDirective - Implements the #endif directive.
///
void Preprocessor::HandleEndifDirective(LexerToken &EndifToken) {
  ++NumEndif;
  // Check that this is the whole directive.
  CheckEndOfDirective("#endif");
  
  PPConditionalInfo CondInfo;
  if (CurLexer->popConditionalLevel(CondInfo)) {
    // No conditionals on the stack: this is an #endif without an #if.
    return Diag(EndifToken, diag::err_pp_endif_without_if);
  }
  
  assert(!CondInfo.WasSkipping && !isSkipping() &&
         "This code should only be reachable in the non-skipping case!");
}


void Preprocessor::HandleElseDirective(LexerToken &Result) {
  ++NumElse;
  // #else directive in a non-skipping conditional... start skipping.
  CheckEndOfDirective("#else");
  
  PPConditionalInfo CI;
  if (CurLexer->popConditionalLevel(CI))
    return Diag(Result, diag::pp_err_else_without_if);

  // If this is a #else with a #else before it, report the error.
  if (CI.FoundElse) Diag(Result, diag::pp_err_else_after_else);
  
  // Finally, skip the rest of the contents of this block and return the first
  // token after it.
  return SkipExcludedConditionalBlock(CI.IfLoc, /*Foundnonskip*/true,
                                      /*FoundElse*/true);
}

void Preprocessor::HandleElifDirective(LexerToken &ElifToken) {
  ++NumElse;
  // #elif directive in a non-skipping conditional... start skipping.
  // We don't care what the condition is, because we will always skip it (since
  // the block immediately before it was included).
  DiscardUntilEndOfDirective();

  PPConditionalInfo CI;
  if (CurLexer->popConditionalLevel(CI))
    return Diag(ElifToken, diag::pp_err_elif_without_if);
  
  // If this is a #elif with a #else before it, report the error.
  if (CI.FoundElse) Diag(ElifToken, diag::pp_err_elif_after_else);

  // Finally, skip the rest of the contents of this block and return the first
  // token after it.
  return SkipExcludedConditionalBlock(CI.IfLoc, /*Foundnonskip*/true,
                                      /*FoundElse*/CI.FoundElse);
}

