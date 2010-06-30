//===--- PCHWriterStmt.cpp - Statement and Expression Serialization -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements serialization for Statements and Expressions.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/PCHWriter.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/Bitcode/BitstreamWriter.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// Statement/expression serialization
//===----------------------------------------------------------------------===//

namespace clang {
  class PCHStmtWriter : public StmtVisitor<PCHStmtWriter, void> {
    PCHWriter &Writer;
    PCHWriter::RecordData &Record;

  public:
    pch::StmtCode Code;

    PCHStmtWriter(PCHWriter &Writer, PCHWriter::RecordData &Record)
      : Writer(Writer), Record(Record) { }
    
    void
    AddExplicitTemplateArgumentList(const ExplicitTemplateArgumentList &Args);

    void VisitStmt(Stmt *S);
    void VisitNullStmt(NullStmt *S);
    void VisitCompoundStmt(CompoundStmt *S);
    void VisitSwitchCase(SwitchCase *S);
    void VisitCaseStmt(CaseStmt *S);
    void VisitDefaultStmt(DefaultStmt *S);
    void VisitLabelStmt(LabelStmt *S);
    void VisitIfStmt(IfStmt *S);
    void VisitSwitchStmt(SwitchStmt *S);
    void VisitWhileStmt(WhileStmt *S);
    void VisitDoStmt(DoStmt *S);
    void VisitForStmt(ForStmt *S);
    void VisitGotoStmt(GotoStmt *S);
    void VisitIndirectGotoStmt(IndirectGotoStmt *S);
    void VisitContinueStmt(ContinueStmt *S);
    void VisitBreakStmt(BreakStmt *S);
    void VisitReturnStmt(ReturnStmt *S);
    void VisitDeclStmt(DeclStmt *S);
    void VisitAsmStmt(AsmStmt *S);
    void VisitExpr(Expr *E);
    void VisitPredefinedExpr(PredefinedExpr *E);
    void VisitDeclRefExpr(DeclRefExpr *E);
    void VisitIntegerLiteral(IntegerLiteral *E);
    void VisitFloatingLiteral(FloatingLiteral *E);
    void VisitImaginaryLiteral(ImaginaryLiteral *E);
    void VisitStringLiteral(StringLiteral *E);
    void VisitCharacterLiteral(CharacterLiteral *E);
    void VisitParenExpr(ParenExpr *E);
    void VisitParenListExpr(ParenListExpr *E);
    void VisitUnaryOperator(UnaryOperator *E);
    void VisitOffsetOfExpr(OffsetOfExpr *E);
    void VisitSizeOfAlignOfExpr(SizeOfAlignOfExpr *E);
    void VisitArraySubscriptExpr(ArraySubscriptExpr *E);
    void VisitCallExpr(CallExpr *E);
    void VisitMemberExpr(MemberExpr *E);
    void VisitCastExpr(CastExpr *E);
    void VisitBinaryOperator(BinaryOperator *E);
    void VisitCompoundAssignOperator(CompoundAssignOperator *E);
    void VisitConditionalOperator(ConditionalOperator *E);
    void VisitImplicitCastExpr(ImplicitCastExpr *E);
    void VisitExplicitCastExpr(ExplicitCastExpr *E);
    void VisitCStyleCastExpr(CStyleCastExpr *E);
    void VisitCompoundLiteralExpr(CompoundLiteralExpr *E);
    void VisitExtVectorElementExpr(ExtVectorElementExpr *E);
    void VisitInitListExpr(InitListExpr *E);
    void VisitDesignatedInitExpr(DesignatedInitExpr *E);
    void VisitImplicitValueInitExpr(ImplicitValueInitExpr *E);
    void VisitVAArgExpr(VAArgExpr *E);
    void VisitAddrLabelExpr(AddrLabelExpr *E);
    void VisitStmtExpr(StmtExpr *E);
    void VisitTypesCompatibleExpr(TypesCompatibleExpr *E);
    void VisitChooseExpr(ChooseExpr *E);
    void VisitGNUNullExpr(GNUNullExpr *E);
    void VisitShuffleVectorExpr(ShuffleVectorExpr *E);
    void VisitBlockExpr(BlockExpr *E);
    void VisitBlockDeclRefExpr(BlockDeclRefExpr *E);

    // Objective-C Expressions
    void VisitObjCStringLiteral(ObjCStringLiteral *E);
    void VisitObjCEncodeExpr(ObjCEncodeExpr *E);
    void VisitObjCSelectorExpr(ObjCSelectorExpr *E);
    void VisitObjCProtocolExpr(ObjCProtocolExpr *E);
    void VisitObjCIvarRefExpr(ObjCIvarRefExpr *E);
    void VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E);
    void VisitObjCImplicitSetterGetterRefExpr(
                        ObjCImplicitSetterGetterRefExpr *E);
    void VisitObjCMessageExpr(ObjCMessageExpr *E);
    void VisitObjCSuperExpr(ObjCSuperExpr *E);
    void VisitObjCIsaExpr(ObjCIsaExpr *E);

    // Objective-C Statements
    void VisitObjCForCollectionStmt(ObjCForCollectionStmt *);
    void VisitObjCAtCatchStmt(ObjCAtCatchStmt *);
    void VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *);
    void VisitObjCAtTryStmt(ObjCAtTryStmt *);
    void VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *);
    void VisitObjCAtThrowStmt(ObjCAtThrowStmt *);

    // C++ Statements
    void VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E);
    void VisitCXXMemberCallExpr(CXXMemberCallExpr *E);
    void VisitCXXConstructExpr(CXXConstructExpr *E);
    void VisitCXXNamedCastExpr(CXXNamedCastExpr *E);
    void VisitCXXStaticCastExpr(CXXStaticCastExpr *E);
    void VisitCXXDynamicCastExpr(CXXDynamicCastExpr *E);
    void VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E);
    void VisitCXXConstCastExpr(CXXConstCastExpr *E);
    void VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E);
    void VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E);
    void VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E);
    void VisitCXXTypeidExpr(CXXTypeidExpr *E);
    void VisitCXXThisExpr(CXXThisExpr *E);
    void VisitCXXThrowExpr(CXXThrowExpr *E);
    void VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E);
    void VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E);

    void VisitCXXZeroInitValueExpr(CXXZeroInitValueExpr *E);
    void VisitCXXNewExpr(CXXNewExpr *E);
    void VisitCXXDeleteExpr(CXXDeleteExpr *E);
    void VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E);

    void VisitCXXExprWithTemporaries(CXXExprWithTemporaries *E);
    void VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr *E);
    void VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E);
    void VisitCXXUnresolvedConstructExpr(CXXUnresolvedConstructExpr *E);

    void VisitOverloadExpr(OverloadExpr *E);
    void VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E);
    void VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E);
  };
}

