//===--- Action.h - Parser Action Interface ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Action and EmptyAction interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_ACTION_H
#define LLVM_CLANG_PARSE_ACTION_H

#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TokenKinds.h"

namespace clang {
  // Semantic.
  class DeclSpec;
  class ObjcDeclSpec;
  class Declarator;
  class AttributeList;
  // Parse.
  class Scope;
  class Action;
  class Selector;
  // Lex.
  class Token;

/// Action - As the parser reads the input file and recognizes the productions
/// of the grammar, it invokes methods on this class to turn the parsed input
/// into something useful: e.g. a parse tree.
///
/// The callback methods that this class provides are phrased as actions that
/// the parser has just done or is about to do when the method is called.  They
/// are not requests that the actions module do the specified action.
///
/// All of the methods here are optional except isTypeName(), which must be
/// specified in order for the parse to complete accurately.  The EmptyAction
/// class does this bare-minimum of tracking to implement this functionality.
class Action {
public:
  /// Out-of-line virtual destructor to provide home for this class.
  virtual ~Action();
  
  // Types - Though these don't actually enforce strong typing, they document
  // what types are required to be identical for the actions.
  typedef void ExprTy;
  typedef void StmtTy;
  typedef void DeclTy;
  typedef void TypeTy;
  typedef void AttrTy;
  
  /// ActionResult - This structure is used while parsing/acting on expressions,
  /// stmts, etc.  It encapsulates both the object returned by the action, plus
  /// a sense of whether or not it is valid.
  template<unsigned UID>
  struct ActionResult {
    void *Val;
    bool isInvalid;
    
    ActionResult(bool Invalid = false) : Val(0), isInvalid(Invalid) {}
    template<typename ActualExprTy>
    ActionResult(ActualExprTy *val) : Val(val), isInvalid(false) {}
    
    const ActionResult &operator=(void *RHS) {
      Val = RHS;
      isInvalid = false;
      return *this;
    }
  };

  /// Expr/Stmt/TypeResult - Provide a unique type to wrap ExprTy/StmtTy/TypeTy,
  /// providing strong typing and allowing for failure.
  typedef ActionResult<0> ExprResult;
  typedef ActionResult<1> StmtResult;
  typedef ActionResult<2> TypeResult;
  
  /// Deletion callbacks - Since the parser doesn't know the concrete types of
  /// the AST nodes being generated, it must do callbacks to delete objects when
  /// recovering from errors.
  virtual void DeleteExpr(ExprTy *E) {}
  virtual void DeleteStmt(StmtTy *E) {}
  
  /// Statistics.
  virtual void PrintStats() const {}
  //===--------------------------------------------------------------------===//
  // Declaration Tracking Callbacks.
  //===--------------------------------------------------------------------===//
  
  /// isTypeName - Return non-null if the specified identifier is a typedef name
  /// in the current scope.
  virtual DeclTy *isTypeName(const IdentifierInfo &II, Scope *S) const = 0;
  
  /// ActOnDeclarator - This callback is invoked when a declarator is parsed and
  /// 'Init' specifies the initializer if any.  This is for things like:
  /// "int X = 4" or "typedef int foo".
  ///
  /// LastInGroup is non-null for cases where one declspec has multiple
  /// declarators on it.  For example in 'int A, B', ActOnDeclarator will be
  /// called with LastInGroup=A when invoked for B.
  virtual DeclTy *ActOnDeclarator(Scope *S, Declarator &D,DeclTy *LastInGroup) {
    return 0;
  }

  virtual DeclTy *ObjcActOnMethodDefinition(Scope *S, DeclTy *D,
					    DeclTy *LastInGroup) {
    return 0;
  }
  /// AddInitializerToDecl - This action is called immediately after 
  /// ParseDeclarator (when an initializer is present). The code is factored 
  /// this way to make sure we are able to handle the following:
  ///   void func() { int xx = xx; }
  /// This allows ActOnDeclarator to register "xx" prior to parsing the
  /// initializer. The declaration above should still result in a warning, 
  /// since the reference to "xx" is uninitialized.
  virtual void AddInitializerToDecl(DeclTy *Dcl, ExprTy *Init) {
    return;
  }
  /// FinalizeDeclaratorGroup - After a sequence of declarators are parsed, this
  /// gives the actions implementation a chance to process the group as a whole.
  virtual DeclTy *FinalizeDeclaratorGroup(Scope *S, DeclTy *Group) {
    return Group;
  }

