//===- lib/Tooling/Execution.cpp - Implements tool execution framework. ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Execution.h"
#include "clang/Tooling/ToolExecutorPluginRegistry.h"
#include "clang/Tooling/Tooling.h"

LLVM_INSTANTIATE_REGISTRY(clang::tooling::ToolExecutorPluginRegistry)

namespace clang {
namespace tooling {

static llvm::cl::opt<std::string>
    ExecutorName("executor", llvm::cl::desc("The name of the executor to use."),
                 llvm::cl::init("standalone"));

void InMemoryToolResults::addResult(StringRef Key, StringRef Value) {
  KVResults.push_back({Key.str(), Value.str()});
}

std::vector<std::pair<std::string, std::string>>
InMemoryToolResults::AllKVResults() {
  return KVResults;
}

void InMemoryToolResults::forEachResult(
    llvm::function_ref<void(StringRef Key, StringRef Value)> Callback) {
  for (const auto &KV : KVResults) {
    Callback(KV.first, KV.second);
  }
}

void ExecutionContext::reportResult(StringRef Key, StringRef Value) {
  Results->addResult(Key, Value);
}

llvm::Error
ToolExecutor::execute(std::unique_ptr<FrontendActionFactory> Action) {
  return execute(std::move(Action), ArgumentsAdjuster());
}

llvm::Error ToolExecutor::execute(std::unique_ptr<FrontendActionFactory> Action,
                                  ArgumentsAdjuster Adjuster) {
  std::vector<
      std::pair<std::unique_ptr<FrontendActionFactory>, ArgumentsAdjuster>>
      Actions;
  Actions.emplace_back(std::move(Action), std::move(Adjuster));
  return execute(Actions);
}

namespace internal {
llvm::Expected<std::unique_ptr<ToolExecutor>>
createExecutorFromCommandLineArgsImpl(int &argc, const char **argv,
                                      llvm::cl::OptionCategory &Category,
                                      const char *Overview) {
  auto OptionsParser =
      CommonOptionsParser::create(argc, argv, Category, llvm::cl::ZeroOrMore,
                                  /*Overview=*/Overview);
  if (!OptionsParser)
    return OptionsParser.takeError();
  for (auto I = ToolExecutorPluginRegistry::begin(),
            E = ToolExecutorPluginRegistry::end();
       I != E; ++I) {
    if (I->getName() != ExecutorName) {
      continue;
    }
    std::unique_ptr<ToolExecutorPlugin> Plugin(I->instantiate());
    llvm::Expected<std::unique_ptr<ToolExecutor>> Executor =
        Plugin->create(*OptionsParser);
    if (!Executor) {
      return llvm::make_error<llvm::StringError>(
          llvm::Twine("Failed to create '") + I->getName() +
              "': " + llvm::toString(Executor.takeError()) + "\n",
          llvm::inconvertibleErrorCode());
    }
    return std::move(*Executor);
  }
  return llvm::make_error<llvm::StringError>(
      llvm::Twine("Executor \"") + ExecutorName + "\" is not registered.",
      llvm::inconvertibleErrorCode());
}
} // end namespace internal

llvm::Expected<std::unique_ptr<ToolExecutor>>
createExecutorFromCommandLineArgs(int &argc, const char **argv,
                                  llvm::cl::OptionCategory &Category,
                                  const char *Overview) {
  return internal::createExecutorFromCommandLineArgsImpl(argc, argv, Category,
                                                         Overview);
}

// This anchor is used to force the linker to link in the generated object file
// and thus register the StandaloneToolExecutorPlugin.
extern volatile int StandaloneToolExecutorAnchorSource;
static int LLVM_ATTRIBUTE_UNUSED StandaloneToolExecutorAnchorDest =
    StandaloneToolExecutorAnchorSource;

} // end namespace tooling
} // end namespace clang
