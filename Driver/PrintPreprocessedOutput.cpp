//===--- PrintPreprocessedOutput.cpp - Implement the -E mode --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This code simply runs the preprocessor on the input file and prints out the
// result.  This is the traditional behavior of the -E option.
//
//===----------------------------------------------------------------------===//

#include "clang.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Pragma.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/CommandLine.h"
// NOTE: we use stdio because it is empirically much faster than iostreams.
#include <cstdio>
using namespace llvm;
using namespace clang;

static cl::opt<bool>
DisableLineMarkers("P", cl::desc("Disable linemarker output in -E mode"));

static unsigned EModeCurLine;
static std::string EModeCurFilename;
static Preprocessor *EModePP;
static bool EmodeEmittedTokensOnThisLine;
static DirectoryLookup::DirType EmodeFileType =DirectoryLookup::NormalHeaderDir;

static void MoveToLine(unsigned LineNo) {
  // If this line is "close enough" to the original line, just print newlines,
  // otherwise print a #line directive.
  if (LineNo-EModeCurLine < 8) {
    unsigned CurLine = EModeCurLine;
    //static const char Newlines[] = "\n\n\n\n\n\n\n\n";
    for (; CurLine != LineNo; ++CurLine)
      putchar_unlocked('\n');
    EModeCurLine = CurLine;
  } else {
    if (EmodeEmittedTokensOnThisLine) {
      putchar_unlocked('\n');
      EmodeEmittedTokensOnThisLine = false;
    }
    
    EModeCurLine = LineNo;
    if (DisableLineMarkers) return;
    
    printf("# %d %s", LineNo, EModeCurFilename.c_str());
    
    if (EmodeFileType == DirectoryLookup::SystemHeaderDir)
      printf(" 3");
    else if (EmodeFileType == DirectoryLookup::ExternCSystemHeaderDir)
      printf(" 3 4");
    putchar_unlocked('\n');
  } 
}

/// HandleFileChange - Whenever the preprocessor enters or exits a #include file
/// it invokes this handler.  Update our conception of the current 
static void HandleFileChange(SourceLocation Loc,
                             Preprocessor::FileChangeReason Reason,
                             DirectoryLookup::DirType FileType) {
  SourceManager &SourceMgr = EModePP->getSourceManager();
  
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  if (Reason == Preprocessor::EnterFile) {
    SourceLocation IncludeLoc = SourceMgr.getIncludeLoc(Loc.getFileID());
    MoveToLine(SourceMgr.getLineNumber(IncludeLoc));
  } else if (Reason == Preprocessor::SystemHeaderPragma) {
    MoveToLine(SourceMgr.getLineNumber(Loc));
    
    // TODO GCC emits the # directive for this directive on the line AFTER the
    // directive and emits a bunch of spaces that aren't needed.  Emulate this
    // strange behavior.
  }
  
  EModeCurLine = SourceMgr.getLineNumber(Loc);
  EModeCurFilename = Lexer::Stringify(SourceMgr.getSourceName(Loc));
  EmodeFileType = FileType;
  
  if (EmodeEmittedTokensOnThisLine) {
    putchar_unlocked('\n');
    EmodeEmittedTokensOnThisLine = false;
  }
  
  if (DisableLineMarkers) return;
  
  printf("# %d %s", EModeCurLine, EModeCurFilename.c_str());
  switch (Reason) {
    case Preprocessor::EnterFile:
      printf(" 1");
      break;
    case Preprocessor::ExitFile:
      printf(" 2");
      break;
    case Preprocessor::SystemHeaderPragma: break;
    case Preprocessor::RenameFile: break;
  }
  
  if (FileType == DirectoryLookup::SystemHeaderDir)
    printf(" 3");
  else if (FileType == DirectoryLookup::ExternCSystemHeaderDir)
    printf(" 3 4");
  
  putchar_unlocked('\n');
}

