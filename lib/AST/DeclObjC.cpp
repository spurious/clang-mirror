//===--- DeclObjC.cpp - ObjC Declaration AST Node Implementation ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Objective-C related Decl classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclObjC.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
using namespace clang;

//===----------------------------------------------------------------------===//
// ObjC Decl Allocation/Deallocation Method Implementations
//===----------------------------------------------------------------------===//

ObjCMethodDecl *ObjCMethodDecl::Create(ASTContext &C,
                                       SourceLocation beginLoc, 
                                       SourceLocation endLoc,
                                       Selector SelInfo, QualType T,
                                       Decl *contextDecl,
                                       bool isInstance,
                                       bool isVariadic,
                                       bool isSynthesized,
                                       ImplementationControl impControl) {
  void *Mem = C.getAllocator().Allocate<ObjCMethodDecl>();
  return new (Mem) ObjCMethodDecl(beginLoc, endLoc,
                                  SelInfo, T, contextDecl,
                                  isInstance, 
                                  isVariadic, isSynthesized, impControl);
}

ObjCMethodDecl::~ObjCMethodDecl() {  
  delete [] ParamInfo;
}

void ObjCMethodDecl::Destroy(ASTContext& C) {
  if (Body) Body->Destroy(C);
  if (SelfDecl) SelfDecl->Destroy(C);
  
  for (param_iterator I=param_begin(), E=param_end(); I!=E; ++I)
    if (*I) (*I)->Destroy(C);
  
  Decl::Destroy(C);
}

ObjCInterfaceDecl *ObjCInterfaceDecl::Create(ASTContext &C,
                                             SourceLocation atLoc,
                                             IdentifierInfo *Id, 
                                             SourceLocation ClassLoc,
                                             bool ForwardDecl, bool isInternal){
  void *Mem = C.getAllocator().Allocate<ObjCInterfaceDecl>();
  return new (Mem) ObjCInterfaceDecl(atLoc, Id, ClassLoc, ForwardDecl,
                                     isInternal);
}

ObjCInterfaceDecl::~ObjCInterfaceDecl() {
  delete [] Ivars;
  delete [] InstanceMethods;
  delete [] ClassMethods;
  delete [] PropertyDecl;
  // FIXME: CategoryList?
}

void ObjCInterfaceDecl::Destroy(ASTContext& C) {  
  for (ivar_iterator I=ivar_begin(), E=ivar_end(); I!=E; ++I)
    if (*I) (*I)->Destroy(C);
  
  for (instmeth_iterator I=instmeth_begin(), E=instmeth_end(); I!=E; ++I)
    if (*I) (*I)->Destroy(C);
  
  for (classmeth_iterator I=classmeth_begin(), E=classmeth_end(); I!=E; ++I)
    if (*I) (*I)->Destroy(C);

  // FIXME: Because there is no clear ownership
  //  role between ObjCInterfaceDecls and the ObjCPropertyDecls that they
  //  reference, we destroy ObjCPropertyDecls in ~TranslationUnit.

  Decl::Destroy(C);
}


ObjCIvarDecl *ObjCIvarDecl::Create(ASTContext &C, SourceLocation L,
                                   IdentifierInfo *Id, QualType T, 
                                   AccessControl ac, Expr *BW) {
  void *Mem = C.getAllocator().Allocate<ObjCIvarDecl>();
  return new (Mem) ObjCIvarDecl(L, Id, T, ac, BW);
}


ObjCAtDefsFieldDecl
*ObjCAtDefsFieldDecl::Create(ASTContext &C, SourceLocation L,
                             IdentifierInfo *Id, QualType T, Expr *BW) {
  void *Mem = C.getAllocator().Allocate<ObjCAtDefsFieldDecl>();
  return new (Mem) ObjCAtDefsFieldDecl(L, Id, T, BW);
}

void ObjCAtDefsFieldDecl::Destroy(ASTContext& C) {
  this->~ObjCAtDefsFieldDecl();
  C.getAllocator().Deallocate((void *)this); 
}