void PCHStmtWriter::
AddExplicitTemplateArgumentList(const ExplicitTemplateArgumentList &Args) {
  Writer.AddSourceLocation(Args.LAngleLoc, Record);
  Writer.AddSourceLocation(Args.RAngleLoc, Record);
  for (unsigned i=0; i != Args.NumTemplateArgs; ++i)
    Writer.AddTemplateArgumentLoc(Args.getTemplateArgs()[i], Record);
}

void PCHStmtWriter::VisitStmt(Stmt *S) {
}

void PCHStmtWriter::VisitNullStmt(NullStmt *S) {
  VisitStmt(S);
  Writer.AddSourceLocation(S->getSemiLoc(), Record);
  Code = pch::STMT_NULL;
}

void PCHStmtWriter::VisitCompoundStmt(CompoundStmt *S) {
  VisitStmt(S);
  Record.push_back(S->size());
  for (CompoundStmt::body_iterator CS = S->body_begin(), CSEnd = S->body_end();
       CS != CSEnd; ++CS)
    Writer.AddStmt(*CS);
  Writer.AddSourceLocation(S->getLBracLoc(), Record);
  Writer.AddSourceLocation(S->getRBracLoc(), Record);
  Code = pch::STMT_COMPOUND;
}

void PCHStmtWriter::VisitSwitchCase(SwitchCase *S) {
  VisitStmt(S);
  Record.push_back(Writer.getSwitchCaseID(S));
}

void PCHStmtWriter::VisitCaseStmt(CaseStmt *S) {
  VisitSwitchCase(S);
  Writer.AddStmt(S->getLHS());
  Writer.AddStmt(S->getRHS());
  Writer.AddStmt(S->getSubStmt());
  Writer.AddSourceLocation(S->getCaseLoc(), Record);
  Writer.AddSourceLocation(S->getEllipsisLoc(), Record);
  Writer.AddSourceLocation(S->getColonLoc(), Record);
  Code = pch::STMT_CASE;
}

void PCHStmtWriter::VisitDefaultStmt(DefaultStmt *S) {
  VisitSwitchCase(S);
  Writer.AddStmt(S->getSubStmt());
  Writer.AddSourceLocation(S->getDefaultLoc(), Record);
  Writer.AddSourceLocation(S->getColonLoc(), Record);
  Code = pch::STMT_DEFAULT;
}

void PCHStmtWriter::VisitLabelStmt(LabelStmt *S) {
  VisitStmt(S);
  Writer.AddIdentifierRef(S->getID(), Record);
  Writer.AddStmt(S->getSubStmt());
  Writer.AddSourceLocation(S->getIdentLoc(), Record);
  Record.push_back(Writer.GetLabelID(S));
  Code = pch::STMT_LABEL;
}

void PCHStmtWriter::VisitIfStmt(IfStmt *S) {
  VisitStmt(S);
  Writer.AddDeclRef(S->getConditionVariable(), Record);
  Writer.AddStmt(S->getCond());
  Writer.AddStmt(S->getThen());
  Writer.AddStmt(S->getElse());
  Writer.AddSourceLocation(S->getIfLoc(), Record);
  Writer.AddSourceLocation(S->getElseLoc(), Record);
  Code = pch::STMT_IF;
}

void PCHStmtWriter::VisitSwitchStmt(SwitchStmt *S) {
  VisitStmt(S);
  Writer.AddDeclRef(S->getConditionVariable(), Record);
  Writer.AddStmt(S->getCond());
  Writer.AddStmt(S->getBody());
  Writer.AddSourceLocation(S->getSwitchLoc(), Record);
  for (SwitchCase *SC = S->getSwitchCaseList(); SC;
       SC = SC->getNextSwitchCase())
    Record.push_back(Writer.RecordSwitchCaseID(SC));
  Code = pch::STMT_SWITCH;
}

void PCHStmtWriter::VisitWhileStmt(WhileStmt *S) {
  VisitStmt(S);
  Writer.AddDeclRef(S->getConditionVariable(), Record);
  Writer.AddStmt(S->getCond());
  Writer.AddStmt(S->getBody());
  Writer.AddSourceLocation(S->getWhileLoc(), Record);
  Code = pch::STMT_WHILE;
}

void PCHStmtWriter::VisitDoStmt(DoStmt *S) {
  VisitStmt(S);
  Writer.AddStmt(S->getCond());
  Writer.AddStmt(S->getBody());
  Writer.AddSourceLocation(S->getDoLoc(), Record);
  Writer.AddSourceLocation(S->getWhileLoc(), Record);
  Writer.AddSourceLocation(S->getRParenLoc(), Record);
  Code = pch::STMT_DO;
}

void PCHStmtWriter::VisitForStmt(ForStmt *S) {
  VisitStmt(S);
  Writer.AddStmt(S->getInit());
  Writer.AddStmt(S->getCond());
  Writer.AddDeclRef(S->getConditionVariable(), Record);
  Writer.AddStmt(S->getInc());
  Writer.AddStmt(S->getBody());
  Writer.AddSourceLocation(S->getForLoc(), Record);
  Writer.AddSourceLocation(S->getLParenLoc(), Record);
  Writer.AddSourceLocation(S->getRParenLoc(), Record);
  Code = pch::STMT_FOR;
}

void PCHStmtWriter::VisitGotoStmt(GotoStmt *S) {
  VisitStmt(S);
  Record.push_back(Writer.GetLabelID(S->getLabel()));
  Writer.AddSourceLocation(S->getGotoLoc(), Record);
  Writer.AddSourceLocation(S->getLabelLoc(), Record);
  Code = pch::STMT_GOTO;
}

void PCHStmtWriter::VisitIndirectGotoStmt(IndirectGotoStmt *S) {
  VisitStmt(S);
  Writer.AddSourceLocation(S->getGotoLoc(), Record);
  Writer.AddSourceLocation(S->getStarLoc(), Record);
  Writer.AddStmt(S->getTarget());
  Code = pch::STMT_INDIRECT_GOTO;
}

void PCHStmtWriter::VisitContinueStmt(ContinueStmt *S) {
  VisitStmt(S);
  Writer.AddSourceLocation(S->getContinueLoc(), Record);
  Code = pch::STMT_CONTINUE;
}

