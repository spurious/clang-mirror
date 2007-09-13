//===--- Sema.h - Semantic Analysis & AST Building --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Sema class, which performs semantic analysis and
// builds ASTs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_SEMA_H
#define LLVM_CLANG_AST_SEMA_H

#include "clang/Parse/Action.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <vector>
#include <string>

namespace llvm {
  class APSInt;
}

namespace clang {
  class ASTContext;
  class Preprocessor;
  class Decl;
  class ScopedDecl;
  class Expr;
  class InitListExpr;
  class VarDecl;
  class ParmVarDecl;
  class TypedefDecl;
  class FunctionDecl;
  class QualType;
  struct LangOptions;
  struct DeclaratorChunk;
  class Token;
  class IntegerLiteral;
  class ArrayType;
  class LabelStmt;
  class SwitchStmt;
  class OCUVectorType;
  class TypedefDecl;

/// Sema - This implements semantic analysis and AST building for C.
class Sema : public Action {
  Preprocessor &PP;
  
  ASTContext &Context;
  
  /// CurFunctionDecl - If inside of a function body, this contains a pointer to
  /// the function decl for the function being parsed.
  FunctionDecl *CurFunctionDecl;
  
  /// LastInGroupList - This vector is populated when there are multiple
  /// declarators in a single decl group (e.g. "int A, B, C").  In this case,
  /// all but the last decl will be entered into this.  This is used by the
  /// ASTStreamer.
  std::vector<Decl*> &LastInGroupList;
  
  /// LabelMap - This is a mapping from label identifiers to the LabelStmt for
  /// it (which acts like the label decl in some ways).  Forward referenced
  /// labels have a LabelStmt created for them with a null location & SubStmt.
  llvm::DenseMap<IdentifierInfo*, LabelStmt*> LabelMap;
  
  llvm::SmallVector<SwitchStmt*, 8> SwitchStack;
  
  /// OCUVectorDecls - This is a list all the OCU vector types. This allows
  /// us to associate a raw vector type with one of the OCU type names.
  /// This is only necessary for issuing pretty diagnostics.
  llvm::SmallVector<TypedefDecl*, 24> OCUVectorDecls;

  // Enum values used by KnownFunctionIDs (see below).
  enum {
    id_printf,
    id_fprintf,
    id_sprintf,
    id_snprintf,
    id_asprintf,
    id_vsnprintf,
    id_vasprintf,
    id_vfprintf,
    id_vsprintf,
    id_vprintf,
    id_num_known_functions
  };
  
  /// KnownFunctionIDs - This is a list of IdentifierInfo objects to a set
  /// of known functions used by the semantic analysis to do various
  /// kinds of checking (e.g. checking format string errors in printf calls).
  /// This list is populated upon the creation of a Sema object.    
  IdentifierInfo* KnownFunctionIDs[ id_num_known_functions ];
  
public:
  Sema(Preprocessor &pp, ASTContext &ctxt, std::vector<Decl*> &prevInGroup);
  
  const LangOptions &getLangOptions() const;
  
  /// The primitive diagnostic helpers - always returns true, which simplifies 
  /// error handling (i.e. less code).
  bool Diag(SourceLocation Loc, unsigned DiagID);
  bool Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg);
  bool Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg1,
            const std::string &Msg2);

  /// More expressive diagnostic helpers for expressions (say that 6 times:-)
  bool Diag(SourceLocation Loc, unsigned DiagID, SourceRange R1);
  bool Diag(SourceLocation Loc, unsigned DiagID, 
            SourceRange R1, SourceRange R2);
  bool Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg,
            SourceRange R1);
  bool Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg,
            SourceRange R1, SourceRange R2);
  bool Diag(SourceLocation Loc, unsigned DiagID, const std::string &Msg1, 
            const std::string &Msg2, SourceRange R1);
  bool Diag(SourceLocation Loc, unsigned DiagID, 
            const std::string &Msg1, const std::string &Msg2, 
            SourceRange R1, SourceRange R2);

  virtual void DeleteExpr(ExprTy *E);
  virtual void DeleteStmt(StmtTy *S);

  //===--------------------------------------------------------------------===//
  // Type Analysis / Processing: SemaType.cpp.
  //
  QualType GetTypeForDeclarator(Declarator &D, Scope *S);
  
  virtual TypeResult ParseTypeName(Scope *S, Declarator &D);
  
  virtual TypeResult ParseParamDeclaratorType(Scope *S, Declarator &D);