ObjCProtocolDecl *ObjCProtocolDecl::Create(ASTContext &C,
                                           SourceLocation L, 
                                           IdentifierInfo *Id) {
  void *Mem = C.getAllocator().Allocate<ObjCProtocolDecl>();
  return new (Mem) ObjCProtocolDecl(L, Id);
}

ObjCProtocolDecl::~ObjCProtocolDecl() {
  delete [] InstanceMethods;
  delete [] ClassMethods;
  delete [] PropertyDecl;
}

void ObjCProtocolDecl::Destroy(ASTContext& C) {
  
  // Referenced Protocols are not owned, so don't Destroy them.
  
  for (instmeth_iterator I=instmeth_begin(), E=instmeth_end(); I!=E; ++I)
    if (*I) (*I)->Destroy(C);
  
  for (classmeth_iterator I=classmeth_begin(), E=classmeth_end(); I!=E; ++I)
    if (*I) (*I)->Destroy(C);
  
  // FIXME: Because there is no clear ownership
  //  role between ObjCProtocolDecls and the ObjCPropertyDecls that they
  //  reference, we destroy ObjCPropertyDecls in ~TranslationUnit.
  
  Decl::Destroy(C);
}


ObjCClassDecl *ObjCClassDecl::Create(ASTContext &C,
                                     SourceLocation L,
                                     ObjCInterfaceDecl **Elts, unsigned nElts) {
  void *Mem = C.getAllocator().Allocate<ObjCClassDecl>();
  return new (Mem) ObjCClassDecl(L, Elts, nElts);
}

ObjCClassDecl::~ObjCClassDecl() {
  delete [] ForwardDecls;
}

void ObjCClassDecl::Destroy(ASTContext& C) {
  
  // FIXME: There is no clear ownership policy now for referenced
  //  ObjCInterfaceDecls.  Some of them can be forward declarations that
  //  are never later defined (in which case the ObjCClassDecl owns them)
  //  or the ObjCInterfaceDecl later becomes a real definition later.  Ideally
  //  we should have separate objects for forward declarations and definitions,
  //  obviating this problem.  Because of this situation, referenced
  //  ObjCInterfaceDecls are destroyed in ~TranslationUnit.
  
  Decl::Destroy(C);
}

ObjCForwardProtocolDecl *
ObjCForwardProtocolDecl::Create(ASTContext &C,
                                SourceLocation L, 
                                ObjCProtocolDecl **Elts, unsigned NumElts) {
  void *Mem = C.getAllocator().Allocate<ObjCForwardProtocolDecl>();
  return new (Mem) ObjCForwardProtocolDecl(L, Elts, NumElts);
}

ObjCForwardProtocolDecl::~ObjCForwardProtocolDecl() {
  delete [] ReferencedProtocols;
}

ObjCCategoryDecl *ObjCCategoryDecl::Create(ASTContext &C,
                                           SourceLocation L,
                                           IdentifierInfo *Id) {
  void *Mem = C.getAllocator().Allocate<ObjCCategoryDecl>();
  return new (Mem) ObjCCategoryDecl(L, Id);
}

ObjCCategoryImplDecl *
ObjCCategoryImplDecl::Create(ASTContext &C,
                             SourceLocation L,IdentifierInfo *Id,
                             ObjCInterfaceDecl *ClassInterface) {
  void *Mem = C.getAllocator().Allocate<ObjCCategoryImplDecl>();
  return new (Mem) ObjCCategoryImplDecl(L, Id, ClassInterface);
}

ObjCImplementationDecl *
ObjCImplementationDecl::Create(ASTContext &C,
                               SourceLocation L,
                               IdentifierInfo *Id,
                               ObjCInterfaceDecl *ClassInterface,
                               ObjCInterfaceDecl *SuperDecl) {
  void *Mem = C.getAllocator().Allocate<ObjCImplementationDecl>();
  return new (Mem) ObjCImplementationDecl(L, Id, ClassInterface, SuperDecl);
}

ObjCCompatibleAliasDecl *
ObjCCompatibleAliasDecl::Create(ASTContext &C,
                                SourceLocation L,
                                IdentifierInfo *Id, 
                                ObjCInterfaceDecl* AliasedClass) {
  void *Mem = C.getAllocator().Allocate<ObjCCompatibleAliasDecl>();
  return new (Mem) ObjCCompatibleAliasDecl(L, Id, AliasedClass);
}

