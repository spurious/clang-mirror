//===--- RewriteTest.cpp - Playground for the code rewriter ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Hacks and fun related to the code rewriter.
//
//===----------------------------------------------------------------------===//

#include "ASTConsumers.h"
#include "clang/Rewrite/Rewriter.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/MemoryBuffer.h"
#include <sstream>
using namespace clang;
using llvm::utostr;

namespace {
  class RewriteTest : public ASTConsumer {
    Rewriter Rewrite;
    Diagnostic &Diags;
    ASTContext *Context;
    SourceManager *SM;
    unsigned MainFileID;
    const char *MainFileStart, *MainFileEnd;
    SourceLocation LastIncLoc;
    llvm::SmallVector<ObjCImplementationDecl *, 8> ClassImplementation;
    llvm::SmallVector<ObjCCategoryImplDecl *, 8> CategoryImplementation;
    llvm::SmallPtrSet<ObjCInterfaceDecl*, 8> ObjCSynthesizedStructs;
    llvm::SmallPtrSet<ObjCInterfaceDecl*, 8> ObjCForwardDecls;
    llvm::DenseMap<ObjCMethodDecl*, std::string> MethodInternalNames;
    
    FunctionDecl *MsgSendFunctionDecl;
    FunctionDecl *MsgSendSuperFunctionDecl;
    FunctionDecl *MsgSendStretFunctionDecl;
    FunctionDecl *MsgSendSuperStretFunctionDecl;
    FunctionDecl *MsgSendFpretFunctionDecl;
    FunctionDecl *GetClassFunctionDecl;
    FunctionDecl *GetMetaClassFunctionDecl;
    FunctionDecl *SelGetUidFunctionDecl;
    FunctionDecl *CFStringFunctionDecl;
    FunctionDecl *GetProtocolFunctionDecl;
      
    // ObjC string constant support.
    FileVarDecl *ConstantStringClassReference;
    RecordDecl *NSStringRecord;
    
    // Needed for super.
    ObjCMethodDecl *CurMethodDecl;
    RecordDecl *SuperStructDecl;
    
    static const int OBJC_ABI_VERSION =7 ;
  public:
    void Initialize(ASTContext &context) {
      Context = &context;
      SM = &Context->getSourceManager();
      MsgSendFunctionDecl = 0;
      MsgSendSuperFunctionDecl = 0;
      MsgSendStretFunctionDecl = 0;
      MsgSendSuperStretFunctionDecl = 0;
      MsgSendFpretFunctionDecl = 0;
      GetClassFunctionDecl = 0;
      GetMetaClassFunctionDecl = 0;
      SelGetUidFunctionDecl = 0;
      CFStringFunctionDecl = 0;
      GetProtocolFunctionDecl = 0;
      ConstantStringClassReference = 0;
      NSStringRecord = 0;
      CurMethodDecl = 0;
      SuperStructDecl = 0;
      
      // Get the ID and start/end of the main file.
      MainFileID = SM->getMainFileID();
      const llvm::MemoryBuffer *MainBuf = SM->getBuffer(MainFileID);
      MainFileStart = MainBuf->getBufferStart();
      MainFileEnd = MainBuf->getBufferEnd();
      
      
      Rewrite.setSourceMgr(Context->getSourceManager());
      // declaring objc_selector outside the parameter list removes a silly
      // scope related warning...
      const char *s = "struct objc_selector; struct objc_class;\n"
                      "#ifndef OBJC_SUPER\n"
                      "struct objc_super { struct objc_object *o; "
                      "struct objc_object *superClass; };\n"
                      "#define OBJC_SUPER\n"
                      "#endif\n"
                      "#ifndef _REWRITER_typedef_Protocol\n"
                      "typedef struct objc_object Protocol;\n"
                      "#define _REWRITER_typedef_Protocol\n"
                      "#endif\n"
                      "extern struct objc_object *objc_msgSend"
                      "(struct objc_object *, struct objc_selector *, ...);\n"
                      "extern struct objc_object *objc_msgSendSuper"
                      "(struct objc_super *, struct objc_selector *, ...);\n"
                      "extern struct objc_object *objc_msgSend_stret"
                      "(struct objc_object *, struct objc_selector *, ...);\n"
                      "extern struct objc_object *objc_msgSendSuper_stret"
                      "(struct objc_super *, struct objc_selector *, ...);\n"
                      "extern struct objc_object *objc_msgSend_fpret"
                      "(struct objc_object *, struct objc_selector *, ...);\n"
                      "extern struct objc_object *objc_getClass"
                      "(const char *);\n"
                      "extern struct objc_object *objc_getMetaClass"
                      "(const char *);\n"
                      "extern void objc_exception_throw(struct objc_object *);\n"
                      "extern void objc_exception_try_enter(void *);\n"
                      "extern void objc_exception_try_exit(void *);\n"
                      "extern struct objc_object *objc_exception_extract(void *);\n"
                      "extern int objc_exception_match"
                      "(struct objc_class *, struct objc_object *, ...);\n"
                      "extern Protocol *objc_getProtocol(const char *);\n"
                      "#include <objc/objc.h>\n"
                      "#ifndef __FASTENUMERATIONSTATE\n"
                      "struct __objcFastEnumerationState {\n\t"
                      "unsigned long state;\n\t"
                      "id *itemsPtr;\n\t"
                      "unsigned long *mutationsPtr;\n\t"
                      "unsigned long extra[5];\n};\n"
                      "#define __FASTENUMERATIONSTATE\n"
                      "#endif\n";
                      
      Rewrite.InsertText(SourceLocation::getFileLoc(MainFileID, 0), 
                         s, strlen(s));
    }

    // Top Level Driver code.
    virtual void HandleTopLevelDecl(Decl *D);
    void HandleDeclInMainFile(Decl *D);
    RewriteTest(Diagnostic &D) : Diags(D) {}
    ~RewriteTest();

    // Syntactic Rewriting.
    void RewritePrologue(SourceLocation Loc);
    void RewriteInclude(SourceLocation Loc);
    void RewriteTabs();
    void RewriteForwardClassDecl(ObjCClassDecl *Dcl);
    void RewriteInterfaceDecl(ObjCInterfaceDecl *Dcl);
    void RewriteImplementationDecl(NamedDecl *Dcl);
    void RewriteObjCMethodDecl(ObjCMethodDecl *MDecl, std::string &ResultStr);
    void RewriteCategoryDecl(ObjCCategoryDecl *Dcl);
    void RewriteProtocolDecl(ObjCProtocolDecl *Dcl);
    void RewriteForwardProtocolDecl(ObjCForwardProtocolDecl *Dcl);
    void RewriteMethodDeclaration(ObjCMethodDecl *Method);
    void RewriteProperties(int nProperties, ObjCPropertyDecl **Properties);
    void RewriteFunctionDecl(FunctionDecl *FD);
    void RewriteObjCQualifiedInterfaceTypes(Decl *Dcl);
    bool needToScanForQualifiers(QualType T);
    ObjCInterfaceDecl *isSuperReceiver(Expr *recExpr);
    QualType getSuperStructType();
    
    // Expression Rewriting.
    Stmt *RewriteFunctionBodyOrGlobalInitializer(Stmt *S);
    Stmt *RewriteAtEncode(ObjCEncodeExpr *Exp);
    Stmt *RewriteObjCIvarRefExpr(ObjCIvarRefExpr *IV);
    Stmt *RewriteAtSelector(ObjCSelectorExpr *Exp);
    Stmt *RewriteMessageExpr(ObjCMessageExpr *Exp);
    Stmt *RewriteObjCStringLiteral(ObjCStringLiteral *Exp);
    Stmt *RewriteObjCProtocolExpr(ObjCProtocolExpr *Exp);
    Stmt *RewriteObjCTryStmt(ObjCAtTryStmt *S);
    Stmt *RewriteObjCCatchStmt(ObjCAtCatchStmt *S);
    Stmt *RewriteObjCFinallyStmt(ObjCAtFinallyStmt *S);
    Stmt *RewriteObjCThrowStmt(ObjCAtThrowStmt *S);
    Stmt *RewriteObjCForCollectionStmt(ObjCForCollectionStmt *S);
    CallExpr *SynthesizeCallToFunctionDecl(FunctionDecl *FD, 
                                           Expr **args, unsigned nargs);
    Stmt *SynthMessageExpr(ObjCMessageExpr *Exp);
    void SynthCountByEnumWithState(std::string &buf);
    
    void SynthMsgSendFunctionDecl();
    void SynthMsgSendSuperFunctionDecl();
    void SynthMsgSendStretFunctionDecl();
    void SynthMsgSendFpretFunctionDecl();
    void SynthMsgSendSuperStretFunctionDecl();
    void SynthGetClassFunctionDecl();
    void SynthGetMetaClassFunctionDecl();
    void SynthCFStringFunctionDecl();
    void SynthSelGetUidFunctionDecl();
    void SynthGetProtocolFunctionDecl();
      
    // Metadata emission.
    void RewriteObjCClassMetaData(ObjCImplementationDecl *IDecl,
                                  std::string &Result);
    
    void RewriteObjCCategoryImplDecl(ObjCCategoryImplDecl *CDecl,
                                     std::string &Result);
    
    typedef ObjCCategoryImplDecl::instmeth_iterator instmeth_iterator;
    void RewriteObjCMethodsMetaData(instmeth_iterator MethodBegin,
                                    instmeth_iterator MethodEnd,
                                    bool IsInstanceMethod,
                                    const char *prefix,
                                    const char *ClassName,
                                    std::string &Result);
    
    void RewriteObjCProtocolsMetaData(ObjCProtocolDecl **Protocols,
                                      int NumProtocols,
                                      const char *prefix,
                                      const char *ClassName,
                                      std::string &Result);
    void SynthesizeObjCInternalStruct(ObjCInterfaceDecl *CDecl,
                                      std::string &Result);
    void SynthesizeIvarOffsetComputation(ObjCImplementationDecl *IDecl, 
                                         ObjCIvarDecl *ivar, 
                                         std::string &Result);
    void RewriteImplementations(std::string &Result);
  };
}

ASTConsumer *clang::CreateCodeRewriterTest(Diagnostic &Diags) {
  return new RewriteTest(Diags);
}

//===----------------------------------------------------------------------===//
// Top Level Driver Code
//===----------------------------------------------------------------------===//

void RewriteTest::HandleTopLevelDecl(Decl *D) {
  // Two cases: either the decl could be in the main file, or it could be in a
  // #included file.  If the former, rewrite it now.  If the later, check to see
  // if we rewrote the #include/#import.
  SourceLocation Loc = D->getLocation();
  Loc = SM->getLogicalLoc(Loc);
  
  // If this is for a builtin, ignore it.
  if (Loc.isInvalid()) return;

  // Look for built-in declarations that we need to refer during the rewrite.
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    RewriteFunctionDecl(FD);
  } else if (FileVarDecl *FVD = dyn_cast<FileVarDecl>(D)) {
    // declared in <Foundation/NSString.h>
    if (strcmp(FVD->getName(), "_NSConstantStringClassReference") == 0) {
      ConstantStringClassReference = FVD;
      return;
    }
  } else if (ObjCInterfaceDecl *MD = dyn_cast<ObjCInterfaceDecl>(D)) {
    RewriteInterfaceDecl(MD);
  } else if (ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(D)) {
    RewriteCategoryDecl(CD);
  } else if (ObjCProtocolDecl *PD = dyn_cast<ObjCProtocolDecl>(D)) {
    RewriteProtocolDecl(PD);
  } else if (ObjCForwardProtocolDecl *FP = 
             dyn_cast<ObjCForwardProtocolDecl>(D)){
    RewriteForwardProtocolDecl(FP);
  }
  // If we have a decl in the main file, see if we should rewrite it.
  if (SM->getDecomposedFileLoc(Loc).first == MainFileID)
    return HandleDeclInMainFile(D);

  // Otherwise, see if there is a #import in the main file that should be
  // rewritten.
  //RewriteInclude(Loc);
}

/// HandleDeclInMainFile - This is called for each top-level decl defined in the
/// main file of the input.
void RewriteTest::HandleDeclInMainFile(Decl *D) {
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    if (Stmt *Body = FD->getBody())
      FD->setBody(RewriteFunctionBodyOrGlobalInitializer(Body));
	  
  if (ObjCMethodDecl *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (Stmt *Body = MD->getBody()) {
      //Body->dump();
      CurMethodDecl = MD;
      MD->setBody(RewriteFunctionBodyOrGlobalInitializer(Body));
      CurMethodDecl = 0;
    }
  }
  if (ObjCImplementationDecl *CI = dyn_cast<ObjCImplementationDecl>(D))
    ClassImplementation.push_back(CI);
  else if (ObjCCategoryImplDecl *CI = dyn_cast<ObjCCategoryImplDecl>(D))
    CategoryImplementation.push_back(CI);
  else if (ObjCClassDecl *CD = dyn_cast<ObjCClassDecl>(D))
    RewriteForwardClassDecl(CD);
  else if (VarDecl *VD = dyn_cast<VarDecl>(D)) {
    RewriteObjCQualifiedInterfaceTypes(VD);
    if (VD->getInit())
      RewriteFunctionBodyOrGlobalInitializer(VD->getInit());
  }
  // Nothing yet.
}

RewriteTest::~RewriteTest() {
  // Get the top-level buffer that this corresponds to.
  
  // Rewrite tabs if we care.
  //RewriteTabs();
  
  // Rewrite Objective-c meta data*
  std::string ResultStr;
  RewriteImplementations(ResultStr);
  
  // Get the buffer corresponding to MainFileID.  If we haven't changed it, then
  // we are done.
  if (const RewriteBuffer *RewriteBuf = 
      Rewrite.getRewriteBufferFor(MainFileID)) {
    //printf("Changed:\n");
    std::string S(RewriteBuf->begin(), RewriteBuf->end());
    printf("%s\n", S.c_str());
  } else {
    printf("No changes\n");
  }
  // Emit metadata.
  printf("%s", ResultStr.c_str());
}

//===----------------------------------------------------------------------===//
// Syntactic (non-AST) Rewriting Code
//===----------------------------------------------------------------------===//

