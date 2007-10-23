//===--- RewriteTest.cpp - Playground for the code rewriter ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under the
// University of Illinois Open Source License. See LICENSE.TXT for details.
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
using namespace clang;


namespace {
  class RewriteTest : public ASTConsumer {
    Rewriter Rewrite;
    ASTContext *Context;
    SourceManager *SM;
    unsigned MainFileID;
    SourceLocation LastIncLoc;
    llvm::SmallVector<ObjcImplementationDecl *, 8> ClassImplementation;
    llvm::SmallVector<ObjcCategoryImplDecl *, 8> CategoryImplementation;
    
    FunctionDecl *MsgSendFunctionDecl;
    FunctionDecl *GetClassFunctionDecl;
    
    static const int OBJC_ABI_VERSION =7 ;
  public:
    void Initialize(ASTContext &context, unsigned mainFileID) {
      Context = &context;
      SM = &Context->SourceMgr;
      MainFileID = mainFileID;
      MsgSendFunctionDecl = 0;
      Rewrite.setSourceMgr(Context->SourceMgr);
    }
    
    virtual void HandleTopLevelDecl(Decl *D);

    void HandleDeclInMainFile(Decl *D);
    void RewriteInclude(SourceLocation Loc);
    
    void RewriteFunctionBody(Stmt *S);
    void RewriteAtEncode(ObjCEncodeExpr *Exp);
    void RewriteForwardClassDecl(ObjcClassDecl *Dcl);
    void RewriteMessageExpr(ObjCMessageExpr *Exp);
    
    void WriteObjcClassMetaData(ObjcImplementationDecl *IDecl);
    void WriteObjcMetaData();
    
    ~RewriteTest();
  };
}

ASTConsumer *clang::CreateCodeRewriterTest() { return new RewriteTest(); }

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
    if (FD->getIdentifier() == &Context->Idents.get("objc_msgSend"))
      MsgSendFunctionDecl = FD;
    else if (FD->getIdentifier() == &Context->Idents.get("objc_getClass"))
      GetClassFunctionDecl = FD;
  }
  if (SM->getDecomposedFileLoc(Loc).first == MainFileID)
    return HandleDeclInMainFile(D);

  RewriteInclude(Loc);
}

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

/// HandleDeclInMainFile - This is called for each top-level decl defined in the
/// main file of the input.
void RewriteTest::HandleDeclInMainFile(Decl *D) {
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    if (Stmt *Body = FD->getBody())
      RewriteFunctionBody(Body);
  
  if (ObjcImplementationDecl *CI = dyn_cast<ObjcImplementationDecl>(D))
    ClassImplementation.push_back(CI);
  else if (ObjcCategoryImplDecl *CI = dyn_cast<ObjcCategoryImplDecl>(D))
    CategoryImplementation.push_back(CI);
  else if (ObjcClassDecl *CD = dyn_cast<ObjcClassDecl>(D))
    RewriteForwardClassDecl(CD);
  // Nothing yet.
}


void RewriteTest::RewriteFunctionBody(Stmt *S) {
  // Otherwise, just rewrite all children.
  for (Stmt::child_iterator CI = S->child_begin(), E = S->child_end();
       CI != E; ++CI)
    if (*CI)
      RewriteFunctionBody(*CI);
      
  // Handle specific things.
  if (ObjCEncodeExpr *AtEncode = dyn_cast<ObjCEncodeExpr>(S))
    return RewriteAtEncode(AtEncode);
    
  if (ObjCMessageExpr *MessExpr = dyn_cast<ObjCMessageExpr>(S))
    return RewriteMessageExpr(MessExpr);
  
}
 
void RewriteTest::RewriteAtEncode(ObjCEncodeExpr *Exp) {
  // Create a new string expression.
  QualType StrType = Context->getPointerType(Context->CharTy);
  Expr *Replacement = new StringLiteral("foo", 3, false, StrType, 
                                        SourceLocation(), SourceLocation());
  Rewrite.ReplaceStmt(Exp, Replacement);
  delete Replacement;
}


