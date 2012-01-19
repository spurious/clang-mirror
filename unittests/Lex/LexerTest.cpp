//===- unittests/Basic/LexerTest.cpp ------ Lexer tests -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/SourceManager.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/Config/config.h"

#include "gtest/gtest.h"

using namespace llvm;
using namespace clang;

namespace {

// The test fixture.
class LexerTest : public ::testing::Test {
protected:
  LexerTest()
    : FileMgr(FileMgrOpts),
      DiagID(new DiagnosticIDs()),
      Diags(DiagID, new IgnoringDiagConsumer()),
      SourceMgr(Diags, FileMgr) {
    TargetOpts.Triple = "x86_64-apple-darwin11.1.0";
    Target = TargetInfo::CreateTargetInfo(Diags, TargetOpts);
  }

  FileSystemOptions FileMgrOpts;
  FileManager FileMgr;
  llvm::IntrusiveRefCntPtr<DiagnosticIDs> DiagID;
  DiagnosticsEngine Diags;
  SourceManager SourceMgr;
  LangOptions LangOpts;
  TargetOptions TargetOpts;
  llvm::IntrusiveRefCntPtr<TargetInfo> Target;
};

class VoidModuleLoader : public ModuleLoader {
  virtual Module *loadModule(SourceLocation ImportLoc, ModuleIdPath Path,
                             Module::NameVisibilityKind Visibility,
                             bool IsInclusionDirective) {
    return 0;
  }
};

TEST_F(LexerTest, LexAPI) {
  const char *source =
    "#define M(x) [x]\n"
    "M(foo)";
  MemoryBuffer *buf = MemoryBuffer::getMemBuffer(source);
  SourceMgr.createMainFileIDForMemBuffer(buf);

  VoidModuleLoader ModLoader;
  HeaderSearch HeaderInfo(FileMgr, Diags, LangOpts);
  Preprocessor PP(Diags, LangOpts,
                  Target.getPtr(),
                  SourceMgr, HeaderInfo, ModLoader,
                  /*IILookup =*/ 0,
                  /*OwnsHeaderSearch =*/false,
                  /*DelayInitialization =*/ false);
  PP.EnterMainSourceFile();

  std::vector<Token> toks;
  while (1) {
    Token tok;
    PP.Lex(tok);
    if (tok.is(tok::eof))
      break;
    toks.push_back(tok);
  }

  // Make sure we got the tokens that we expected.
  ASSERT_EQ(3U, toks.size());
  ASSERT_EQ(tok::l_square, toks[0].getKind());
  ASSERT_EQ(tok::identifier, toks[1].getKind());
  ASSERT_EQ(tok::r_square, toks[2].getKind());
  
  SourceLocation lsqrLoc = toks[0].getLocation();
  SourceLocation idLoc = toks[1].getLocation();
  SourceLocation rsqrLoc = toks[2].getLocation();
  std::pair<SourceLocation,SourceLocation>
    macroPair = SourceMgr.getExpansionRange(lsqrLoc);
  SourceRange macroRange = SourceRange(macroPair.first, macroPair.second);

  SourceLocation Loc;
  EXPECT_TRUE(Lexer::isAtStartOfMacroExpansion(lsqrLoc, SourceMgr, LangOpts, &Loc));
  EXPECT_EQ(Loc, macroRange.getBegin());
  EXPECT_FALSE(Lexer::isAtStartOfMacroExpansion(idLoc, SourceMgr, LangOpts));
  EXPECT_FALSE(Lexer::isAtEndOfMacroExpansion(idLoc, SourceMgr, LangOpts));
  EXPECT_TRUE(Lexer::isAtEndOfMacroExpansion(rsqrLoc, SourceMgr, LangOpts, &Loc));
  EXPECT_EQ(Loc, macroRange.getEnd());

  CharSourceRange range = Lexer::makeFileCharRange(SourceRange(lsqrLoc, idLoc),
                                                   SourceMgr, LangOpts);
  EXPECT_TRUE(range.isInvalid());
  range = Lexer::makeFileCharRange(SourceRange(idLoc, rsqrLoc),
                                   SourceMgr, LangOpts);
  EXPECT_TRUE(range.isInvalid());
  range = Lexer::makeFileCharRange(SourceRange(lsqrLoc, rsqrLoc),
                                   SourceMgr, LangOpts);
  EXPECT_TRUE(!range.isTokenRange());
  EXPECT_EQ(range.getAsRange(),
            SourceRange(macroRange.getBegin(),
                        macroRange.getEnd().getLocWithOffset(1)));

  StringRef text = Lexer::getSourceText(
                  CharSourceRange::getTokenRange(SourceRange(lsqrLoc, rsqrLoc)),
                  SourceMgr, LangOpts);
  EXPECT_EQ(text, "M(foo)");
}

} // anonymous namespace
