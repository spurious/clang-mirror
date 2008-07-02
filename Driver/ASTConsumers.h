//===--- ASTConsumers.h - ASTConsumer implementations -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// AST Consumers.
//
//===----------------------------------------------------------------------===//

#ifndef DRIVER_ASTCONSUMERS_H
#define DRIVER_ASTCONSUMERS_H

#include <string>
#include <iosfwd>

namespace llvm {
  class Module;
  namespace sys { class Path; }
}
namespace clang {

class ASTConsumer;
class Diagnostic;
class FileManager;
struct LangOptions;
class Preprocessor;
class PreprocessorFactory;

ASTConsumer *CreateASTPrinter(std::ostream* OS = NULL);

ASTConsumer *CreateASTDumper();

ASTConsumer *CreateASTViewer();

ASTConsumer *CreateCodeRewriterTest(const std::string& InFile,
                                    const std::string& OutFile,
                                    Diagnostic &Diags,
                                    const LangOptions &LOpts);

ASTConsumer* CreateHTMLPrinter(const std::string &OutFile, Diagnostic &D,
                               Preprocessor *PP, PreprocessorFactory* PPF);

ASTConsumer *CreateSerializationTest(Diagnostic &Diags,
                                     FileManager& FMgr);
  
ASTConsumer *CreateASTSerializer(const std::string& InFile,
                                 const std::string& EmitDir,
                                 Diagnostic &Diags);

} // end clang namespace

#include "AnalysisConsumer.h"

#endif