ObjCPropertyDecl *ObjCPropertyDecl::Create(ASTContext &C,
                                           SourceLocation L,
                                           IdentifierInfo *Id,
                                           QualType T,
                                           PropertyControl propControl) {
  void *Mem = C.getAllocator().Allocate<ObjCPropertyDecl>();
  return new (Mem) ObjCPropertyDecl(L, Id, T);
}

//===----------------------------------------------------------------------===//
// Objective-C Decl Implementation
//===----------------------------------------------------------------------===//

void ObjCMethodDecl::createImplicitParams(ASTContext &Context) {
  QualType selfTy;
  if (isInstance()) {
    // There may be no interface context due to error in declaration
    // of the interface (which has been reported). Recover gracefully.
    if (ObjCInterfaceDecl *OID = getClassInterface()) {
      selfTy = Context.getObjCInterfaceType(OID);
      selfTy = Context.getPointerType(selfTy);
    } else {
      selfTy = Context.getObjCIdType();
    }
  } else // we have a factory method.
    selfTy = Context.getObjCClassType();

  SelfDecl = ImplicitParamDecl::Create(Context, this, 
                                       SourceLocation(), 
                                       &Context.Idents.get("self"),
                                       selfTy, 0);

  CmdDecl = ImplicitParamDecl::Create(Context, this, 
                                      SourceLocation(), 
                                      &Context.Idents.get("_cmd"), 
                                      Context.getObjCSelType(), 0);
}

void ObjCMethodDecl::setMethodParams(ParmVarDecl **NewParamInfo,
                                     unsigned NumParams) {
  assert(ParamInfo == 0 && "Already has param info!");

  // Zero params -> null pointer.
  if (NumParams) {
    ParamInfo = new ParmVarDecl*[NumParams];
    memcpy(ParamInfo, NewParamInfo, sizeof(ParmVarDecl*)*NumParams);
    NumMethodParams = NumParams;
  }
}

/// FindPropertyDeclaration - Finds declaration of the property given its name
/// in 'PropertyId' and returns it. It returns 0, if not found.
///
ObjCPropertyDecl *
  ObjCInterfaceDecl::FindPropertyDeclaration(IdentifierInfo *PropertyId) const {
  for (ObjCInterfaceDecl::classprop_iterator I = classprop_begin(),
       E = classprop_end(); I != E; ++I) {
    ObjCPropertyDecl *property = *I;
    if (property->getIdentifier() == PropertyId)
      return property;
  }
  // Look through categories.
  for (ObjCCategoryDecl *Category = getCategoryList();
       Category; Category = Category->getNextClassCategory()) {
    ObjCPropertyDecl *property = Category->FindPropertyDeclaration(PropertyId);
    if (property)
      return property;
  }
  // Look through protocols.
  for (ObjCInterfaceDecl::protocol_iterator I = protocol_begin(),
       E = protocol_end(); I != E; ++I) {
    ObjCProtocolDecl *Protocol = *I;
    ObjCPropertyDecl *property = Protocol->FindPropertyDeclaration(PropertyId);
    if (property)
      return property;
  }
  if (getSuperClass())
    return getSuperClass()->FindPropertyDeclaration(PropertyId);
  return 0;
}

/// FindCategoryDeclaration - Finds category declaration in the list of
/// categories for this class and returns it. Name of the category is passed
/// in 'CategoryId'. If category not found, return 0;
///
ObjCCategoryDecl *
  ObjCInterfaceDecl::FindCategoryDeclaration(IdentifierInfo *CategoryId) const {
    for (ObjCCategoryDecl *Category = getCategoryList();
         Category; Category = Category->getNextClassCategory())
      if (Category->getIdentifier() == CategoryId)
        return Category;
    return 0;
}