void RewriteTest::RewriteInclude(SourceLocation Loc) {
  // Rip up the #include stack to the main file.
  SourceLocation IncLoc = Loc, NextLoc = Loc;
  do {
    IncLoc = Loc;
    Loc = SM->getLogicalLoc(NextLoc);
    NextLoc = SM->getIncludeLoc(Loc);
  } while (!NextLoc.isInvalid());

  // Loc is now the location of the #include filename "foo" or <foo/bar.h>.
  // IncLoc indicates the header that was included if it is useful.
  IncLoc = SM->getLogicalLoc(IncLoc);
  if (SM->getDecomposedFileLoc(Loc).first != MainFileID ||
      Loc == LastIncLoc)
    return;
  LastIncLoc = Loc;
  
  unsigned IncCol = SM->getColumnNumber(Loc);
  SourceLocation LineStartLoc = Loc.getFileLocWithOffset(-IncCol+1);

  // Replace the #import with #include.
  Rewrite.ReplaceText(LineStartLoc, IncCol-1, "#include ", strlen("#include "));
}

void RewriteTest::RewriteTabs() {
  std::pair<const char*, const char*> MainBuf = SM->getBufferData(MainFileID);
  const char *MainBufStart = MainBuf.first;
  const char *MainBufEnd = MainBuf.second;
  
  // Loop over the whole file, looking for tabs.
  for (const char *BufPtr = MainBufStart; BufPtr != MainBufEnd; ++BufPtr) {
    if (*BufPtr != '\t')
      continue;
    
    // Okay, we found a tab.  This tab will turn into at least one character,
    // but it depends on which 'virtual column' it is in.  Compute that now.
    unsigned VCol = 0;
    while (BufPtr-VCol != MainBufStart && BufPtr[-VCol-1] != '\t' &&
           BufPtr[-VCol-1] != '\n' && BufPtr[-VCol-1] != '\r')
      ++VCol;
    
    // Okay, now that we know the virtual column, we know how many spaces to
    // insert.  We assume 8-character tab-stops.
    unsigned Spaces = 8-(VCol & 7);
    
    // Get the location of the tab.
    SourceLocation TabLoc =
      SourceLocation::getFileLoc(MainFileID, BufPtr-MainBufStart);
    
    // Rewrite the single tab character into a sequence of spaces.
    Rewrite.ReplaceText(TabLoc, 1, "        ", Spaces);
  }
}


void RewriteTest::RewriteForwardClassDecl(ObjCClassDecl *ClassDecl) {
  int numDecls = ClassDecl->getNumForwardDecls();
  ObjCInterfaceDecl **ForwardDecls = ClassDecl->getForwardDecls();
  
  // Get the start location and compute the semi location.
  SourceLocation startLoc = ClassDecl->getLocation();
  const char *startBuf = SM->getCharacterData(startLoc);
  const char *semiPtr = strchr(startBuf, ';');
  
  // Translate to typedef's that forward reference structs with the same name
  // as the class. As a convenience, we include the original declaration
  // as a comment.
  std::string typedefString;
  typedefString += "// ";
  typedefString.append(startBuf, semiPtr-startBuf+1);
  typedefString += "\n";
  for (int i = 0; i < numDecls; i++) {
    ObjCInterfaceDecl *ForwardDecl = ForwardDecls[i];
    typedefString += "#ifndef _REWRITER_typedef_";
    typedefString += ForwardDecl->getName();
    typedefString += "\n";
    typedefString += "#define _REWRITER_typedef_";
    typedefString += ForwardDecl->getName();
    typedefString += "\n";
    typedefString += "typedef struct objc_object ";
    typedefString += ForwardDecl->getName();
    typedefString += ";\n#endif\n";
  }
  
  // Replace the @class with typedefs corresponding to the classes.
  Rewrite.ReplaceText(startLoc, semiPtr-startBuf+1, 
                      typedefString.c_str(), typedefString.size());
}

void RewriteTest::RewriteMethodDeclaration(ObjCMethodDecl *Method) {
  SourceLocation LocStart = Method->getLocStart();
  SourceLocation LocEnd = Method->getLocEnd();
    
  if (SM->getLineNumber(LocEnd) > SM->getLineNumber(LocStart)) {
	Rewrite.InsertText(LocStart, "/* ", 3);
	Rewrite.ReplaceText(LocEnd, 1, ";*/ ", 4);
  } else {
	Rewrite.InsertText(LocStart, "// ", 3);
  }
}

void RewriteTest::RewriteProperties(int nProperties, ObjCPropertyDecl **Properties) 
{
  for (int i = 0; i < nProperties; i++) {
    ObjCPropertyDecl *Property = Properties[i];
    SourceLocation Loc = Property->getLocation();
    
    Rewrite.ReplaceText(Loc, 0, "// ", 3);
    
    // FIXME: handle properties that are declared across multiple lines.
  }
}

void RewriteTest::RewriteCategoryDecl(ObjCCategoryDecl *CatDecl) {
  SourceLocation LocStart = CatDecl->getLocStart();
  
  // FIXME: handle category headers that are declared across multiple lines.
  Rewrite.ReplaceText(LocStart, 0, "// ", 3);
  
  for (ObjCCategoryDecl::instmeth_iterator I = CatDecl->instmeth_begin(), 
       E = CatDecl->instmeth_end(); I != E; ++I)
    RewriteMethodDeclaration(*I);
  for (ObjCCategoryDecl::classmeth_iterator I = CatDecl->classmeth_begin(), 
       E = CatDecl->classmeth_end(); I != E; ++I)
    RewriteMethodDeclaration(*I);

  // Lastly, comment out the @end.
  Rewrite.ReplaceText(CatDecl->getAtEndLoc(), 0, "// ", 3);
}

void RewriteTest::RewriteProtocolDecl(ObjCProtocolDecl *PDecl) {
  std::pair<const char*, const char*> MainBuf = SM->getBufferData(MainFileID);
  
  SourceLocation LocStart = PDecl->getLocStart();
  
  // FIXME: handle protocol headers that are declared across multiple lines.
  Rewrite.ReplaceText(LocStart, 0, "// ", 3);
  
  for (ObjCProtocolDecl::instmeth_iterator I = PDecl->instmeth_begin(), 
       E = PDecl->instmeth_end(); I != E; ++I)
    RewriteMethodDeclaration(*I);
  for (ObjCProtocolDecl::classmeth_iterator I = PDecl->classmeth_begin(), 
       E = PDecl->classmeth_end(); I != E; ++I)
    RewriteMethodDeclaration(*I);

  // Lastly, comment out the @end.
  SourceLocation LocEnd = PDecl->getAtEndLoc();
  Rewrite.ReplaceText(LocEnd, 0, "// ", 3);

  // Must comment out @optional/@required
  const char *startBuf = SM->getCharacterData(LocStart);
  const char *endBuf = SM->getCharacterData(LocEnd);
  for (const char *p = startBuf; p < endBuf; p++) {
    if (*p == '@' && !strncmp(p+1, "optional", strlen("optional"))) {
      std::string CommentedOptional = "/* @optional */";
      SourceLocation OptionalLoc = LocStart.getFileLocWithOffset(p-startBuf);
      Rewrite.ReplaceText(OptionalLoc, strlen("@optional"),
                          CommentedOptional.c_str(), CommentedOptional.size());
      
    }
    else if (*p == '@' && !strncmp(p+1, "required", strlen("required"))) {
      std::string CommentedRequired = "/* @required */";
      SourceLocation OptionalLoc = LocStart.getFileLocWithOffset(p-startBuf);
      Rewrite.ReplaceText(OptionalLoc, strlen("@required"),
                          CommentedRequired.c_str(), CommentedRequired.size());
      
    }
  }
}

void RewriteTest::RewriteForwardProtocolDecl(ObjCForwardProtocolDecl *PDecl) {
  SourceLocation LocStart = PDecl->getLocation();
  if (LocStart.isInvalid())
    assert(false && "Invalid SourceLocation");
  // FIXME: handle forward protocol that are declared across multiple lines.
  Rewrite.ReplaceText(LocStart, 0, "// ", 3);
}

void RewriteTest::RewriteObjCMethodDecl(ObjCMethodDecl *OMD, 
                                        std::string &ResultStr) {
  ResultStr += "\nstatic ";
  if (OMD->getResultType()->isObjCQualifiedIdType())
    ResultStr += "id";
  else
    ResultStr += OMD->getResultType().getAsString();
  ResultStr += " ";
  
  // Unique method name
  std::string NameStr;
  
  if (OMD->isInstance())
    NameStr += "_I_";
  else
    NameStr += "_C_";
  
  NameStr += OMD->getClassInterface()->getName();
  NameStr += "_";
  
  NamedDecl *MethodContext = OMD->getMethodContext();
  if (ObjCCategoryImplDecl *CID = 
      dyn_cast<ObjCCategoryImplDecl>(MethodContext)) {
    NameStr += CID->getName();
    NameStr += "_";
  }
  // Append selector names, replacing ':' with '_'
  const char *selName = OMD->getSelector().getName().c_str();
  if (!strchr(selName, ':'))
    NameStr +=  OMD->getSelector().getName();
  else {
    std::string selString = OMD->getSelector().getName();
    int len = selString.size();
    for (int i = 0; i < len; i++)
      if (selString[i] == ':')
        selString[i] = '_';
    NameStr += selString;
  }
  // Remember this name for metadata emission
  MethodInternalNames[OMD] = NameStr;
  ResultStr += NameStr;
  
  // Rewrite arguments
  ResultStr += "(";
  
  // invisible arguments
  if (OMD->isInstance()) {
    QualType selfTy = Context->getObjCInterfaceType(OMD->getClassInterface());
    selfTy = Context->getPointerType(selfTy);
    if (ObjCSynthesizedStructs.count(OMD->getClassInterface()))
      ResultStr += "struct ";
    ResultStr += selfTy.getAsString();
  }
  else
    ResultStr += Context->getObjCIdType().getAsString();
  
  ResultStr += " self, ";
  ResultStr += Context->getObjCSelType().getAsString();
  ResultStr += " _cmd";
  
  // Method arguments.
  for (int i = 0; i < OMD->getNumParams(); i++) {
    ParmVarDecl *PDecl = OMD->getParamDecl(i);
    ResultStr += ", ";
    if (PDecl->getType()->isObjCQualifiedIdType())
      ResultStr += "id";
    else
      ResultStr += PDecl->getType().getAsString();
    ResultStr += " ";
    ResultStr += PDecl->getName();
  }
  ResultStr += ") ";
  
}
void RewriteTest::RewriteImplementationDecl(NamedDecl *OID) {
  ObjCImplementationDecl *IMD = dyn_cast<ObjCImplementationDecl>(OID);
  ObjCCategoryImplDecl *CID = dyn_cast<ObjCCategoryImplDecl>(OID);
  
  if (IMD)
    Rewrite.InsertText(IMD->getLocStart(), "// ", 3);
  else
    Rewrite.InsertText(CID->getLocStart(), "// ", 3);
  
  for (ObjCCategoryImplDecl::instmeth_iterator
       I = IMD ? IMD->instmeth_begin() : CID->instmeth_begin(),
       E = IMD ? IMD->instmeth_end() : CID->instmeth_end(); I != E; ++I) {
    std::string ResultStr;
    ObjCMethodDecl *OMD = *I;
    RewriteObjCMethodDecl(OMD, ResultStr);
    SourceLocation LocStart = OMD->getLocStart();
    SourceLocation LocEnd = OMD->getBody()->getLocStart();
    
    const char *startBuf = SM->getCharacterData(LocStart);
    const char *endBuf = SM->getCharacterData(LocEnd);
    Rewrite.ReplaceText(LocStart, endBuf-startBuf,
                        ResultStr.c_str(), ResultStr.size());
  }
  
  for (ObjCCategoryImplDecl::classmeth_iterator
       I = IMD ? IMD->classmeth_begin() : CID->classmeth_begin(),
       E = IMD ? IMD->classmeth_end() : CID->classmeth_end(); I != E; ++I) {
    std::string ResultStr;
    ObjCMethodDecl *OMD = *I;
    RewriteObjCMethodDecl(OMD, ResultStr);
    SourceLocation LocStart = OMD->getLocStart();
    SourceLocation LocEnd = OMD->getBody()->getLocStart();
    
    const char *startBuf = SM->getCharacterData(LocStart);
    const char *endBuf = SM->getCharacterData(LocEnd);
    Rewrite.ReplaceText(LocStart, endBuf-startBuf,
                        ResultStr.c_str(), ResultStr.size());    
  }
  if (IMD)
    Rewrite.InsertText(IMD->getLocEnd(), "// ", 3);
  else
   Rewrite.InsertText(CID->getLocEnd(), "// ", 3); 
}

void RewriteTest::RewriteInterfaceDecl(ObjCInterfaceDecl *ClassDecl) {
  std::string ResultStr;
  if (!ObjCForwardDecls.count(ClassDecl)) {
    // we haven't seen a forward decl - generate a typedef.
    ResultStr = "#ifndef _REWRITER_typedef_";
    ResultStr += ClassDecl->getName();
    ResultStr += "\n";
    ResultStr += "#define _REWRITER_typedef_";
    ResultStr += ClassDecl->getName();
    ResultStr += "\n";
    ResultStr += "typedef struct ";
    ResultStr += ClassDecl->getName();
    ResultStr += " ";
    ResultStr += ClassDecl->getName();
    ResultStr += ";\n#endif\n";
    
    // Mark this typedef as having been generated.
    ObjCForwardDecls.insert(ClassDecl);
  }
  SynthesizeObjCInternalStruct(ClassDecl, ResultStr);
    
  RewriteProperties(ClassDecl->getNumPropertyDecl(),
                    ClassDecl->getPropertyDecl());
  for (ObjCInterfaceDecl::instmeth_iterator I = ClassDecl->instmeth_begin(), 
       E = ClassDecl->instmeth_end(); I != E; ++I)
    RewriteMethodDeclaration(*I);
  for (ObjCInterfaceDecl::classmeth_iterator I = ClassDecl->classmeth_begin(), 
       E = ClassDecl->classmeth_end(); I != E; ++I)
    RewriteMethodDeclaration(*I);

  // Lastly, comment out the @end.
  Rewrite.ReplaceText(ClassDecl->getAtEndLoc(), 0, "// ", 3);
}

