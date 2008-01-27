//===--- PrintPreprocessedOutput.cpp - Implement the -E mode --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This code simply runs the preprocessor on the input file and prints out the
// result.  This is the traditional behavior of the -E option.
//
//===----------------------------------------------------------------------===//

#include "clang.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Pragma.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include <cstdio>
using namespace clang;

//===----------------------------------------------------------------------===//
// Simple buffered I/O
//===----------------------------------------------------------------------===//
//
// Empirically, iostream is over 30% slower than stdio for this workload, and
// stdio itself isn't very well suited.  The problem with stdio is use of
// putchar_unlocked.  We have many newline characters that need to be emitted,
// but stdio needs to do extra checks to handle line buffering mode.  These
// extra checks make putchar_unlocked fall off its inlined code path, hitting
// slow system code.  In practice, using 'write' directly makes 'clang -E -P'
// about 10% faster than using the stdio path on darwin.

#if defined(HAVE_UNISTD_H) && defined(HAVE_FCNTL_H)
#include <unistd.h>
#include <fcntl.h>
#else
#define USE_STDIO 1
#endif

#ifdef USE_STDIO
FILE *OutputFILE;
#else
static int OutputFD;
static char *OutBufStart = 0, *OutBufEnd, *OutBufCur;
#endif

/// InitOutputBuffer - Initialize our output buffer.
///
static void InitOutputBuffer(const std::string& Output) {
#ifdef USE_STDIO
  if (!Output.size() || Output == "-")
    OutputFILE = stdout;
  else
    OutputFILE = fopen(Output.c_str(), "w+");

  assert(OutputFILE && "failed to open output file");
#else
  OutBufStart = new char[64*1024];
  OutBufEnd = OutBufStart+64*1024;
  OutBufCur = OutBufStart;

  if (!Output.size() || Output == "-")
    OutputFD = STDOUT_FILENO;
  else
    OutputFD = open(Output.c_str(), O_WRONLY|O_CREAT|O_TRUNC, 0644);

  assert(OutputFD >= 0 && "failed to open output file");
#endif
}

#ifndef USE_STDIO
/// FlushBuffer - Write the accumulated bytes to the output stream.
///
static void FlushBuffer() {
  write(OutputFD, OutBufStart, OutBufCur-OutBufStart);
  OutBufCur = OutBufStart;
}
#endif

/// CleanupOutputBuffer - Finish up output.
///
static void CleanupOutputBuffer() {
#ifndef USE_STDIO
  FlushBuffer();
  delete [] OutBufStart;
#endif
}

static void OutputChar(char c) {
#if defined(_MSC_VER)
  putc(c, OutputFILE);
#elif defined(USE_STDIO)
  putc_unlocked(c, OutputFILE);
#else
  if (OutBufCur >= OutBufEnd)
    FlushBuffer();
  *OutBufCur++ = c;
#endif
}

static void OutputString(const char *Ptr, unsigned Size) {
#ifdef USE_STDIO
  fwrite(Ptr, Size, 1, OutputFILE);
#else
  if (OutBufCur+Size >= OutBufEnd)
    FlushBuffer();
  
  switch (Size) {
  default: 
    memcpy(OutBufCur, Ptr, Size);
    break;
  case 3:
    OutBufCur[2] = Ptr[2];
  case 2:
    OutBufCur[1] = Ptr[1];
  case 1:
    OutBufCur[0] = Ptr[0];
  case 0:
    break;
  }
  OutBufCur += Size;
#endif
}


//===----------------------------------------------------------------------===//
// Preprocessed token printer
//===----------------------------------------------------------------------===//

static llvm::cl::opt<bool>
DisableLineMarkers("P", llvm::cl::desc("Disable linemarker output in -E mode"));
static llvm::cl::opt<bool>
EnableCommentOutput("C", llvm::cl::desc("Enable comment output in -E mode"));
static llvm::cl::opt<bool>
EnableMacroCommentOutput("CC",
                         llvm::cl::desc("Enable comment output in -E mode, "
                                        "even from macro expansions"));