/// FindIvarDeclaration - Find an Ivar declaration in this class given its
/// name in 'IvarId'. On failure to find, return 0;
///
ObjCIvarDecl *
  ObjCInterfaceDecl::FindIvarDeclaration(IdentifierInfo *IvarId) const {
  for (ObjCInterfaceDecl::ivar_iterator IVI = ivar_begin(), 
       IVE = ivar_end(); IVI != IVE; ++IVI) {
    ObjCIvarDecl* Ivar = (*IVI);
    if (Ivar->getIdentifier() == IvarId)
      return Ivar;
  }
  if (getSuperClass())
    return getSuperClass()->FindIvarDeclaration(IvarId);
  return 0;
}

/// ObjCAddInstanceVariablesToClass - Inserts instance variables
/// into ObjCInterfaceDecl's fields.
///
void ObjCInterfaceDecl::addInstanceVariablesToClass(ObjCIvarDecl **ivars,
                                                    unsigned numIvars,
                                                    SourceLocation RBrac) {
  NumIvars = numIvars;
  if (numIvars) {
    Ivars = new ObjCIvarDecl*[numIvars];
    memcpy(Ivars, ivars, numIvars*sizeof(ObjCIvarDecl*));
  }
  setLocEnd(RBrac);
}

/// ObjCAddInstanceVariablesToClassImpl - Checks for correctness of Instance 
/// Variables (Ivars) relative to what declared in @implementation;s class. 
/// Ivars into ObjCImplementationDecl's fields.
///
void ObjCImplementationDecl::ObjCAddInstanceVariablesToClassImpl(
                               ObjCIvarDecl **ivars, unsigned numIvars) {
  NumIvars = numIvars;
  if (numIvars) {
    Ivars = new ObjCIvarDecl*[numIvars];
    memcpy(Ivars, ivars, numIvars*sizeof(ObjCIvarDecl*));
  }
}

/// addMethods - Insert instance and methods declarations into
/// ObjCInterfaceDecl's InsMethods and ClsMethods fields.
///
void ObjCInterfaceDecl::addMethods(ObjCMethodDecl **insMethods, 
                                   unsigned numInsMembers,
                                   ObjCMethodDecl **clsMethods,
                                   unsigned numClsMembers,
                                   SourceLocation endLoc) {
  NumInstanceMethods = numInsMembers;
  if (numInsMembers) {
    InstanceMethods = new ObjCMethodDecl*[numInsMembers];
    memcpy(InstanceMethods, insMethods, numInsMembers*sizeof(ObjCMethodDecl*));
  }
  NumClassMethods = numClsMembers;
  if (numClsMembers) {
    ClassMethods = new ObjCMethodDecl*[numClsMembers];
    memcpy(ClassMethods, clsMethods, numClsMembers*sizeof(ObjCMethodDecl*));
  }
  AtEndLoc = endLoc;
}

/// addProperties - Insert property declaration AST nodes into
/// ObjCInterfaceDecl's PropertyDecl field.
///
void ObjCInterfaceDecl::addProperties(ObjCPropertyDecl **Properties, 
                                      unsigned NumProperties) {
  if (NumProperties == 0) return;
  
  NumPropertyDecl = NumProperties;
  PropertyDecl = new ObjCPropertyDecl*[NumProperties];
  memcpy(PropertyDecl, Properties, NumProperties*sizeof(ObjCPropertyDecl*));
}                                   

/// mergeProperties - Adds properties to the end of list of current properties
/// for this class.

void ObjCInterfaceDecl::mergeProperties(ObjCPropertyDecl **Properties, 
                                        unsigned NumNewProperties) {
  if (NumNewProperties == 0) return;
  
  if (PropertyDecl) {
    ObjCPropertyDecl **newPropertyDecl =  
      new ObjCPropertyDecl*[NumNewProperties + NumPropertyDecl];
    ObjCPropertyDecl **buf = newPropertyDecl;
    // put back original properties in buffer.
    memcpy(buf, PropertyDecl, NumPropertyDecl*sizeof(ObjCPropertyDecl*));
    // Add new properties to this buffer.
    memcpy(buf+NumPropertyDecl, Properties, 
           NumNewProperties*sizeof(ObjCPropertyDecl*));
    delete[] PropertyDecl;
    PropertyDecl = newPropertyDecl;
    NumPropertyDecl += NumNewProperties;
  }
  else {
    addProperties(Properties, NumNewProperties);
  }
}

