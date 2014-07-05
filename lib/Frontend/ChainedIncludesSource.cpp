//===- ChainedIncludesSource.cpp - Chained PCHs in Memory -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ChainedIncludesSource class, which converts headers
//  to chained PCHs in memory, mainly used for testing.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/ChainedIncludesSource.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace clang;

static ASTReader *
createASTReader(CompilerInstance &CI, StringRef pchFile,
                SmallVectorImpl<llvm::MemoryBuffer *> &memBufs,
                SmallVectorImpl<std::string> &bufNames,
                ASTDeserializationListener *deserialListener = nullptr) {
  Preprocessor &PP = CI.getPreprocessor();
  std::unique_ptr<ASTReader> Reader;
  Reader.reset(new ASTReader(PP, CI.getASTContext(), /*isysroot=*/"",
                             /*DisableValidation=*/true));
  for (unsigned ti = 0; ti < bufNames.size(); ++ti) {
    StringRef sr(bufNames[ti]);
    Reader->addInMemoryBuffer(sr, memBufs[ti]);
  }
  Reader->setDeserializationListener(deserialListener);
  switch (Reader->ReadAST(pchFile, serialization::MK_PCH, SourceLocation(),
                          ASTReader::ARR_None)) {
  case ASTReader::Success:
    // Set the predefines buffer as suggested by the PCH reader.
    PP.setPredefines(Reader->getSuggestedPredefines());
    return Reader.release();

  case ASTReader::Failure:
  case ASTReader::Missing:
  case ASTReader::OutOfDate:
  case ASTReader::VersionMismatch:
  case ASTReader::ConfigurationMismatch:
  case ASTReader::HadErrors:
    break;
  }
  return nullptr;
}

ChainedIncludesSource::~ChainedIncludesSource() {
  for (unsigned i = 0, e = CIs.size(); i != e; ++i)
    delete CIs[i];
}

IntrusiveRefCntPtr<ChainedIncludesSource>
ChainedIncludesSource::create(CompilerInstance &CI) {

  std::vector<std::string> &includes = CI.getPreprocessorOpts().ChainedIncludes;
  assert(!includes.empty() && "No '-chain-include' in options!");

  IntrusiveRefCntPtr<ChainedIncludesSource> source(new ChainedIncludesSource());
  InputKind IK = CI.getFrontendOpts().Inputs[0].getKind();

  SmallVector<llvm::MemoryBuffer *, 4> serialBufs;
  SmallVector<std::string, 4> serialBufNames;

  for (unsigned i = 0, e = includes.size(); i != e; ++i) {
    bool firstInclude = (i == 0);
    std::unique_ptr<CompilerInvocation> CInvok;
    CInvok.reset(new CompilerInvocation(CI.getInvocation()));
    
    CInvok->getPreprocessorOpts().ChainedIncludes.clear();
    CInvok->getPreprocessorOpts().ImplicitPCHInclude.clear();
    CInvok->getPreprocessorOpts().ImplicitPTHInclude.clear();
    CInvok->getPreprocessorOpts().DisablePCHValidation = true;
    CInvok->getPreprocessorOpts().Includes.clear();
    CInvok->getPreprocessorOpts().MacroIncludes.clear();
    CInvok->getPreprocessorOpts().Macros.clear();
    
    CInvok->getFrontendOpts().Inputs.clear();
    FrontendInputFile InputFile(includes[i], IK);
    CInvok->getFrontendOpts().Inputs.push_back(InputFile);

    TextDiagnosticPrinter *DiagClient =
      new TextDiagnosticPrinter(llvm::errs(), new DiagnosticOptions());
    IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
        new DiagnosticsEngine(DiagID, &CI.getDiagnosticOpts(), DiagClient));

    std::unique_ptr<CompilerInstance> Clang(new CompilerInstance());
    Clang->setInvocation(CInvok.release());
    Clang->setDiagnostics(Diags.get());
    Clang->setTarget(TargetInfo::CreateTargetInfo(Clang->getDiagnostics(),
                                                  &Clang->getTargetOpts()));
    Clang->createFileManager();
    Clang->createSourceManager(Clang->getFileManager());
    Clang->createPreprocessor(TU_Prefix);
    Clang->getDiagnosticClient().BeginSourceFile(Clang->getLangOpts(),
                                                 &Clang->getPreprocessor());
    Clang->createASTContext();

    SmallVector<char, 256> serialAST;
    llvm::raw_svector_ostream OS(serialAST);
    std::unique_ptr<ASTConsumer> consumer;
    consumer.reset(new PCHGenerator(Clang->getPreprocessor(), "-", nullptr,
                                    /*isysroot=*/"", &OS));
    Clang->getASTContext().setASTMutationListener(
                                            consumer->GetASTMutationListener());
    Clang->setASTConsumer(consumer.release());
    Clang->createSema(TU_Prefix, nullptr);

    if (firstInclude) {
      Preprocessor &PP = Clang->getPreprocessor();
      PP.getBuiltinInfo().InitializeBuiltins(PP.getIdentifierTable(),
                                             PP.getLangOpts());
    } else {
      assert(!serialBufs.empty());
      SmallVector<llvm::MemoryBuffer *, 4> bufs;
      for (unsigned si = 0, se = serialBufs.size(); si != se; ++si) {
        bufs.push_back(llvm::MemoryBuffer::getMemBufferCopy(
                             StringRef(serialBufs[si]->getBufferStart(),
                                             serialBufs[si]->getBufferSize())));
      }
      std::string pchName = includes[i-1];
      llvm::raw_string_ostream os(pchName);
      os << ".pch" << i-1;
      os.flush();
      
      serialBufNames.push_back(pchName);

      IntrusiveRefCntPtr<ASTReader> Reader;
      Reader = createASTReader(*Clang, pchName, bufs, serialBufNames, 
        Clang->getASTConsumer().GetASTDeserializationListener());
      if (!Reader)
        return nullptr;
      Clang->setModuleManager(Reader);
      Clang->getASTContext().setExternalSource(Reader);
    }
    
    if (!Clang->InitializeSourceManager(InputFile))
      return nullptr;

    ParseAST(Clang->getSema());
    OS.flush();
    Clang->getDiagnosticClient().EndSourceFile();
    serialBufs.push_back(
      llvm::MemoryBuffer::getMemBufferCopy(StringRef(serialAST.data(),
                                                           serialAST.size())));
    source->CIs.push_back(Clang.release());
  }

  assert(!serialBufs.empty());
  std::string pchName = includes.back() + ".pch-final";
  serialBufNames.push_back(pchName);
  IntrusiveRefCntPtr<ASTReader> Reader;
  Reader = createASTReader(CI, pchName, serialBufs, serialBufNames);
  if (!Reader)
    return nullptr;

  source->FinalReader = Reader;
  return source;
}