static void HandleIdent(SourceLocation Loc, const std::string &Val) {
  SourceManager &SourceMgr = EModePP->getSourceManager();
  MoveToLine(SourceMgr.getLineNumber(Loc));
  
  printf("#ident %s", Val.c_str());
  EmodeEmittedTokensOnThisLine = true;
}

/// HandleFirstTokOnLine - When emitting a preprocessed file in -E mode, this
/// is called for the first token on each new line.
static void HandleFirstTokOnLine(LexerToken &Tok, Preprocessor &PP) {
  // Figure out what line we went to and insert the appropriate number of
  // newline characters.
  unsigned LineNo = PP.getSourceManager().getLineNumber(Tok.getLocation());
  
  // Move to the specified line.
  MoveToLine(LineNo);
  
  
  // Print out space characters so that the first token on a line is
  // indented for easy reading.
  unsigned ColNo = 
    PP.getSourceManager().getColumnNumber(Tok.getLocation());
  
  // This hack prevents stuff like:
  // #define HASH #
  // HASH define foo bar
  // From having the # character end up at column 1, which makes it so it
  // is not handled as a #define next time through the preprocessor if in
  // -fpreprocessed mode.
  if (ColNo <= 1 && Tok.getKind() == tok::hash)
    putchar_unlocked(' ');
  
  // Otherwise, indent the appropriate number of spaces.
  for (; ColNo > 1; --ColNo)
    putchar_unlocked(' ');
}

namespace {
struct UnknownPragmaHandler : public PragmaHandler {
  const char *Prefix;
  UnknownPragmaHandler(const char *prefix) : PragmaHandler(0), Prefix(prefix) {}
  virtual void HandlePragma(Preprocessor &PP, LexerToken &PragmaTok) {
    // Figure out what line we went to and insert the appropriate number of
    // newline characters.
    MoveToLine(PP.getSourceManager().getLineNumber(PragmaTok.getLocation()));
    printf(Prefix);
    
    // Read and print all of the pragma tokens.
    while (PragmaTok.getKind() != tok::eom) {
      if (PragmaTok.hasLeadingSpace())
        putchar_unlocked(' ');
      printf("%s", PP.getSpelling(PragmaTok).c_str());
      PP.LexUnexpandedToken(PragmaTok);
    }
    putchar_unlocked('\n');
  }
};
} // end anonymous namespace

/// DoPrintPreprocessedInput - This implements -E mode.
void clang::DoPrintPreprocessedInput(Preprocessor &PP) {
  LexerToken Tok;
  char Buffer[256];
  EModeCurLine = 0;
  EModeCurFilename = "\"<uninit>\"";
  PP.setFileChangeHandler(HandleFileChange);
  PP.setIdentHandler(HandleIdent);
  EModePP = &PP;
  EmodeEmittedTokensOnThisLine = false;
  
  PP.AddPragmaHandler(0, new UnknownPragmaHandler("#pragma"));
  PP.AddPragmaHandler("GCC", new UnknownPragmaHandler("#pragma GCC"));
  do {
    PP.Lex(Tok);
    
    // If this token is at the start of a line.  Emit the \n and indentation.
    // FIXME: this shouldn't use the isAtStartOfLine flag.  This should use a
    // "newline callback" from the lexer.
    // FIXME: For some tests, this fails just because there is no col# info from
    // macro expansions!
    if (Tok.isAtStartOfLine()) {
      HandleFirstTokOnLine(Tok, PP);
    } else if (Tok.hasLeadingSpace()) {
      putchar_unlocked(' ');
    }
    
    if (Tok.getLength() < 256) {
      unsigned Len = PP.getSpelling(Tok, Buffer);
      Buffer[Len] = 0;
      fwrite(Buffer, Len, 1, stdout);
    } else {
      std::string S = PP.getSpelling(Tok);
      fwrite(&S[0], S.size(), 1, stdout);
    }
    EmodeEmittedTokensOnThisLine = true;
  } while (Tok.getKind() != tok::eof);
  putchar_unlocked('\n');
}