static void 
addPropertyMethods(Decl *D,
                   ASTContext &Context,
                   ObjCPropertyDecl *property,
                   llvm::SmallVector<ObjCMethodDecl*, 32> &insMethods) {
  ObjCMethodDecl *GetterDecl, *SetterDecl = 0;

  if (ObjCInterfaceDecl *OID = dyn_cast<ObjCInterfaceDecl>(D)) {
    GetterDecl = OID->getInstanceMethod(property->getGetterName());
    if (!property->isReadOnly())
      SetterDecl = OID->getInstanceMethod(property->getSetterName());
  } else if (ObjCCategoryDecl *OCD = dyn_cast<ObjCCategoryDecl>(D)) {
    GetterDecl = OCD->getInstanceMethod(property->getGetterName());
    if (!property->isReadOnly())
      SetterDecl = OCD->getInstanceMethod(property->getSetterName());
  } else {
    ObjCProtocolDecl *OPD = cast<ObjCProtocolDecl>(D);
    GetterDecl = OPD->getInstanceMethod(property->getGetterName());
    if (!property->isReadOnly())
      SetterDecl = OPD->getInstanceMethod(property->getSetterName());
  }

  // FIXME: The synthesized property we set here is misleading. We
  // almost always synthesize these methods unless the user explicitly
  // provided prototypes (which is odd, but allowed). Sema should be
  // typechecking that the declarations jive in that situation (which
  // it is not currently).

  // Find the default getter and if one not found, add one.
  if (!GetterDecl) {
    // No instance method of same name as property getter name was found.
    // Declare a getter method and add it to the list of methods 
    // for this class.
    GetterDecl = 
      ObjCMethodDecl::Create(Context, property->getLocation(), 
                             property->getLocation(), 
                             property->getGetterName(), 
                             property->getType(),
                             D,
                             true, false, true, ObjCMethodDecl::Required);
    insMethods.push_back(GetterDecl);
  }
  property->setGetterMethodDecl(GetterDecl);

  // Skip setter if property is read-only.
  if (property->isReadOnly())
    return;

  // Find the default setter and if one not found, add one.
  if (!SetterDecl) {
    // No instance method of same name as property setter name was found.
    // Declare a setter method and add it to the list of methods 
    // for this class.
    SetterDecl =
      ObjCMethodDecl::Create(Context, property->getLocation(), 
                             property->getLocation(), 
                             property->getSetterName(), 
                             Context.VoidTy,
                             D,
                             true, false, true, ObjCMethodDecl::Required);
    insMethods.push_back(SetterDecl);

    // Invent the arguments for the setter. We don't bother making a
    // nice name for the argument.
    ParmVarDecl *Argument = ParmVarDecl::Create(Context, 
                                               SetterDecl,
                                               SourceLocation(), 
                                               property->getIdentifier(),
                                               property->getType(),
                                               VarDecl::None,
                                               0, 0);
    SetterDecl->setMethodParams(&Argument, 1);
  }
  property->setSetterMethodDecl(SetterDecl);
}

/// addPropertyMethods - Goes through list of properties declared in this class
/// and builds setter/getter method declartions depending on the setter/getter
/// attributes of the property.
///
void ObjCInterfaceDecl::addPropertyMethods(
       ASTContext &Context,
       ObjCPropertyDecl *property,
       llvm::SmallVector<ObjCMethodDecl*, 32> &insMethods) {
  ::addPropertyMethods(this, Context, property, insMethods);
}

/// addPropertyMethods - Goes through list of properties declared in this class
/// and builds setter/getter method declartions depending on the setter/getter
/// attributes of the property.
///
void ObjCCategoryDecl::addPropertyMethods(
       ASTContext &Context,
       ObjCPropertyDecl *property,
       llvm::SmallVector<ObjCMethodDecl*, 32> &insMethods) {
  ::addPropertyMethods(this, Context, property, insMethods);
}