  /// ActOnStartOfFunctionDef - This is called at the start of a function
  /// definition, instead of calling ActOnDeclarator.  The Declarator includes
  /// information about formal arguments that are part of this function.
  virtual DeclTy *ActOnStartOfFunctionDef(Scope *FnBodyScope, Declarator &D) {
    // Default to ActOnDeclarator.
    return ActOnDeclarator(FnBodyScope, D, 0);
  }

  virtual DeclTy *ObjcActOnStartOfMethodDef(Scope *FnBodyScope, DeclTy *D) {
    // Default to ObjcActOnMethodDefinition.
    return ObjcActOnMethodDefinition(FnBodyScope, D, 0);
  }
  
  /// ActOnFunctionDefBody - This is called when a function body has completed
  /// parsing.  Decl is the DeclTy returned by ParseStartOfFunctionDef.
  virtual DeclTy *ActOnFunctionDefBody(DeclTy *Decl, StmtTy *Body) {
    return Decl;
  }

  virtual void ActOnMethodDefBody(DeclTy *Decl, StmtTy *Body) {
    return;
  }
  
  /// ActOnPopScope - This callback is called immediately before the specified
  /// scope is popped and deleted.
  virtual void ActOnPopScope(SourceLocation Loc, Scope *S) {}

  /// ActOnTranslationUnitScope - This callback is called once, immediately
  /// after creating the translation unit scope (in Parser::Initialize).
  virtual void ActOnTranslationUnitScope(SourceLocation Loc, Scope *S) {}
    
  /// ParsedFreeStandingDeclSpec - This method is invoked when a declspec with
  /// no declarator (e.g. "struct foo;") is parsed.
  virtual DeclTy *ParsedFreeStandingDeclSpec(Scope *S, DeclSpec &DS) {
    return 0;
  }
  
  //===--------------------------------------------------------------------===//
  // Type Parsing Callbacks.
  //===--------------------------------------------------------------------===//
  
  virtual TypeResult ActOnTypeName(Scope *S, Declarator &D) {
    return 0;
  }
  
  virtual TypeResult ActOnParamDeclaratorType(Scope *S, Declarator &D) {
    return 0;
  }
  
  enum TagKind {
    TK_Reference,   // Reference to a tag:  'struct foo *X;'
    TK_Declaration, // Fwd decl of a tag:   'struct foo;'
    TK_Definition   // Definition of a tag: 'struct foo { int X; } Y;'
  };
  virtual DeclTy *ActOnTag(Scope *S, unsigned TagType, TagKind TK,
                           SourceLocation KWLoc, IdentifierInfo *Name,
                           SourceLocation NameLoc, AttributeList *Attr) {
    // TagType is an instance of DeclSpec::TST, indicating what kind of tag this
    // is (struct/union/enum/class).
    return 0;
  }
  
  virtual DeclTy *ActOnField(Scope *S, DeclTy *TagDecl,SourceLocation DeclStart,
                             Declarator &D, ExprTy *BitfieldWidth) {
    return 0;
  }
  virtual void ActOnFields(Scope* S, SourceLocation RecLoc, DeclTy *TagDecl,
                           DeclTy **Fields, unsigned NumFields, 
                           SourceLocation LBrac, SourceLocation RBrac,
                           tok::ObjCKeywordKind *visibility = 0) {}
  virtual DeclTy *ActOnEnumConstant(Scope *S, DeclTy *EnumDecl,
                                    DeclTy *LastEnumConstant,
                                    SourceLocation IdLoc, IdentifierInfo *Id,
                                    SourceLocation EqualLoc, ExprTy *Val) {
    return 0;
  }
  virtual void ActOnEnumBody(SourceLocation EnumLoc, DeclTy *EnumDecl,
                             DeclTy **Elements, unsigned NumElements) {}

  //===--------------------------------------------------------------------===//
  // Statement Parsing Callbacks.
  //===--------------------------------------------------------------------===//
  