Stmt *RewriteTest::RewriteObjCIvarRefExpr(ObjCIvarRefExpr *IV) {
  ObjCIvarDecl *D = IV->getDecl();
  if (IV->isFreeIvar()) {
    Expr *Replacement = new MemberExpr(IV->getBase(), true, D, 
                                       IV->getLocation());
    if (Rewrite.ReplaceStmt(IV, Replacement)) {
      // replacement failed.
      unsigned DiagID = Diags.getCustomDiagID(Diagnostic::Warning, 
                     "rewriting sub-expression within a macro (may not be correct)");
      SourceRange Range = IV->getSourceRange();
      Diags.Report(Context->getFullLoc(IV->getLocation()), DiagID, 0, 0, &Range, 1);
    }
    delete IV;
    return Replacement;
  } else {
    if (CurMethodDecl) {
      if (const PointerType *pType = IV->getBase()->getType()->getAsPointerType()) {
        ObjCInterfaceType *intT = dyn_cast<ObjCInterfaceType>(pType->getPointeeType());
        if (CurMethodDecl->getClassInterface() == intT->getDecl()) {
          IdentifierInfo *II = intT->getDecl()->getIdentifier();
          RecordDecl *RD = new RecordDecl(Decl::Struct, SourceLocation(),
                                          II, 0);
          QualType castT = Context->getPointerType(Context->getTagDeclType(RD));
          
          CastExpr *castExpr = new CastExpr(castT, IV->getBase(), SourceLocation());
          // Don't forget the parens to enforce the proper binding.
          ParenExpr *PE = new ParenExpr(SourceLocation(), SourceLocation(), castExpr);
          if (Rewrite.ReplaceStmt(IV->getBase(), PE)) {
            // replacement failed.
            unsigned DiagID = Diags.getCustomDiagID(Diagnostic::Warning, 
                           "rewriting sub-expression within a macro (may not be correct)");
            SourceRange Range = IV->getBase()->getSourceRange();
            Diags.Report(Context->getFullLoc(IV->getBase()->getLocStart()), DiagID, 0, 0, &Range, 1);
          }
          delete IV->getBase();
          return PE;
        }
      }
    }
    return IV;
  }
}

//===----------------------------------------------------------------------===//
// Function Body / Expression rewriting
//===----------------------------------------------------------------------===//

Stmt *RewriteTest::RewriteFunctionBodyOrGlobalInitializer(Stmt *S) {
  // Otherwise, just rewrite all children.
  for (Stmt::child_iterator CI = S->child_begin(), E = S->child_end();
       CI != E; ++CI)
    if (*CI) {
      Stmt *newStmt = RewriteFunctionBodyOrGlobalInitializer(*CI);
      if (newStmt) 
        *CI = newStmt;
    }
      
  // Handle specific things.
  if (ObjCEncodeExpr *AtEncode = dyn_cast<ObjCEncodeExpr>(S))
    return RewriteAtEncode(AtEncode);
  
  if (ObjCIvarRefExpr *IvarRefExpr = dyn_cast<ObjCIvarRefExpr>(S))
    return RewriteObjCIvarRefExpr(IvarRefExpr);

  if (ObjCSelectorExpr *AtSelector = dyn_cast<ObjCSelectorExpr>(S))
    return RewriteAtSelector(AtSelector);
	
  if (ObjCStringLiteral *AtString = dyn_cast<ObjCStringLiteral>(S))
    return RewriteObjCStringLiteral(AtString);
    
  if (ObjCMessageExpr *MessExpr = dyn_cast<ObjCMessageExpr>(S)) {
    // Before we rewrite it, put the original message expression in a comment.
    SourceLocation startLoc = MessExpr->getLocStart();
    SourceLocation endLoc = MessExpr->getLocEnd();
    
    const char *startBuf = SM->getCharacterData(startLoc);
    const char *endBuf = SM->getCharacterData(endLoc);
    
    std::string messString;
    messString += "// ";
    messString.append(startBuf, endBuf-startBuf+1);
    messString += "\n";
        
    // FIXME: Missing definition of Rewrite.InsertText(clang::SourceLocation, char const*, unsigned int).
    // Rewrite.InsertText(startLoc, messString.c_str(), messString.size());
    // Tried this, but it didn't work either...
    // Rewrite.ReplaceText(startLoc, 0, messString.c_str(), messString.size());
    return RewriteMessageExpr(MessExpr);
  }
  
  if (ObjCAtTryStmt *StmtTry = dyn_cast<ObjCAtTryStmt>(S))
    return RewriteObjCTryStmt(StmtTry);

  if (ObjCAtThrowStmt *StmtThrow = dyn_cast<ObjCAtThrowStmt>(S))
    return RewriteObjCThrowStmt(StmtThrow);
  
  if (ObjCProtocolExpr *ProtocolExp = dyn_cast<ObjCProtocolExpr>(S))
    return RewriteObjCProtocolExpr(ProtocolExp);
  
  if (ObjCForCollectionStmt *StmtForCollection = 
        dyn_cast<ObjCForCollectionStmt>(S))
    return RewriteObjCForCollectionStmt(StmtForCollection);
  
#if 0
  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(S)) {
    CastExpr *Replacement = new CastExpr(ICE->getType(), ICE->getSubExpr(), SourceLocation());
    // Get the new text.
    std::ostringstream Buf;
    Replacement->printPretty(Buf);
    const std::string &Str = Buf.str();

    printf("CAST = %s\n", &Str[0]);
    Rewrite.InsertText(ICE->getSubExpr()->getLocStart(), &Str[0], Str.size());
    delete S;
    return Replacement;
  }
#endif
  // Return this stmt unmodified.
  return S;
}

/// SynthCountByEnumWithState - To print:
/// ((unsigned int (*)
///  (id, SEL, struct __objcFastEnumerationState *, id *, unsigned int))
///  (void *)objc_msgSend)((id)l_collection, 
///                        sel_registerName(
///                          "countByEnumeratingWithState:objects:count:"), 
///                        &enumState, 
///                        (id *)items, (unsigned int)16)
///
void RewriteTest::SynthCountByEnumWithState(std::string &buf) {
  buf += "((unsigned int (*) (id, SEL, struct __objcFastEnumerationState *, "
  "id *, unsigned int))(void *)objc_msgSend)";
  buf += "\n\t\t";
  buf += "((id)l_collection,\n\t\t";
  buf += "sel_registerName(\"countByEnumeratingWithState:objects:count:\"),";
  buf += "\n\t\t";
  buf += "&enumState, "
         "(id *)items, (unsigned int)16)";
}

/// RewriteObjCTryStmt - Rewriter for ObjC2's foreach statement.
///  It rewrites:
/// for ( type elem in collection) { stmts; }
 
/// Into:
/// {
///   type elem; 
///   struct __objcFastEnumerationState enumState = { 0 };
///   id items[16];
///   id l_collection = (id)collection;
///   unsigned long limit = [l_collection countByEnumeratingWithState:&enumState 
///                                       objects:items count:16];
/// if (limit) {
///   unsigned long startMutations = *enumState.mutationsPtr;
///   do {
///        unsigned long counter = 0;
///        do {
///             if (startMutations != *enumState.mutationsPtr) 
///               objc_enumerationMutation(l_collection);
///             elem = (type)enumState.itemsPtr[counter++];
///             stmts;
///        } while (counter < limit);
///   } while (limit = [l_collection countByEnumeratingWithState:&enumState 
///                                  objects:items count:16]);
///   elem = nil;
///   loopend: ;
///  }
///  else
///       elem = nil;
///  }
///
Stmt *RewriteTest::RewriteObjCForCollectionStmt(ObjCForCollectionStmt *S) {
  SourceLocation startLoc = S->getLocStart();
  const char *startBuf = SM->getCharacterData(startLoc);
  const char *elementName;
  std::string elementTypeAsString;
  std::string buf;
  buf = "\n{\n\t";
  if (DeclStmt *DS = dyn_cast<DeclStmt>(S->getElement())) {
    // type elem;
    QualType ElementType = cast<ValueDecl>(DS->getDecl())->getType();
    elementTypeAsString = ElementType.getAsString();
    buf += elementTypeAsString;
    buf += " ";
    elementName = DS->getDecl()->getName();
    buf += elementName;
    buf += ";\n\t";
  }
  else if (DeclRefExpr *DR = dyn_cast<DeclRefExpr>(S->getElement())) {
    elementName = DR->getDecl()->getName();
    elementTypeAsString = DR->getDecl()->getType().getAsString();
  }
  else
    assert(false && "RewriteObjCForCollectionStmt - bad element kind");
  
  // struct __objcFastEnumerationState enumState = { 0 };
  buf += "struct __objcFastEnumerationState enumState = { 0 };\n\t";
  // id items[16];
  buf += "id items[16];\n\t";
  // id l_collection = (id)
  buf += "id l_collection = (id)";
  // Find start location of 'collection' the hard way!
  const char *startCollectionBuf = startBuf;
  startCollectionBuf += 3;  // skip 'for'
  startCollectionBuf = strchr(startCollectionBuf, '(');
  startCollectionBuf++; // skip '('
  // find 'in' and skip it.
  while (*startCollectionBuf != ' ' ||
         *(startCollectionBuf+1) != 'i' || *(startCollectionBuf+2) != 'n' ||
         (*(startCollectionBuf+3) != ' ' &&
          *(startCollectionBuf+3) != '[' && *(startCollectionBuf+3) != '('))
    startCollectionBuf++;
  startCollectionBuf += 3;
  
  // Replace: "for (type element in" with string constructed thus far. 
  Rewrite.ReplaceText(startLoc, startCollectionBuf - startBuf,
                      buf.c_str(), buf.size());
  // Replace ')' in for '(' type elem in collection ')' with ';'
  SourceLocation rightParenLoc = S->getRParenLoc();
  const char *rparenBuf = SM->getCharacterData(rightParenLoc);
  SourceLocation lparenLoc = startLoc.getFileLocWithOffset(rparenBuf-startBuf);
  buf = ";\n\t";
  
  // unsigned long limit = [l_collection countByEnumeratingWithState:&enumState
  //                                   objects:items count:16];
  // which is synthesized into:
  // unsigned int limit = 
  // ((unsigned int (*)
  //  (id, SEL, struct __objcFastEnumerationState *, id *, unsigned int))
  //  (void *)objc_msgSend)((id)l_collection, 
  //                        sel_registerName(
  //                          "countByEnumeratingWithState:objects:count:"), 
  //                        (struct __objcFastEnumerationState *)&state, 
  //                        (id *)items, (unsigned int)16);
  buf += "unsigned long limit =\n\t\t";
  SynthCountByEnumWithState(buf);
  buf += ";\n\t";
  /// if (limit) {
  ///   unsigned long startMutations = *enumState.mutationsPtr;
  ///   do {
  ///        unsigned long counter = 0;
  ///        do {
  ///             if (startMutations != *enumState.mutationsPtr) 
  ///               objc_enumerationMutation(l_collection);
  ///             elem = (type)enumState.itemsPtr[counter++];
  buf += "if (limit) {\n\t";
  buf += "unsigned long startMutations = *enumState.mutationsPtr;\n\t";
  buf += "do {\n\t\t";
  buf += "unsigned long counter = 0;\n\t\t";
  buf += "do {\n\t\t\t";
  buf += "if (startMutations != *enumState.mutationsPtr)\n\t\t\t\t";
  buf += "objc_enumerationMutation(l_collection);\n\t\t\t";
  buf += elementName;
  buf += " = (";
  buf += elementTypeAsString;
  buf += ")enumState.itemsPtr[counter++];";
  // Replace ')' in for '(' type elem in collection ')' with all of these.
  Rewrite.ReplaceText(lparenLoc, 1, buf.c_str(), buf.size());
  
  ///        } while (counter < limit);
  ///   } while (limit = [l_collection countByEnumeratingWithState:&enumState 
  ///                                  objects:items count:16]);
  ///   elem = nil;
  ///   loopend: ;
  ///  }
  ///  else
  ///       elem = nil;
  ///  }
  buf = ";\n\t\t";
  buf += "} while (counter < limit);\n\t";
  buf += "} while (limit = ";
  SynthCountByEnumWithState(buf);
  buf += ");\n\t";
  buf += elementName;
  buf += " = nil;\n\t";
  // TODO: Generate a unique label to exit the for loop on break statement.
  // buf += "loopend: ;\n\t";
  buf += "}\n\t";
  buf += "else\n\t\t";
  buf += elementName;
  buf += " = nil;\n";
  buf += "}\n";
  // Insert all these *after* the statement body.
  SourceLocation endBodyLoc = S->getBody()->getLocEnd();
  const char *endBodyBuf = SM->getCharacterData(endBodyLoc)+1;
  endBodyLoc = startLoc.getFileLocWithOffset(endBodyBuf-startBuf);
  Rewrite.InsertText(endBodyLoc, buf.c_str(), buf.size());
 
  return 0;
}