void RewriteTest::RewriteMessageExpr(ObjCMessageExpr *Exp) {
  assert(MsgSendFunctionDecl && "Can't find objc_msgSend() decl");
  //Exp->dumpPretty();
  //printf("\n");
  
  // Synthesize a call to objc_msgSend().
  
  // Get the type, we will need to reference it in a couple spots.
  QualType msgSendType = MsgSendFunctionDecl->getType();
  
  // Create a reference to the objc_msgSend() declaration.
  DeclRefExpr *DRE = new DeclRefExpr(MsgSendFunctionDecl, msgSendType,
                                     SourceLocation());
                                     
  // Now, we cast the reference to a pointer to the objc_msgSend type.
  QualType pToFunc = Context->getPointerType(msgSendType);                                  
  ImplicitCastExpr *ICE = new ImplicitCastExpr(pToFunc, DRE);
  
  const FunctionType *FT = msgSendType->getAsFunctionType();
  CallExpr *CE = new CallExpr(ICE, 0, 0, FT->getResultType(), 
                              SourceLocation());
  Rewrite.ReplaceStmt(Exp, CE);
  //Exp->dump();
  //CE->dump();
}

void RewriteTest::RewriteForwardClassDecl(ObjcClassDecl *ClassDecl) {
  int numDecls = ClassDecl->getNumForwardDecls();
  ObjcInterfaceDecl **ForwardDecls = ClassDecl->getForwardDecls();
  
  // Get the start location and compute the semi location.
  SourceLocation startLoc = ClassDecl->getLocation();
  const char *startBuf = SM->getCharacterData(startLoc);
  const char *semiPtr = startBuf;
  while (semiPtr && (*semiPtr != ';')) semiPtr++;

  // Translate to typedef's that forward reference structs with the same name
  // as the class. As a convenience, we include the original declaration
  // as a comment.
  std::string typedefString;
  typedefString += "// ";
  typedefString.append(startBuf, semiPtr-startBuf+1);
  typedefString += "\n";
  for (int i = 0; i < numDecls; i++) {
    ObjcInterfaceDecl *ForwardDecl = ForwardDecls[i];
    typedefString += "typedef struct ";
    typedefString += ForwardDecl->getName();
    typedefString += " ";
    typedefString += ForwardDecl->getName();
    typedefString += ";\n";
  }
  
  // Replace the @class with typedefs corresponding to the classes.
  Rewrite.ReplaceText(startLoc, semiPtr-startBuf+1, 
                      typedefString.c_str(), typedefString.size());
}