private:
  //===--------------------------------------------------------------------===//
  // Symbol table / Decl tracking callbacks: SemaDecl.cpp.
  //
  virtual DeclTy *isTypeName(const IdentifierInfo &II, Scope *S) const;
  virtual DeclTy *ParseDeclarator(Scope *S, Declarator &D, DeclTy *LastInGroup);
  void AddInitializerToDecl(DeclTy *dcl, ExprTy *init);
  virtual DeclTy *FinalizeDeclaratorGroup(Scope *S, DeclTy *Group);

  virtual DeclTy *ParseStartOfFunctionDef(Scope *S, Declarator &D);
  virtual DeclTy *ParseFunctionDefBody(DeclTy *Decl, StmtTy *Body);
  virtual void PopScope(SourceLocation Loc, Scope *S);

  /// ParsedFreeStandingDeclSpec - This method is invoked when a declspec with
  /// no declarator (e.g. "struct foo;") is parsed.
  virtual DeclTy *ParsedFreeStandingDeclSpec(Scope *S, DeclSpec &DS);  
  
  virtual DeclTy *ParseTag(Scope *S, unsigned TagType, TagKind TK,
                           SourceLocation KWLoc, IdentifierInfo *Name,
                           SourceLocation NameLoc, AttributeList *Attr);
  virtual DeclTy *ParseField(Scope *S, DeclTy *TagDecl,SourceLocation DeclStart,
                             Declarator &D, ExprTy *BitfieldWidth);
  virtual void ParseRecordBody(SourceLocation RecLoc, DeclTy *TagDecl,
                               DeclTy **Fields, unsigned NumFields);
  virtual DeclTy *ParseEnumConstant(Scope *S, DeclTy *EnumDecl,
                                    DeclTy *LastEnumConstant,
                                    SourceLocation IdLoc, IdentifierInfo *Id,
                                    SourceLocation EqualLoc, ExprTy *Val);
  virtual void ParseEnumBody(SourceLocation EnumLoc, DeclTy *EnumDecl,
                             DeclTy **Elements, unsigned NumElements);
private:
  /// Subroutines of ParseDeclarator()...
  TypedefDecl *ParseTypedefDecl(Scope *S, Declarator &D, ScopedDecl *LastDecl);
  TypedefDecl *MergeTypeDefDecl(TypedefDecl *New, ScopedDecl *Old);
  FunctionDecl *MergeFunctionDecl(FunctionDecl *New, ScopedDecl *Old);
  VarDecl *MergeVarDecl(VarDecl *New, ScopedDecl *Old);
  /// AddTopLevelDecl - called after the decl has been fully processed.
  /// Allows for bookkeeping and post-processing of each declaration.
  void AddTopLevelDecl(Decl *current, Decl *last);

  /// More parsing and symbol table subroutines...
  ParmVarDecl *ParseParamDeclarator(DeclaratorChunk &FI, unsigned ArgNo,
                                    Scope *FnBodyScope);
  ScopedDecl *LookupScopedDecl(IdentifierInfo *II, unsigned NSI, 
                               SourceLocation IdLoc, Scope *S);  
  ScopedDecl *LazilyCreateBuiltin(IdentifierInfo *II, unsigned ID, Scope *S);
  Decl *ImplicitlyDefineFunction(SourceLocation Loc, IdentifierInfo &II,
                                 Scope *S);
  // Decl attributes - this routine is the top level dispatcher. 
  void HandleDeclAttributes(Decl *New, AttributeList *declspec_prefix,
                            AttributeList *declarator_postfix);
  void HandleDeclAttribute(Decl *New, AttributeList *rawAttr);
                                
  // HandleVectorTypeAttribute - this attribute is only applicable to 
  // integral and float scalars, although arrays, pointers, and function
  // return values are allowed in conjunction with this construct. Aggregates
  // with this attribute are invalid, even if they are of the same size as a
  // corresponding scalar.
  // The raw attribute should contain precisely 1 argument, the vector size 
  // for the variable, measured in bytes. If curType and rawAttr are well
  // formed, this routine will return a new vector type.
  QualType HandleVectorTypeAttribute(QualType curType, AttributeList *rawAttr);
  void HandleOCUVectorTypeAttribute(TypedefDecl *d, AttributeList *rawAttr);
  
  //===--------------------------------------------------------------------===//
  // Statement Parsing Callbacks: SemaStmt.cpp.