  virtual StmtResult ActOnNullStmt(SourceLocation SemiLoc) {
    return 0;
  }
  
  virtual StmtResult ActOnCompoundStmt(SourceLocation L, SourceLocation R,
                                       StmtTy **Elts, unsigned NumElts,
                                       bool isStmtExpr) {
    return 0;
  }
  virtual StmtResult ActOnDeclStmt(DeclTy *Decl) {
    return 0;
  }
  
  virtual StmtResult ActOnExprStmt(ExprTy *Expr) {
    return StmtResult(Expr);
  }
  
  /// ActOnCaseStmt - Note that this handles the GNU 'case 1 ... 4' extension,
  /// which can specify an RHS value.
  virtual StmtResult ActOnCaseStmt(SourceLocation CaseLoc, ExprTy *LHSVal,
                                   SourceLocation DotDotDotLoc, ExprTy *RHSVal,
                                   SourceLocation ColonLoc, StmtTy *SubStmt) {
    return 0;
  }
  virtual StmtResult ActOnDefaultStmt(SourceLocation DefaultLoc,
                                      SourceLocation ColonLoc, StmtTy *SubStmt,
                                      Scope *CurScope){
    return 0;
  }
  
  virtual StmtResult ActOnLabelStmt(SourceLocation IdentLoc, IdentifierInfo *II,
                                    SourceLocation ColonLoc, StmtTy *SubStmt) {
    return 0;
  }
  
  virtual StmtResult ActOnIfStmt(SourceLocation IfLoc, ExprTy *CondVal,
                                 StmtTy *ThenVal, SourceLocation ElseLoc,
                                 StmtTy *ElseVal) {
    return 0; 
  }
  
  virtual StmtResult ActOnStartOfSwitchStmt(ExprTy *Cond) {
    return 0;
  }
  
  virtual StmtResult ActOnFinishSwitchStmt(SourceLocation SwitchLoc, 
                                           StmtTy *Switch, ExprTy *Body) {
    return 0;
  }

  virtual StmtResult ActOnWhileStmt(SourceLocation WhileLoc, ExprTy *Cond,
                                    StmtTy *Body) {
    return 0;
  }
  virtual StmtResult ActOnDoStmt(SourceLocation DoLoc, StmtTy *Body,
                                 SourceLocation WhileLoc, ExprTy *Cond) {
    return 0;
  }
  virtual StmtResult ActOnForStmt(SourceLocation ForLoc, 
                                  SourceLocation LParenLoc, 
                                  StmtTy *First, ExprTy *Second, ExprTy *Third,
                                  SourceLocation RParenLoc, StmtTy *Body) {
    return 0;
  }
  virtual StmtResult ActOnGotoStmt(SourceLocation GotoLoc,
                                   SourceLocation LabelLoc,
                                   IdentifierInfo *LabelII) {
    return 0;
  }
  virtual StmtResult ActOnIndirectGotoStmt(SourceLocation GotoLoc,
                                           SourceLocation StarLoc,
                                           ExprTy *DestExp) {
    return 0;
  }
  virtual StmtResult ActOnContinueStmt(SourceLocation ContinueLoc,
                                       Scope *CurScope) {
    return 0;
  }
  virtual StmtResult ActOnBreakStmt(SourceLocation GotoLoc, Scope *CurScope) {
    return 0;
  }
  virtual StmtResult ActOnReturnStmt(SourceLocation ReturnLoc,
                                     ExprTy *RetValExp) {
    return 0;
  }
  virtual StmtResult ActOnAsmStmt(SourceLocation AsmLoc, 
                                  SourceLocation RParenLoc) {
    return 0;
  }
  
  // Objective-c statements
  virtual StmtResult ActOnObjcAtCatchStmt(SourceLocation AtLoc, 
                                          SourceLocation RParen, StmtTy *Parm, 
                                          StmtTy *Body, StmtTy *CatchList) {
    return 0;
  }
  
  virtual StmtResult ActOnObjcAtFinallyStmt(SourceLocation AtLoc, 
                                            StmtTy *Body) {
    return 0;
  }
  
  virtual StmtResult ActOnObjcAtTryStmt(SourceLocation AtLoc, 
                                        StmtTy *Try, 
                                        StmtTy *Catch, StmtTy *Finally) {
    return 0;
  }
  