void RewriteTest::WriteObjcClassMetaData(ObjcImplementationDecl *IDecl) {
  ObjcInterfaceDecl *CDecl = IDecl->getClassInterface();
  
  // Build _objc_ivar_list metadata for classes ivars if needed
  int NumIvars = IDecl->getImplDeclNumIvars() > 0 
                   ? IDecl->getImplDeclNumIvars() 
                   : (CDecl ? CDecl->getIntfDeclNumIvars() : 0);
  
  if (NumIvars > 0) {
    static bool objc_ivar = false;
    if (!objc_ivar) {
      /* struct _objc_ivar {
          char *ivar_name;
          char *ivar_type;
          int ivar_offset;
        };  
       */
      printf("\nstruct _objc_ivar {\n");
      printf("\tchar *ivar_name;\n");
      printf("\tchar *ivar_type;\n");
      printf("\tint ivar_offset;\n");
      printf("};\n");
      objc_ivar = true;
    }

    /* struct _objc_ivar_list {
        int ivar_count;
        struct _objc_ivar ivar_list[ivar_count];
     };  
    */
    printf("\nstatic struct {\n");
    printf("\tint ivar_count;\n");
    printf("\tstruct _objc_ivar ivar_list[%d];\n", NumIvars);
    printf("} _OBJC_INSTANCE_VARIABLES_%s "
      "__attribute__ ((section (\"__OBJC, __instance_vars\")))= "
      "{\n\t%d\n",IDecl->getName(), 
           NumIvars);
    ObjcIvarDecl **Ivars = IDecl->getImplDeclIVars() 
                             ? IDecl->getImplDeclIVars() 
                             : CDecl->getIntfDeclIvars();
    for (int i = 0; i < NumIvars; i++)
      // TODO: 1) ivar names may have to go to another section. 2) encode
      // ivar_type type of each ivar . 3) compute and add ivar offset.
      printf("\t,\"%s\", \"\", 0\n", Ivars[i]->getName());
    printf("};\n");
  }
  
  // Build _objc_method for class's instance or class methods metadata if needed
  static bool objc_impl_method = false;
  if (IDecl->getNumInstanceMethods() > 0 || IDecl->getNumClassMethods() > 0) {
    if (!objc_impl_method) {
      /* struct _objc_method {
       SEL _cmd;
       char *method_types;
       void *_imp;
       }
       */
      printf("\nstruct _objc_method {\n");
      printf("\tSEL _cmd;\n");
      printf("\tchar *method_types;\n");
      printf("\tvoid *_imp;\n");
      printf("};\n");
      objc_impl_method = true;
    }
  }
  // Build _objc_method_list for class's instance methods if needed
  if (IDecl->getNumInstanceMethods() > 0) {
    int NumMethods = IDecl->getNumInstanceMethods();
    /* struct _objc_method_list {
         struct _objc_method_list *next_method;
         int method_count;
         struct _objc_method method_list[method_count];
       }
    */
    printf("\nstatic struct {\n");
    printf("\tstruct _objc_method_list *next_method;\n");
    printf("\tint method_count;\n");
    printf("\tstruct _objc_method method_list[%d];\n", NumMethods);
    printf("} _OBJC_INSTANCE_METHODS_%s "
           "__attribute__ ((section (\"__OBJC, __inst_meth\")))= "
           "{\n\t0, %d\n", IDecl->getName(), NumMethods);
    ObjcMethodDecl **Methods = IDecl->getInstanceMethods();
    for (int i = 0; i < NumMethods; i++)
      // TODO: 1) method selector name may hav to go into their own section
      // 2) encode method types for use here (which may have to go into 
      // __meth_var_types section, 3) Need method address as 3rd initializer.
      printf("\t,(SEL)\"%s\", \"\", 0\n", 
             Methods[i]->getSelector().getName().c_str());
    printf("};\n");
  }
  
  // Build _objc_method_list for class's class methods if needed
  if (IDecl->getNumClassMethods() > 0) {
    int NumMethods = IDecl->getNumClassMethods();
    /* struct _objc_method_list {
     struct _objc_method_list *next_method;
     int method_count;
     struct _objc_method method_list[method_count];
     }
     */
    printf("\nstatic struct {\n");
    printf("\tstruct _objc_method_list *next_method;\n");
    printf("\tint method_count;\n");
    printf("\tstruct _objc_method method_list[%d];\n", NumMethods);
    printf("} _OBJC_CLASS_METHODS_%s "
           "__attribute__ ((section (\"__OBJC, __cls_meth\")))= "
           "{\n\t0, %d\n", IDecl->getName(), NumMethods);
    ObjcMethodDecl **Methods = IDecl->getClassMethods();
    for (int i = 0; i < NumMethods; i++)
      // TODO: 1) method selector name may hav to go into their own section
      // 2) encode method types for use here (which may have to go into 
      // __meth_var_types section, 3) Need method address as 3rd initializer.
      printf("\t,(SEL)\"%s\", \"\", 0\n", 
             Methods[i]->getSelector().getName().c_str());
    printf("};\n");
  }
  
  // Protocols referenced in class declaration?
  static bool objc_protocol_methods = false;
  int NumProtocols = CDecl->getNumIntfRefProtocols();
  if (NumProtocols > 0) {
    ObjcProtocolDecl **Protocols = CDecl->getReferencedProtocols();
    for (int i = 0; i < NumProtocols; i++) {
      ObjcProtocolDecl *PDecl = Protocols[i];
      // Output struct protocol_methods holder of method selector and type.
      if (!objc_protocol_methods &&
          (PDecl->getNumInstanceMethods() > 0 
           || PDecl->getNumClassMethods() > 0)) {
        /* struct protocol_methods {
         SEL _cmd;
         char *method_types;
         }
         */
        printf("\nstruct protocol_methods {\n");
        printf("\tSEL _cmd;\n");
        printf("\tchar *method_types;\n");
        printf("};\n");
        objc_protocol_methods = true;
      }
      // Output instance methods declared in this protocol.
      /* struct _objc_protocol_method_list {
       int protocol_method_count;
       struct protocol_methods protocols[protocol_method_count];
       }
       */      
      int NumMethods = PDecl->getNumInstanceMethods();
      if (NumMethods > 0) {
        printf("\nstatic struct {\n");
        printf("\tint protocol_method_count;\n");
        printf("\tstruct protocol_methods protocols[%d];\n", NumMethods);
        printf("} _OBJC_PROTOCOL_INSTANCE_METHODS_%s "
               "__attribute__ ((section (\"__OBJC, __cat_inst_meth\")))= "
               "{\n\t%d\n",PDecl->getName(), NumMethods);
        ObjcMethodDecl **Methods = PDecl->getInstanceMethods();
        for (int i = 0; i < NumMethods; i++)
          // TODO: 1) method selector name may hav to go into their own section
          // 2) encode method types for use here (which may have to go into 
          // __meth_var_types section.
          printf("\t,(SEL)\"%s\", \"\"\n", 
                 Methods[i]->getSelector().getName().c_str());
        printf("};\n");
      }
      
      // Output class methods declared in this protocol.
      NumMethods = PDecl->getNumClassMethods();
      if (NumMethods > 0) {
        printf("\nstatic struct {\n");
        printf("\tint protocol_method_count;\n");
        printf("\tstruct protocol_methods protocols[%d];\n", NumMethods);
        printf("} _OBJC_PROTOCOL_CLASS_METHODS_%s "
               "__attribute__ ((section (\"__OBJC, __cat_cls_meth\")))= "
               "{\n\t%d\n",PDecl->getName(), NumMethods);
        ObjcMethodDecl **Methods = PDecl->getClassMethods();
        for (int i = 0; i < NumMethods; i++)
          // TODO: 1) method selector name may hav to go into their own section
          // 2) encode method types for use here (which may have to go into 
          // __meth_var_types section.
          printf("\t,(SEL)\"%s\", \"\"\n", 
                 Methods[i]->getSelector().getName().c_str());
        printf("};\n");
      }
      // Output:
      /* struct _objc_protocol {
       // Objective-C 1.0 extensions
       struct _objc_protocol_extension *isa;
       char *protocol_name;
       struct _objc_protocol **protocol_list;
       struct _objc__method_prototype_list *instance_methods;
       struct _objc__method_prototype_list *class_methods;
       };  
       */
      static bool objc_protocol = false;
      if (!objc_protocol) {
        printf("\nstruct _objc_protocol {\n");
        printf("\tstruct _objc_protocol_extension *isa;\n");
        printf("\tchar *protocol_name;\n");
        printf("\tstruct _objc_protocol **protocol_list;\n");
        printf("\tstruct _objc__method_prototype_list *instance_methods;\n");
        printf("\tstruct _objc__method_prototype_list *class_methods;\n");
        printf("};\n");
        objc_protocol = true;
      }
      
      printf("\nstatic struct _objc_protocol _OBJC_PROTOCOL_%s "
             "__attribute__ ((section (\"__OBJC, __protocol\")))= "
             "{\n\t0, \"%s\", 0, ", PDecl->getName(), PDecl->getName());
      if (PDecl->getInstanceMethods() > 0)
        printf("(struct _objc__method_prototype_list *)"
               "&_OBJC_PROTOCOL_INSTANCE_METHODS_%s, ", PDecl->getName());
      else
        printf("0, ");
      if (PDecl->getClassMethods() > 0)
        printf("(struct _objc__method_prototype_list *)"
               "&_OBJC_PROTOCOL_CLASS_METHODS_%s\n", PDecl->getName());
      else
        printf("0\n");
      printf("};\n");
    }
  }
  if (NumProtocols > 0) {
    // Output the top lovel protocol meta-data for the class.
    /* struct _objc_protocol_list {
     struct _objc_protocol_list *next;
     int    protocol_count;
     struct _objc_protocol *class_protocols[protocol_count];
     }
    */
    printf("\nstatic struct {\n");
    printf("\tstruct _objc_protocol_list *next;\n");
    printf("\tint    protocol_count;\n");
    printf("\tstruct _objc_protocol *class_protocols[%d];\n"
           "} _OBJC_CLASS_PROTOCOLS_%s "
           "__attribute__ ((section (\"__OBJC, __cat_cls_meth\")))= "
           "{\n\t0, %d\n",NumProtocols, CDecl->getName(), NumProtocols);
    ObjcProtocolDecl **Protocols = CDecl->getReferencedProtocols();
    for (int i = 0; i < NumProtocols; i++) {
      ObjcProtocolDecl *PDecl = Protocols[i];
      printf("\t,&_OBJC_PROTOCOL_%s \n", 
           PDecl->getName());
    }
    printf("};\n");
  }
  
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
    printf("\nstruct _objc_class {\n");
    printf("\tstruct _objc_class *isa;\n");
    printf("\tconst char *super_class_name;\n");
    printf("\tchar *name;\n");
    printf("\tlong version;\n");
    printf("\tlong info;\n");
    printf("\tlong instance_size;\n");
    printf("\tstruct _objc_ivar_list *ivars;\n");
    printf("\tstruct _objc_method_list *methods;\n");
    printf("\tstruct objc_cache *cache;\n");
    printf("\tstruct _objc_protocol_list *protocols;\n");
    printf("\tconst char *ivar_layout;\n");
    printf("\tstruct _objc_class_ext  *ext;\n");
    printf("};\n");
    objc_class = true;
  }
  
  // Meta-class metadata generation.
  ObjcInterfaceDecl *RootClass = 0;
  ObjcInterfaceDecl *SuperClass = CDecl->getSuperClass();
  while (SuperClass) {
    RootClass = SuperClass;
    SuperClass = SuperClass->getSuperClass();
  }
  SuperClass = CDecl->getSuperClass();
  
  printf("\nstatic struct _objc_class _OBJC_METACLASS_%s "
         "__attribute__ ((section (\"__OBJC, __meta_class\")))= "
         "{\n\t(struct _objc_class *)\"%s\"", 
         CDecl->getName(), RootClass ? RootClass->getName() 
                                     :  CDecl->getName());
  if (SuperClass)
    printf(", \"%s\", \"%s\"", SuperClass->getName(), CDecl->getName());
  else
    printf(", 0, \"%s\"", CDecl->getName());
  // FIXME: better way of getting size struct _objc_class (48)
  // TODO: 'ivars' field for root class is currently set to 0.
  // 'info' field is initialized to CLS_META(2) for metaclass
  printf(", 0,2,48,0");
  if (CDecl->getNumClassMethods() > 0)
    printf("\n\t, (struct _objc_method_list *)&_OBJC_CLASS_METHODS_%s\n", 
           CDecl->getName());
  else
    printf(", 0\n");
  printf("\t,0,0,0,0\n");
  printf("};\n");
  
  // class metadata generation.
  printf("\nstatic struct _objc_class _OBJC_CLASS_%s "
         "__attribute__ ((section (\"__OBJC, __class\")))= "
         "{\n\t&_OBJC_METACLASS_%s", CDecl->getName(), CDecl->getName());
  if (SuperClass)
    printf(", \"%s\", \"%s\"", SuperClass->getName(), CDecl->getName());
  else
    printf(", 0, \"%s\"", CDecl->getName());
  // 'info' field is initialized to CLS_CLASS(1) for class
  // TODO: instance_size is curently set to 0.
  printf(", 0,1,0");
  if (NumIvars > 0)
    printf(", (struct _objc_ivar_list *)&_OBJC_INSTANCE_VARIABLES_%s\n\t", 
           CDecl->getName());
  else
    printf(",0");
  if (IDecl->getNumInstanceMethods() > 0)
    printf(", (struct _objc_method_list*)&_OBJC_INSTANCE_METHODS_%s, 0\n\t", 
           CDecl->getName());
  else
    printf(",0,0");
  if (NumProtocols > 0)
    printf(", (struct _objc_protocol_list*)&_OBJC_CLASS_PROTOCOLS_%s, 0,0\n", 
           CDecl->getName());
  else
    printf(",0,0,0\n");
  printf("};\n");
}