/// addPropertyMethods - Goes through list of properties declared in this class
/// and builds setter/getter method declartions depending on the setter/getter
/// attributes of the property.
///
void ObjCProtocolDecl::addPropertyMethods(
       ASTContext &Context,
       ObjCPropertyDecl *property,
       llvm::SmallVector<ObjCMethodDecl*, 32> &insMethods) {
  ::addPropertyMethods(this, Context, property, insMethods);
}

/// addProperties - Insert property declaration AST nodes into
/// ObjCProtocolDecl's PropertyDecl field.
///
void ObjCProtocolDecl::addProperties(ObjCPropertyDecl **Properties, 
                                     unsigned NumProperties) {
  if (NumProperties == 0) return;
  
  NumPropertyDecl = NumProperties;
  PropertyDecl = new ObjCPropertyDecl*[NumProperties];
  memcpy(PropertyDecl, Properties, NumProperties*sizeof(ObjCPropertyDecl*));
}

/// addProperties - Insert property declaration AST nodes into
/// ObjCCategoryDecl's PropertyDecl field.
///
void ObjCCategoryDecl::addProperties(ObjCPropertyDecl **Properties, 
                                     unsigned NumProperties) {
  if (NumProperties == 0) return;
  
  NumPropertyDecl = NumProperties;
  PropertyDecl = new ObjCPropertyDecl*[NumProperties];
  memcpy(PropertyDecl, Properties, NumProperties*sizeof(ObjCPropertyDecl*));
}

/// addMethods - Insert instance and methods declarations into
/// ObjCProtocolDecl's ProtoInsMethods and ProtoClsMethods fields.
///
void ObjCProtocolDecl::addMethods(ObjCMethodDecl **insMethods, 
                                  unsigned numInsMembers,
                                  ObjCMethodDecl **clsMethods,
                                  unsigned numClsMembers,
                                  SourceLocation endLoc) {
  NumInstanceMethods = numInsMembers;
  if (numInsMembers) {
    InstanceMethods = new ObjCMethodDecl*[numInsMembers];
    memcpy(InstanceMethods, insMethods, numInsMembers*sizeof(ObjCMethodDecl*));
  }
  NumClassMethods = numClsMembers;
  if (numClsMembers) {
    ClassMethods = new ObjCMethodDecl*[numClsMembers];
    memcpy(ClassMethods, clsMethods, numClsMembers*sizeof(ObjCMethodDecl*));
  }
  AtEndLoc = endLoc;
}



/// addMethods - Insert instance and methods declarations into
/// ObjCCategoryDecl's CatInsMethods and CatClsMethods fields.
///
void ObjCCategoryDecl::addMethods(ObjCMethodDecl **insMethods, 
                                  unsigned numInsMembers,
                                  ObjCMethodDecl **clsMethods,
                                  unsigned numClsMembers,
                                  SourceLocation endLoc) {
  NumInstanceMethods = numInsMembers;
  if (numInsMembers) {
    InstanceMethods = new ObjCMethodDecl*[numInsMembers];
    memcpy(InstanceMethods, insMethods, numInsMembers*sizeof(ObjCMethodDecl*));
  }
  NumClassMethods = numClsMembers;
  if (numClsMembers) {
    ClassMethods = new ObjCMethodDecl*[numClsMembers];
    memcpy(ClassMethods, clsMethods, numClsMembers*sizeof(ObjCMethodDecl*));
  }
  AtEndLoc = endLoc;
}

/// FindPropertyDeclaration - Finds declaration of the property given its name
/// in 'PropertyId' and returns it. It returns 0, if not found.
///
ObjCPropertyDecl *
ObjCCategoryDecl::FindPropertyDeclaration(IdentifierInfo *PropertyId) const {
  for (ObjCCategoryDecl::classprop_iterator I = classprop_begin(),
       E = classprop_end(); I != E; ++I) {
    ObjCPropertyDecl *property = *I;
    if (property->getIdentifier() == PropertyId)
      return property;
  }
  return 0;
}