Stmt *RewriteTest::RewriteObjCTryStmt(ObjCAtTryStmt *S) {
  // Get the start location and compute the semi location.
  SourceLocation startLoc = S->getLocStart();
  const char *startBuf = SM->getCharacterData(startLoc);
  
  assert((*startBuf == '@') && "bogus @try location");

  std::string buf;
  // declare a new scope with two variables, _stack and _rethrow.
  buf = "/* @try scope begin */ { struct _objc_exception_data {\n";
  buf += "int buf[18/*32-bit i386*/];\n";
  buf += "char *pointers[4];} _stack;\n";
  buf += "id volatile _rethrow = 0;\n";
  buf += "objc_exception_try_enter(&_stack);\n";
  buf += "if (!_setjmp(_stack.buf)) /* @try block continue */\n";

  Rewrite.ReplaceText(startLoc, 4, buf.c_str(), buf.size());
  
  startLoc = S->getTryBody()->getLocEnd();
  startBuf = SM->getCharacterData(startLoc);

  assert((*startBuf == '}') && "bogus @try block");

  SourceLocation lastCurlyLoc = startLoc;
  
  startLoc = startLoc.getFileLocWithOffset(1);
  buf = " /* @catch begin */ else {\n";
  buf += " id _caught = objc_exception_extract(&_stack);\n";
  buf += " objc_exception_try_enter (&_stack);\n";
  buf += " if (_setjmp(_stack.buf))\n";
  buf += "   _rethrow = objc_exception_extract(&_stack);\n";
  buf += " else { /* @catch continue */";
  
  Rewrite.InsertText(startLoc, buf.c_str(), buf.size());
  
  bool sawIdTypedCatch = false;
  Stmt *lastCatchBody = 0;
  ObjCAtCatchStmt *catchList = S->getCatchStmts();
  while (catchList) {
    Stmt *catchStmt = catchList->getCatchParamStmt();

    if (catchList == S->getCatchStmts()) 
      buf = "if ("; // we are generating code for the first catch clause
    else
      buf = "else if (";
    startLoc = catchList->getLocStart();
    startBuf = SM->getCharacterData(startLoc);
    
    assert((*startBuf == '@') && "bogus @catch location");
    
    const char *lParenLoc = strchr(startBuf, '(');

    if (DeclStmt *declStmt = dyn_cast<DeclStmt>(catchStmt)) {
      QualType t = dyn_cast<ValueDecl>(declStmt->getDecl())->getType();
      if (t == Context->getObjCIdType()) {
        buf += "1) { ";
        Rewrite.ReplaceText(startLoc, lParenLoc-startBuf+1, 
                            buf.c_str(), buf.size());
        sawIdTypedCatch = true;
      } else if (const PointerType *pType = t->getAsPointerType()) { 
        ObjCInterfaceType *cls; // Should be a pointer to a class.
        
        cls = dyn_cast<ObjCInterfaceType>(pType->getPointeeType().getTypePtr());
        if (cls) {
          buf += "objc_exception_match((struct objc_class *)objc_getClass(\"";
          buf += cls->getDecl()->getName();
          buf += "\"), (struct objc_object *)_caught)) { ";
          Rewrite.ReplaceText(startLoc, lParenLoc-startBuf+1, 
                              buf.c_str(), buf.size());
        }
      }
      // Now rewrite the body...
      lastCatchBody = catchList->getCatchBody();
      SourceLocation rParenLoc = catchList->getRParenLoc();
      SourceLocation bodyLoc = lastCatchBody->getLocStart();
      const char *bodyBuf = SM->getCharacterData(bodyLoc);
      const char *rParenBuf = SM->getCharacterData(rParenLoc);
      assert((*rParenBuf == ')') && "bogus @catch paren location");
      assert((*bodyBuf == '{') && "bogus @catch body location");
        
      buf = " = _caught;";
      // Here we replace ") {" with "= _caught;" (which initializes and 
      // declares the @catch parameter).
      Rewrite.ReplaceText(rParenLoc, bodyBuf-rParenBuf+1, 
                          buf.c_str(), buf.size());
    } else if (!isa<NullStmt>(catchStmt)) {
      assert(false && "@catch rewrite bug");
    }
    catchList = catchList->getNextCatchStmt();
  }
  // Complete the catch list...
  if (lastCatchBody) {
    SourceLocation bodyLoc = lastCatchBody->getLocEnd();
    const char *bodyBuf = SM->getCharacterData(bodyLoc);
    assert((*bodyBuf == '}') && "bogus @catch body location");
    bodyLoc = bodyLoc.getFileLocWithOffset(1);
    buf = " } } /* @catch end */\n";
  
    Rewrite.InsertText(bodyLoc, buf.c_str(), buf.size());
    
    // Set lastCurlyLoc
    lastCurlyLoc = lastCatchBody->getLocEnd();
  }
  if (ObjCAtFinallyStmt *finalStmt = S->getFinallyStmt()) {
    startLoc = finalStmt->getLocStart();
    startBuf = SM->getCharacterData(startLoc);
    assert((*startBuf == '@') && "bogus @finally start");
    
    buf = "/* @finally */";
    Rewrite.ReplaceText(startLoc, 8, buf.c_str(), buf.size());
    
    Stmt *body = finalStmt->getFinallyBody();
    SourceLocation startLoc = body->getLocStart();
    SourceLocation endLoc = body->getLocEnd();
    const char *startBuf = SM->getCharacterData(startLoc);
    const char *endBuf = SM->getCharacterData(endLoc);
    assert((*startBuf == '{') && "bogus @finally body location");
    assert((*endBuf == '}') && "bogus @finally body location");
  
    startLoc = startLoc.getFileLocWithOffset(1);
    buf = " if (!_rethrow) objc_exception_try_exit(&_stack);\n";
    Rewrite.InsertText(startLoc, buf.c_str(), buf.size());
    endLoc = endLoc.getFileLocWithOffset(-1);
    buf = " if (_rethrow) objc_exception_throw(_rethrow);\n";
    Rewrite.InsertText(endLoc, buf.c_str(), buf.size());
    
    // Set lastCurlyLoc
    lastCurlyLoc = body->getLocEnd();
  }
  // Now emit the final closing curly brace...
  lastCurlyLoc = lastCurlyLoc.getFileLocWithOffset(1);
  buf = " } /* @try scope end */\n";
  Rewrite.InsertText(lastCurlyLoc, buf.c_str(), buf.size());
  return 0;
}

Stmt *RewriteTest::RewriteObjCCatchStmt(ObjCAtCatchStmt *S) {
  return 0;
}

Stmt *RewriteTest::RewriteObjCFinallyStmt(ObjCAtFinallyStmt *S) {
  return 0;
}

// This can't be done with Rewrite.ReplaceStmt(S, ThrowExpr), since 
// the throw expression is typically a message expression that's already 
// been rewritten! (which implies the SourceLocation's are invalid).
Stmt *RewriteTest::RewriteObjCThrowStmt(ObjCAtThrowStmt *S) {
  // Get the start location and compute the semi location.
  SourceLocation startLoc = S->getLocStart();
  const char *startBuf = SM->getCharacterData(startLoc);
  
  assert((*startBuf == '@') && "bogus @throw location");

  std::string buf;
  /* void objc_exception_throw(id) __attribute__((noreturn)); */
  buf = "objc_exception_throw(";
  Rewrite.ReplaceText(startLoc, 6, buf.c_str(), buf.size());
  const char *semiBuf = strchr(startBuf, ';');
  assert((*semiBuf == ';') && "@throw: can't find ';'");
  SourceLocation semiLoc = startLoc.getFileLocWithOffset(semiBuf-startBuf);
  buf = ");";
  Rewrite.ReplaceText(semiLoc, 1, buf.c_str(), buf.size());
  return 0;
}

Stmt *RewriteTest::RewriteAtEncode(ObjCEncodeExpr *Exp) {
  // Create a new string expression.
  QualType StrType = Context->getPointerType(Context->CharTy);
  std::string StrEncoding;
  Context->getObjCEncodingForType(Exp->getEncodedType(), StrEncoding);
  Expr *Replacement = new StringLiteral(StrEncoding.c_str(),
                                        StrEncoding.length(), false, StrType, 
                                        SourceLocation(), SourceLocation());
  if (Rewrite.ReplaceStmt(Exp, Replacement)) {
    // replacement failed.
    unsigned DiagID = Diags.getCustomDiagID(Diagnostic::Warning, 
                     "rewriting sub-expression within a macro (may not be correct)");
    SourceRange Range = Exp->getSourceRange();
    Diags.Report(Context->getFullLoc(Exp->getAtLoc()), DiagID, 0, 0, &Range, 1);
  }
  
  // Replace this subexpr in the parent.
  delete Exp;
  return Replacement;
}

Stmt *RewriteTest::RewriteAtSelector(ObjCSelectorExpr *Exp) {
  assert(SelGetUidFunctionDecl && "Can't find sel_registerName() decl");
  // Create a call to sel_registerName("selName").
  llvm::SmallVector<Expr*, 8> SelExprs;
  QualType argType = Context->getPointerType(Context->CharTy);
  SelExprs.push_back(new StringLiteral(Exp->getSelector().getName().c_str(),
                                       Exp->getSelector().getName().size(),
                                       false, argType, SourceLocation(),
                                       SourceLocation()));
  CallExpr *SelExp = SynthesizeCallToFunctionDecl(SelGetUidFunctionDecl,
                                                 &SelExprs[0], SelExprs.size());
  if (Rewrite.ReplaceStmt(Exp, SelExp)) {
    // replacement failed.
    unsigned DiagID = Diags.getCustomDiagID(Diagnostic::Warning, 
                     "rewriting sub-expression within a macro (may not be correct)");
    SourceRange Range = Exp->getSourceRange();
    Diags.Report(Context->getFullLoc(Exp->getAtLoc()), DiagID, 0, 0, &Range, 1);
  }
  delete Exp;
  return SelExp;
}

CallExpr *RewriteTest::SynthesizeCallToFunctionDecl(
  FunctionDecl *FD, Expr **args, unsigned nargs) {
  // Get the type, we will need to reference it in a couple spots.
  QualType msgSendType = FD->getType();
  
  // Create a reference to the objc_msgSend() declaration.
  DeclRefExpr *DRE = new DeclRefExpr(FD, msgSendType, SourceLocation());
                                     
  // Now, we cast the reference to a pointer to the objc_msgSend type.
  QualType pToFunc = Context->getPointerType(msgSendType);
  ImplicitCastExpr *ICE = new ImplicitCastExpr(pToFunc, DRE);
  
  const FunctionType *FT = msgSendType->getAsFunctionType();
  
  return new CallExpr(ICE, args, nargs, FT->getResultType(), SourceLocation());
}

static bool scanForProtocolRefs(const char *startBuf, const char *endBuf,
                                const char *&startRef, const char *&endRef) {
  while (startBuf < endBuf) {
    if (*startBuf == '<')
      startRef = startBuf; // mark the start.
    if (*startBuf == '>') {
      if (startRef && *startRef == '<') {
        endRef = startBuf; // mark the end.
        return true;
      }
      return false;
    }
    startBuf++;
  }
  return false;
}

static void scanToNextArgument(const char *&argRef) {
  int angle = 0;
  while (*argRef != ')' && (*argRef != ',' || angle > 0)) {
    if (*argRef == '<')
      angle++;
    else if (*argRef == '>')
      angle--;
    argRef++;
  }
  assert(angle == 0 && "scanToNextArgument - bad protocol type syntax");
}

bool RewriteTest::needToScanForQualifiers(QualType T) {
  
  if (T == Context->getObjCIdType())
    return true;
    
  if (T->isObjCQualifiedIdType())
    return true;
  
  if (const PointerType *pType = T->getAsPointerType()) {
    Type *pointeeType = pType->getPointeeType().getTypePtr();
    if (isa<ObjCQualifiedInterfaceType>(pointeeType))
      return true; // we have "Class <Protocol> *".
  }
  return false;
}

void RewriteTest::RewriteObjCQualifiedInterfaceTypes(Decl *Dcl) {
  SourceLocation Loc;
  QualType Type;
  const FunctionTypeProto *proto = 0;
  if (VarDecl *VD = dyn_cast<VarDecl>(Dcl)) {
    Loc = VD->getLocation();
    Type = VD->getType();
  }
  else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(Dcl)) {
    Loc = FD->getLocation();
    // Check for ObjC 'id' and class types that have been adorned with protocol
    // information (id<p>, C<p>*). The protocol references need to be rewritten!
    const FunctionType *funcType = FD->getType()->getAsFunctionType();
    assert(funcType && "missing function type");
    proto = dyn_cast<FunctionTypeProto>(funcType);
    if (!proto)
      return;
    Type = proto->getResultType();
  }
  else
    return;
  
  if (needToScanForQualifiers(Type)) {
    // Since types are unique, we need to scan the buffer.
    
    const char *endBuf = SM->getCharacterData(Loc);
    const char *startBuf = endBuf;
    while (*startBuf != ';' && startBuf != MainFileStart)
      startBuf--; // scan backward (from the decl location) for return type.
    const char *startRef = 0, *endRef = 0;
    if (scanForProtocolRefs(startBuf, endBuf, startRef, endRef)) {
      // Get the locations of the startRef, endRef.
      SourceLocation LessLoc = Loc.getFileLocWithOffset(startRef-endBuf);
      SourceLocation GreaterLoc = Loc.getFileLocWithOffset(endRef-endBuf+1);
      // Comment out the protocol references.
      Rewrite.InsertText(LessLoc, "/*", 2);
      Rewrite.InsertText(GreaterLoc, "*/", 2);
    }
  }
  if (!proto)
      return; // most likely, was a variable
  // Now check arguments.
  const char *startBuf = SM->getCharacterData(Loc);
  const char *startFuncBuf = startBuf;
  for (unsigned i = 0; i < proto->getNumArgs(); i++) {
    if (needToScanForQualifiers(proto->getArgType(i))) {
      // Since types are unique, we need to scan the buffer.
      
      const char *endBuf = startBuf;
      // scan forward (from the decl location) for argument types.
      scanToNextArgument(endBuf);
      const char *startRef = 0, *endRef = 0;
      if (scanForProtocolRefs(startBuf, endBuf, startRef, endRef)) {
        // Get the locations of the startRef, endRef.
        SourceLocation LessLoc = 
          Loc.getFileLocWithOffset(startRef-startFuncBuf);
        SourceLocation GreaterLoc = 
          Loc.getFileLocWithOffset(endRef-startFuncBuf+1);
        // Comment out the protocol references.
        Rewrite.InsertText(LessLoc, "/*", 2);
        Rewrite.InsertText(GreaterLoc, "*/", 2);
      }
      startBuf = ++endBuf;
    }
    else {
      while (*startBuf != ')' && *startBuf != ',')
        startBuf++; // scan forward (from the decl location) for argument types.
      startBuf++;
    }
  }
}

// SynthSelGetUidFunctionDecl - SEL sel_registerName(const char *str);
void RewriteTest::SynthSelGetUidFunctionDecl() {
  IdentifierInfo *SelGetUidIdent = &Context->Idents.get("sel_registerName");
  llvm::SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(
    Context->CharTy.getQualifiedType(QualType::Const)));
  QualType getFuncType = Context->getFunctionType(Context->getObjCSelType(),
                                                   &ArgTys[0], ArgTys.size(),
                                                   false /*isVariadic*/);
  SelGetUidFunctionDecl = new FunctionDecl(SourceLocation(), 
                                           SelGetUidIdent, getFuncType,
                                           FunctionDecl::Extern, false, 0);
}

// SynthGetProtocolFunctionDecl - Protocol objc_getProtocol(const char *proto);
void RewriteTest::SynthGetProtocolFunctionDecl() {
  IdentifierInfo *SelGetProtoIdent = &Context->Idents.get("objc_getProtocol");
  llvm::SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(
    Context->CharTy.getQualifiedType(QualType::Const)));
  QualType getFuncType = Context->getFunctionType(Context->getObjCProtoType(),
                                                  &ArgTys[0], ArgTys.size(),
                                                  false /*isVariadic*/);
  GetProtocolFunctionDecl = new FunctionDecl(SourceLocation(), 
                                             SelGetProtoIdent, getFuncType,
                                             FunctionDecl::Extern, false, 0);
}