void PCHStmtWriter::VisitBreakStmt(BreakStmt *S) {
  VisitStmt(S);
  Writer.AddSourceLocation(S->getBreakLoc(), Record);
  Code = pch::STMT_BREAK;
}

void PCHStmtWriter::VisitReturnStmt(ReturnStmt *S) {
  VisitStmt(S);
  Writer.AddStmt(S->getRetValue());
  Writer.AddSourceLocation(S->getReturnLoc(), Record);
  Writer.AddDeclRef(S->getNRVOCandidate(), Record);
  Code = pch::STMT_RETURN;
}

void PCHStmtWriter::VisitDeclStmt(DeclStmt *S) {
  VisitStmt(S);
  Writer.AddSourceLocation(S->getStartLoc(), Record);
  Writer.AddSourceLocation(S->getEndLoc(), Record);
  DeclGroupRef DG = S->getDeclGroup();
  for (DeclGroupRef::iterator D = DG.begin(), DEnd = DG.end(); D != DEnd; ++D)
    Writer.AddDeclRef(*D, Record);
  Code = pch::STMT_DECL;
}

void PCHStmtWriter::VisitAsmStmt(AsmStmt *S) {
  VisitStmt(S);
  Record.push_back(S->getNumOutputs());
  Record.push_back(S->getNumInputs());
  Record.push_back(S->getNumClobbers());
  Writer.AddSourceLocation(S->getAsmLoc(), Record);
  Writer.AddSourceLocation(S->getRParenLoc(), Record);
  Record.push_back(S->isVolatile());
  Record.push_back(S->isSimple());
  Record.push_back(S->isMSAsm());
  Writer.AddStmt(S->getAsmString());

  // Outputs
  for (unsigned I = 0, N = S->getNumOutputs(); I != N; ++I) {      
    Writer.AddIdentifierRef(S->getOutputIdentifier(I), Record);
    Writer.AddStmt(S->getOutputConstraintLiteral(I));
    Writer.AddStmt(S->getOutputExpr(I));
  }

  // Inputs
  for (unsigned I = 0, N = S->getNumInputs(); I != N; ++I) {
    Writer.AddIdentifierRef(S->getInputIdentifier(I), Record);
    Writer.AddStmt(S->getInputConstraintLiteral(I));
    Writer.AddStmt(S->getInputExpr(I));
  }

  // Clobbers
  for (unsigned I = 0, N = S->getNumClobbers(); I != N; ++I)
    Writer.AddStmt(S->getClobber(I));

  Code = pch::STMT_ASM;
}

void PCHStmtWriter::VisitExpr(Expr *E) {
  VisitStmt(E);
  Writer.AddTypeRef(E->getType(), Record);
  Record.push_back(E->isTypeDependent());
  Record.push_back(E->isValueDependent());
}

void PCHStmtWriter::VisitPredefinedExpr(PredefinedExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Record.push_back(E->getIdentType()); // FIXME: stable encoding
  Code = pch::EXPR_PREDEFINED;
}

void PCHStmtWriter::VisitDeclRefExpr(DeclRefExpr *E) {
  VisitExpr(E);
  Writer.AddDeclRef(E->getDecl(), Record);
  Writer.AddSourceLocation(E->getLocation(), Record);
  // FIXME: write qualifier
  // FIXME: write explicit template arguments
  Code = pch::EXPR_DECL_REF;
}

void PCHStmtWriter::VisitIntegerLiteral(IntegerLiteral *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Writer.AddAPInt(E->getValue(), Record);
  Code = pch::EXPR_INTEGER_LITERAL;
}

void PCHStmtWriter::VisitFloatingLiteral(FloatingLiteral *E) {
  VisitExpr(E);
  Writer.AddAPFloat(E->getValue(), Record);
  Record.push_back(E->isExact());
  Writer.AddSourceLocation(E->getLocation(), Record);
  Code = pch::EXPR_FLOATING_LITERAL;
}

void PCHStmtWriter::VisitImaginaryLiteral(ImaginaryLiteral *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getSubExpr());
  Code = pch::EXPR_IMAGINARY_LITERAL;
}

void PCHStmtWriter::VisitStringLiteral(StringLiteral *E) {
  VisitExpr(E);
  Record.push_back(E->getByteLength());
  Record.push_back(E->getNumConcatenated());
  Record.push_back(E->isWide());
  // FIXME: String data should be stored as a blob at the end of the
  // StringLiteral. However, we can't do so now because we have no
  // provision for coping with abbreviations when we're jumping around
  // the PCH file during deserialization.
  Record.insert(Record.end(),
                E->getStrData(), E->getStrData() + E->getByteLength());
  for (unsigned I = 0, N = E->getNumConcatenated(); I != N; ++I)
    Writer.AddSourceLocation(E->getStrTokenLoc(I), Record);
  Code = pch::EXPR_STRING_LITERAL;
}

void PCHStmtWriter::VisitCharacterLiteral(CharacterLiteral *E) {
  VisitExpr(E);
  Record.push_back(E->getValue());
  Writer.AddSourceLocation(E->getLocation(), Record);
  Record.push_back(E->isWide());
  Code = pch::EXPR_CHARACTER_LITERAL;
}

void PCHStmtWriter::VisitParenExpr(ParenExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getLParen(), Record);
  Writer.AddSourceLocation(E->getRParen(), Record);
  Writer.AddStmt(E->getSubExpr());
  Code = pch::EXPR_PAREN;
}

void PCHStmtWriter::VisitParenListExpr(ParenListExpr *E) {
  VisitExpr(E);
  Record.push_back(E->NumExprs);
  for (unsigned i=0; i != E->NumExprs; ++i)
    Writer.AddStmt(E->Exprs[i]);
  Writer.AddSourceLocation(E->LParenLoc, Record);
  Writer.AddSourceLocation(E->RParenLoc, Record);
  Code = pch::EXPR_PAREN_LIST;
}

void PCHStmtWriter::VisitUnaryOperator(UnaryOperator *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getSubExpr());
  Record.push_back(E->getOpcode()); // FIXME: stable encoding
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
  Code = pch::EXPR_UNARY_OPERATOR;
}