/// FindPropertyDeclaration - Finds declaration of the property given its name
/// in 'PropertyId' and returns it. It returns 0, if not found.
///
ObjCPropertyDecl *
ObjCProtocolDecl::FindPropertyDeclaration(IdentifierInfo *PropertyId) const {
  for (ObjCProtocolDecl::classprop_iterator I = classprop_begin(),
       E = classprop_end(); I != E; ++I) {
    ObjCPropertyDecl *property = *I;
    if (property->getIdentifier() == PropertyId)
      return property;
  }
  return 0;
}

ObjCIvarDecl *ObjCInterfaceDecl::lookupInstanceVariable(
  IdentifierInfo *ID, ObjCInterfaceDecl *&clsDeclared) {
  ObjCInterfaceDecl* ClassDecl = this;
  while (ClassDecl != NULL) {
    for (ivar_iterator I = ClassDecl->ivar_begin(), E = ClassDecl->ivar_end();
         I != E; ++I) {
      if ((*I)->getIdentifier() == ID) {
        clsDeclared = ClassDecl;
        return *I;
      }
    }
    ClassDecl = ClassDecl->getSuperClass();
  }
  return NULL;
}

/// lookupInstanceMethod - This method returns an instance method by looking in
/// the class, its categories, and its super classes (using a linear search).
ObjCMethodDecl *ObjCInterfaceDecl::lookupInstanceMethod(Selector Sel) {
  ObjCInterfaceDecl* ClassDecl = this;
  ObjCMethodDecl *MethodDecl = 0;
  
  while (ClassDecl != NULL) {
    if ((MethodDecl = ClassDecl->getInstanceMethod(Sel)))
      return MethodDecl;
      
    // Didn't find one yet - look through protocols.
    const ObjCList<ObjCProtocolDecl> &Protocols =
      ClassDecl->getReferencedProtocols();
    for (ObjCList<ObjCProtocolDecl>::iterator I = Protocols.begin(),
         E = Protocols.end(); I != E; ++I)
      if ((MethodDecl = (*I)->getInstanceMethod(Sel)))
        return MethodDecl;
    
    // Didn't find one yet - now look through categories.
    ObjCCategoryDecl *CatDecl = ClassDecl->getCategoryList();
    while (CatDecl) {
      if ((MethodDecl = CatDecl->getInstanceMethod(Sel)))
        return MethodDecl;
      CatDecl = CatDecl->getNextClassCategory();
    }
    ClassDecl = ClassDecl->getSuperClass();
  }
  return NULL;
}

// lookupClassMethod - This method returns a class method by looking in the
// class, its categories, and its super classes (using a linear search).
ObjCMethodDecl *ObjCInterfaceDecl::lookupClassMethod(Selector Sel) {
  ObjCInterfaceDecl* ClassDecl = this;
  ObjCMethodDecl *MethodDecl = 0;

  while (ClassDecl != NULL) {
    if ((MethodDecl = ClassDecl->getClassMethod(Sel)))
      return MethodDecl;

    // Didn't find one yet - look through protocols.
    for (ObjCInterfaceDecl::protocol_iterator I = ClassDecl->protocol_begin(),
         E = ClassDecl->protocol_end(); I != E; ++I)
      if ((MethodDecl = (*I)->getClassMethod(Sel)))
        return MethodDecl;
    
    // Didn't find one yet - now look through categories.
    ObjCCategoryDecl *CatDecl = ClassDecl->getCategoryList();
    while (CatDecl) {
      if ((MethodDecl = CatDecl->getClassMethod(Sel)))
        return MethodDecl;
      CatDecl = CatDecl->getNextClassCategory();
    }
    ClassDecl = ClassDecl->getSuperClass();
  }
  return NULL;
}

/// getInstanceMethod - This method returns an instance method by
/// looking in the class implementation. Unlike interfaces, we don't
/// look outside the implementation.
ObjCMethodDecl *ObjCImplementationDecl::getInstanceMethod(Selector Sel) const {
  for (instmeth_iterator I = instmeth_begin(), E = instmeth_end(); I != E; ++I)
    if ((*I)->getSelector() == Sel)
      return *I;
  return NULL;
}