void RewriteTest::RewriteFunctionDecl(FunctionDecl *FD) {
  // declared in <objc/objc.h>
  if (strcmp(FD->getName(), "sel_registerName") == 0) {
    SelGetUidFunctionDecl = FD;
    return;
  }
  RewriteObjCQualifiedInterfaceTypes(FD);
}

// SynthMsgSendFunctionDecl - id objc_msgSend(id self, SEL op, ...);
void RewriteTest::SynthMsgSendFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSend");
  llvm::SmallVector<QualType, 16> ArgTys;
  QualType argT = Context->getObjCIdType();
  assert(!argT.isNull() && "Can't find 'id' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = Context->getFunctionType(Context->getObjCIdType(),
                                                  &ArgTys[0], ArgTys.size(),
                                                  true /*isVariadic*/);
  MsgSendFunctionDecl = new FunctionDecl(SourceLocation(), 
                                         msgSendIdent, msgSendType,
                                         FunctionDecl::Extern, false, 0);
}

// SynthMsgSendSuperFunctionDecl - id objc_msgSendSuper(struct objc_super *, SEL op, ...);
void RewriteTest::SynthMsgSendSuperFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSendSuper");
  llvm::SmallVector<QualType, 16> ArgTys;
  RecordDecl *RD = new RecordDecl(Decl::Struct, SourceLocation(),
                                  &Context->Idents.get("objc_super"), 0);
  QualType argT = Context->getPointerType(Context->getTagDeclType(RD));
  assert(!argT.isNull() && "Can't build 'struct objc_super *' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = Context->getFunctionType(Context->getObjCIdType(),
                                                  &ArgTys[0], ArgTys.size(),
                                                  true /*isVariadic*/);
  MsgSendSuperFunctionDecl = new FunctionDecl(SourceLocation(), 
                                              msgSendIdent, msgSendType,
                                              FunctionDecl::Extern, false, 0);
}

// SynthMsgSendStretFunctionDecl - id objc_msgSend_stret(id self, SEL op, ...);
void RewriteTest::SynthMsgSendStretFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSend_stret");
  llvm::SmallVector<QualType, 16> ArgTys;
  QualType argT = Context->getObjCIdType();
  assert(!argT.isNull() && "Can't find 'id' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = Context->getFunctionType(Context->getObjCIdType(),
                                                  &ArgTys[0], ArgTys.size(),
                                                  true /*isVariadic*/);
  MsgSendStretFunctionDecl = new FunctionDecl(SourceLocation(), 
                                         msgSendIdent, msgSendType,
                                         FunctionDecl::Extern, false, 0);
}

// SynthMsgSendSuperStretFunctionDecl - 
// id objc_msgSendSuper_stret(struct objc_super *, SEL op, ...);
void RewriteTest::SynthMsgSendSuperStretFunctionDecl() {
  IdentifierInfo *msgSendIdent = 
    &Context->Idents.get("objc_msgSendSuper_stret");
  llvm::SmallVector<QualType, 16> ArgTys;
  RecordDecl *RD = new RecordDecl(Decl::Struct, SourceLocation(),
                                  &Context->Idents.get("objc_super"), 0);
  QualType argT = Context->getPointerType(Context->getTagDeclType(RD));
  assert(!argT.isNull() && "Can't build 'struct objc_super *' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = Context->getFunctionType(Context->getObjCIdType(),
                                                  &ArgTys[0], ArgTys.size(),
                                                  true /*isVariadic*/);
  MsgSendSuperStretFunctionDecl = new FunctionDecl(SourceLocation(), 
                                              msgSendIdent, msgSendType,
                                              FunctionDecl::Extern, false, 0);
}

// SynthMsgSendFpretFunctionDecl - id objc_msgSend_fpret(id self, SEL op, ...);
void RewriteTest::SynthMsgSendFpretFunctionDecl() {
  IdentifierInfo *msgSendIdent = &Context->Idents.get("objc_msgSend_fpret");
  llvm::SmallVector<QualType, 16> ArgTys;
  QualType argT = Context->getObjCIdType();
  assert(!argT.isNull() && "Can't find 'id' type");
  ArgTys.push_back(argT);
  argT = Context->getObjCSelType();
  assert(!argT.isNull() && "Can't find 'SEL' type");
  ArgTys.push_back(argT);
  QualType msgSendType = Context->getFunctionType(Context->getObjCIdType(),
                                                  &ArgTys[0], ArgTys.size(),
                                                  true /*isVariadic*/);
  MsgSendFpretFunctionDecl = new FunctionDecl(SourceLocation(), 
                                              msgSendIdent, msgSendType,
                                              FunctionDecl::Extern, false, 0);
}

// SynthGetClassFunctionDecl - id objc_getClass(const char *name);
void RewriteTest::SynthGetClassFunctionDecl() {
  IdentifierInfo *getClassIdent = &Context->Idents.get("objc_getClass");
  llvm::SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(
                     Context->CharTy.getQualifiedType(QualType::Const)));
  QualType getClassType = Context->getFunctionType(Context->getObjCIdType(),
                                                   &ArgTys[0], ArgTys.size(),
                                                   false /*isVariadic*/);
  GetClassFunctionDecl = new FunctionDecl(SourceLocation(), 
                                          getClassIdent, getClassType,
                                          FunctionDecl::Extern, false, 0);
}

// SynthGetMetaClassFunctionDecl - id objc_getClass(const char *name);
void RewriteTest::SynthGetMetaClassFunctionDecl() {
  IdentifierInfo *getClassIdent = &Context->Idents.get("objc_getMetaClass");
  llvm::SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(
                     Context->CharTy.getQualifiedType(QualType::Const)));
  QualType getClassType = Context->getFunctionType(Context->getObjCIdType(),
                                                   &ArgTys[0], ArgTys.size(),
                                                   false /*isVariadic*/);
  GetMetaClassFunctionDecl = new FunctionDecl(SourceLocation(), 
                                              getClassIdent, getClassType,
                                              FunctionDecl::Extern, false, 0);
}

// SynthCFStringFunctionDecl - id __builtin___CFStringMakeConstantString(const char *name);
void RewriteTest::SynthCFStringFunctionDecl() {
  IdentifierInfo *getClassIdent = &Context->Idents.get("__builtin___CFStringMakeConstantString");
  llvm::SmallVector<QualType, 16> ArgTys;
  ArgTys.push_back(Context->getPointerType(
                     Context->CharTy.getQualifiedType(QualType::Const)));
  QualType getClassType = Context->getFunctionType(Context->getObjCIdType(),
                                                   &ArgTys[0], ArgTys.size(),
                                                   false /*isVariadic*/);
  CFStringFunctionDecl = new FunctionDecl(SourceLocation(), 
                                          getClassIdent, getClassType,
                                          FunctionDecl::Extern, false, 0);
}

Stmt *RewriteTest::RewriteObjCStringLiteral(ObjCStringLiteral *Exp) {
#if 1
  // This rewrite is specific to GCC, which has builtin support for CFString.
  if (!CFStringFunctionDecl)
    SynthCFStringFunctionDecl();
  // Create a call to __builtin___CFStringMakeConstantString("cstr").
  llvm::SmallVector<Expr*, 8> StrExpr;
  StrExpr.push_back(Exp->getString());
  CallExpr *call = SynthesizeCallToFunctionDecl(CFStringFunctionDecl,
                                                &StrExpr[0], StrExpr.size());
  // cast to NSConstantString *
  CastExpr *cast = new CastExpr(Exp->getType(), call, SourceLocation());
  if (Rewrite.ReplaceStmt(Exp, cast)) {
    // replacement failed.
    unsigned DiagID = Diags.getCustomDiagID(Diagnostic::Warning, 
                     "rewriting sub-expression within a macro (may not be correct)");
    SourceRange Range = Exp->getSourceRange();
    Diags.Report(Context->getFullLoc(Exp->getAtLoc()), DiagID, 0, 0, &Range, 1);
  }
  delete Exp;
  return cast;
#else
  assert(ConstantStringClassReference && "Can't find constant string reference");
  llvm::SmallVector<Expr*, 4> InitExprs;
  
  // Synthesize "(Class)&_NSConstantStringClassReference"
  DeclRefExpr *ClsRef = new DeclRefExpr(ConstantStringClassReference,
                                        ConstantStringClassReference->getType(),
                                        SourceLocation());
  QualType expType = Context->getPointerType(ClsRef->getType());
  UnaryOperator *Unop = new UnaryOperator(ClsRef, UnaryOperator::AddrOf,
                                          expType, SourceLocation());
  CastExpr *cast = new CastExpr(Context->getObjCClassType(), Unop, 
                                SourceLocation());
  InitExprs.push_back(cast); // set the 'isa'.
  InitExprs.push_back(Exp->getString()); // set "char *bytes".
  unsigned IntSize = static_cast<unsigned>(
      Context->getTypeSize(Context->IntTy, Exp->getLocStart()));
  llvm::APInt IntVal(IntSize, Exp->getString()->getByteLength());
  IntegerLiteral *len = new IntegerLiteral(IntVal, Context->IntTy, 
                                           Exp->getLocStart());
  InitExprs.push_back(len); // set "int numBytes".
  
  // struct NSConstantString
  QualType CFConstantStrType = Context->getCFConstantStringType();
  // (struct NSConstantString) { <exprs from above> }
  InitListExpr *ILE = new InitListExpr(SourceLocation(), 
                                       &InitExprs[0], InitExprs.size(), 
                                       SourceLocation());
  CompoundLiteralExpr *StrRep = new CompoundLiteralExpr(CFConstantStrType, ILE, false);
  // struct NSConstantString *
  expType = Context->getPointerType(StrRep->getType());
  Unop = new UnaryOperator(StrRep, UnaryOperator::AddrOf, expType, 
                           SourceLocation());
  // cast to NSConstantString *
  cast = new CastExpr(Exp->getType(), Unop, SourceLocation());
  Rewrite.ReplaceStmt(Exp, cast);
  delete Exp;
  return cast;
#endif
}

ObjCInterfaceDecl *RewriteTest::isSuperReceiver(Expr *recExpr) {
  // check if we are sending a message to 'super'
  if (CurMethodDecl && CurMethodDecl->isInstance()) {
    if (CastExpr *CE = dyn_cast<CastExpr>(recExpr)) {
      if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(CE->getSubExpr())) {
        if (ParmVarDecl *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl())) {
          if (!strcmp(PVD->getName(), "self")) {
            // is this id<P1..> type?
            if (CE->getType()->isObjCQualifiedIdType())
              return 0;
            if (const PointerType *PT = CE->getType()->getAsPointerType()) {
              if (ObjCInterfaceType *IT = 
                    dyn_cast<ObjCInterfaceType>(PT->getPointeeType())) {
                if (IT->getDecl() == 
                    CurMethodDecl->getClassInterface()->getSuperClass())
                  return IT->getDecl();
              }
            }
          }
        }
      }
    }
  }
  return 0;
}

// struct objc_super { struct objc_object *receiver; struct objc_class *super; };
QualType RewriteTest::getSuperStructType() {
  if (!SuperStructDecl) {
    SuperStructDecl = new RecordDecl(Decl::Struct, SourceLocation(), 
                                     &Context->Idents.get("objc_super"), 0);
    QualType FieldTypes[2];
  
    // struct objc_object *receiver;
    FieldTypes[0] = Context->getObjCIdType();  
    // struct objc_class *super;
    FieldTypes[1] = Context->getObjCClassType();  
    // Create fields
    FieldDecl *FieldDecls[2];
  
    for (unsigned i = 0; i < 2; ++i)
      FieldDecls[i] = new FieldDecl(SourceLocation(), 0, FieldTypes[i]);
  
    SuperStructDecl->defineBody(FieldDecls, 4);
  }
  return Context->getTagDeclType(SuperStructDecl);
}