public:
  virtual StmtResult ParseExprStmt(ExprTy *Expr);
  
  virtual StmtResult ParseNullStmt(SourceLocation SemiLoc);
  virtual StmtResult ParseCompoundStmt(SourceLocation L, SourceLocation R,
                                       StmtTy **Elts, unsigned NumElts,
                                       bool isStmtExpr);
  virtual StmtResult ParseDeclStmt(DeclTy *Decl);
  virtual StmtResult ParseCaseStmt(SourceLocation CaseLoc, ExprTy *LHSVal,
                                   SourceLocation DotDotDotLoc, ExprTy *RHSVal,
                                   SourceLocation ColonLoc, StmtTy *SubStmt);
  virtual StmtResult ParseDefaultStmt(SourceLocation DefaultLoc,
                                      SourceLocation ColonLoc, StmtTy *SubStmt,
                                      Scope *CurScope);
  virtual StmtResult ParseLabelStmt(SourceLocation IdentLoc, IdentifierInfo *II,
                                    SourceLocation ColonLoc, StmtTy *SubStmt);
  virtual StmtResult ParseIfStmt(SourceLocation IfLoc, ExprTy *CondVal,
                                 StmtTy *ThenVal, SourceLocation ElseLoc,
                                 StmtTy *ElseVal);
  virtual StmtResult StartSwitchStmt(ExprTy *Cond);
  virtual StmtResult FinishSwitchStmt(SourceLocation SwitchLoc, StmtTy *Switch, 
                                      ExprTy *Body);
  virtual StmtResult ParseWhileStmt(SourceLocation WhileLoc, ExprTy *Cond,
                                    StmtTy *Body);
  virtual StmtResult ParseDoStmt(SourceLocation DoLoc, StmtTy *Body,
                                 SourceLocation WhileLoc, ExprTy *Cond);
  
  virtual StmtResult ParseForStmt(SourceLocation ForLoc, 
                                  SourceLocation LParenLoc, 
                                  StmtTy *First, ExprTy *Second, ExprTy *Third,
                                  SourceLocation RParenLoc, StmtTy *Body);
  virtual StmtResult ParseGotoStmt(SourceLocation GotoLoc,
                                   SourceLocation LabelLoc,
                                   IdentifierInfo *LabelII);
  virtual StmtResult ParseIndirectGotoStmt(SourceLocation GotoLoc,
                                           SourceLocation StarLoc,
                                           ExprTy *DestExp);
  virtual StmtResult ParseContinueStmt(SourceLocation ContinueLoc,
                                       Scope *CurScope);
  virtual StmtResult ParseBreakStmt(SourceLocation GotoLoc, Scope *CurScope);
  
  virtual StmtResult ParseReturnStmt(SourceLocation ReturnLoc,
                                     ExprTy *RetValExp);
  
  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks: SemaExpr.cpp.

  // Primary Expressions.
  virtual ExprResult ParseIdentifierExpr(Scope *S, SourceLocation Loc,
                                         IdentifierInfo &II,
                                         bool HasTrailingLParen);
  virtual ExprResult ParsePreDefinedExpr(SourceLocation Loc,
                                            tok::TokenKind Kind);
  virtual ExprResult ParseNumericConstant(const Token &);
  virtual ExprResult ParseCharacterConstant(const Token &);
  virtual ExprResult ParseParenExpr(SourceLocation L, SourceLocation R,
                                    ExprTy *Val);

  /// ParseStringLiteral - The specified tokens were lexed as pasted string
  /// fragments (e.g. "foo" "bar" L"baz").
  virtual ExprResult ParseStringLiteral(const Token *Toks, unsigned NumToks);
    
  // Binary/Unary Operators.  'Tok' is the token for the operator.
  virtual ExprResult ParseUnaryOp(SourceLocation OpLoc, tok::TokenKind Op,
                                  ExprTy *Input);
  virtual ExprResult 
    ParseSizeOfAlignOfTypeExpr(SourceLocation OpLoc, bool isSizeof, 
                               SourceLocation LParenLoc, TypeTy *Ty,
                               SourceLocation RParenLoc);
  
  virtual ExprResult ParsePostfixUnaryOp(SourceLocation OpLoc, 
                                         tok::TokenKind Kind, ExprTy *Input);
  
  virtual ExprResult ParseArraySubscriptExpr(ExprTy *Base, SourceLocation LLoc,
                                             ExprTy *Idx, SourceLocation RLoc);
  virtual ExprResult ParseMemberReferenceExpr(ExprTy *Base,SourceLocation OpLoc,
                                              tok::TokenKind OpKind,
                                              SourceLocation MemberLoc,
                                              IdentifierInfo &Member);
  
  /// ParseCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.
  virtual ExprResult ParseCallExpr(ExprTy *Fn, SourceLocation LParenLoc,
                                   ExprTy **Args, unsigned NumArgs,
                                   SourceLocation *CommaLocs,
                                   SourceLocation RParenLoc);
  
  virtual ExprResult ParseCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
                                   SourceLocation RParenLoc, ExprTy *Op);
                                   
  virtual ExprResult ParseCompoundLiteral(SourceLocation LParenLoc, TypeTy *Ty,
                                          SourceLocation RParenLoc, ExprTy *Op);
  
  virtual ExprResult ParseInitList(SourceLocation LParenLoc, 
                                   ExprTy **InitList, unsigned NumInit,
                                   SourceLocation RParenLoc);
                                   
  virtual ExprResult ParseBinOp(SourceLocation TokLoc, tok::TokenKind Kind,
                                ExprTy *LHS,ExprTy *RHS);
  
  /// ParseConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  virtual ExprResult ParseConditionalOp(SourceLocation QuestionLoc, 
                                        SourceLocation ColonLoc,
                                        ExprTy *Cond, ExprTy *LHS, ExprTy *RHS);

  /// ParseAddrLabel - Parse the GNU address of label extension: "&&foo".
  virtual ExprResult ParseAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                                    IdentifierInfo *LabelII);
  
  virtual ExprResult ParseStmtExpr(SourceLocation LPLoc, StmtTy *SubStmt,
                                   SourceLocation RPLoc); // "({..})"

  /// __builtin_offsetof(type, a.b[123][456].c)
  virtual ExprResult ParseBuiltinOffsetOf(SourceLocation BuiltinLoc,
                                          SourceLocation TypeLoc, TypeTy *Arg1,
                                          OffsetOfComponent *CompPtr,
                                          unsigned NumComponents,
                                          SourceLocation RParenLoc);
    
  // __builtin_types_compatible_p(type1, type2)
  virtual ExprResult ParseTypesCompatibleExpr(SourceLocation BuiltinLoc, 
                                              TypeTy *arg1, TypeTy *arg2,
                                              SourceLocation RPLoc);
                                              
  // __builtin_choose_expr(constExpr, expr1, expr2)
  virtual ExprResult ParseChooseExpr(SourceLocation BuiltinLoc, 
                                     ExprTy *cond, ExprTy *expr1, ExprTy *expr2,
                                     SourceLocation RPLoc);
  
  /// ParseCXXCasts - Parse {dynamic,static,reinterpret,const}_cast's.
  virtual ExprResult ParseCXXCasts(SourceLocation OpLoc, tok::TokenKind Kind,
                                   SourceLocation LAngleBracketLoc, TypeTy *Ty,
                                   SourceLocation RAngleBracketLoc,
                                   SourceLocation LParenLoc, ExprTy *E,
                                   SourceLocation RParenLoc);

  /// ParseCXXBoolLiteral - Parse {true,false} literals.
  virtual ExprResult ParseCXXBoolLiteral(SourceLocation OpLoc,
                                         tok::TokenKind Kind);
  
  // ParseObjCStringLiteral - Parse Objective-C string literals.
  virtual ExprResult ParseObjCStringLiteral(ExprTy *string);
  virtual ExprResult ParseObjCEncodeExpression(SourceLocation AtLoc,
                                               SourceLocation LParenLoc,
                                               TypeTy *Ty,
                                               SourceLocation RParenLoc);
  
  // Objective-C declarations.
  virtual DeclTy *ObjcStartClassInterface(SourceLocation AtInterafceLoc,
                    IdentifierInfo *ClassName, SourceLocation ClassLoc,
                    IdentifierInfo *SuperName, SourceLocation SuperLoc,
                    IdentifierInfo **ProtocolNames, unsigned NumProtocols,
                    AttributeList *AttrList);
                    
  virtual DeclTy *ObjcClassDeclaration(Scope *S, SourceLocation AtClassLoc,
                                       IdentifierInfo **IdentList,
                                       unsigned NumElts);

  virtual void ObjcAddMethodsToClass(DeclTy *ClassDecl, 
				     DeclTy **allMethods, unsigned allNum);
  virtual DeclTy *ObjcBuildMethodDeclaration(SourceLocation MethodLoc, 
	            tok::TokenKind MethodType, TypeTy *ReturnType,
     		    ObjcKeywordInfo *Keywords, unsigned NumKeywords, 
     		    AttributeList *AttrList);
  virtual DeclTy *ObjcBuildMethodDeclaration(SourceLocation MethodLoc, 
 	     	    tok::TokenKind MethodType, TypeTy *ReturnType,
     		    IdentifierInfo *SelectorName, AttributeList *AttrList);
                                      
  virtual void ObjcAddInstanceVariable(DeclTy *ClassDec, DeclTy **Ivar,
				       unsigned numIvars,
                                       tok::ObjCKeywordKind *visibility);