  virtual StmtResult ActOnObjcAtThrowStmt(SourceLocation AtLoc, 
                                          StmtTy *Throw) {
    return 0;
  }
  
  //===--------------------------------------------------------------------===//
  // Expression Parsing Callbacks.
  //===--------------------------------------------------------------------===//
  
  // Primary Expressions.
  
  /// ActOnIdentifierExpr - Parse an identifier in expression context.
  /// 'HasTrailingLParen' indicates whether or not the identifier has a '('
  /// token immediately after it.
  virtual ExprResult ActOnIdentifierExpr(Scope *S, SourceLocation Loc,
                                         IdentifierInfo &II,
                                         bool HasTrailingLParen) {
    return 0;
  }
  
  virtual ExprResult ActOnPreDefinedExpr(SourceLocation Loc,
                                         tok::TokenKind Kind) {
    return 0;
  }
  virtual ExprResult ActOnCharacterConstant(const Token &) { return 0; }
  virtual ExprResult ActOnNumericConstant(const Token &) { return 0; }
  
  /// ActOnStringLiteral - The specified tokens were lexed as pasted string
  /// fragments (e.g. "foo" "bar" L"baz").
  virtual ExprResult ActOnStringLiteral(const Token *Toks, unsigned NumToks) {
    return 0;
  }
  
  virtual ExprResult ActOnParenExpr(SourceLocation L, SourceLocation R,
                                    ExprTy *Val) {
    return Val;  // Default impl returns operand.
  }
  
  // Postfix Expressions.
  virtual ExprResult ActOnPostfixUnaryOp(SourceLocation OpLoc, 
                                         tok::TokenKind Kind, ExprTy *Input) {
    return 0;
  }
  virtual ExprResult ActOnArraySubscriptExpr(ExprTy *Base, SourceLocation LLoc,
                                             ExprTy *Idx, SourceLocation RLoc) {
    return 0;
  }
  virtual ExprResult ActOnMemberReferenceExpr(ExprTy *Base,SourceLocation OpLoc,
                                              tok::TokenKind OpKind,
                                              SourceLocation MemberLoc,
                                              IdentifierInfo &Member) {
    return 0;
  }
  
  /// ActOnCallExpr - Handle a call to Fn with the specified array of arguments.
  /// This provides the location of the left/right parens and a list of comma
  /// locations.  There are guaranteed to be one fewer commas than arguments,
  /// unless there are zero arguments.
  virtual ExprResult ActOnCallExpr(ExprTy *Fn, SourceLocation LParenLoc,
                                   ExprTy **Args, unsigned NumArgs,
                                   SourceLocation *CommaLocs,
                                   SourceLocation RParenLoc) {
    return 0;
  }
  
  // Unary Operators.  'Tok' is the token for the operator.
  virtual ExprResult ActOnUnaryOp(SourceLocation OpLoc, tok::TokenKind Op,
                                  ExprTy *Input) {
    return 0;
  }
  virtual ExprResult 
    ActOnSizeOfAlignOfTypeExpr(SourceLocation OpLoc, bool isSizeof, 
                               SourceLocation LParenLoc, TypeTy *Ty,
                               SourceLocation RParenLoc) {
    return 0;
  }
  
  virtual ExprResult ActOnCompoundLiteral(SourceLocation LParen, TypeTy *Ty,
                                          SourceLocation RParen, ExprTy *Op) {
    return 0;
  }
  virtual ExprResult ActOnInitList(SourceLocation LParenLoc,
                                   ExprTy **InitList, unsigned NumInit,
                                   SourceLocation RParenLoc) {
    return 0;
  }
  virtual ExprResult ActOnCastExpr(SourceLocation LParenLoc, TypeTy *Ty,
                                   SourceLocation RParenLoc, ExprTy *Op) {
    return 0;
  }
  
  virtual ExprResult ActOnBinOp(SourceLocation TokLoc, tok::TokenKind Kind,
                                ExprTy *LHS, ExprTy *RHS) {
    return 0;
  }