void RewriteTest::WriteObjcMetaData() {
  int ClsDefCount = ClassImplementation.size();
  int CatDefCount = CategoryImplementation.size();
  if (ClsDefCount == 0 && CatDefCount == 0)
    return;
  
  // For each defined class, write out all its meta data.
  for (int i = 0; i < ClsDefCount; i++)
    WriteObjcClassMetaData(ClassImplementation[i]);
  
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
  
  printf("\nstruct _objc_symtab {\n");
  printf("\tlong sel_ref_cnt;\n");
  printf("\tSEL *refs;\n");
  printf("\tshort cls_def_cnt;\n");
  printf("\tshort cat_def_cnt;\n");
  printf("\tvoid *defs[%d];\n", ClsDefCount + CatDefCount);
  printf("};\n\n");
  
  printf("static struct _objc_symtab "
         "_OBJC_SYMBOLS __attribute__ ((section (\"__OBJC, __symbols\")))= {\n");
  printf("\t0, 0, %d, %d\n", ClsDefCount, CatDefCount);
  for (int i = 0; i < ClsDefCount; i++)
    printf("\t,&_OBJC_CLASS_%s\n", ClassImplementation[i]->getName());
  
  for (int i = 0; i < CatDefCount; i++)
    printf("\t,&_OBJC_CATEGORY_%s_%s\n", 
           CategoryImplementation[i]->getClassInterface()->getName(), 
           CategoryImplementation[i]->getName());
  
  printf("};\n\n");
  
  // Write objc_module metadata
  
  /*
   struct _objc_module {
    long version;
    long size;
    const char *name;
    struct _objc_symtab *symtab;
   }
  */
  
  printf("\nstruct _objc_module {\n");
  printf("\tlong version;\n");
  printf("\tlong size;\n");
  printf("\tconst char *name;\n");
  printf("\tstruct _objc_symtab *symtab;");
  printf("};\n\n");
  printf("static struct _objc_module "
    "_OBJC_MODULES __attribute__ ((section (\"__OBJC, __module_info\")))= {\n");
  printf("\t%d, %d, \"\", &_OBJC_SYMBOLS\n", OBJC_ABI_VERSION, 16);
  printf("};\n\n");
}

RewriteTest::~RewriteTest() {
  // Get the top-level buffer that this corresponds to.
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
  
  // Get the buffer corresponding to MainFileID.  If we haven't changed it, then
  // we are done.
  if (const RewriteBuffer *RewriteBuf = 
          Rewrite.getRewriteBufferFor(MainFileID)) {
    printf("Changed:\n");
    std::string S(RewriteBuf->begin(), RewriteBuf->end());
    printf("%s\n", S.c_str());
  } else {
    printf("No changes\n");
  }
  // Rewrite Objective-c meta data*
  WriteObjcMetaData();
}