Stmt *RewriteTest::SynthMessageExpr(ObjCMessageExpr *Exp) {
  if (!SelGetUidFunctionDecl)
    SynthSelGetUidFunctionDecl();
  if (!MsgSendFunctionDecl)
    SynthMsgSendFunctionDecl();
  if (!MsgSendSuperFunctionDecl)
    SynthMsgSendSuperFunctionDecl();
  if (!MsgSendStretFunctionDecl)
    SynthMsgSendStretFunctionDecl();
  if (!MsgSendSuperStretFunctionDecl)
    SynthMsgSendSuperStretFunctionDecl();
  if (!MsgSendFpretFunctionDecl)
    SynthMsgSendFpretFunctionDecl();
  if (!GetClassFunctionDecl)
    SynthGetClassFunctionDecl();
  if (!GetMetaClassFunctionDecl)
    SynthGetMetaClassFunctionDecl();
  
  // default to objc_msgSend().
  FunctionDecl *MsgSendFlavor = MsgSendFunctionDecl;
  // May need to use objc_msgSend_stret() as well.
  FunctionDecl *MsgSendStretFlavor = 0;
  if (ObjCMethodDecl *mDecl = Exp->getMethodDecl()) {
    QualType resultType = mDecl->getResultType();
    if (resultType.getCanonicalType()->isStructureType() 
        || resultType.getCanonicalType()->isUnionType())
      MsgSendStretFlavor = MsgSendStretFunctionDecl;
    else if (resultType.getCanonicalType()->isRealFloatingType())
      MsgSendFlavor = MsgSendFpretFunctionDecl;
  }
  
  // Synthesize a call to objc_msgSend().
  llvm::SmallVector<Expr*, 8> MsgExprs;
  IdentifierInfo *clsName = Exp->getClassName();
  
  // Derive/push the receiver/selector, 2 implicit arguments to objc_msgSend().
  if (clsName) { // class message.
    if (!strcmp(clsName->getName(), "super")) {
      MsgSendFlavor = MsgSendSuperFunctionDecl;
      if (MsgSendStretFlavor)
        MsgSendStretFlavor = MsgSendSuperStretFunctionDecl;
      assert(MsgSendFlavor && "MsgSendFlavor is NULL!");
      
      ObjCInterfaceDecl *SuperDecl = 
        CurMethodDecl->getClassInterface()->getSuperClass();

      llvm::SmallVector<Expr*, 4> InitExprs;
      
      // set the receiver to self, the first argument to all methods.
      InitExprs.push_back(new DeclRefExpr(CurMethodDecl->getSelfDecl(), 
                                          Context->getObjCIdType(),
                                          SourceLocation())); 
      llvm::SmallVector<Expr*, 8> ClsExprs;
      QualType argType = Context->getPointerType(Context->CharTy);
      ClsExprs.push_back(new StringLiteral(SuperDecl->getIdentifier()->getName(), 
                                           SuperDecl->getIdentifier()->getLength(),
                                           false, argType, SourceLocation(),
                                           SourceLocation()));
      CallExpr *Cls = SynthesizeCallToFunctionDecl(GetMetaClassFunctionDecl,
                                                   &ClsExprs[0], 
                                                   ClsExprs.size());
      // To turn off a warning, type-cast to 'id'
      InitExprs.push_back(
        new CastExpr(Context->getObjCIdType(), 
        Cls, SourceLocation())); // set 'super class', using objc_getClass().
      // struct objc_super
      QualType superType = getSuperStructType();
      // (struct objc_super) { <exprs from above> }
      InitListExpr *ILE = new InitListExpr(SourceLocation(), 
                                           &InitExprs[0], InitExprs.size(), 
                                           SourceLocation());
      CompoundLiteralExpr *SuperRep = new CompoundLiteralExpr(SourceLocation(),
                                                              superType, ILE, false);
      // struct objc_super *
      Expr *Unop = new UnaryOperator(SuperRep, UnaryOperator::AddrOf,
                               Context->getPointerType(SuperRep->getType()), 
                               SourceLocation());
      MsgExprs.push_back(Unop);
    } else {
      llvm::SmallVector<Expr*, 8> ClsExprs;
      QualType argType = Context->getPointerType(Context->CharTy);
      ClsExprs.push_back(new StringLiteral(clsName->getName(), 
                                           clsName->getLength(),
                                           false, argType, SourceLocation(),
                                           SourceLocation()));
      CallExpr *Cls = SynthesizeCallToFunctionDecl(GetClassFunctionDecl,
                                                   &ClsExprs[0], 
                                                   ClsExprs.size());
      MsgExprs.push_back(Cls);
    }
  } else { // instance message.
    Expr *recExpr = Exp->getReceiver();

    if (ObjCInterfaceDecl *SuperDecl = isSuperReceiver(recExpr)) {
      MsgSendFlavor = MsgSendSuperFunctionDecl;
      if (MsgSendStretFlavor)
        MsgSendStretFlavor = MsgSendSuperStretFunctionDecl;
      assert(MsgSendFlavor && "MsgSendFlavor is NULL!");
      
      llvm::SmallVector<Expr*, 4> InitExprs;
      
      InitExprs.push_back(
        new CastExpr(Context->getObjCIdType(), 
                     recExpr, SourceLocation())); // set the 'receiver'.
      
      llvm::SmallVector<Expr*, 8> ClsExprs;
      QualType argType = Context->getPointerType(Context->CharTy);
      ClsExprs.push_back(new StringLiteral(SuperDecl->getIdentifier()->getName(), 
                                           SuperDecl->getIdentifier()->getLength(),
                                           false, argType, SourceLocation(),
                                           SourceLocation()));
      CallExpr *Cls = SynthesizeCallToFunctionDecl(GetClassFunctionDecl,
                                                   &ClsExprs[0], 
                                                   ClsExprs.size());
      // To turn off a warning, type-cast to 'id'
      InitExprs.push_back(
        new CastExpr(Context->getObjCIdType(), 
        Cls, SourceLocation())); // set 'super class', using objc_getClass().
      // struct objc_super
      QualType superType = getSuperStructType();
      // (struct objc_super) { <exprs from above> }
      InitListExpr *ILE = new InitListExpr(SourceLocation(), 
                                           &InitExprs[0], InitExprs.size(), 
                                           SourceLocation());
      CompoundLiteralExpr *SuperRep = new CompoundLiteralExpr(SourceLocation(),
                                                              superType, ILE, false);
      // struct objc_super *
      Expr *Unop = new UnaryOperator(SuperRep, UnaryOperator::AddrOf,
                               Context->getPointerType(SuperRep->getType()), 
                               SourceLocation());
      MsgExprs.push_back(Unop);
    } else {
      // Remove all type-casts because it may contain objc-style types; e.g.
      // Foo<Proto> *.
      while (CastExpr *CE = dyn_cast<CastExpr>(recExpr))
        recExpr = CE->getSubExpr();
      recExpr = new CastExpr(Context->getObjCIdType(), recExpr, SourceLocation());
      MsgExprs.push_back(recExpr);
    }
  }
  // Create a call to sel_registerName("selName"), it will be the 2nd argument.
  llvm::SmallVector<Expr*, 8> SelExprs;
  QualType argType = Context->getPointerType(Context->CharTy);
  SelExprs.push_back(new StringLiteral(Exp->getSelector().getName().c_str(),
                                       Exp->getSelector().getName().size(),
                                       false, argType, SourceLocation(),
                                       SourceLocation()));
  CallExpr *SelExp = SynthesizeCallToFunctionDecl(SelGetUidFunctionDecl,
                                                 &SelExprs[0], SelExprs.size());
  MsgExprs.push_back(SelExp);
  
  // Now push any user supplied arguments.
  for (unsigned i = 0; i < Exp->getNumArgs(); i++) {
    Expr *userExpr = Exp->getArg(i);
    // Make all implicit casts explicit...ICE comes in handy:-)
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(userExpr)) {
      // Reuse the ICE type, it is exactly what the doctor ordered.
      userExpr = new CastExpr(ICE->getType()->isObjCQualifiedIdType()
                                ? Context->getObjCIdType()
                                : ICE->getType(), userExpr, SourceLocation());
    }
    // Make id<P...> cast into an 'id' cast.
    else if (CastExpr *CE = dyn_cast<CastExpr>(userExpr)) {
      if (CE->getType()->isObjCQualifiedIdType()) {
        while ((CE = dyn_cast<CastExpr>(userExpr)))
          userExpr = CE->getSubExpr();
        userExpr = new CastExpr(Context->getObjCIdType(), 
                                userExpr, SourceLocation());
      }
    } 
    MsgExprs.push_back(userExpr);
    // We've transferred the ownership to MsgExprs. Null out the argument in
    // the original expression, since we will delete it below.
    Exp->setArg(i, 0);
  }
  // Generate the funky cast.
  CastExpr *cast;
  llvm::SmallVector<QualType, 8> ArgTypes;
  QualType returnType;
  
  // Push 'id' and 'SEL', the 2 implicit arguments.
  if (MsgSendFlavor == MsgSendSuperFunctionDecl)
    ArgTypes.push_back(Context->getPointerType(getSuperStructType()));
  else
    ArgTypes.push_back(Context->getObjCIdType());
  ArgTypes.push_back(Context->getObjCSelType());
  if (ObjCMethodDecl *mDecl = Exp->getMethodDecl()) {
    // Push any user argument types.
    for (int i = 0; i < mDecl->getNumParams(); i++) {
      QualType t = mDecl->getParamDecl(i)->getType()->isObjCQualifiedIdType()
                     ? Context->getObjCIdType() 
                     : mDecl->getParamDecl(i)->getType();
      ArgTypes.push_back(t);
    }
    returnType = mDecl->getResultType()->isObjCQualifiedIdType()
                   ? Context->getObjCIdType() : mDecl->getResultType();
  } else {
    returnType = Context->getObjCIdType();
  }
  // Get the type, we will need to reference it in a couple spots.
  QualType msgSendType = MsgSendFlavor->getType();
  
  // Create a reference to the objc_msgSend() declaration.
  DeclRefExpr *DRE = new DeclRefExpr(MsgSendFlavor, msgSendType, 
                                     SourceLocation());

  // Need to cast objc_msgSend to "void *" (to workaround a GCC bandaid). 
  // If we don't do this cast, we get the following bizarre warning/note:
  // xx.m:13: warning: function called through a non-compatible type
  // xx.m:13: note: if this code is reached, the program will abort
  cast = new CastExpr(Context->getPointerType(Context->VoidTy), DRE, 
                      SourceLocation());
    
  // Now do the "normal" pointer to function cast.
  QualType castType = Context->getFunctionType(returnType, 
    &ArgTypes[0], ArgTypes.size(),
    Exp->getMethodDecl() ? Exp->getMethodDecl()->isVariadic() : false);
  castType = Context->getPointerType(castType);
  cast = new CastExpr(castType, cast, SourceLocation());

  // Don't forget the parens to enforce the proper binding.
  ParenExpr *PE = new ParenExpr(SourceLocation(), SourceLocation(), cast);
  
  const FunctionType *FT = msgSendType->getAsFunctionType();
  CallExpr *CE = new CallExpr(PE, &MsgExprs[0], MsgExprs.size(), 
                              FT->getResultType(), SourceLocation());
  Stmt *ReplacingStmt = CE;
  if (MsgSendStretFlavor) {
    // We have the method which returns a struct/union. Must also generate
    // call to objc_msgSend_stret and hang both varieties on a conditional
    // expression which dictate which one to envoke depending on size of
    // method's return type.
    
    // Create a reference to the objc_msgSend_stret() declaration.
    DeclRefExpr *STDRE = new DeclRefExpr(MsgSendStretFlavor, msgSendType, 
                                         SourceLocation());
    // Need to cast objc_msgSend_stret to "void *" (see above comment).
    cast = new CastExpr(Context->getPointerType(Context->VoidTy), STDRE, 
                        SourceLocation());
    // Now do the "normal" pointer to function cast.
    castType = Context->getFunctionType(returnType, 
      &ArgTypes[0], ArgTypes.size(),
      Exp->getMethodDecl() ? Exp->getMethodDecl()->isVariadic() : false);
    castType = Context->getPointerType(castType);
    cast = new CastExpr(castType, cast, SourceLocation());
    
    // Don't forget the parens to enforce the proper binding.
    PE = new ParenExpr(SourceLocation(), SourceLocation(), cast);
    
    FT = msgSendType->getAsFunctionType();
    CallExpr *STCE = new CallExpr(PE, &MsgExprs[0], MsgExprs.size(), 
                                  FT->getResultType(), SourceLocation());
    
    // Build sizeof(returnType)
    SizeOfAlignOfTypeExpr *sizeofExpr = new SizeOfAlignOfTypeExpr(true, 
                                          returnType, Context->getSizeType(), 
                                          SourceLocation(), SourceLocation());
    // (sizeof(returnType) <= 8 ? objc_msgSend(...) : objc_msgSend_stret(...))
    // FIXME: Value of 8 is base on ppc32/x86 ABI for the most common cases.
    // For X86 it is more complicated and some kind of target specific routine
    // is needed to decide what to do.
    unsigned IntSize = static_cast<unsigned>(
      Context->getTypeSize(Context->IntTy, SourceLocation()));

    IntegerLiteral *limit = new IntegerLiteral(llvm::APInt(IntSize, 8), 
                                               Context->IntTy,
                                               SourceLocation());
    BinaryOperator *lessThanExpr = new BinaryOperator(sizeofExpr, limit, 
                                                      BinaryOperator::LE, 
                                                      Context->IntTy, 
                                                      SourceLocation());
    // (sizeof(returnType) <= 8 ? objc_msgSend(...) : objc_msgSend_stret(...))
    ConditionalOperator *CondExpr = 
      new ConditionalOperator(lessThanExpr, CE, STCE, returnType);
    ReplacingStmt = new ParenExpr(SourceLocation(), SourceLocation(), CondExpr);
  }
  return ReplacingStmt;
}

Stmt *RewriteTest::RewriteMessageExpr(ObjCMessageExpr *Exp) {
  Stmt *ReplacingStmt = SynthMessageExpr(Exp);
  // Now do the actual rewrite.
  if (Rewrite.ReplaceStmt(Exp, ReplacingStmt)) {
    // replacement failed.
    unsigned DiagID = Diags.getCustomDiagID(Diagnostic::Warning, 
                     "rewriting sub-expression within a macro (may not be correct)");
    SourceRange Range = Exp->getSourceRange();
    Diags.Report(Context->getFullLoc(Exp->getLocStart()), DiagID, 0, 0, &Range, 1);
  }
  
  delete Exp;
  return ReplacingStmt;
}

/// RewriteObjCProtocolExpr - Rewrite a protocol expression into
/// call to objc_getProtocol("proto-name").
Stmt *RewriteTest::RewriteObjCProtocolExpr(ObjCProtocolExpr *Exp) {
  if (!GetProtocolFunctionDecl)
    SynthGetProtocolFunctionDecl();
  // Create a call to objc_getProtocol("ProtocolName").
  llvm::SmallVector<Expr*, 8> ProtoExprs;
  QualType argType = Context->getPointerType(Context->CharTy);
  ProtoExprs.push_back(new StringLiteral(Exp->getProtocol()->getName(),
                                       strlen(Exp->getProtocol()->getName()),
                                       false, argType, SourceLocation(),
                                       SourceLocation()));
  CallExpr *ProtoExp = SynthesizeCallToFunctionDecl(GetProtocolFunctionDecl,
                                                    &ProtoExprs[0], 
                                                    ProtoExprs.size());
  if (Rewrite.ReplaceStmt(Exp, ProtoExp)) {
    // replacement failed.
    unsigned DiagID = Diags.getCustomDiagID(Diagnostic::Warning, 
                     "rewriting sub-expression within a macro (may not be correct)");
    SourceRange Range = Exp->getSourceRange();
    Diags.Report(Context->getFullLoc(Exp->getAtLoc()), DiagID, 0, 0, &Range, 1);
  }
  delete Exp;
  return ProtoExp;
  
}

