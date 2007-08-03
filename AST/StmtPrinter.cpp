//===--- StmtPrinter.cpp - Printing implementation for Stmt ASTs ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Stmt::dump/Stmt::print methods.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/StmtVisitor.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Lex/IdentifierTable.h"
#include "llvm/Support/Compiler.h"
#include <iostream>
#include <iomanip>
using namespace clang;

//===----------------------------------------------------------------------===//
// StmtPrinter Visitor
//===----------------------------------------------------------------------===//

namespace  {
  class VISIBILITY_HIDDEN StmtPrinter : public StmtVisitor {
    std::ostream &OS;
    unsigned IndentLevel;
  public:
    StmtPrinter(std::ostream &os) : OS(os), IndentLevel(0) {}
    
    void PrintStmt(Stmt *S, int SubIndent = 1) {
      IndentLevel += SubIndent;
      if (S && isa<Expr>(S)) {
        // If this is an expr used in a stmt context, indent and newline it.
        Indent();
        S->visit(*this);
        OS << ";\n";
      } else if (S) {
        S->visit(*this);
      } else {
        Indent() << "<<<NULL STATEMENT>>>\n";
      }
      IndentLevel -= SubIndent;
    }
    
    void PrintRawCompoundStmt(CompoundStmt *S);
    void PrintRawDecl(Decl *D);
    void PrintRawIfStmt(IfStmt *If);
    
    void PrintExpr(Expr *E) {
      if (E)
        E->visit(*this);
      else
        OS << "<null expr>";
    }
    
    std::ostream &Indent(int Delta = 0) const {
      for (int i = 0, e = IndentLevel+Delta; i < e; ++i)
        OS << "  ";
      return OS;
    }
    
    virtual void VisitStmt(Stmt *Node);
#define STMT(N, CLASS, PARENT) \
    virtual void Visit##CLASS(CLASS *Node);
#include "clang/AST/StmtNodes.def"
  };
}

//===----------------------------------------------------------------------===//
//  Stmt printing methods.
//===----------------------------------------------------------------------===//

void StmtPrinter::VisitStmt(Stmt *Node) {
  Indent() << "<<unknown stmt type>>\n";
}

/// PrintRawCompoundStmt - Print a compound stmt without indenting the {, and
/// with no newline after the }.
void StmtPrinter::PrintRawCompoundStmt(CompoundStmt *Node) {
  OS << "{\n";
  for (CompoundStmt::body_iterator I = Node->body_begin(), E = Node->body_end();
       I != E; ++I)
    PrintStmt(*I);
  
  Indent() << "}";
}

void StmtPrinter::PrintRawDecl(Decl *D) {
  // FIXME: Need to complete/beautify this... this code simply shows the
  // nodes are where they need to be.
  if (TypedefDecl *localType = dyn_cast<TypedefDecl>(D)) {
    OS << "typedef " << localType->getUnderlyingType().getAsString();
    OS << " " << localType->getName();
  } else if (ValueDecl *VD = dyn_cast<ValueDecl>(D)) {
    // Emit storage class for vardecls.
    if (VarDecl *V = dyn_cast<VarDecl>(VD)) {
      switch (V->getStorageClass()) {
        default: assert(0 && "Unknown storage class!");
        case VarDecl::None:     break;
        case VarDecl::Extern:   OS << "extern "; break;
        case VarDecl::Static:   OS << "static "; break; 
        case VarDecl::Auto:     OS << "auto "; break;
        case VarDecl::Register: OS << "register "; break;
      }
    }
    
    std::string Name = VD->getName();
    VD->getType().getAsStringInternal(Name);
    OS << Name;
    
    // If this is a vardecl with an initializer, emit it.
    if (VarDecl *V = dyn_cast<VarDecl>(VD)) {
      if (V->getInit()) {
        OS << " = ";
        PrintExpr(V->getInit());
      }
    }
  } else {
    // FIXME: "struct x;"
    assert(0 && "Unexpected decl");
  }
}


void StmtPrinter::VisitNullStmt(NullStmt *Node) {
  Indent() << ";\n";
}

void StmtPrinter::VisitDeclStmt(DeclStmt *Node) {
  for (Decl *D = Node->getDecl(); D; D = D->getNextDeclarator()) {
    Indent();
    PrintRawDecl(D);
    OS << ";\n";
  }
}