  /// ActOnConditionalOp - Parse a ?: operation.  Note that 'LHS' may be null
  /// in the case of a the GNU conditional expr extension.
  virtual ExprResult ActOnConditionalOp(SourceLocation QuestionLoc, 
                                        SourceLocation ColonLoc,
                                        ExprTy *Cond, ExprTy *LHS, ExprTy *RHS){
    return 0;
  }
  
  //===---------------------- GNU Extension Expressions -------------------===//

  virtual ExprResult ActOnAddrLabel(SourceLocation OpLoc, SourceLocation LabLoc,
                                    IdentifierInfo *LabelII) { // "&&foo"
    return 0;
  }
  
  virtual ExprResult ActOnStmtExpr(SourceLocation LPLoc, StmtTy *SubStmt,
                                   SourceLocation RPLoc) { // "({..})"
    return 0;
  }
  
  // __builtin_offsetof(type, identifier(.identifier|[expr])*)
  struct OffsetOfComponent {
    SourceLocation LocStart, LocEnd;
    bool isBrackets;  // true if [expr], false if .ident
    union {
      IdentifierInfo *IdentInfo;
      ExprTy *E;
    } U;
  };
  
  virtual ExprResult ActOnBuiltinOffsetOf(SourceLocation BuiltinLoc,
                                          SourceLocation TypeLoc, TypeTy *Arg1,
                                          OffsetOfComponent *CompPtr,
                                          unsigned NumComponents,
                                          SourceLocation RParenLoc) {
    return 0;
  }
  
  // __builtin_types_compatible_p(type1, type2)
  virtual ExprResult ActOnTypesCompatibleExpr(SourceLocation BuiltinLoc, 
                                              TypeTy *arg1, TypeTy *arg2,
                                              SourceLocation RPLoc) {
    return 0;
  }
  // __builtin_choose_expr(constExpr, expr1, expr2)
  virtual ExprResult ActOnChooseExpr(SourceLocation BuiltinLoc, 
                                     ExprTy *cond, ExprTy *expr1, ExprTy *expr2,
                                     SourceLocation RPLoc) {
    return 0;
  }

  // __builtin_va_arg(expr, type)
  virtual ExprResult ActOnVAArg(SourceLocation BuiltinLoc,
                                ExprTy *expr, TypeTy *type,
                                SourceLocation RPLoc) {
    return 0;
  }
  
  //===------------------------- C++ Expressions --------------------------===//
  
  /// ActOnCXXCasts - Parse {dynamic,static,reinterpret,const}_cast's.
  virtual ExprResult ActOnCXXCasts(SourceLocation OpLoc, tok::TokenKind Kind,
                                   SourceLocation LAngleBracketLoc, TypeTy *Ty,
                                   SourceLocation RAngleBracketLoc,
                                   SourceLocation LParenLoc, ExprTy *Op,
                                   SourceLocation RParenLoc) {
    return 0;
  }

  /// ActOnCXXBoolLiteral - Parse {true,false} literals.
  virtual ExprResult ActOnCXXBoolLiteral(SourceLocation OpLoc,
                                         tok::TokenKind Kind) {
    return 0;
  }
  //===----------------------- Obj-C Declarations -------------------------===//
  
  // ActOnStartClassInterface - this action is called immdiately after parsing
  // the prologue for a class interface (before parsing the instance 
  // variables). Instance variables are processed by ActOnFields().
  virtual DeclTy *ActOnStartClassInterface(
    SourceLocation AtInterafceLoc,
    IdentifierInfo *ClassName, 
    SourceLocation ClassLoc,
    IdentifierInfo *SuperName, 
    SourceLocation SuperLoc,
    IdentifierInfo **ProtocolNames, 
    unsigned NumProtocols,
    SourceLocation EndProtoLoc,
    AttributeList *AttrList) {
    return 0;
  }
  
  /// ActOnCompatiblityAlias - this action is called after complete parsing of
  /// @compaatibility_alias declaration. It sets up the alias relationships.
  virtual DeclTy *ActOnCompatiblityAlias(
    SourceLocation AtCompatibilityAliasLoc,
    IdentifierInfo *AliasName,  SourceLocation AliasLocation,
    IdentifierInfo *ClassName, SourceLocation ClassLocation) {
    return 0;
  }
  