private:
  // UsualUnaryConversions - promotes integers (C99 6.3.1.1p2) and converts
  // functions and arrays to their respective pointers (C99 6.3.2.1). 
  void UsualUnaryConversions(Expr *&expr); 
  
  // DefaultFunctionArrayConversion - converts functions and arrays
  // to their respective pointers (C99 6.3.2.1). 
  void DefaultFunctionArrayConversion(Expr *&expr);
  
  // DefaultArgumentPromotion (C99 6.5.2.2p6). Used for function calls that
  // do not have a prototype. Integer promotions are performed on each 
  // argument, and arguments that have type float are promoted to double.
  void DefaultArgumentPromotion(Expr *&expr);
  
  // UsualArithmeticConversions - performs the UsualUnaryConversions on it's
  // operands and then handles various conversions that are common to binary
  // operators (C99 6.3.1.8). If both operands aren't arithmetic, this
  // routine returns the first non-arithmetic type found. The client is 
  // responsible for emitting appropriate error diagnostics.
  QualType UsualArithmeticConversions(Expr *&lExpr, Expr *&rExpr,
                                      bool isCompAssign = false);
  enum AssignmentCheckResult {
    Compatible,
    Incompatible,
    PointerFromInt, 
    IntFromPointer,
    IncompatiblePointer,
    CompatiblePointerDiscardsQualifiers
  };
  // CheckAssignmentConstraints - Perform type checking for assignment, 
  // argument passing, variable initialization, and function return values. 
  // This routine is only used by the following two methods. C99 6.5.16.
  AssignmentCheckResult CheckAssignmentConstraints(QualType lhs, QualType rhs);
  
  // CheckSingleAssignmentConstraints - Currently used by ParseCallExpr,
  // CheckAssignmentOperands, and ParseReturnStmt. Prior to type checking, 
  // this routine performs the default function/array converions.
  AssignmentCheckResult CheckSingleAssignmentConstraints(QualType lhs, 
                                                         Expr *&rExpr);
  // CheckCompoundAssignmentConstraints - Type check without performing any 
  // conversions. For compound assignments, the "Check...Operands" methods 
  // perform the necessary conversions. 
  AssignmentCheckResult CheckCompoundAssignmentConstraints(QualType lhs, 
                                                           QualType rhs);
  
  // Helper function for CheckAssignmentConstraints (C99 6.5.16.1p1)
  AssignmentCheckResult CheckPointerTypesForAssignment(QualType lhsType, 
                                                       QualType rhsType);
  
  /// the following "Check" methods will return a valid/converted QualType
  /// or a null QualType (indicating an error diagnostic was issued).
    
  /// type checking binary operators (subroutines of ParseBinOp).
  inline void InvalidOperands(SourceLocation l, Expr *&lex, Expr *&rex);
  inline QualType CheckVectorOperands(SourceLocation l, Expr *&lex, Expr *&rex);
  inline QualType CheckMultiplyDivideOperands( // C99 6.5.5
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false); 
  inline QualType CheckRemainderOperands( // C99 6.5.5
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false); 
  inline QualType CheckAdditionOperands( // C99 6.5.6
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false);
  inline QualType CheckSubtractionOperands( // C99 6.5.6
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false);
  inline QualType CheckShiftOperands( // C99 6.5.7
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false);
  inline QualType CheckCompareOperands( // C99 6.5.8/9
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isRelational);
  inline QualType CheckBitwiseOperands( // C99 6.5.[10...12]
    Expr *&lex, Expr *&rex, SourceLocation OpLoc, bool isCompAssign = false); 
  inline QualType CheckLogicalOperands( // C99 6.5.[13,14]
    Expr *&lex, Expr *&rex, SourceLocation OpLoc);
  // CheckAssignmentOperands is used for both simple and compound assignment.
  // For simple assignment, pass both expressions and a null converted type.
  // For compound assignment, pass both expressions and the converted type.
  inline QualType CheckAssignmentOperands( // C99 6.5.16.[1,2]
    Expr *lex, Expr *&rex, SourceLocation OpLoc, QualType convertedType);
  inline QualType CheckCommaOperands( // C99 6.5.17
    Expr *&lex, Expr *&rex, SourceLocation OpLoc);
  inline QualType CheckConditionalOperands( // C99 6.5.15
    Expr *&cond, Expr *&lhs, Expr *&rhs, SourceLocation questionLoc);
  
  /// type checking unary operators (subroutines of ParseUnaryOp).
  /// C99 6.5.3.1, 6.5.3.2, 6.5.3.4
  QualType CheckIncrementDecrementOperand(Expr *op, SourceLocation OpLoc);   
  QualType CheckAddressOfOperand(Expr *op, SourceLocation OpLoc);
  QualType CheckIndirectionOperand(Expr *op, SourceLocation OpLoc);
  QualType CheckSizeOfAlignOfOperand(QualType type, SourceLocation loc, 
                                     bool isSizeof);
  QualType CheckRealImagOperand(Expr *&Op, SourceLocation OpLoc);
  
  /// type checking primary expressions.
  QualType CheckOCUVectorComponent(QualType baseType, SourceLocation OpLoc,
                                   IdentifierInfo &Comp, SourceLocation CmpLoc);
  
  /// type checking declaration initializers (C99 6.7.8)
  bool CheckInitializer(Expr *&simpleInit_or_initList, QualType &declType,
                        bool isStatic);
  bool CheckSingleInitializer(Expr *&simpleInit, QualType declType);
  bool CheckInitExpr(Expr *expr, InitListExpr *IList, unsigned slot,
                     bool isStatic, QualType ElementType);
  void CheckVariableInitList(QualType DeclType, InitListExpr *IList, 
                             QualType ElementType, bool isStatic, 
                             int &nInitializers, bool &hadError);
  void CheckConstantInitList(QualType DeclType, InitListExpr *IList, 
                             QualType ElementType, bool isStatic, 
                             int &nInitializers, bool &hadError);
   
  /// ConvertIntegerToTypeWarnOnOverflow - Convert the specified APInt to have
  /// the specified width and sign.  If an overflow occurs, detect it and emit
  /// the specified diagnostic.
  void ConvertIntegerToTypeWarnOnOverflow(llvm::APSInt &OldVal, 
                                          unsigned NewWidth, bool NewSign,
                                          SourceLocation Loc, unsigned DiagID);
  
  //===--------------------------------------------------------------------===//
  // Extra semantic analysis beyond the C type system
  private:
  
  bool CheckFunctionCall(Expr *Fn,
                         SourceLocation LParenLoc, SourceLocation RParenLoc,
                         FunctionDecl *FDecl,
                         Expr** Args, unsigned NumArgsInCall);

  void CheckPrintfArguments(Expr *Fn,
                            SourceLocation LParenLoc, SourceLocation RParenLoc,
                            bool HasVAListArg, FunctionDecl *FDecl,
                            unsigned format_idx, Expr** Args,
                            unsigned NumArgsInCall);
                            
  void CheckReturnStackAddr(Expr *RetValExp, QualType lhsType,
                            SourceLocation ReturnLoc);

  
  bool CheckBuiltinCFStringArgument(Expr* Arg);
};


}  // end namespace clang

#endif
