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

ASTConsumer *CreateASTPrinter(std::ostream* OS = NULL);

ASTConsumer *CreateASTDumper();

ASTConsumer *CreateASTViewer();

ASTConsumer *CreateCFGDumper(bool ViewGraphs = false);

ASTConsumer *CreateLiveVarAnalyzer();

ASTConsumer *CreateDeadStoreChecker(Diagnostic &Diags);

ASTConsumer *CreateUnitValsChecker(Diagnostic &Diags);
  
ASTConsumer *CreateGRSimpleVals(Diagnostic &Diags);

ASTConsumer *CreateCodeRewriterTest(const std::string& InFile,
                                    Diagnostic &Diags);

ASTConsumer *CreateSerializationTest(Diagnostic &Diags,
                                     FileManager& FMgr, 
                                     const LangOptions &LOpts);
  
ASTConsumer *CreateASTSerializer(const std::string& InFile,
                                 const std::string& EmitDir,
                                 Diagnostic &Diags,
                                 const LangOptions &LOpts);

} // end clang namespace

#endif