//===----------------------------------------------------------------------===//
// ExternalASTSource interface.
//===----------------------------------------------------------------------===//

Decl *ChainedIncludesSource::GetExternalDecl(uint32_t ID) {
  return getFinalReader().GetExternalDecl(ID);
}
Selector ChainedIncludesSource::GetExternalSelector(uint32_t ID) {
  return getFinalReader().GetExternalSelector(ID);
}
uint32_t ChainedIncludesSource::GetNumExternalSelectors() {
  return getFinalReader().GetNumExternalSelectors();
}
Stmt *ChainedIncludesSource::GetExternalDeclStmt(uint64_t Offset) {
  return getFinalReader().GetExternalDeclStmt(Offset);
}
CXXBaseSpecifier *
ChainedIncludesSource::GetExternalCXXBaseSpecifiers(uint64_t Offset) {
  return getFinalReader().GetExternalCXXBaseSpecifiers(Offset);
}
bool
ChainedIncludesSource::FindExternalVisibleDeclsByName(const DeclContext *DC,
                                                      DeclarationName Name) {
  return getFinalReader().FindExternalVisibleDeclsByName(DC, Name);
}
ExternalLoadResult 
ChainedIncludesSource::FindExternalLexicalDecls(const DeclContext *DC,
                                      bool (*isKindWeWant)(Decl::Kind),
                                      SmallVectorImpl<Decl*> &Result) {
  return getFinalReader().FindExternalLexicalDecls(DC, isKindWeWant, Result);
}
void ChainedIncludesSource::CompleteType(TagDecl *Tag) {
  return getFinalReader().CompleteType(Tag);
}
void ChainedIncludesSource::CompleteType(ObjCInterfaceDecl *Class) {
  return getFinalReader().CompleteType(Class);
}
void ChainedIncludesSource::StartedDeserializing() {
  return getFinalReader().StartedDeserializing();
}
void ChainedIncludesSource::FinishedDeserializing() {
  return getFinalReader().FinishedDeserializing();
}
void ChainedIncludesSource::StartTranslationUnit(ASTConsumer *Consumer) {
  return getFinalReader().StartTranslationUnit(Consumer);
}
void ChainedIncludesSource::PrintStats() {
  return getFinalReader().PrintStats();
}
void ChainedIncludesSource::getMemoryBufferSizes(MemoryBufferSizes &sizes)const{
  for (unsigned i = 0, e = CIs.size(); i != e; ++i) {
    if (const ExternalASTSource *eSrc =
        CIs[i]->getASTContext().getExternalSource()) {
      eSrc->getMemoryBufferSizes(sizes);
    }
  }

  getFinalReader().getMemoryBufferSizes(sizes);
}

void ChainedIncludesSource::InitializeSema(Sema &S) {
  return getFinalReader().InitializeSema(S);
}
void ChainedIncludesSource::ForgetSema() {
  return getFinalReader().ForgetSema();
}
void ChainedIncludesSource::ReadMethodPool(Selector Sel) {
  getFinalReader().ReadMethodPool(Sel);
}
bool ChainedIncludesSource::LookupUnqualified(LookupResult &R, Scope *S) {
  return getFinalReader().LookupUnqualified(R, S);
}