namespace {
class PrintPPOutputPPCallbacks : public PPCallbacks {
  Preprocessor &PP;
  unsigned CurLine;
  bool EmittedTokensOnThisLine;
  DirectoryLookup::DirType FileType;
  llvm::SmallString<512> CurFilename;
public:
  PrintPPOutputPPCallbacks(Preprocessor &pp) : PP(pp) {
    CurLine = 0;
    CurFilename += "<uninit>";
    EmittedTokensOnThisLine = false;
    FileType = DirectoryLookup::NormalHeaderDir;
  }
  
  void SetEmittedTokensOnThisLine() { EmittedTokensOnThisLine = true; }
  bool hasEmittedTokensOnThisLine() const { return EmittedTokensOnThisLine; }
  
  virtual void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                           DirectoryLookup::DirType FileType);
  virtual void Ident(SourceLocation Loc, const std::string &str);
  

  bool HandleFirstTokOnLine(Token &Tok);
  bool MoveToLine(SourceLocation Loc);
  bool AvoidConcat(const Token &PrevTok, const Token &Tok);
};
}

/// UToStr - Do itoa on the specified number, in-place in the specified buffer.
/// endptr points to the end of the buffer.
static char *UToStr(unsigned N, char *EndPtr) {
  // Null terminate the buffer.
  *--EndPtr = '\0';
  if (N == 0)          // Zero is a special case.
    *--EndPtr = '0';
  while (N) {
    *--EndPtr = '0' + char(N % 10);
    N /= 10;
  }
  return EndPtr;
}


/// MoveToLine - Move the output to the source line specified by the location
/// object.  We can do this by emitting some number of \n's, or be emitting a
/// #line directive.  This returns false if already at the specified line, true
/// if some newlines were emitted.
bool PrintPPOutputPPCallbacks::MoveToLine(SourceLocation Loc) {
  if (DisableLineMarkers) {
    unsigned LineNo = PP.getSourceManager().getLogicalLineNumber(Loc);
    if (LineNo == CurLine) return false;
    
    CurLine = LineNo;
    
    if (!EmittedTokensOnThisLine)
      return true;
    
    OutputChar('\n');
    EmittedTokensOnThisLine = false;
    return true;
  }
  
  unsigned LineNo = PP.getSourceManager().getLogicalLineNumber(Loc);
  
  // If this line is "close enough" to the original line, just print newlines,
  // otherwise print a #line directive.
  if (LineNo-CurLine < 8) {
    if (LineNo-CurLine == 1)
      OutputChar('\n');
    else if (LineNo == CurLine)
      return false;    // Phys line moved, but logical line didn't.
    else {
      const char *NewLines = "\n\n\n\n\n\n\n\n";
      OutputString(NewLines, LineNo-CurLine);
    }
    CurLine = LineNo;
  } else {
    if (EmittedTokensOnThisLine) {
      OutputChar('\n');
      EmittedTokensOnThisLine = false;
    }
    
    CurLine = LineNo;
    
    OutputChar('#');
    OutputChar(' ');
    char NumberBuffer[20];
    const char *NumStr = UToStr(LineNo, NumberBuffer+20);
    OutputString(NumStr, (NumberBuffer+20)-NumStr-1);
    OutputChar(' ');
    OutputChar('"');
    OutputString(&CurFilename[0], CurFilename.size());
    OutputChar('"');
    
    if (FileType == DirectoryLookup::SystemHeaderDir)
      OutputString(" 3", 2);
    else if (FileType == DirectoryLookup::ExternCSystemHeaderDir)
      OutputString(" 3 4", 4);
    OutputChar('\n');
  } 
  return true;
}