/// getClassMethod - This method returns a class method by looking in
/// the class implementation. Unlike interfaces, we don't look outside
/// the implementation.
ObjCMethodDecl *ObjCImplementationDecl::getClassMethod(Selector Sel) const {
  for (classmeth_iterator I = classmeth_begin(), E = classmeth_end();
       I != E; ++I)
    if ((*I)->getSelector() == Sel)
      return *I;
  return NULL;
}

// lookupInstanceMethod - This method returns an instance method by looking in
// the class implementation. Unlike interfaces, we don't look outside the
// implementation.
ObjCMethodDecl *ObjCCategoryImplDecl::getInstanceMethod(Selector Sel) const {
  for (instmeth_iterator I = instmeth_begin(), E = instmeth_end(); I != E; ++I)
    if ((*I)->getSelector() == Sel)
      return *I;
  return NULL;
}

// lookupClassMethod - This method returns an instance method by looking in
// the class implementation. Unlike interfaces, we don't look outside the
// implementation.
ObjCMethodDecl *ObjCCategoryImplDecl::getClassMethod(Selector Sel) const {
  for (classmeth_iterator I = classmeth_begin(), E = classmeth_end();
       I != E; ++I)
    if ((*I)->getSelector() == Sel)
      return *I;
  return NULL;
}

// lookupInstanceMethod - Lookup a instance method in the protocol and protocols
// it inherited.
ObjCMethodDecl *ObjCProtocolDecl::lookupInstanceMethod(Selector Sel) {
  ObjCMethodDecl *MethodDecl = NULL;
  
  if ((MethodDecl = getInstanceMethod(Sel)))
    return MethodDecl;
    
  for (protocol_iterator I = protocol_begin(), E = protocol_end(); I != E; ++I)
    if ((MethodDecl = (*I)->getInstanceMethod(Sel)))
      return MethodDecl;
  return NULL;
}

// lookupInstanceMethod - Lookup a class method in the protocol and protocols
// it inherited.
ObjCMethodDecl *ObjCProtocolDecl::lookupClassMethod(Selector Sel) {
  ObjCMethodDecl *MethodDecl = NULL;

  if ((MethodDecl = getClassMethod(Sel)))
    return MethodDecl;
    
  for (protocol_iterator I = protocol_begin(), E = protocol_end(); I != E; ++I)
    if ((MethodDecl = (*I)->getClassMethod(Sel)))
      return MethodDecl;
  return NULL;
}

/// getSynthesizedMethodSize - Compute size of synthesized method name
/// as done be the rewrite.
///
unsigned ObjCMethodDecl::getSynthesizedMethodSize() const {
  // syntesized method name is a concatenation of -/+[class-name selector]
  // Get length of this name.
  unsigned length = 3;  // _I_ or _C_
  length += strlen(getClassInterface()->getName()) +1; // extra for _
  NamedDecl *MethodContext = getMethodContext();
  if (ObjCCategoryImplDecl *CID = 
      dyn_cast<ObjCCategoryImplDecl>(MethodContext))
    length += strlen(CID->getName()) +1;
  length += getSelector().getName().size(); // selector name
  return length; 
}

ObjCInterfaceDecl *ObjCMethodDecl::getClassInterface() {
  if (ObjCInterfaceDecl *ID = dyn_cast<ObjCInterfaceDecl>(MethodContext))
    return ID;
  if (ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(MethodContext))
    return CD->getClassInterface();
  if (ObjCImplementationDecl *IMD = 
        dyn_cast<ObjCImplementationDecl>(MethodContext))
    return IMD->getClassInterface();
  if (ObjCCategoryImplDecl *CID = dyn_cast<ObjCCategoryImplDecl>(MethodContext))
    return CID->getClassInterface();
  assert(false && "unknown method context");
  return 0;
}

ObjCPropertyImplDecl *ObjCPropertyImplDecl::Create(ASTContext &C,
                                                   SourceLocation atLoc,
                                                   SourceLocation L,
                                                   ObjCPropertyDecl *property,
                                                   Kind PK,
                                                   ObjCIvarDecl *ivar) {
  void *Mem = C.getAllocator().Allocate<ObjCPropertyImplDecl>();
  return new (Mem) ObjCPropertyImplDecl(atLoc, L, property, PK, ivar);
}