/// SynthesizeObjCInternalStruct - Rewrite one internal struct corresponding to
/// an objective-c class with ivars.
void RewriteTest::SynthesizeObjCInternalStruct(ObjCInterfaceDecl *CDecl,
                                               std::string &Result) {
  assert(CDecl && "Class missing in SynthesizeObjCInternalStruct");
  assert(CDecl->getName() && "Name missing in SynthesizeObjCInternalStruct");
  // Do not synthesize more than once.
  if (ObjCSynthesizedStructs.count(CDecl))
    return;
  ObjCInterfaceDecl *RCDecl = CDecl->getSuperClass();
  int NumIvars = CDecl->getNumInstanceVariables();
  SourceLocation LocStart = CDecl->getLocStart();
  SourceLocation LocEnd = CDecl->getLocEnd();
  
  const char *startBuf = SM->getCharacterData(LocStart);
  const char *endBuf = SM->getCharacterData(LocEnd);
  // If no ivars and no root or if its root, directly or indirectly,
  // have no ivars (thus not synthesized) then no need to synthesize this class.
  if (NumIvars <= 0 && (!RCDecl || !ObjCSynthesizedStructs.count(RCDecl))) {
    endBuf += Lexer::MeasureTokenLength(LocEnd, *SM);
    Rewrite.ReplaceText(LocStart, endBuf-startBuf, 
                        Result.c_str(), Result.size());
    return;
  }
  
  // FIXME: This has potential of causing problem. If 
  // SynthesizeObjCInternalStruct is ever called recursively.
  Result += "\nstruct ";
  Result += CDecl->getName();
  
  if (NumIvars > 0) {
    const char *cursor = strchr(startBuf, '{');
    assert((cursor && endBuf) 
           && "SynthesizeObjCInternalStruct - malformed @interface");
    
    // rewrite the original header *without* disturbing the '{'
    Rewrite.ReplaceText(LocStart, cursor-startBuf-1, 
                        Result.c_str(), Result.size());
    if (RCDecl && ObjCSynthesizedStructs.count(RCDecl)) {
      Result = "\n    struct ";
      Result += RCDecl->getName();
	  // Note: We don't name the field decl. This simplifies the "codegen" for
	  // accessing a superclasses instance variables (and is similar to what gcc
	  // does internally). The unnamed struct field feature is enabled with
	  // -fms-extensions. If the struct definition were "inlined", we wouldn't
	  // need to use this switch. That said, I don't want to inline the def.
      Result += ";\n";
      
      // insert the super class structure definition.
      SourceLocation OnePastCurly = LocStart.getFileLocWithOffset(cursor-startBuf+1);
      Rewrite.InsertText(OnePastCurly, Result.c_str(), Result.size());
    }
    cursor++; // past '{'
    
    // Now comment out any visibility specifiers.
    while (cursor < endBuf) {
      if (*cursor == '@') {
        SourceLocation atLoc = LocStart.getFileLocWithOffset(cursor-startBuf);
        // Skip whitespace.
        for (++cursor; cursor[0] == ' ' || cursor[0] == '\t'; ++cursor)
          /*scan*/;

        // FIXME: presence of @public, etc. inside comment results in
        // this transformation as well, which is still correct c-code.
        if (!strncmp(cursor, "public", strlen("public")) ||
            !strncmp(cursor, "private", strlen("private")) ||
            !strncmp(cursor, "protected", strlen("protected")))
          Rewrite.InsertText(atLoc, "// ", 3);
      }
      // FIXME: If there are cases where '<' is used in ivar declaration part
      // of user code, then scan the ivar list and use needToScanForQualifiers
      // for type checking.
      else if (*cursor == '<') {
        SourceLocation atLoc = LocStart.getFileLocWithOffset(cursor-startBuf);
        Rewrite.InsertText(atLoc, "/* ", 3);
        cursor = strchr(cursor, '>');
        cursor++;
        atLoc = LocStart.getFileLocWithOffset(cursor-startBuf);
        Rewrite.InsertText(atLoc, " */", 3);
      }
      cursor++;
    }
    // Don't forget to add a ';'!!
    Rewrite.InsertText(LocEnd.getFileLocWithOffset(1), ";", 1);
  } else { // we don't have any instance variables - insert super struct.
    endBuf += Lexer::MeasureTokenLength(LocEnd, *SM);
    Result += " {\n    struct ";
    Result += RCDecl->getName();
	// Note: We don't name the field decl. This simplifies the "codegen" for
	// accessing a superclasses instance variables (and is similar to what gcc
	// does internally). The unnamed struct field feature is enabled with
	// -fms-extensions. If the struct definition were "inlined", we wouldn't
	// need to use this switch. That said, I don't want to inline the def.
    Result += ";\n};\n";
    Rewrite.ReplaceText(LocStart, endBuf-startBuf, 
                        Result.c_str(), Result.size());
  }
  // Mark this struct as having been generated.
  if (!ObjCSynthesizedStructs.insert(CDecl))
  assert(false && "struct already synthesize- SynthesizeObjCInternalStruct");
}

// RewriteObjCMethodsMetaData - Rewrite methods metadata for instance or
/// class methods.
void RewriteTest::RewriteObjCMethodsMetaData(instmeth_iterator MethodBegin,
                                             instmeth_iterator MethodEnd,
                                             bool IsInstanceMethod,
                                             const char *prefix,
                                             const char *ClassName,
                                             std::string &Result) {
  if (MethodBegin == MethodEnd) return;
  
  static bool objc_impl_method = false;
  if (!objc_impl_method) {
    /* struct _objc_method {
       SEL _cmd;
       char *method_types;
       void *_imp;
       }
     */
    Result += "\nstruct _objc_method {\n";
    Result += "\tSEL _cmd;\n";
    Result += "\tchar *method_types;\n";
    Result += "\tvoid *_imp;\n";
    Result += "};\n";
    
    /* struct _objc_method_list {
     struct _objc_method_list *next_method;
     int method_count;
     struct _objc_method method_list[];
     }
     */
    Result += "\nstruct _objc_method_list {\n";
    Result += "\tstruct _objc_method_list *next_method;\n";
    Result += "\tint method_count;\n";
    Result += "\tstruct _objc_method method_list[];\n};\n";
    objc_impl_method = true;
  }
  
  // Build _objc_method_list for class's methods if needed
  Result += "\nstatic struct _objc_method_list _OBJC_";
  Result += prefix;
  Result += IsInstanceMethod ? "INSTANCE" : "CLASS";
  Result += "_METHODS_";
  Result += ClassName;
  Result += " __attribute__ ((section (\"__OBJC, __";
  Result += IsInstanceMethod ? "inst" : "cls";
  Result += "_meth\")))= ";
  Result += "{\n\t0, " + utostr(MethodEnd-MethodBegin) + "\n";

  Result += "\t,{{(SEL)\"";
  Result += (*MethodBegin)->getSelector().getName().c_str();
  std::string MethodTypeString;
  Context->getObjCEncodingForMethodDecl(*MethodBegin, MethodTypeString);
  Result += "\", \"";
  Result += MethodTypeString;
  Result += "\", ";
  Result += MethodInternalNames[*MethodBegin];
  Result += "}\n";
  for (++MethodBegin; MethodBegin != MethodEnd; ++MethodBegin) {
    Result += "\t  ,{(SEL)\"";
    Result += (*MethodBegin)->getSelector().getName().c_str();
    std::string MethodTypeString;
    Context->getObjCEncodingForMethodDecl(*MethodBegin, MethodTypeString);
    Result += "\", \"";
    Result += MethodTypeString;
    Result += "\", ";
    Result += MethodInternalNames[*MethodBegin];
    Result += "}\n";
  }
  Result += "\t }\n};\n";
}

/// RewriteObjCProtocolsMetaData - Rewrite protocols meta-data.
void RewriteTest::RewriteObjCProtocolsMetaData(ObjCProtocolDecl **Protocols,
                                               int NumProtocols,
                                               const char *prefix,
                                               const char *ClassName,
                                               std::string &Result) {
  static bool objc_protocol_methods = false;
  if (NumProtocols > 0) {
    for (int i = 0; i < NumProtocols; i++) {
      ObjCProtocolDecl *PDecl = Protocols[i];
      // Output struct protocol_methods holder of method selector and type.
      if (!objc_protocol_methods &&
          (PDecl->getNumInstanceMethods() > 0 
           || PDecl->getNumClassMethods() > 0)) {
        /* struct protocol_methods {
         SEL _cmd;
         char *method_types;
         }
         */
        Result += "\nstruct protocol_methods {\n";
        Result += "\tSEL _cmd;\n";
        Result += "\tchar *method_types;\n";
        Result += "};\n";
        
        /* struct _objc_protocol_method_list {
         int protocol_method_count;
         struct protocol_methods protocols[];
         }
         */
        Result += "\nstruct _objc_protocol_method_list {\n";
        Result += "\tint protocol_method_count;\n";
        Result += "\tstruct protocol_methods protocols[];\n};\n";
        objc_protocol_methods = true;
      }
      
      int NumMethods = PDecl->getNumInstanceMethods();
      if(NumMethods > 0) {
        Result += "\nstatic struct _objc_protocol_method_list "
               "_OBJC_PROTOCOL_INSTANCE_METHODS_";
        Result += PDecl->getName();
        Result += " __attribute__ ((section (\"__OBJC, __cat_inst_meth\")))= "
          "{\n\t" + utostr(NumMethods) + "\n";
        
        // Output instance methods declared in this protocol.
        for (ObjCProtocolDecl::instmeth_iterator I = PDecl->instmeth_begin(), 
             E = PDecl->instmeth_end(); I != E; ++I) {
          if (I == PDecl->instmeth_begin())
            Result += "\t  ,{{(SEL)\"";
          else
            Result += "\t  ,{(SEL)\"";
          Result += (*I)->getSelector().getName().c_str();
          std::string MethodTypeString;
          Context->getObjCEncodingForMethodDecl((*I), MethodTypeString);
          Result += "\", \"";
          Result += MethodTypeString;
          Result += "\"}\n";
        }
        Result += "\t }\n};\n";
      }
      
      // Output class methods declared in this protocol.
      NumMethods = PDecl->getNumClassMethods();
      if (NumMethods > 0) {
        Result += "\nstatic struct _objc_protocol_method_list "
               "_OBJC_PROTOCOL_CLASS_METHODS_";
        Result += PDecl->getName();
        Result += " __attribute__ ((section (\"__OBJC, __cat_cls_meth\")))= "
               "{\n\t";
        Result += utostr(NumMethods);
        Result += "\n";
        
        // Output instance methods declared in this protocol.
        for (ObjCProtocolDecl::classmeth_iterator I = PDecl->classmeth_begin(), 
             E = PDecl->classmeth_end(); I != E; ++I) {
          if (I == PDecl->classmeth_begin())
            Result += "\t  ,{{(SEL)\"";
          else
            Result += "\t  ,{(SEL)\"";
          Result += (*I)->getSelector().getName().c_str();
          std::string MethodTypeString;
          Context->getObjCEncodingForMethodDecl((*I), MethodTypeString);
          Result += "\", \"";
          Result += MethodTypeString;
          Result += "\"}\n";
        }
        Result += "\t }\n};\n";
      }
      // Output:
      /* struct _objc_protocol {
       // Objective-C 1.0 extensions
       struct _objc_protocol_extension *isa;
       char *protocol_name;
       struct _objc_protocol **protocol_list;
       struct _objc_protocol_method_list *instance_methods;
       struct _objc_protocol_method_list *class_methods;
       };  
       */
      static bool objc_protocol = false;
      if (!objc_protocol) {
        Result += "\nstruct _objc_protocol {\n";
        Result += "\tstruct _objc_protocol_extension *isa;\n";
        Result += "\tchar *protocol_name;\n";
        Result += "\tstruct _objc_protocol **protocol_list;\n";
        Result += "\tstruct _objc_protocol_method_list *instance_methods;\n";
        Result += "\tstruct _objc_protocol_method_list *class_methods;\n";
        Result += "};\n";
        
        /* struct _objc_protocol_list {
         struct _objc_protocol_list *next;
         int    protocol_count;
         struct _objc_protocol *class_protocols[];
         }
         */
        Result += "\nstruct _objc_protocol_list {\n";
        Result += "\tstruct _objc_protocol_list *next;\n";
        Result += "\tint    protocol_count;\n";
        Result += "\tstruct _objc_protocol *class_protocols[];\n";
        Result += "};\n";
        objc_protocol = true;
      }
      
      Result += "\nstatic struct _objc_protocol _OBJC_PROTOCOL_";
      Result += PDecl->getName();
      Result += " __attribute__ ((section (\"__OBJC, __protocol\")))= "
        "{\n\t0, \"";
      Result += PDecl->getName();
      Result += "\", 0, ";
      if (PDecl->getNumInstanceMethods() > 0) {
        Result += "&_OBJC_PROTOCOL_INSTANCE_METHODS_";
        Result += PDecl->getName();
        Result += ", ";
      }
      else
        Result += "0, ";
      if (PDecl->getNumClassMethods() > 0) {
        Result += "&_OBJC_PROTOCOL_CLASS_METHODS_";
        Result += PDecl->getName();
        Result += "\n";
      }
      else
        Result += "0\n";
      Result += "};\n";
    }
    // Output the top lovel protocol meta-data for the class.
    Result += "\nstatic struct _objc_protocol_list _OBJC_";
    Result += prefix;
    Result += "_PROTOCOLS_";
    Result += ClassName;
    Result += " __attribute__ ((section (\"__OBJC, __cat_cls_meth\")))= "
      "{\n\t0, ";
    Result += utostr(NumProtocols);
    Result += "\n";
    
    Result += "\t,{&_OBJC_PROTOCOL_";
    Result += Protocols[0]->getName();
    Result += " \n";
    
    for (int i = 1; i < NumProtocols; i++) {
      ObjCProtocolDecl *PDecl = Protocols[i];
      Result += "\t ,&_OBJC_PROTOCOL_";
      Result += PDecl->getName();
      Result += "\n";
    }
    Result += "\t }\n};\n";
  }  
}