/// FileChanged - Whenever the preprocessor enters or exits a #include file
/// it invokes this handler.  Update our conception of the current source
/// position.
void PrintPPOutputPPCallbacks::FileChanged(SourceLocation Loc,
                                           FileChangeReason Reason,
                                           DirectoryLookup::DirType FileType) {
  // Unless we are exiting a #include, make sure to skip ahead to the line the
  // #include directive was at.
  SourceManager &SourceMgr = PP.getSourceManager();
  if (Reason == PPCallbacks::EnterFile) {
    MoveToLine(SourceMgr.getIncludeLoc(Loc));
  } else if (Reason == PPCallbacks::SystemHeaderPragma) {
    MoveToLine(Loc);
    
    // TODO GCC emits the # directive for this directive on the line AFTER the
    // directive and emits a bunch of spaces that aren't needed.  Emulate this
    // strange behavior.
  }
  
  Loc = SourceMgr.getLogicalLoc(Loc);
  CurLine = SourceMgr.getLineNumber(Loc);
  
  if (DisableLineMarkers) return;

  CurFilename.clear();
  CurFilename += SourceMgr.getSourceName(Loc);
  Lexer::Stringify(CurFilename);
  FileType = FileType;
  
  if (EmittedTokensOnThisLine) {
    OutputChar('\n');
    EmittedTokensOnThisLine = false;
  }
  
  OutputChar('#');
  OutputChar(' ');
  
  char NumberBuffer[20];
  const char *NumStr = UToStr(CurLine, NumberBuffer+20);
  OutputString(NumStr, (NumberBuffer+20)-NumStr-1);
  OutputChar(' ');
  OutputChar('"');
  OutputString(&CurFilename[0], CurFilename.size());
  OutputChar('"');
  
  switch (Reason) {
  case PPCallbacks::EnterFile:
    OutputString(" 1", 2);
    break;
  case PPCallbacks::ExitFile:
    OutputString(" 2", 2);
    break;
  case PPCallbacks::SystemHeaderPragma: break;
  case PPCallbacks::RenameFile: break;
  }
  
  if (FileType == DirectoryLookup::SystemHeaderDir)
    OutputString(" 3", 2);
  else if (FileType == DirectoryLookup::ExternCSystemHeaderDir)
    OutputString(" 3 4", 4);
  
  OutputChar('\n');
}

/// HandleIdent - Handle #ident directives when read by the preprocessor.
///
void PrintPPOutputPPCallbacks::Ident(SourceLocation Loc, const std::string &S) {
  MoveToLine(Loc);
  
  OutputString("#ident ", strlen("#ident "));
  OutputString(&S[0], S.size());
  EmittedTokensOnThisLine = true;
}

/// HandleFirstTokOnLine - When emitting a preprocessed file in -E mode, this
/// is called for the first token on each new line.  If this really is the start
/// of a new logical line, handle it and return true, otherwise return false.
/// This may not be the start of a logical line because the "start of line"
/// marker is set for physical lines, not logical ones.
bool PrintPPOutputPPCallbacks::HandleFirstTokOnLine(Token &Tok) {
  // Figure out what line we went to and insert the appropriate number of
  // newline characters.
  if (!MoveToLine(Tok.getLocation()))
    return false;
  
  // Print out space characters so that the first token on a line is
  // indented for easy reading.
  const SourceManager &SourceMgr = PP.getSourceManager();
  unsigned ColNo = SourceMgr.getLogicalColumnNumber(Tok.getLocation());
  
  // This hack prevents stuff like:
  // #define HASH #
  // HASH define foo bar
  // From having the # character end up at column 1, which makes it so it
  // is not handled as a #define next time through the preprocessor if in
  // -fpreprocessed mode.
  if (ColNo <= 1 && Tok.is(tok::hash))
    OutputChar(' ');
  
  // Otherwise, indent the appropriate number of spaces.
  for (; ColNo > 1; --ColNo)
    OutputChar(' ');
  
  return true;
}

namespace {
struct UnknownPragmaHandler : public PragmaHandler {
  const char *Prefix;
  PrintPPOutputPPCallbacks *Callbacks;
  
  UnknownPragmaHandler(const char *prefix, PrintPPOutputPPCallbacks *callbacks)
    : PragmaHandler(0), Prefix(prefix), Callbacks(callbacks) {}
  virtual void HandlePragma(Preprocessor &PP, Token &PragmaTok) {
    // Figure out what line we went to and insert the appropriate number of
    // newline characters.
    Callbacks->MoveToLine(PragmaTok.getLocation());
    OutputString(Prefix, strlen(Prefix));
    
    // Read and print all of the pragma tokens.
    while (PragmaTok.isNot(tok::eom)) {
      if (PragmaTok.hasLeadingSpace())
        OutputChar(' ');
      std::string TokSpell = PP.getSpelling(PragmaTok);
      OutputString(&TokSpell[0], TokSpell.size());
      PP.LexUnexpandedToken(PragmaTok);
    }
    OutputChar('\n');
  }
};
} // end anonymous namespace