  // ActOnStartProtocolInterface - this action is called immdiately after
  // parsing the prologue for a protocol interface.
  virtual DeclTy *ActOnStartProtocolInterface(
    SourceLocation AtProtoInterfaceLoc,
    IdentifierInfo *ProtocolName, 
    SourceLocation ProtocolLoc,
    IdentifierInfo **ProtoRefNames, 
    unsigned NumProtoRefs,
    SourceLocation EndProtoLoc) {
    return 0;
  }
  // ActOnStartCategoryInterface - this action is called immdiately after
  // parsing the prologue for a category interface.
  virtual DeclTy *ActOnStartCategoryInterface(
    SourceLocation AtInterfaceLoc,
    IdentifierInfo *ClassName, 
    SourceLocation ClassLoc,
    IdentifierInfo *CategoryName, 
    SourceLocation CategoryLoc,
    IdentifierInfo **ProtoRefNames, 
    unsigned NumProtoRefs,
    SourceLocation EndProtoLoc) {
    return 0;
  }
  // ActOnStartClassImplementation - this action is called immdiately after
  // parsing the prologue for a class implementation. Instance variables are 
  // processed by ActOnFields().
  virtual DeclTy *ActOnStartClassImplementation(
    SourceLocation AtClassImplLoc,
    IdentifierInfo *ClassName, 
    SourceLocation ClassLoc,
    IdentifierInfo *SuperClassname, 
    SourceLocation SuperClassLoc) {
    return 0;
  }
  // ActOnStartCategoryImplementation - this action is called immdiately after
  // parsing the prologue for a category implementation.
  virtual DeclTy *ActOnStartCategoryImplementation(
    SourceLocation AtCatImplLoc,
    IdentifierInfo *ClassName, 
    SourceLocation ClassLoc,
    IdentifierInfo *CatName,
    SourceLocation CatLoc) {
    return 0;
  }  
  // ActOnMethodDeclaration - called for all method declarations. 
  virtual DeclTy *ActOnMethodDeclaration(
    SourceLocation BeginLoc,   // location of the + or -.
    SourceLocation EndLoc,     // location of the ; or {.
    tok::TokenKind MethodType, // tok::minus for instance, tok::plus for class.
    DeclTy *ClassDecl,         // class this methods belongs to.
    ObjcDeclSpec &ReturnQT,    // for return type's in inout etc.
    TypeTy *ReturnType,        // the method return type.
    Selector Sel,              // a unique name for the method.
    ObjcDeclSpec *ArgQT,       // for arguments' in inout etc.
    TypeTy **ArgTypes,         // non-zero when Sel.getNumArgs() > 0
    IdentifierInfo **ArgNames, // non-zero when Sel.getNumArgs() > 0
    AttributeList *AttrList,   // optional
    // tok::objc_not_keyword, tok::objc_optional, tok::objc_required    
    tok::ObjCKeywordKind impKind) {
    return 0;
  }
  // ActOnAddMethodsToObjcDecl - called to associate methods with an interface,
  // protocol, category, or implementation. 
  virtual void ActOnAddMethodsToObjcDecl(
    Scope* S, 
    DeclTy *ClassDecl,
    DeclTy **allMethods, 
    unsigned allNum,
    DeclTy **allProperties,
    unsigned NumProperties,
    SourceLocation AtEndLoc) {
    return;
  }
  
  // ActOnAddObjcProperties - called to build one property AST
  virtual DeclTy *ActOnAddObjcProperties (SourceLocation AtLoc,
    DeclTy **allProperties, unsigned NumProperties, ObjcDeclSpec &DS) {
    return 0;
  }
                                     
  // ActOnClassMessage - used for both unary and keyword messages.
  // ArgExprs is optional - if it is present, the number of expressions
  // is obtained from Sel.getNumArgs().
  virtual ExprResult ActOnClassMessage(
    IdentifierInfo *receivingClassName, 
    Selector Sel,
    SourceLocation lbrac, 
    SourceLocation rbrac, 
    ExprTy **ArgExprs) {
    return 0;
  }
  // ActOnInstanceMessage - used for both unary and keyword messages.
  // ArgExprs is optional - if it is present, the number of expressions
  // is obtained from Sel.getNumArgs().
  virtual ExprResult ActOnInstanceMessage(
    ExprTy *receiver, Selector Sel,
    SourceLocation lbrac, SourceLocation rbrac, ExprTy **ArgExprs) {
    return 0;
  }
  virtual DeclTy *ActOnForwardClassDeclaration(
    SourceLocation AtClassLoc,
    IdentifierInfo **IdentList,
    unsigned NumElts) {
    return 0;
  }
  virtual DeclTy *ActOnForwardProtocolDeclaration(
    SourceLocation AtProtocolLoc,
    IdentifierInfo **IdentList,
    unsigned NumElts) {
    return 0;
  }
  