/// RewriteObjCCategoryImplDecl - Rewrite metadata for each category 
/// implementation.
void RewriteTest::RewriteObjCCategoryImplDecl(ObjCCategoryImplDecl *IDecl,
                                              std::string &Result) {
  ObjCInterfaceDecl *ClassDecl = IDecl->getClassInterface();
  // Find category declaration for this implementation.
  ObjCCategoryDecl *CDecl;
  for (CDecl = ClassDecl->getCategoryList(); CDecl; 
       CDecl = CDecl->getNextClassCategory())
    if (CDecl->getIdentifier() == IDecl->getIdentifier())
      break;
  
  std::string FullCategoryName = ClassDecl->getName();
  FullCategoryName += '_';
  FullCategoryName += IDecl->getName();
    
  // Build _objc_method_list for class's instance methods if needed
  RewriteObjCMethodsMetaData(IDecl->instmeth_begin(), IDecl->instmeth_end(),
                             true, "CATEGORY_", FullCategoryName.c_str(),
                             Result);
  
  // Build _objc_method_list for class's class methods if needed
  RewriteObjCMethodsMetaData(IDecl->classmeth_begin(), IDecl->classmeth_end(),
                             false, "CATEGORY_", FullCategoryName.c_str(),
                             Result);
  
  // Protocols referenced in class declaration?
  // Null CDecl is case of a category implementation with no category interface
  if (CDecl)
    RewriteObjCProtocolsMetaData(CDecl->getReferencedProtocols(),
                                 CDecl->getNumReferencedProtocols(),
                                 "CATEGORY",
                                 FullCategoryName.c_str(), Result);
  
  /* struct _objc_category {
   char *category_name;
   char *class_name;
   struct _objc_method_list *instance_methods;
   struct _objc_method_list *class_methods;
   struct _objc_protocol_list *protocols;
   // Objective-C 1.0 extensions
   uint32_t size;     // sizeof (struct _objc_category)
   struct _objc_property_list *instance_properties;  // category's own 
                                                     // @property decl.
   };   
   */
  
  static bool objc_category = false;
  if (!objc_category) {
    Result += "\nstruct _objc_category {\n";
    Result += "\tchar *category_name;\n";
    Result += "\tchar *class_name;\n";
    Result += "\tstruct _objc_method_list *instance_methods;\n";
    Result += "\tstruct _objc_method_list *class_methods;\n";
    Result += "\tstruct _objc_protocol_list *protocols;\n";
    Result += "\tunsigned int size;\n";   
    Result += "\tstruct _objc_property_list *instance_properties;\n";
    Result += "};\n";
    objc_category = true;
  }
  Result += "\nstatic struct _objc_category _OBJC_CATEGORY_";
  Result += FullCategoryName;
  Result += " __attribute__ ((section (\"__OBJC, __category\")))= {\n\t\"";
  Result += IDecl->getName();
  Result += "\"\n\t, \"";
  Result += ClassDecl->getName();
  Result += "\"\n";
  
  if (IDecl->getNumInstanceMethods() > 0) {
    Result += "\t, (struct _objc_method_list *)"
           "&_OBJC_CATEGORY_INSTANCE_METHODS_";
    Result += FullCategoryName;
    Result += "\n";
  }
  else
    Result += "\t, 0\n";
  if (IDecl->getNumClassMethods() > 0) {
    Result += "\t, (struct _objc_method_list *)"
           "&_OBJC_CATEGORY_CLASS_METHODS_";
    Result += FullCategoryName;
    Result += "\n";
  }
  else
    Result += "\t, 0\n";
  
  if (CDecl && CDecl->getNumReferencedProtocols() > 0) {
    Result += "\t, (struct _objc_protocol_list *)&_OBJC_CATEGORY_PROTOCOLS_"; 
    Result += FullCategoryName;
    Result += "\n";
  }
  else
    Result += "\t, 0\n";
  Result += "\t, sizeof(struct _objc_category), 0\n};\n";
}

/// SynthesizeIvarOffsetComputation - This rutine synthesizes computation of
/// ivar offset.
void RewriteTest::SynthesizeIvarOffsetComputation(ObjCImplementationDecl *IDecl, 
                                                  ObjCIvarDecl *ivar, 
                                                  std::string &Result) {
  Result += "offsetof(struct ";
  Result += IDecl->getName();
  Result += ", ";
  Result += ivar->getName();
  Result += ")";
}

//===----------------------------------------------------------------------===//
// Meta Data Emission
//===----------------------------------------------------------------------===//

void RewriteTest::RewriteObjCClassMetaData(ObjCImplementationDecl *IDecl,
                                           std::string &Result) {
  ObjCInterfaceDecl *CDecl = IDecl->getClassInterface();
  
  // Explictly declared @interface's are already synthesized.
  if (CDecl->ImplicitInterfaceDecl()) {
    // FIXME: Implementation of a class with no @interface (legacy) doese not 
    // produce correct synthesis as yet.
    SynthesizeObjCInternalStruct(CDecl, Result);
  }
  
  // Build _objc_ivar_list metadata for classes ivars if needed
  int NumIvars = IDecl->getImplDeclNumIvars() > 0 
                   ? IDecl->getImplDeclNumIvars() 
                   : (CDecl ? CDecl->getNumInstanceVariables() : 0);
  if (NumIvars > 0) {
    static bool objc_ivar = false;
    if (!objc_ivar) {
      /* struct _objc_ivar {
          char *ivar_name;
          char *ivar_type;
          int ivar_offset;
        };  
       */
      Result += "\nstruct _objc_ivar {\n";
      Result += "\tchar *ivar_name;\n";
      Result += "\tchar *ivar_type;\n";
      Result += "\tint ivar_offset;\n";
      Result += "};\n";
      
      /* struct _objc_ivar_list {
       int ivar_count;
       struct _objc_ivar ivar_list[];
       };  
       */
      Result += "\nstruct _objc_ivar_list {\n";
      Result += "\tint ivar_count;\n";
      Result += "\tstruct _objc_ivar ivar_list[];\n};\n";
      objc_ivar = true;
    }

    Result += "\nstatic struct _objc_ivar_list _OBJC_INSTANCE_VARIABLES_";
    Result += IDecl->getName();
    Result += " __attribute__ ((section (\"__OBJC, __instance_vars\")))= "
      "{\n\t";
    Result += utostr(NumIvars);
    Result += "\n";
    
    ObjCInterfaceDecl::ivar_iterator IVI, IVE;
    if (IDecl->getImplDeclNumIvars() > 0) {
      IVI = IDecl->ivar_begin();
      IVE = IDecl->ivar_end();
    } else {
      IVI = CDecl->ivar_begin();
      IVE = CDecl->ivar_end();
    }
    Result += "\t,{{\"";
    Result += (*IVI)->getName();
    Result += "\", \"";
    std::string StrEncoding;
    Context->getObjCEncodingForType((*IVI)->getType(), StrEncoding);
    Result += StrEncoding;
    Result += "\", ";
    SynthesizeIvarOffsetComputation(IDecl, *IVI, Result);
    Result += "}\n";
    for (++IVI; IVI != IVE; ++IVI) {
      Result += "\t  ,{\"";
      Result += (*IVI)->getName();
      Result += "\", \"";
      std::string StrEncoding;
      Context->getObjCEncodingForType((*IVI)->getType(), StrEncoding);
      Result += StrEncoding;
      Result += "\", ";
      SynthesizeIvarOffsetComputation(IDecl, (*IVI), Result);
      Result += "}\n";
    }
    
    Result += "\t }\n};\n";
  }
  
  // Build _objc_method_list for class's instance methods if needed
  RewriteObjCMethodsMetaData(IDecl->instmeth_begin(), IDecl->instmeth_end(), 
                             true, "", IDecl->getName(), Result);
  
  // Build _objc_method_list for class's class methods if needed
  RewriteObjCMethodsMetaData(IDecl->classmeth_begin(), IDecl->classmeth_end(),
                             false, "", IDecl->getName(), Result);
    
  // Protocols referenced in class declaration?
  RewriteObjCProtocolsMetaData(CDecl->getReferencedProtocols(), 
                               CDecl->getNumIntfRefProtocols(),
                               "CLASS", CDecl->getName(), Result);
    
  
  // Declaration of class/meta-class metadata
  /* struct _objc_class {
   struct _objc_class *isa; // or const char *root_class_name when metadata
   const char *super_class_name;
   char *name;
   long version;
   long info;
   long instance_size;
   struct _objc_ivar_list *ivars;
   struct _objc_method_list *methods;
   struct objc_cache *cache;
   struct objc_protocol_list *protocols;
   const char *ivar_layout;
   struct _objc_class_ext  *ext;
   };  
  */
  static bool objc_class = false;
  if (!objc_class) {
    Result += "\nstruct _objc_class {\n";
    Result += "\tstruct _objc_class *isa;\n";
    Result += "\tconst char *super_class_name;\n";
    Result += "\tchar *name;\n";
    Result += "\tlong version;\n";
    Result += "\tlong info;\n";
    Result += "\tlong instance_size;\n";
    Result += "\tstruct _objc_ivar_list *ivars;\n";
    Result += "\tstruct _objc_method_list *methods;\n";
    Result += "\tstruct objc_cache *cache;\n";
    Result += "\tstruct _objc_protocol_list *protocols;\n";
    Result += "\tconst char *ivar_layout;\n";
    Result += "\tstruct _objc_class_ext  *ext;\n";
    Result += "};\n";
    objc_class = true;
  }
  
  // Meta-class metadata generation.
  ObjCInterfaceDecl *RootClass = 0;
  ObjCInterfaceDecl *SuperClass = CDecl->getSuperClass();
  while (SuperClass) {
    RootClass = SuperClass;
    SuperClass = SuperClass->getSuperClass();
  }
  SuperClass = CDecl->getSuperClass();
  
  Result += "\nstatic struct _objc_class _OBJC_METACLASS_";
  Result += CDecl->getName();
  Result += " __attribute__ ((section (\"__OBJC, __meta_class\")))= "
  "{\n\t(struct _objc_class *)\"";
  Result += (RootClass ? RootClass->getName() : CDecl->getName());
  Result += "\"";

  if (SuperClass) {
    Result += ", \"";
    Result += SuperClass->getName();
    Result += "\", \"";
    Result += CDecl->getName();
    Result += "\"";
  }
  else {
    Result += ", 0, \"";
    Result += CDecl->getName();
    Result += "\"";
  }
  // Set 'ivars' field for root class to 0. ObjC1 runtime does not use it.
  // 'info' field is initialized to CLS_META(2) for metaclass
  Result += ", 0,2, sizeof(struct _objc_class), 0";
  if (IDecl->getNumClassMethods() > 0) {
    Result += "\n\t, &_OBJC_CLASS_METHODS_";
    Result += IDecl->getName();
    Result += "\n"; 
  }
  else
    Result += ", 0\n";
  if (CDecl->getNumIntfRefProtocols() > 0) {
    Result += "\t,0, &_OBJC_CLASS_PROTOCOLS_";
    Result += CDecl->getName();
    Result += ",0,0\n";
  }
  else
    Result += "\t,0,0,0,0\n";
  Result += "};\n";
  
  // class metadata generation.
  Result += "\nstatic struct _objc_class _OBJC_CLASS_";
  Result += CDecl->getName();
  Result += " __attribute__ ((section (\"__OBJC, __class\")))= "
            "{\n\t&_OBJC_METACLASS_";
  Result += CDecl->getName();
  if (SuperClass) {
    Result += ", \"";
    Result += SuperClass->getName();
    Result += "\", \"";
    Result += CDecl->getName();
    Result += "\"";
  }
  else {
    Result += ", 0, \"";
    Result += CDecl->getName();
    Result += "\"";
  }
  // 'info' field is initialized to CLS_CLASS(1) for class
  Result += ", 0,1";
  if (!ObjCSynthesizedStructs.count(CDecl))
    Result += ",0";
  else {
    // class has size. Must synthesize its size.
    Result += ",sizeof(struct ";
    Result += CDecl->getName();
    Result += ")";
  }
  if (NumIvars > 0) {
    Result += ", &_OBJC_INSTANCE_VARIABLES_";
    Result += CDecl->getName();
    Result += "\n\t";
  }
  else
    Result += ",0";
  if (IDecl->getNumInstanceMethods() > 0) {
    Result += ", &_OBJC_INSTANCE_METHODS_";
    Result += CDecl->getName();
    Result += ", 0\n\t"; 
  }
  else
    Result += ",0,0";
  if (CDecl->getNumIntfRefProtocols() > 0) {
    Result += ", &_OBJC_CLASS_PROTOCOLS_";
    Result += CDecl->getName();
    Result += ", 0,0\n";
  }
  else
    Result += ",0,0,0\n";
  Result += "};\n";
}

/// RewriteImplementations - This routine rewrites all method implementations
/// and emits meta-data.

void RewriteTest::RewriteImplementations(std::string &Result) {
  int ClsDefCount = ClassImplementation.size();
  int CatDefCount = CategoryImplementation.size();
  
  if (ClsDefCount == 0 && CatDefCount == 0)
    return;
  // Rewrite implemented methods
  for (int i = 0; i < ClsDefCount; i++)
    RewriteImplementationDecl(ClassImplementation[i]);
  
  for (int i = 0; i < CatDefCount; i++)
    RewriteImplementationDecl(CategoryImplementation[i]);
  
  // This is needed for use of offsetof
  Result += "#include <stddef.h>\n";
    
  // For each implemented class, write out all its meta data.
  for (int i = 0; i < ClsDefCount; i++)
    RewriteObjCClassMetaData(ClassImplementation[i], Result);
  
  // For each implemented category, write out all its meta data.
  for (int i = 0; i < CatDefCount; i++)
    RewriteObjCCategoryImplDecl(CategoryImplementation[i], Result);
  
  // Write objc_symtab metadata
  /*
   struct _objc_symtab
   {
   long sel_ref_cnt;
   SEL *refs;
   short cls_def_cnt;
   short cat_def_cnt;
   void *defs[cls_def_cnt + cat_def_cnt];
   }; 
   */
  
  Result += "\nstruct _objc_symtab {\n";
  Result += "\tlong sel_ref_cnt;\n";
  Result += "\tSEL *refs;\n";
  Result += "\tshort cls_def_cnt;\n";
  Result += "\tshort cat_def_cnt;\n";
  Result += "\tvoid *defs[" + utostr(ClsDefCount + CatDefCount)+ "];\n";
  Result += "};\n\n";
  
  Result += "static struct _objc_symtab "
         "_OBJC_SYMBOLS __attribute__((section (\"__OBJC, __symbols\")))= {\n";
  Result += "\t0, 0, " + utostr(ClsDefCount) 
            + ", " + utostr(CatDefCount) + "\n";
  for (int i = 0; i < ClsDefCount; i++) {
    Result += "\t,&_OBJC_CLASS_";
    Result += ClassImplementation[i]->getName();
    Result += "\n";
  }
  
  for (int i = 0; i < CatDefCount; i++) {
    Result += "\t,&_OBJC_CATEGORY_";
    Result += CategoryImplementation[i]->getClassInterface()->getName();
    Result += "_";
    Result += CategoryImplementation[i]->getName();
    Result += "\n";
  }
  
  Result += "};\n\n";
  
  // Write objc_module metadata
  
  /*
   struct _objc_module {
    long version;
    long size;
    const char *name;
    struct _objc_symtab *symtab;
   }
  */
  
  Result += "\nstruct _objc_module {\n";
  Result += "\tlong version;\n";
  Result += "\tlong size;\n";
  Result += "\tconst char *name;\n";
  Result += "\tstruct _objc_symtab *symtab;\n";
  Result += "};\n\n";
  Result += "static struct _objc_module "
    "_OBJC_MODULES __attribute__ ((section (\"__OBJC, __module_info\")))= {\n";
  Result += "\t" + utostr(OBJC_ABI_VERSION) + 
  ", sizeof(struct _objc_module), \"\", &_OBJC_SYMBOLS\n";
  Result += "};\n\n";
  
}