enum AvoidConcatInfo {
  /// By default, a token never needs to avoid concatenation.  Most tokens (e.g.
  /// ',', ')', etc) don't cause a problem when concatenated.
  aci_never_avoid_concat = 0,

  /// aci_custom_firstchar - AvoidConcat contains custom code to handle this
  /// token's requirements, and it needs to know the first character of the
  /// token.
  aci_custom_firstchar = 1,

  /// aci_custom - AvoidConcat contains custom code to handle this token's
  /// requirements, but it doesn't need to know the first character of the
  /// token.
  aci_custom = 2,
  
  /// aci_avoid_equal - Many tokens cannot be safely followed by an '='
  /// character.  For example, "<<" turns into "<<=" when followed by an =.
  aci_avoid_equal = 4
};

/// This array contains information for each token on what action to take when
/// avoiding concatenation of tokens in the AvoidConcat method.
static char TokenInfo[tok::NUM_TOKENS];

/// InitAvoidConcatTokenInfo - Tokens that must avoid concatenation should be
/// marked by this function.
static void InitAvoidConcatTokenInfo() {
  // These tokens have custom code in AvoidConcat.
  TokenInfo[tok::identifier      ] |= aci_custom;
  TokenInfo[tok::numeric_constant] |= aci_custom_firstchar;
  TokenInfo[tok::period          ] |= aci_custom_firstchar;
  TokenInfo[tok::amp             ] |= aci_custom_firstchar;
  TokenInfo[tok::plus            ] |= aci_custom_firstchar;
  TokenInfo[tok::minus           ] |= aci_custom_firstchar;
  TokenInfo[tok::slash           ] |= aci_custom_firstchar;
  TokenInfo[tok::less            ] |= aci_custom_firstchar;
  TokenInfo[tok::greater         ] |= aci_custom_firstchar;
  TokenInfo[tok::pipe            ] |= aci_custom_firstchar;
  TokenInfo[tok::percent         ] |= aci_custom_firstchar;
  TokenInfo[tok::colon           ] |= aci_custom_firstchar;
  TokenInfo[tok::hash            ] |= aci_custom_firstchar;
  TokenInfo[tok::arrow           ] |= aci_custom_firstchar;
  
  // These tokens change behavior if followed by an '='.
  TokenInfo[tok::amp         ] |= aci_avoid_equal;           // &=
  TokenInfo[tok::plus        ] |= aci_avoid_equal;           // +=
  TokenInfo[tok::minus       ] |= aci_avoid_equal;           // -=
  TokenInfo[tok::slash       ] |= aci_avoid_equal;           // /=
  TokenInfo[tok::less        ] |= aci_avoid_equal;           // <=
  TokenInfo[tok::greater     ] |= aci_avoid_equal;           // >=
  TokenInfo[tok::pipe        ] |= aci_avoid_equal;           // |=
  TokenInfo[tok::percent     ] |= aci_avoid_equal;           // %=
  TokenInfo[tok::star        ] |= aci_avoid_equal;           // *=
  TokenInfo[tok::exclaim     ] |= aci_avoid_equal;           // !=
  TokenInfo[tok::lessless    ] |= aci_avoid_equal;           // <<=
  TokenInfo[tok::greaterequal] |= aci_avoid_equal;           // >>=
  TokenInfo[tok::caret       ] |= aci_avoid_equal;           // ^=
  TokenInfo[tok::equal       ] |= aci_avoid_equal;           // ==
}

/// StartsWithL - Return true if the spelling of this token starts with 'L'.
static bool StartsWithL(const Token &Tok, Preprocessor &PP) {
  if (!Tok.needsCleaning()) {
    SourceManager &SrcMgr = PP.getSourceManager();
    return *SrcMgr.getCharacterData(SrcMgr.getPhysicalLoc(Tok.getLocation()))
               == 'L';
  }
  
  if (Tok.getLength() < 256) {
    char Buffer[256];
    const char *TokPtr = Buffer;
    PP.getSpelling(Tok, TokPtr);
    return TokPtr[0] == 'L';
  }

  return PP.getSpelling(Tok)[0] == 'L';
}