void PCHStmtWriter::VisitOffsetOfExpr(OffsetOfExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumComponents());
  Record.push_back(E->getNumExpressions());
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Writer.AddTypeSourceInfo(E->getTypeSourceInfo(), Record);
  for (unsigned I = 0, N = E->getNumComponents(); I != N; ++I) {
    const OffsetOfExpr::OffsetOfNode &ON = E->getComponent(I);
    Record.push_back(ON.getKind()); // FIXME: Stable encoding
    Writer.AddSourceLocation(ON.getRange().getBegin(), Record);
    Writer.AddSourceLocation(ON.getRange().getEnd(), Record);
    switch (ON.getKind()) {
    case OffsetOfExpr::OffsetOfNode::Array:
      Record.push_back(ON.getArrayExprIndex());
      break;
        
    case OffsetOfExpr::OffsetOfNode::Field:
      Writer.AddDeclRef(ON.getField(), Record);
      break;
        
    case OffsetOfExpr::OffsetOfNode::Identifier:
      Writer.AddIdentifierRef(ON.getFieldName(), Record);
      break;
        
    case OffsetOfExpr::OffsetOfNode::Base:
      // FIXME: Implement this!
      llvm_unreachable("PCH for offsetof(base-specifier) not implemented");
      break;
    }
  }
  for (unsigned I = 0, N = E->getNumExpressions(); I != N; ++I)
    Writer.AddStmt(E->getIndexExpr(I));
  Code = pch::EXPR_OFFSETOF;
}

void PCHStmtWriter::VisitSizeOfAlignOfExpr(SizeOfAlignOfExpr *E) {
  VisitExpr(E);
  Record.push_back(E->isSizeOf());
  if (E->isArgumentType())
    Writer.AddTypeSourceInfo(E->getArgumentTypeInfo(), Record);
  else {
    Record.push_back(0);
    Writer.AddStmt(E->getArgumentExpr());
  }
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_SIZEOF_ALIGN_OF;
}

void PCHStmtWriter::VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getLHS());
  Writer.AddStmt(E->getRHS());
  Writer.AddSourceLocation(E->getRBracketLoc(), Record);
  Code = pch::EXPR_ARRAY_SUBSCRIPT;
}

void PCHStmtWriter::VisitCallExpr(CallExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumArgs());
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Writer.AddStmt(E->getCallee());
  for (CallExpr::arg_iterator Arg = E->arg_begin(), ArgEnd = E->arg_end();
       Arg != ArgEnd; ++Arg)
    Writer.AddStmt(*Arg);
  Code = pch::EXPR_CALL;
}

void PCHStmtWriter::VisitMemberExpr(MemberExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getBase());
  Writer.AddDeclRef(E->getMemberDecl(), Record);
  Writer.AddSourceLocation(E->getMemberLoc(), Record);
  Record.push_back(E->isArrow());
  // FIXME: C++ nested-name-specifier
  // FIXME: C++ template argument list
  Code = pch::EXPR_MEMBER;
}

void PCHStmtWriter::VisitObjCIsaExpr(ObjCIsaExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getBase());
  Writer.AddSourceLocation(E->getIsaMemberLoc(), Record);
  Record.push_back(E->isArrow());
  Code = pch::EXPR_OBJC_ISA;
}

void PCHStmtWriter::VisitCastExpr(CastExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getSubExpr());
  Record.push_back(E->getCastKind()); // FIXME: stable encoding
}

void PCHStmtWriter::VisitBinaryOperator(BinaryOperator *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getLHS());
  Writer.AddStmt(E->getRHS());
  Record.push_back(E->getOpcode()); // FIXME: stable encoding
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
  Code = pch::EXPR_BINARY_OPERATOR;
}

void PCHStmtWriter::VisitCompoundAssignOperator(CompoundAssignOperator *E) {
  VisitBinaryOperator(E);
  Writer.AddTypeRef(E->getComputationLHSType(), Record);
  Writer.AddTypeRef(E->getComputationResultType(), Record);
  Code = pch::EXPR_COMPOUND_ASSIGN_OPERATOR;
}

void PCHStmtWriter::VisitConditionalOperator(ConditionalOperator *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getCond());
  Writer.AddStmt(E->getLHS());
  Writer.AddStmt(E->getRHS());
  Writer.AddSourceLocation(E->getQuestionLoc(), Record);
  Writer.AddSourceLocation(E->getColonLoc(), Record);
  Code = pch::EXPR_CONDITIONAL_OPERATOR;
}

void PCHStmtWriter::VisitImplicitCastExpr(ImplicitCastExpr *E) {
  VisitCastExpr(E);
  Record.push_back(E->isLvalueCast());
  Code = pch::EXPR_IMPLICIT_CAST;
}

void PCHStmtWriter::VisitExplicitCastExpr(ExplicitCastExpr *E) {
  VisitCastExpr(E);
  Writer.AddTypeSourceInfo(E->getTypeInfoAsWritten(), Record);
}

void PCHStmtWriter::VisitCStyleCastExpr(CStyleCastExpr *E) {
  VisitExplicitCastExpr(E);
  Writer.AddSourceLocation(E->getLParenLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_CSTYLE_CAST;
}

void PCHStmtWriter::VisitCompoundLiteralExpr(CompoundLiteralExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getLParenLoc(), Record);
  Writer.AddTypeSourceInfo(E->getTypeSourceInfo(), Record);
  Writer.AddStmt(E->getInitializer());
  Record.push_back(E->isFileScope());
  Code = pch::EXPR_COMPOUND_LITERAL;
}

void PCHStmtWriter::VisitExtVectorElementExpr(ExtVectorElementExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getBase());
  Writer.AddIdentifierRef(&E->getAccessor(), Record);
  Writer.AddSourceLocation(E->getAccessorLoc(), Record);
  Code = pch::EXPR_EXT_VECTOR_ELEMENT;
}

void PCHStmtWriter::VisitInitListExpr(InitListExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumInits());
  for (unsigned I = 0, N = E->getNumInits(); I != N; ++I)
    Writer.AddStmt(E->getInit(I));
  Writer.AddStmt(E->getSyntacticForm());
  Writer.AddSourceLocation(E->getLBraceLoc(), Record);
  Writer.AddSourceLocation(E->getRBraceLoc(), Record);
  Writer.AddDeclRef(E->getInitializedFieldInUnion(), Record);
  Record.push_back(E->hadArrayRangeDesignator());
  Code = pch::EXPR_INIT_LIST;
}