void StmtPrinter::VisitCompoundStmt(CompoundStmt *Node) {
  Indent();
  PrintRawCompoundStmt(Node);
  OS << "\n";
}

void StmtPrinter::VisitCaseStmt(CaseStmt *Node) {
  Indent(-1) << "case ";
  PrintExpr(Node->getLHS());
  if (Node->getRHS()) {
    OS << " ... ";
    PrintExpr(Node->getRHS());
  }
  OS << ":\n";
  
  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitDefaultStmt(DefaultStmt *Node) {
  Indent(-1) << "default:\n";
  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitLabelStmt(LabelStmt *Node) {
  Indent(-1) << Node->getName() << ":\n";
  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::PrintRawIfStmt(IfStmt *If) {
  OS << "if ";
  PrintExpr(If->getCond());
  
  if (CompoundStmt *CS = dyn_cast<CompoundStmt>(If->getThen())) {
    OS << ' ';
    PrintRawCompoundStmt(CS);
    OS << (If->getElse() ? ' ' : '\n');
  } else {
    OS << '\n';
    PrintStmt(If->getThen());
    if (If->getElse()) Indent();
  }
  
  if (Stmt *Else = If->getElse()) {
    OS << "else";
    
    if (CompoundStmt *CS = dyn_cast<CompoundStmt>(Else)) {
      OS << ' ';
      PrintRawCompoundStmt(CS);
      OS << '\n';
    } else if (IfStmt *ElseIf = dyn_cast<IfStmt>(Else)) {
      OS << ' ';
      PrintRawIfStmt(ElseIf);
    } else {
      OS << '\n';
      PrintStmt(If->getElse());
    }
  }
}

void StmtPrinter::VisitIfStmt(IfStmt *If) {
  Indent();
  PrintRawIfStmt(If);
}

void StmtPrinter::VisitSwitchStmt(SwitchStmt *Node) {
  Indent() << "switch (";
  PrintExpr(Node->getCond());
  OS << ")";
  
  // Pretty print compoundstmt bodies (very common).
  if (CompoundStmt *CS = dyn_cast<CompoundStmt>(Node->getBody())) {
    OS << " ";
    PrintRawCompoundStmt(CS);
    OS << "\n";
  } else {
    OS << "\n";
    PrintStmt(Node->getBody());
  }
}

void StmtPrinter::VisitSwitchCase(SwitchCase*) {
  assert(0 && "SwitchCase is an abstract class");
}

void StmtPrinter::VisitWhileStmt(WhileStmt *Node) {
  Indent() << "while (";
  PrintExpr(Node->getCond());
  OS << ")\n";
  PrintStmt(Node->getBody());
}

void StmtPrinter::VisitDoStmt(DoStmt *Node) {
  Indent() << "do\n";
  PrintStmt(Node->getBody());
  Indent() << "while ";
  PrintExpr(Node->getCond());
  OS << ";\n";
}

void StmtPrinter::VisitForStmt(ForStmt *Node) {
  Indent() << "for (";
  if (Node->getInit()) {
    if (DeclStmt *DS = dyn_cast<DeclStmt>(Node->getInit()))
      PrintRawDecl(DS->getDecl());
    else
      PrintExpr(cast<Expr>(Node->getInit()));
  }
  OS << "; ";
  if (Node->getCond())
    PrintExpr(Node->getCond());
  OS << "; ";
  if (Node->getInc())
    PrintExpr(Node->getInc());
  OS << ")\n";
  PrintStmt(Node->getBody());
}

void StmtPrinter::VisitGotoStmt(GotoStmt *Node) {
  Indent() << "goto " << Node->getLabel()->getName() << ";\n";
}

void StmtPrinter::VisitIndirectGotoStmt(IndirectGotoStmt *Node) {
  Indent() << "goto *";
  PrintExpr(Node->getTarget());
  OS << ";\n";
}

void StmtPrinter::VisitContinueStmt(ContinueStmt *Node) {
  Indent() << "continue;\n";
}

void StmtPrinter::VisitBreakStmt(BreakStmt *Node) {
  Indent() << "break;\n";
}


void StmtPrinter::VisitReturnStmt(ReturnStmt *Node) {
  Indent() << "return";
  if (Node->getRetValue()) {
    OS << " ";
    PrintExpr(Node->getRetValue());
  }
  OS << ";\n";
}

//===----------------------------------------------------------------------===//
//  Expr printing methods.
//===----------------------------------------------------------------------===//

void StmtPrinter::VisitExpr(Expr *Node) {
  OS << "<<unknown expr type>>";
}

void StmtPrinter::VisitDeclRefExpr(DeclRefExpr *Node) {
  OS << Node->getDecl()->getName();
}

void StmtPrinter::VisitPreDefinedExpr(PreDefinedExpr *Node) {
  switch (Node->getIdentType()) {
    default:
      assert(0 && "unknown case");
    case PreDefinedExpr::Func:
      OS << "__func__";
      break;
    case PreDefinedExpr::Function:
      OS << "__FUNCTION__";
      break;
    case PreDefinedExpr::PrettyFunction:
      OS << "__PRETTY_FUNCTION__";
      break;
  }
}

void StmtPrinter::VisitCharacterLiteral(CharacterLiteral *Node) {
  // FIXME should print an L for wchar_t constants
  unsigned value = Node->getValue();
  switch (value) {
  case '\\':
    OS << "'\\\\'";
    break;
  case '\'':
    OS << "'\\''";
    break;
  case '\a':
    // TODO: K&R: the meaning of '\\a' is different in traditional C
    OS << "'\\a'";
    break;
  case '\b':
    OS << "'\\b'";
    break;
  // Nonstandard escape sequence.
  /*case '\e':
    OS << "'\\e'";
    break;*/
  case '\f':
    OS << "'\\f'";
    break;
  case '\n':
    OS << "'\\n'";
    break;
  case '\r':
    OS << "'\\r'";
    break;
  case '\t':
    OS << "'\\t'";
    break;
  case '\v':
    OS << "'\\v'";
    break;
  default:
    if (isprint(value) && value < 256) {
      OS << "'" << (char)value << "'";
    } else if (value < 256) {
      OS << "'\\x" << std::hex << value << std::dec << "'";
    } else {
      // FIXME what to really do here?
      OS << value;
    }
  }
}

void StmtPrinter::VisitIntegerLiteral(IntegerLiteral *Node) {
  bool isSigned = Node->getType()->isSignedIntegerType();
  OS << Node->getValue().toString(10, isSigned);
  
  // Emit suffixes.  Integer literals are always a builtin integer type.
  switch (cast<BuiltinType>(Node->getType().getCanonicalType())->getKind()) {
  default: assert(0 && "Unexpected type for integer literal!");
  case BuiltinType::Int:       break; // no suffix.
  case BuiltinType::UInt:      OS << 'U'; break;
  case BuiltinType::Long:      OS << 'L'; break;
  case BuiltinType::ULong:     OS << "UL"; break;
  case BuiltinType::LongLong:  OS << "LL"; break;
  case BuiltinType::ULongLong: OS << "ULL"; break;
  }
}
void StmtPrinter::VisitFloatingLiteral(FloatingLiteral *Node) {
  // FIXME: print value more precisely.
  OS << Node->getValue();
}
void StmtPrinter::VisitStringLiteral(StringLiteral *Str) {
  if (Str->isWide()) OS << 'L';
  OS << '"';
  
  // FIXME: this doesn't print wstrings right.
  for (unsigned i = 0, e = Str->getByteLength(); i != e; ++i) {
    switch (Str->getStrData()[i]) {
    default: OS << Str->getStrData()[i]; break;
    // Handle some common ones to make dumps prettier.
    case '\\': OS << "\\\\"; break;
    case '"': OS << "\\\""; break;
    case '\n': OS << "\\n"; break;
    case '\t': OS << "\\t"; break;
    case '\a': OS << "\\a"; break;
    case '\b': OS << "\\b"; break;
    }
  }
  OS << '"';
}
void StmtPrinter::VisitParenExpr(ParenExpr *Node) {
  OS << "(";
  PrintExpr(Node->getSubExpr());
  OS << ")";
}
void StmtPrinter::VisitUnaryOperator(UnaryOperator *Node) {
  if (!Node->isPostfix())
    OS << UnaryOperator::getOpcodeStr(Node->getOpcode());
  PrintExpr(Node->getSubExpr());
  
  if (Node->isPostfix())
    OS << UnaryOperator::getOpcodeStr(Node->getOpcode());

}
void StmtPrinter::VisitSizeOfAlignOfTypeExpr(SizeOfAlignOfTypeExpr *Node) {
  OS << (Node->isSizeOf() ? "sizeof(" : "__alignof(");
  OS << Node->getArgumentType().getAsString() << ")";
}
void StmtPrinter::VisitArraySubscriptExpr(ArraySubscriptExpr *Node) {
  PrintExpr(Node->getBase());
  OS << "[";
  PrintExpr(Node->getIdx());
  OS << "]";
}

void StmtPrinter::VisitCallExpr(CallExpr *Call) {
  PrintExpr(Call->getCallee());
  OS << "(";
  for (unsigned i = 0, e = Call->getNumArgs(); i != e; ++i) {
    if (i) OS << ", ";
    PrintExpr(Call->getArg(i));
  }
  OS << ")";
}
void StmtPrinter::VisitMemberExpr(MemberExpr *Node) {
  PrintExpr(Node->getBase());
  OS << (Node->isArrow() ? "->" : ".");
  
  FieldDecl *Field = Node->getMemberDecl();
  assert(Field && "MemberExpr should alway reference a field!");
  OS << Field->getName();
}
void StmtPrinter::VisitOCUVectorElementExpr(OCUVectorElementExpr *Node) {
  PrintExpr(Node->getBase());
  OS << ".";
  OS << Node->getAccessor().getName();
}
void StmtPrinter::VisitCastExpr(CastExpr *Node) {
  OS << "(" << Node->getType().getAsString() << ")";
  PrintExpr(Node->getSubExpr());
}
void StmtPrinter::VisitCompoundLiteralExpr(CompoundLiteralExpr *Node) {
  OS << "(" << Node->getType().getAsString() << ")";
  PrintExpr(Node->getInitializer());
}
void StmtPrinter::VisitImplicitCastExpr(ImplicitCastExpr *Node) {
  // No need to print anything, simply forward to the sub expression.
  PrintExpr(Node->getSubExpr());
}
void StmtPrinter::VisitBinaryOperator(BinaryOperator *Node) {
  PrintExpr(Node->getLHS());
  OS << " " << BinaryOperator::getOpcodeStr(Node->getOpcode()) << " ";
  PrintExpr(Node->getRHS());
}
void StmtPrinter::VisitConditionalOperator(ConditionalOperator *Node) {
  PrintExpr(Node->getCond());
  OS << " ? ";
  PrintExpr(Node->getLHS());
  OS << " : ";
  PrintExpr(Node->getRHS());
}

// GNU extensions.

void StmtPrinter::VisitAddrLabelExpr(AddrLabelExpr *Node) {
  OS << "&&" << Node->getLabel()->getName();
}

void StmtPrinter::VisitStmtExpr(StmtExpr *E) {
  OS << "(";
  PrintRawCompoundStmt(E->getSubStmt());
  OS << ")";
}

void StmtPrinter::VisitTypesCompatibleExpr(TypesCompatibleExpr *Node) {
  OS << "__builtin_types_compatible_p(";
  OS << Node->getArgType1().getAsString() << ",";
  OS << Node->getArgType2().getAsString() << ")";
}

void StmtPrinter::VisitChooseExpr(ChooseExpr *Node) {
  OS << "__builtin_choose_expr(";
  PrintExpr(Node->getCond());
  OS << ",";
  PrintExpr(Node->getLHS());
  OS << ",";
  PrintExpr(Node->getRHS());
  OS << ")";
}

// C++

void StmtPrinter::VisitCXXCastExpr(CXXCastExpr *Node) {
  switch (Node->getOpcode()) {
    default:
      assert(0 && "Not a C++ cast expression");
      abort();
    case CXXCastExpr::ConstCast:       OS << "const_cast<";       break;
    case CXXCastExpr::DynamicCast:     OS << "dynamic_cast<";     break;
    case CXXCastExpr::ReinterpretCast: OS << "reinterpret_cast<"; break;
    case CXXCastExpr::StaticCast:      OS << "static_cast<";      break;
  }
  
  OS << Node->getDestType().getAsString() << ">(";
  PrintExpr(Node->getSubExpr());
  OS << ")";
}

void StmtPrinter::VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *Node) {
  OS << (Node->getValue() ? "true" : "false");
}


//===----------------------------------------------------------------------===//
// Stmt method implementations
//===----------------------------------------------------------------------===//

void Stmt::dump() const {
  // FIXME: eliminate use of <iostream>
  print(std::cerr);
}

void Stmt::print(std::ostream &OS) const {
  if (this == 0) {
    OS << "<NULL>";
    return;
  }

  StmtPrinter P(OS);
  const_cast<Stmt*>(this)->visit(P);
}