/// IsIdentifierL - Return true if the spelling of this token is literally 'L'.
static bool IsIdentifierL(const Token &Tok, Preprocessor &PP) {
  if (!Tok.needsCleaning()) {
    if (Tok.getLength() != 1)
      return false;
    SourceManager &SrcMgr = PP.getSourceManager();
    return *SrcMgr.getCharacterData(SrcMgr.getPhysicalLoc(Tok.getLocation()))
               == 'L';
  }
  
  if (Tok.getLength() < 256) {
    char Buffer[256];
    const char *TokPtr = Buffer;
    if (PP.getSpelling(Tok, TokPtr) != 1) 
      return false;
    return TokPtr[0] == 'L';
  }
  
  return PP.getSpelling(Tok) == "L";
}


/// AvoidConcat - If printing PrevTok immediately followed by Tok would cause
/// the two individual tokens to be lexed as a single token, return true (which
/// causes a space to be printed between them).  This allows the output of -E
/// mode to be lexed to the same token stream as lexing the input directly
/// would.
///
/// This code must conservatively return true if it doesn't want to be 100%
/// accurate.  This will cause the output to include extra space characters, but
/// the resulting output won't have incorrect concatenations going on.  Examples
/// include "..", which we print with a space between, because we don't want to
/// track enough to tell "x.." from "...".
bool PrintPPOutputPPCallbacks::AvoidConcat(const Token &PrevTok,
                                           const Token &Tok) {
  char Buffer[256];
  
  tok::TokenKind PrevKind = PrevTok.getKind();
  if (PrevTok.getIdentifierInfo())  // Language keyword or named operator.
    PrevKind = tok::identifier;
 
  // Look up information on when we should avoid concatenation with prevtok.
  unsigned ConcatInfo = TokenInfo[PrevKind];
  
  // If prevtok never causes a problem for anything after it, return quickly.
  if (ConcatInfo == 0) return false;

  if (ConcatInfo & aci_avoid_equal) {
    // If the next token is '=' or '==', avoid concatenation.
    if (Tok.is(tok::equal) || Tok.is(tok::equalequal))
      return true;
    ConcatInfo &= ~aci_avoid_equal;
  }
  
  if (ConcatInfo == 0) return false;

  
  
  // Basic algorithm: we look at the first character of the second token, and
  // determine whether it, if appended to the first token, would form (or would
  // contribute) to a larger token if concatenated.
  char FirstChar = 0;
  if (ConcatInfo & aci_custom) {
    // If the token does not need to know the first character, don't get it.
  } else if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
    // Avoid spelling identifiers, the most common form of token.
    FirstChar = II->getName()[0];
  } else if (!Tok.needsCleaning()) {
    SourceManager &SrcMgr = PP.getSourceManager();
    FirstChar =
      *SrcMgr.getCharacterData(SrcMgr.getPhysicalLoc(Tok.getLocation()));
  } else if (Tok.getLength() < 256) {
    const char *TokPtr = Buffer;
    PP.getSpelling(Tok, TokPtr);
    FirstChar = TokPtr[0];
  } else {
    FirstChar = PP.getSpelling(Tok)[0];
  }
 
  switch (PrevKind) {
  default: assert(0 && "InitAvoidConcatTokenInfo built wrong");
  case tok::identifier:   // id+id or id+number or id+L"foo".
    if (Tok.is(tok::numeric_constant) || Tok.getIdentifierInfo() ||
        Tok.is(tok::wide_string_literal) /* ||
        Tok.is(tok::wide_char_literal)*/)
      return true;

    // If this isn't identifier + string, we're done.
    if (Tok.isNot(tok::char_constant) && Tok.isNot(tok::string_literal))
      return false;
      
    // FIXME: need a wide_char_constant!

    // If the string was a wide string L"foo" or wide char L'f', it would concat
    // with the previous identifier into fooL"bar".  Avoid this.
    if (StartsWithL(Tok, PP))
      return true;

    // Otherwise, this is a narrow character or string.  If the *identifier* is
    // a literal 'L', avoid pasting L "foo" -> L"foo".
    return IsIdentifierL(PrevTok, PP);
  case tok::numeric_constant:
    return isalnum(FirstChar) || Tok.is(tok::numeric_constant) ||
           FirstChar == '+' || FirstChar == '-' || FirstChar == '.';
  case tok::period:          // ..., .*, .1234
    return FirstChar == '.' || FirstChar == '*' || isdigit(FirstChar);
  case tok::amp:             // &&
    return FirstChar == '&';
  case tok::plus:            // ++
    return FirstChar == '+';
  case tok::minus:           // --, ->, ->*
    return FirstChar == '-' || FirstChar == '>';
  case tok::slash:           //, /*, //
    return FirstChar == '*' || FirstChar == '/';
  case tok::less:            // <<, <<=, <:, <%
    return FirstChar == '<' || FirstChar == ':' || FirstChar == '%';
  case tok::greater:         // >>, >>=
    return FirstChar == '>';
  case tok::pipe:            // ||
    return FirstChar == '|';
  case tok::percent:         // %>, %:
    return FirstChar == '>' || FirstChar == ':';
  case tok::colon:           // ::, :>
    return FirstChar == ':' || FirstChar == '>';
  case tok::hash:            // ##, #@, %:%:
    return FirstChar == '#' || FirstChar == '@' || FirstChar == '%';
  case tok::arrow:           // ->*
    return FirstChar == '*';
  }
}