void PCHStmtWriter::VisitDesignatedInitExpr(DesignatedInitExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumSubExprs());
  for (unsigned I = 0, N = E->getNumSubExprs(); I != N; ++I)
    Writer.AddStmt(E->getSubExpr(I));
  Writer.AddSourceLocation(E->getEqualOrColonLoc(), Record);
  Record.push_back(E->usesGNUSyntax());
  for (DesignatedInitExpr::designators_iterator D = E->designators_begin(),
                                             DEnd = E->designators_end();
       D != DEnd; ++D) {
    if (D->isFieldDesignator()) {
      if (FieldDecl *Field = D->getField()) {
        Record.push_back(pch::DESIG_FIELD_DECL);
        Writer.AddDeclRef(Field, Record);
      } else {
        Record.push_back(pch::DESIG_FIELD_NAME);
        Writer.AddIdentifierRef(D->getFieldName(), Record);
      }
      Writer.AddSourceLocation(D->getDotLoc(), Record);
      Writer.AddSourceLocation(D->getFieldLoc(), Record);
    } else if (D->isArrayDesignator()) {
      Record.push_back(pch::DESIG_ARRAY);
      Record.push_back(D->getFirstExprIndex());
      Writer.AddSourceLocation(D->getLBracketLoc(), Record);
      Writer.AddSourceLocation(D->getRBracketLoc(), Record);
    } else {
      assert(D->isArrayRangeDesignator() && "Unknown designator");
      Record.push_back(pch::DESIG_ARRAY_RANGE);
      Record.push_back(D->getFirstExprIndex());
      Writer.AddSourceLocation(D->getLBracketLoc(), Record);
      Writer.AddSourceLocation(D->getEllipsisLoc(), Record);
      Writer.AddSourceLocation(D->getRBracketLoc(), Record);
    }
  }
  Code = pch::EXPR_DESIGNATED_INIT;
}

void PCHStmtWriter::VisitImplicitValueInitExpr(ImplicitValueInitExpr *E) {
  VisitExpr(E);
  Code = pch::EXPR_IMPLICIT_VALUE_INIT;
}

void PCHStmtWriter::VisitVAArgExpr(VAArgExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getSubExpr());
  Writer.AddSourceLocation(E->getBuiltinLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_VA_ARG;
}

void PCHStmtWriter::VisitAddrLabelExpr(AddrLabelExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getAmpAmpLoc(), Record);
  Writer.AddSourceLocation(E->getLabelLoc(), Record);
  Record.push_back(Writer.GetLabelID(E->getLabel()));
  Code = pch::EXPR_ADDR_LABEL;
}

void PCHStmtWriter::VisitStmtExpr(StmtExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getSubStmt());
  Writer.AddSourceLocation(E->getLParenLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_STMT;
}

void PCHStmtWriter::VisitTypesCompatibleExpr(TypesCompatibleExpr *E) {
  VisitExpr(E);
  Writer.AddTypeRef(E->getArgType1(), Record);
  Writer.AddTypeRef(E->getArgType2(), Record);
  Writer.AddSourceLocation(E->getBuiltinLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_TYPES_COMPATIBLE;
}

void PCHStmtWriter::VisitChooseExpr(ChooseExpr *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getCond());
  Writer.AddStmt(E->getLHS());
  Writer.AddStmt(E->getRHS());
  Writer.AddSourceLocation(E->getBuiltinLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_CHOOSE;
}

void PCHStmtWriter::VisitGNUNullExpr(GNUNullExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getTokenLocation(), Record);
  Code = pch::EXPR_GNU_NULL;
}

void PCHStmtWriter::VisitShuffleVectorExpr(ShuffleVectorExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumSubExprs());
  for (unsigned I = 0, N = E->getNumSubExprs(); I != N; ++I)
    Writer.AddStmt(E->getExpr(I));
  Writer.AddSourceLocation(E->getBuiltinLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_SHUFFLE_VECTOR;
}

void PCHStmtWriter::VisitBlockExpr(BlockExpr *E) {
  VisitExpr(E);
  Writer.AddDeclRef(E->getBlockDecl(), Record);
  Record.push_back(E->hasBlockDeclRefExprs());
  Code = pch::EXPR_BLOCK;
}

void PCHStmtWriter::VisitBlockDeclRefExpr(BlockDeclRefExpr *E) {
  VisitExpr(E);
  Writer.AddDeclRef(E->getDecl(), Record);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Record.push_back(E->isByRef());
  Record.push_back(E->isConstQualAdded());
  Writer.AddStmt(E->getCopyConstructorExpr());
  Code = pch::EXPR_BLOCK_DECL_REF;
}

//===----------------------------------------------------------------------===//
// Objective-C Expressions and Statements.
//===----------------------------------------------------------------------===//

void PCHStmtWriter::VisitObjCStringLiteral(ObjCStringLiteral *E) {
  VisitExpr(E);
  Writer.AddStmt(E->getString());
  Writer.AddSourceLocation(E->getAtLoc(), Record);
  Code = pch::EXPR_OBJC_STRING_LITERAL;
}

void PCHStmtWriter::VisitObjCEncodeExpr(ObjCEncodeExpr *E) {
  VisitExpr(E);
  Writer.AddTypeSourceInfo(E->getEncodedTypeSourceInfo(), Record);
  Writer.AddSourceLocation(E->getAtLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_OBJC_ENCODE;
}

void PCHStmtWriter::VisitObjCSelectorExpr(ObjCSelectorExpr *E) {
  VisitExpr(E);
  Writer.AddSelectorRef(E->getSelector(), Record);
  Writer.AddSourceLocation(E->getAtLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_OBJC_SELECTOR_EXPR;
}

void PCHStmtWriter::VisitObjCProtocolExpr(ObjCProtocolExpr *E) {
  VisitExpr(E);
  Writer.AddDeclRef(E->getProtocol(), Record);
  Writer.AddSourceLocation(E->getAtLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_OBJC_PROTOCOL_EXPR;
}

void PCHStmtWriter::VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
  VisitExpr(E);
  Writer.AddDeclRef(E->getDecl(), Record);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Writer.AddStmt(E->getBase());
  Record.push_back(E->isArrow());
  Record.push_back(E->isFreeIvar());
  Code = pch::EXPR_OBJC_IVAR_REF_EXPR;
}

void PCHStmtWriter::VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *E) {
  VisitExpr(E);
  Writer.AddDeclRef(E->getProperty(), Record);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Writer.AddStmt(E->getBase());
  Code = pch::EXPR_OBJC_PROPERTY_REF_EXPR;
}

void PCHStmtWriter::VisitObjCImplicitSetterGetterRefExpr(
                                  ObjCImplicitSetterGetterRefExpr *E) {
  VisitExpr(E);
  Writer.AddDeclRef(E->getGetterMethod(), Record);
  Writer.AddDeclRef(E->getSetterMethod(), Record);

  // NOTE: InterfaceDecl and Base are mutually exclusive.
  Writer.AddDeclRef(E->getInterfaceDecl(), Record);
  Writer.AddStmt(E->getBase());
  Writer.AddSourceLocation(E->getLocation(), Record);
  Writer.AddSourceLocation(E->getClassLoc(), Record);
  Code = pch::EXPR_OBJC_KVC_REF_EXPR;
}

void PCHStmtWriter::VisitObjCMessageExpr(ObjCMessageExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumArgs());
  Record.push_back((unsigned)E->getReceiverKind()); // FIXME: stable encoding
  switch (E->getReceiverKind()) {
  case ObjCMessageExpr::Instance:
    Writer.AddStmt(E->getInstanceReceiver());
    break;

  case ObjCMessageExpr::Class:
    Writer.AddTypeSourceInfo(E->getClassReceiverTypeInfo(), Record);
    break;

  case ObjCMessageExpr::SuperClass:
  case ObjCMessageExpr::SuperInstance:
    Writer.AddTypeRef(E->getSuperType(), Record);
    Writer.AddSourceLocation(E->getSuperLoc(), Record);
    break;
  }

  if (E->getMethodDecl()) {
    Record.push_back(1);
    Writer.AddDeclRef(E->getMethodDecl(), Record);
  } else {
    Record.push_back(0);
    Writer.AddSelectorRef(E->getSelector(), Record);    
  }
    
  Writer.AddSourceLocation(E->getLeftLoc(), Record);
  Writer.AddSourceLocation(E->getRightLoc(), Record);

  for (CallExpr::arg_iterator Arg = E->arg_begin(), ArgEnd = E->arg_end();
       Arg != ArgEnd; ++Arg)
    Writer.AddStmt(*Arg);
  Code = pch::EXPR_OBJC_MESSAGE_EXPR;
}