  /// FindProtocolDeclaration - This routine looks up protocols and
  /// issues error if they are not declared. It returns list of valid
  /// protocols found.
  virtual void FindProtocolDeclaration(SourceLocation TypeLoc,
                                       IdentifierInfo **ProtocolId,
                                       unsigned NumProtocols,
                                       llvm::SmallVector<DeclTy *, 8> &
                                       Protocols) {
  }
                                               
                                               
  //===----------------------- Obj-C Expressions --------------------------===//
  virtual ExprResult ParseObjCStringLiteral(SourceLocation AtLoc, 
                                            ExprTy *string) {
    return 0;
  }

  virtual ExprResult ParseObjCEncodeExpression(SourceLocation AtLoc,
                                               SourceLocation EncLoc,
                                               SourceLocation LParenLoc,
                                               TypeTy *Ty,
                                               SourceLocation RParenLoc) {
    return 0;
  }
  
  virtual ExprResult ParseObjCSelectorExpression(Selector Sel,
                                                 SourceLocation AtLoc,
                                                 SourceLocation SelLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation RParenLoc) {
    return 0;
  }
  
  virtual ExprResult ParseObjCProtocolExpression(IdentifierInfo *ProtocolId,
                                                 SourceLocation AtLoc,
                                                 SourceLocation ProtoLoc,
                                                 SourceLocation LParenLoc,
                                                 SourceLocation RParenLoc) {
    return 0;
  } 
};

/// MinimalAction - Minimal actions are used by light-weight clients of the
/// parser that do not need name resolution or significant semantic analysis to
/// be performed.  The actions implemented here are in the form of unresolved
/// identifiers.  By using a simpler interface than the SemanticAction class,
/// the parser doesn't have to build complex data structures and thus runs more
/// quickly.
class MinimalAction : public Action {
  /// Translation Unit Scope - useful to Objective-C actions that need
  /// to lookup file scope declarations in the "ordinary" C decl namespace.
  /// For example, user-defined classes, built-in "id" type, etc.
  Scope *TUScope;
  IdentifierTable &Idents;
public:
  MinimalAction(IdentifierTable &IT) : Idents(IT) {}
  
  /// isTypeName - This looks at the IdentifierInfo::FETokenInfo field to
  /// determine whether the name is a typedef or not in this scope.
  virtual DeclTy *isTypeName(const IdentifierInfo &II, Scope *S) const;
  
  /// ActOnDeclarator - If this is a typedef declarator, we modify the
  /// IdentifierInfo::FETokenInfo field to keep track of this fact, until S is
  /// popped.
  virtual DeclTy *ActOnDeclarator(Scope *S, Declarator &D, DeclTy *LastInGroup);
  
  /// ActOnPopScope - When a scope is popped, if any typedefs are now 
  /// out-of-scope, they are removed from the IdentifierInfo::FETokenInfo field.
  virtual void ActOnPopScope(SourceLocation Loc, Scope *S);
  virtual void ActOnTranslationUnitScope(SourceLocation Loc, Scope *S);
  
  virtual DeclTy *ActOnForwardClassDeclaration(SourceLocation AtClassLoc,
                                               IdentifierInfo **IdentList,
                                               unsigned NumElts);
  
  virtual DeclTy *ActOnStartClassInterface(SourceLocation interLoc,
                    IdentifierInfo *ClassName, SourceLocation ClassLoc,
                    IdentifierInfo *SuperName, SourceLocation SuperLoc,
                    IdentifierInfo **ProtocolNames, unsigned NumProtocols,
                    SourceLocation EndProtoLoc, AttributeList *AttrList);
};

}  // end namespace clang

#endif
