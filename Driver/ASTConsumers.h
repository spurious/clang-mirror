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

ASTConsumer *CreateASTPrinter(std::ostream* OS = NULL);

ASTConsumer *CreateASTDumper();

ASTConsumer *CreateASTViewer();

ASTConsumer *CreateCFGDumper(bool ViewGraphs, const std::string& FName);

ASTConsumer *CreateLiveVarAnalyzer(const std::string& fname);

ASTConsumer *CreateDeadStoreChecker(Diagnostic &Diags);

ASTConsumer *CreateUnitValsChecker(Diagnostic &Diags);
  
ASTConsumer *CreateGRSimpleVals(Diagnostic &Diags,
                                const std::string& Function,
                                bool Visualize = false, bool TrimGraph = false);
  
ASTConsumer* CreateCFRefChecker(Diagnostic &Diags,
                                const std::string& FunctionName); 

ASTConsumer *CreateCodeRewriterTest(const std::string& InFile,
                                    const std::string& OutFile,
                                    Diagnostic &Diags,
                                    const LangOptions &LOpts);

ASTConsumer* CreateHTMLPrinter();
ASTConsumer* CreateHTMLTest();

ASTConsumer *CreateSerializationTest(Diagnostic &Diags,
                                     FileManager& FMgr, 
                                     const LangOptions &LOpts);
  
ASTConsumer *CreateASTSerializer(const std::string& InFile,
                                 const std::string& EmitDir,
                                 Diagnostic &Diags,
                                 const LangOptions &LOpts);

} // end clang namespace

#endif