void PCHStmtWriter::VisitObjCSuperExpr(ObjCSuperExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getLoc(), Record);
  Code = pch::EXPR_OBJC_SUPER_EXPR;
}

void PCHStmtWriter::VisitObjCForCollectionStmt(ObjCForCollectionStmt *S) {
  VisitStmt(S);
  Writer.AddStmt(S->getElement());
  Writer.AddStmt(S->getCollection());
  Writer.AddStmt(S->getBody());
  Writer.AddSourceLocation(S->getForLoc(), Record);
  Writer.AddSourceLocation(S->getRParenLoc(), Record);
  Code = pch::STMT_OBJC_FOR_COLLECTION;
}

void PCHStmtWriter::VisitObjCAtCatchStmt(ObjCAtCatchStmt *S) {
  Writer.AddStmt(S->getCatchBody());
  Writer.AddDeclRef(S->getCatchParamDecl(), Record);
  Writer.AddSourceLocation(S->getAtCatchLoc(), Record);
  Writer.AddSourceLocation(S->getRParenLoc(), Record);
  Code = pch::STMT_OBJC_CATCH;
}

void PCHStmtWriter::VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
  Writer.AddStmt(S->getFinallyBody());
  Writer.AddSourceLocation(S->getAtFinallyLoc(), Record);
  Code = pch::STMT_OBJC_FINALLY;
}

void PCHStmtWriter::VisitObjCAtTryStmt(ObjCAtTryStmt *S) {
  Record.push_back(S->getNumCatchStmts());
  Record.push_back(S->getFinallyStmt() != 0);
  Writer.AddStmt(S->getTryBody());
  for (unsigned I = 0, N = S->getNumCatchStmts(); I != N; ++I)
    Writer.AddStmt(S->getCatchStmt(I));
  if (S->getFinallyStmt())
    Writer.AddStmt(S->getFinallyStmt());
  Writer.AddSourceLocation(S->getAtTryLoc(), Record);
  Code = pch::STMT_OBJC_AT_TRY;
}

void PCHStmtWriter::VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *S) {
  Writer.AddStmt(S->getSynchExpr());
  Writer.AddStmt(S->getSynchBody());
  Writer.AddSourceLocation(S->getAtSynchronizedLoc(), Record);
  Code = pch::STMT_OBJC_AT_SYNCHRONIZED;
}

void PCHStmtWriter::VisitObjCAtThrowStmt(ObjCAtThrowStmt *S) {
  Writer.AddStmt(S->getThrowExpr());
  Writer.AddSourceLocation(S->getThrowLoc(), Record);
  Code = pch::STMT_OBJC_AT_THROW;
}

//===----------------------------------------------------------------------===//
// C++ Expressions and Statements.
//===----------------------------------------------------------------------===//

void PCHStmtWriter::VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
  VisitCallExpr(E);
  Record.push_back(E->getOperator());
  Code = pch::EXPR_CXX_OPERATOR_CALL;
}

void PCHStmtWriter::VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
  VisitCallExpr(E);
  Code = pch::EXPR_CXX_MEMBER_CALL;
}

void PCHStmtWriter::VisitCXXConstructExpr(CXXConstructExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getNumArgs());
  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I)
    Writer.AddStmt(E->getArg(I));
  Writer.AddDeclRef(E->getConstructor(), Record);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Record.push_back(E->isElidable());
  Record.push_back(E->requiresZeroInitialization());
  Record.push_back(E->getConstructionKind()); // FIXME: stable encoding
  Code = pch::EXPR_CXX_CONSTRUCT;
}

void PCHStmtWriter::VisitCXXNamedCastExpr(CXXNamedCastExpr *E) {
  VisitExplicitCastExpr(E);
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
}

void PCHStmtWriter::VisitCXXStaticCastExpr(CXXStaticCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = pch::EXPR_CXX_STATIC_CAST;
}

void PCHStmtWriter::VisitCXXDynamicCastExpr(CXXDynamicCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = pch::EXPR_CXX_DYNAMIC_CAST;
}

void PCHStmtWriter::VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = pch::EXPR_CXX_REINTERPRET_CAST;
}

void PCHStmtWriter::VisitCXXConstCastExpr(CXXConstCastExpr *E) {
  VisitCXXNamedCastExpr(E);
  Code = pch::EXPR_CXX_CONST_CAST;
}

void PCHStmtWriter::VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E) {
  VisitExplicitCastExpr(E);
  Writer.AddSourceLocation(E->getTypeBeginLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_CXX_FUNCTIONAL_CAST;
}

void PCHStmtWriter::VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
  VisitExpr(E);
  Record.push_back(E->getValue());
  Writer.AddSourceLocation(E->getLocation(), Record);
  Code = pch::EXPR_CXX_BOOL_LITERAL;
}

void PCHStmtWriter::VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Code = pch::EXPR_CXX_NULL_PTR_LITERAL;
}

