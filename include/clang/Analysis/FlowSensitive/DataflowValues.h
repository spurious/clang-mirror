//===--- DataflowValues.h - Data structure for dataflow values --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a skeleton data structure for encapsulating the dataflow
// values for a CFG.  Typically this is subclassed to provide methods for
// computing these values from a CFG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSES_DATAFLOW_VALUES
#define LLVM_CLANG_ANALYSES_DATAFLOW_VALUES

#include "clang/AST/CFG.h"
#include "clang/Analysis/ProgramEdge.h"
#include "llvm/ADT/DenseMap.h"

//===----------------------------------------------------------------------===//
/// Dataflow Directional Tag Classes.  These are used for tag dispatching
///  within the dataflow solver/transfer functions to determine what direction
///  a dataflow analysis flows.
//===----------------------------------------------------------------------===//   

namespace clang {
namespace dataflow {
  struct forward_analysis_tag {};
  struct backward_analysis_tag {};
} // end namespace dataflow

//===----------------------------------------------------------------------===//
/// DataflowValues.  Container class to store dataflow values for a CFG.
//===----------------------------------------------------------------------===//   
  
template <typename ValueTypes,
          typename _AnalysisDirTag = dataflow::forward_analysis_tag >
class DataflowValues {

  //===--------------------------------------------------------------------===//
  // Type declarations.
  //===--------------------------------------------------------------------===//    

public:
  typedef typename ValueTypes::ValTy               ValTy;
  typedef typename ValueTypes::AnalysisDataTy      AnalysisDataTy;  
  typedef _AnalysisDirTag                          AnalysisDirTag;
  typedef llvm::DenseMap<ProgramEdge, ValTy>       EdgeDataMapTy;
  typedef llvm::DenseMap<const CFGBlock*, ValTy>   BlockDataMapTy;

  //===--------------------------------------------------------------------===//
  // Predicates.
  //===--------------------------------------------------------------------===//

public:
  /// isForwardAnalysis - Returns true if the dataflow values are computed
  ///  from a forward analysis.
  bool isForwardAnalysis() { return isForwardAnalysis(AnalysisDirTag()); }
  
  /// isBackwardAnalysis - Returns true if the dataflow values are computed
  ///  from a backward analysis.
  bool isBackwardAnalysis() { return !isForwardAnalysis(); }
  
private:
  bool isForwardAnalysis(dataflow::forward_analysis_tag)  { return true; }
  bool isForwardAnalysis(dataflow::backward_analysis_tag) { return false; }  
  
  //===--------------------------------------------------------------------===//
  // Initialization and accessors methods.
  //===--------------------------------------------------------------------===//

public:
  /// InitializeValues - Invoked by the solver to initialize state needed for
  ///  dataflow analysis.  This method is usually specialized by subclasses.
  void InitializeValues(const CFG& cfg) {};  


  /// getEdgeData - Retrieves the dataflow values associated with a
  ///  CFG edge.
  ValTy& getEdgeData(const BlkBlkEdge& E) {
    typename EdgeDataMapTy::iterator I = EdgeDataMap.find(E);
    assert (I != EdgeDataMap.end() && "No data associated with Edge.");
    return I->second;
  }
  
  const ValTy& getEdgeData(const BlkBlkEdge& E) const {
    return reinterpret_cast<DataflowValues*>(this)->getEdgeData(E);
  }  

  /// getBlockData - Retrieves the dataflow values associated with a 
  ///  specified CFGBlock.  If the dataflow analysis is a forward analysis,
  ///  this data is associated with the END of the block.  If the analysis
  ///  is a backwards analysis, it is associated with the ENTRY of the block.  
  ValTy& getBlockData(const CFGBlock* B) {
    typename BlockDataMapTy::iterator I = BlockDataMap.find(B);
    assert (I != BlockDataMap.end() && "No data associated with block.");
    return I->second;
  }
  
  const ValTy& getBlockData(const CFGBlock* B) const {
    return const_cast<DataflowValues*>(this)->getBlockData(B);
  }  
  
  /// getEdgeDataMap - Retrieves the internal map between CFG edges and
  ///  dataflow values.  Usually used by a dataflow solver to compute
  ///  values for blocks.
  EdgeDataMapTy& getEdgeDataMap() { return EdgeDataMap; }
  const EdgeDataMapTy& getEdgeDataMap() const { return EdgeDataMap; }

  /// getBlockDataMap - Retrieves the internal map between CFGBlocks and
  /// dataflow values.  If the dataflow analysis operates in the forward
  /// direction, the values correspond to the dataflow values at the start
  /// of the block.  Otherwise, for a backward analysis, the values correpsond
  /// to the dataflow values at the end of the block.
  BlockDataMapTy& getBlockDataMap() { return BlockDataMap; }
  const BlockDataMapTy& getBlockDataMap() const { return BlockDataMap; }

  /// getAnalysisData - Retrieves the meta data associated with a 
  ///  dataflow analysis for analyzing a particular CFG.  
  ///  This is typically consumed by transfer function code (via the solver).
  ///  This can also be used by subclasses to interpret the dataflow values.
  AnalysisDataTy& getAnalysisData() { return AnalysisData; }
  const AnalysisDataTy& getAnalysisData() const { return AnalysisData; }
  
  //===--------------------------------------------------------------------===//
  // Internal data.
  //===--------------------------------------------------------------------===//
  
protected:
  EdgeDataMapTy      EdgeDataMap;
  BlockDataMapTy     BlockDataMap;
  AnalysisDataTy     AnalysisData;
};          

} // end namespace clang
#endif