/// DoPrintPreprocessedInput - This implements -E mode.
///
void clang::DoPrintPreprocessedInput(Preprocessor &PP, const std::string& OutFile) {
  // Inform the preprocessor whether we want it to retain comments or not, due
  // to -C or -CC.
  PP.SetCommentRetentionState(EnableCommentOutput, EnableMacroCommentOutput);
  
  InitOutputBuffer(OutFile);
  InitAvoidConcatTokenInfo();
  
  Token Tok, PrevTok;
  char Buffer[256];
  PrintPPOutputPPCallbacks *Callbacks = new PrintPPOutputPPCallbacks(PP);
  PP.setPPCallbacks(Callbacks);
  
  PP.AddPragmaHandler(0, new UnknownPragmaHandler("#pragma", Callbacks));
  PP.AddPragmaHandler("GCC", new UnknownPragmaHandler("#pragma GCC",Callbacks));

  // After we have configured the preprocessor, enter the main file.
  
  // Start parsing the specified input file.
  PP.EnterMainSourceFile();

  // Consume all of the tokens that come from the predefines buffer.  Those
  // should not be emitted into the output and are guaranteed to be at the
  // start.
  const SourceManager &SourceMgr = PP.getSourceManager();
  do PP.Lex(Tok);
  while (Tok.isNot(tok::eof) && Tok.getLocation().isFileID() &&
         !strcmp(SourceMgr.getSourceName(Tok.getLocation()), "<predefines>"));

  while (1) {
    
    // If this token is at the start of a line, emit newlines if needed.
    if (Tok.isAtStartOfLine() && Callbacks->HandleFirstTokOnLine(Tok)) {
      // done.
    } else if (Tok.hasLeadingSpace() || 
               // If we haven't emitted a token on this line yet, PrevTok isn't
               // useful to look at and no concatenation could happen anyway.
               (Callbacks->hasEmittedTokensOnThisLine() &&
                // Don't print "-" next to "-", it would form "--".
                Callbacks->AvoidConcat(PrevTok, Tok))) {
      OutputChar(' ');
    }
    
    if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
      const char *Str = II->getName();
      unsigned Len = Tok.needsCleaning() ? strlen(Str) : Tok.getLength();
      OutputString(Str, Len);
    } else if (Tok.getLength() < 256) {
      const char *TokPtr = Buffer;
      unsigned Len = PP.getSpelling(Tok, TokPtr);
      OutputString(TokPtr, Len);
    } else {
      std::string S = PP.getSpelling(Tok);
      OutputString(&S[0], S.size());
    }
    Callbacks->SetEmittedTokensOnThisLine();
    
    if (Tok.is(tok::eof)) break;
   
    PrevTok = Tok;
    PP.Lex(Tok);
  }
  OutputChar('\n');
  
  CleanupOutputBuffer();
}