void PCHStmtWriter::VisitCXXTypeidExpr(CXXTypeidExpr *E) {
  VisitExpr(E);
  Writer.AddSourceRange(E->getSourceRange(), Record);
  if (E->isTypeOperand()) {
    Writer.AddTypeSourceInfo(E->getTypeOperandSourceInfo(), Record);
    Code = pch::EXPR_CXX_TYPEID_TYPE;
  } else {
    Writer.AddStmt(E->getExprOperand());
    Code = pch::EXPR_CXX_TYPEID_EXPR;
  }
}

void PCHStmtWriter::VisitCXXThisExpr(CXXThisExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Record.push_back(E->isImplicit());
  Code = pch::EXPR_CXX_THIS;
}

void PCHStmtWriter::VisitCXXThrowExpr(CXXThrowExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getThrowLoc(), Record);
  Writer.AddStmt(E->getSubExpr());
  Code = pch::EXPR_CXX_THROW;
}

void PCHStmtWriter::VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getUsedLocation(), Record);
  if (E->isExprStored()) {
    Record.push_back(1);
    Writer.AddStmt(E->getExpr());
  } else {
    Record.push_back(0);
  }

  Code = pch::EXPR_CXX_DEFAULT_ARG;
}

void PCHStmtWriter::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *E) {
  VisitExpr(E);
  Writer.AddCXXTemporary(E->getTemporary(), Record);
  Writer.AddStmt(E->getSubExpr());
  Code = pch::EXPR_CXX_BIND_TEMPORARY;
}

void PCHStmtWriter::VisitCXXZeroInitValueExpr(CXXZeroInitValueExpr *E) {
  VisitExpr(E);
  Writer.AddSourceLocation(E->getTypeBeginLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_CXX_ZERO_INIT_VALUE;
}

void PCHStmtWriter::VisitCXXNewExpr(CXXNewExpr *E) {
  VisitExpr(E);
  Record.push_back(E->isGlobalNew());
  Record.push_back(E->isParenTypeId());
  Record.push_back(E->hasInitializer());
  Record.push_back(E->isArray());
  Record.push_back(E->getNumPlacementArgs());
  Record.push_back(E->getNumConstructorArgs());
  Writer.AddDeclRef(E->getOperatorNew(), Record);
  Writer.AddDeclRef(E->getOperatorDelete(), Record);
  Writer.AddDeclRef(E->getConstructor(), Record);
  Writer.AddSourceLocation(E->getStartLoc(), Record);
  Writer.AddSourceLocation(E->getEndLoc(), Record);
  for (CXXNewExpr::arg_iterator I = E->raw_arg_begin(), e = E->raw_arg_end();
       I != e; ++I)
    Writer.AddStmt(*I);
  
  Code = pch::EXPR_CXX_NEW;
}

void PCHStmtWriter::VisitCXXDeleteExpr(CXXDeleteExpr *E) {
  VisitExpr(E);
  Record.push_back(E->isGlobalDelete());
  Record.push_back(E->isArrayForm());
  Writer.AddDeclRef(E->getOperatorDelete(), Record);
  Writer.AddStmt(E->getArgument());
  Writer.AddSourceLocation(E->getSourceRange().getBegin(), Record);
  
  Code = pch::EXPR_CXX_DELETE;
}

void PCHStmtWriter::VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E) {
  VisitExpr(E);

  Writer.AddStmt(E->getBase());
  Record.push_back(E->isArrow());
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
  Writer.AddNestedNameSpecifier(E->getQualifier(), Record);
  Writer.AddSourceRange(E->getQualifierRange(), Record);
  Writer.AddTypeSourceInfo(E->getScopeTypeInfo(), Record);
  Writer.AddSourceLocation(E->getColonColonLoc(), Record);
  Writer.AddSourceLocation(E->getTildeLoc(), Record);

  // PseudoDestructorTypeStorage.
  Writer.AddIdentifierRef(E->getDestroyedTypeIdentifier(), Record);
  if (E->getDestroyedTypeIdentifier())
    Writer.AddSourceLocation(E->getDestroyedTypeLoc(), Record);
  else
    Writer.AddTypeSourceInfo(E->getDestroyedTypeInfo(), Record);

  Code = pch::EXPR_CXX_PSEUDO_DESTRUCTOR;
}

void PCHStmtWriter::VisitCXXExprWithTemporaries(CXXExprWithTemporaries *E) {
  VisitExpr(E);
  Record.push_back(E->getNumTemporaries());
  for (unsigned i = 0, e = E->getNumTemporaries(); i != e; ++i)
    Writer.AddCXXTemporary(E->getTemporary(i), Record);
  
  Writer.AddStmt(E->getSubExpr());
  Code = pch::EXPR_CXX_EXPR_WITH_TEMPORARIES;
}

void
PCHStmtWriter::VisitCXXDependentScopeMemberExpr(CXXDependentScopeMemberExpr *E){
  VisitExpr(E);
  
  // Don't emit anything here, NumTemplateArgs must be emitted first.

  if (E->hasExplicitTemplateArgs()) {
    const ExplicitTemplateArgumentList &Args
      = *E->getExplicitTemplateArgumentList();
    assert(Args.NumTemplateArgs &&
           "Num of template args was zero! PCH reading will mess up!");
    Record.push_back(Args.NumTemplateArgs);
    AddExplicitTemplateArgumentList(Args);
  } else {
    Record.push_back(0);
  }
  
  if (!E->isImplicitAccess())
    Writer.AddStmt(E->getBase());
  else
    Writer.AddStmt(0);
  Writer.AddTypeRef(E->getBaseType(), Record);
  Record.push_back(E->isArrow());
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
  Writer.AddNestedNameSpecifier(E->getQualifier(), Record);
  Writer.AddSourceRange(E->getQualifierRange(), Record);
  Writer.AddDeclRef(E->getFirstQualifierFoundInScope(), Record);
  Writer.AddDeclarationName(E->getMember(), Record);
  Writer.AddSourceLocation(E->getMemberLoc(), Record);
  Code = pch::EXPR_CXX_DEPENDENT_SCOPE_MEMBER;
}

void
PCHStmtWriter::VisitDependentScopeDeclRefExpr(DependentScopeDeclRefExpr *E) {
  VisitExpr(E);
  
  // Don't emit anything here, NumTemplateArgs must be emitted first.

  if (E->hasExplicitTemplateArgs()) {
    const ExplicitTemplateArgumentList &Args = E->getExplicitTemplateArgs();
    assert(Args.NumTemplateArgs &&
           "Num of template args was zero! PCH reading will mess up!");
    Record.push_back(Args.NumTemplateArgs);
    AddExplicitTemplateArgumentList(Args);
  } else {
    Record.push_back(0);
  }

  Writer.AddDeclarationName(E->getDeclName(), Record);
  Writer.AddSourceLocation(E->getLocation(), Record);
  Writer.AddSourceRange(E->getQualifierRange(), Record);
  Writer.AddNestedNameSpecifier(E->getQualifier(), Record);
  Code = pch::EXPR_CXX_DEPENDENT_SCOPE_DECL_REF;
}

void
PCHStmtWriter::VisitCXXUnresolvedConstructExpr(CXXUnresolvedConstructExpr *E) {
  VisitExpr(E);
  Record.push_back(E->arg_size());
  for (CXXUnresolvedConstructExpr::arg_iterator
         ArgI = E->arg_begin(), ArgE = E->arg_end(); ArgI != ArgE; ++ArgI)
    Writer.AddStmt(*ArgI);
  Writer.AddSourceLocation(E->getTypeBeginLoc(), Record);
  Writer.AddTypeRef(E->getTypeAsWritten(), Record);
  Writer.AddSourceLocation(E->getLParenLoc(), Record);
  Writer.AddSourceLocation(E->getRParenLoc(), Record);
  Code = pch::EXPR_CXX_UNRESOLVED_CONSTRUCT;
}

void PCHStmtWriter::VisitOverloadExpr(OverloadExpr *E) {
  VisitExpr(E);
  
  // Don't emit anything here, NumTemplateArgs must be emitted first.

  if (E->hasExplicitTemplateArgs()) {
    const ExplicitTemplateArgumentList &Args = E->getExplicitTemplateArgs();
    assert(Args.NumTemplateArgs &&
           "Num of template args was zero! PCH reading will mess up!");
    Record.push_back(Args.NumTemplateArgs);
    AddExplicitTemplateArgumentList(Args);
  } else {
    Record.push_back(0);
  }

  Record.push_back(E->getNumDecls());
  for (OverloadExpr::decls_iterator
         OvI = E->decls_begin(), OvE = E->decls_end(); OvI != OvE; ++OvI) {
    Writer.AddDeclRef(OvI.getDecl(), Record);
    Record.push_back(OvI.getAccess());
  }

  Writer.AddDeclarationName(E->getName(), Record);
  Writer.AddNestedNameSpecifier(E->getQualifier(), Record);
  Writer.AddSourceRange(E->getQualifierRange(), Record);
  Writer.AddSourceLocation(E->getNameLoc(), Record);
}

void PCHStmtWriter::VisitUnresolvedMemberExpr(UnresolvedMemberExpr *E) {
  VisitOverloadExpr(E);
  Record.push_back(E->isArrow());
  Record.push_back(E->hasUnresolvedUsing());
  Writer.AddStmt(!E->isImplicitAccess() ? E->getBase() : 0);
  Writer.AddTypeRef(E->getBaseType(), Record);
  Writer.AddSourceLocation(E->getOperatorLoc(), Record);
  Code = pch::EXPR_CXX_UNRESOLVED_MEMBER;
}

void PCHStmtWriter::VisitUnresolvedLookupExpr(UnresolvedLookupExpr *E) {
  VisitOverloadExpr(E);
  Record.push_back(E->requiresADL());
  Record.push_back(E->isOverloaded());
  Writer.AddDeclRef(E->getNamingClass(), Record);
  Code = pch::EXPR_CXX_UNRESOLVED_LOOKUP;
}

//===----------------------------------------------------------------------===//
// PCHWriter Implementation
//===----------------------------------------------------------------------===//

unsigned PCHWriter::RecordSwitchCaseID(SwitchCase *S) {
  assert(SwitchCaseIDs.find(S) == SwitchCaseIDs.end() &&
         "SwitchCase recorded twice");
  unsigned NextID = SwitchCaseIDs.size();
  SwitchCaseIDs[S] = NextID;
  return NextID;
}

unsigned PCHWriter::getSwitchCaseID(SwitchCase *S) {
  assert(SwitchCaseIDs.find(S) != SwitchCaseIDs.end() &&
         "SwitchCase hasn't been seen yet");
  return SwitchCaseIDs[S];
}

/// \brief Retrieve the ID for the given label statement, which may
/// or may not have been emitted yet.
unsigned PCHWriter::GetLabelID(LabelStmt *S) {
  std::map<LabelStmt *, unsigned>::iterator Pos = LabelIDs.find(S);
  if (Pos != LabelIDs.end())
    return Pos->second;

  unsigned NextID = LabelIDs.size();
  LabelIDs[S] = NextID;
  return NextID;
}

/// \brief Write the given substatement or subexpression to the
/// bitstream.
void PCHWriter::WriteSubStmt(Stmt *S) {
  RecordData Record;
  PCHStmtWriter Writer(*this, Record);
  ++NumStatements;
  
  if (!S) {
    Stream.EmitRecord(pch::STMT_NULL_PTR, Record);
    return;
  }

  // Redirect PCHWriter::AddStmt to collect sub stmts.
  llvm::SmallVector<Stmt *, 16> SubStmts;
  CollectedStmts = &SubStmts;

  Writer.Code = pch::STMT_NULL_PTR;
  Writer.Visit(S);
  
#ifndef NDEBUG
  if (Writer.Code == pch::STMT_NULL_PTR) {
    SourceManager &SrcMgr
      = DeclIDs.begin()->first->getASTContext().getSourceManager();
    S->dump(SrcMgr);
    assert(0 && "Unhandled sub statement writing PCH file");
  }
#endif

  // Revert PCHWriter::AddStmt.
  CollectedStmts = &StmtsToEmit;

  // Write the sub stmts in reverse order, last to first. When reading them back
  // we will read them in correct order by "pop"ing them from the Stmts stack.
  // This simplifies reading and allows to store a variable number of sub stmts
  // without knowing it in advance.
  while (!SubStmts.empty())
    WriteSubStmt(SubStmts.pop_back_val());
  
  Stream.EmitRecord(Writer.Code, Record);
}

/// \brief Flush all of the statements that have been added to the
/// queue via AddStmt().
void PCHWriter::FlushStmts() {
  RecordData Record;

  for (unsigned I = 0, N = StmtsToEmit.size(); I != N; ++I) {
    WriteSubStmt(StmtsToEmit[I]);
    
    assert(N == StmtsToEmit.size() &&
           "Substatement writen via AddStmt rather than WriteSubStmt!");

    // Note that we are at the end of a full expression. Any
    // expression records that follow this one are part of a different
    // expression.
    Stream.EmitRecord(pch::STMT_STOP, Record);
  }

  StmtsToEmit.clear();
}
