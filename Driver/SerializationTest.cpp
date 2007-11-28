//===--- SerializationTest.cpp - Experimental Object Serialization --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Ted Kremenek and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements prototype code for serialization of objects in clang.
//  It is not intended yet for public use, but simply is a placeholder to
//  experiment with new serialization features.  Serialization will eventually
//  be integrated as a proper component of the clang libraries.
//
//===----------------------------------------------------------------------===//

#include "ASTConsumers.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "llvm/System/Path.h"
#include "llvm/Support/Streams.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Bitcode/Serialize.h"
#include "llvm/Bitcode/Deserialize.h"
#include <stdio.h>
#include <list>

using namespace clang;
using llvm::sys::TimeValue;

//===----------------------------------------------------------------------===//
// Utility classes
//===----------------------------------------------------------------------===//

namespace {

template<typename T> class Janitor {
  T* Obj;
public:
  explicit Janitor(T* obj) : Obj(obj) {}
  ~Janitor() { delete Obj; }
  operator T*() const { return Obj; }
  T* operator->() { return Obj; }
};

class FileSP {
  FILE* f;
public:
  FileSP(const llvm::sys::Path& fname, const char* mode = "wb")
    : f(fopen(fname.c_str(),mode)) {}
  
  ~FileSP() { if (f) fclose(f); }
  
  operator FILE*() const { return f; }
private:
  void operator=(const FileSP& RHS) {}
  FileSP(const FileSP& RHS) {}
};
  
//===----------------------------------------------------------------------===//
// Driver code.
//===----------------------------------------------------------------------===//

class SerializationTest : public ASTConsumer {
  ASTContext* Context;
  std::list<Decl*> Decls;
  
  enum { BasicMetadataBlock,
         ASTContextBlock,
         DeclsBlock };

public:  
  SerializationTest() : Context(NULL) {};
  ~SerializationTest();

  virtual void Initialize(ASTContext& context, unsigned) {
      Context = &context;
  }
  
  virtual void HandleTopLevelDecl(Decl *D) {
    Decls.push_back(D);
  }

private:
  void Serialize(llvm::sys::Path& Filename, llvm::sys::Path& FNameDeclPrint);
  void Deserialize(llvm::sys::Path& Filename, llvm::sys::Path& FNameDeclPrint);
};
  
} // end anonymous namespace

ASTConsumer* clang::CreateSerializationTest() {  
  return new SerializationTest();
}

static void WritePreamble(llvm::BitstreamWriter& Stream) {
  Stream.Emit((unsigned)'B', 8);
  Stream.Emit((unsigned)'C', 8);
  Stream.Emit(0xC, 4);
  Stream.Emit(0xF, 4);
  Stream.Emit(0xE, 4);
  Stream.Emit(0x0, 4);
}

static bool ReadPreamble(llvm::BitstreamReader& Stream) {
  return Stream.Read(8) != 'B' ||
         Stream.Read(8) != 'C' ||
         Stream.Read(4) != 0xC ||
         Stream.Read(4) != 0xF ||
         Stream.Read(4) != 0xE ||
         Stream.Read(4) != 0x0;
}

void SerializationTest::Serialize(llvm::sys::Path& Filename,
                                  llvm::sys::Path& FNameDeclPrint) {
  
  // Reserve 256K for bitstream buffer.
  std::vector<unsigned char> Buffer;
  Buffer.reserve(256*1024);
  
  // Create bitstream and write preamble.    
  llvm::BitstreamWriter Stream(Buffer);
  WritePreamble(Stream);
  
  // Create serializer.
  llvm::Serializer Sezr(Stream);
  
  // ===---------------------------------------------------===/
  //      Serialize the top-level decls.
  // ===---------------------------------------------------===/  
  
  Sezr.EnterBlock(DeclsBlock);
    
  { // Create a printer to "consume" our deserialized ASTS.

    Janitor<ASTConsumer> Printer(CreateASTPrinter());
    FileSP DeclFP(FNameDeclPrint,"w");
    assert (DeclFP && "Could not open file for printing out decls.");
    Janitor<ASTConsumer> FilePrinter(CreateASTPrinter(DeclFP));
    
    for (std::list<Decl*>::iterator I=Decls.begin(), E=Decls.end(); I!=E; ++I) {
      llvm::cerr << "Serializing: Decl.\n";   
      
      Printer->HandleTopLevelDecl(*I);
      FilePrinter->HandleTopLevelDecl(*I);
      
      Sezr.EmitOwnedPtr(*I);
    }
  }
  
  Sezr.ExitBlock();
  
  // ===---------------------------------------------------===/
  //      Serialize the "Translation Unit" metadata.
  // ===---------------------------------------------------===/

  // Emit ASTContext.
  Sezr.EnterBlock(ASTContextBlock);  
  llvm::cerr << "Serializing: ASTContext.\n";  
  Sezr.EmitOwnedPtr(Context);  
  Sezr.ExitBlock();  
  
  
  Sezr.EnterBlock(BasicMetadataBlock);

  // "Fake" emit the SourceManager.
  llvm::cerr << "Faux-serializing: SourceManager.\n";
  Sezr.EmitPtr(&Context->SourceMgr);
  
  // "Fake" emit the Target.
  llvm::cerr << "Faux-serializing: Target.\n";
  Sezr.EmitPtr(&Context->Target);

  // "Fake" emit Selectors.
  llvm::cerr << "Faux-serializing: Selectors.\n";
  Sezr.EmitPtr(&Context->Selectors);
  
  // Emit the Identifier Table.
  llvm::cerr << "Serializing: IdentifierTable.\n";  
  Sezr.EmitOwnedPtr(&Context->Idents);

  Sezr.ExitBlock();  
  
  // ===---------------------------------------------------===/
  // Finalize serialization: write the bits to disk.
  { 
    FileSP fp(Filename);

    if (fp)
      fwrite((char*)&Buffer.front(), sizeof(char), Buffer.size(), fp);
    else { 
      llvm::cerr << "Error: Cannot open " << Filename.c_str() << "\n";
      return;
    }
  }
  
  llvm::cerr << "Commited bitstream to disk: " << Filename.c_str() << "\n";
}


void SerializationTest::Deserialize(llvm::sys::Path& Filename,
                                    llvm::sys::Path& FNameDeclPrint) {
  
  // Create the memory buffer that contains the contents of the file.
  
  using llvm::MemoryBuffer;
  
  Janitor<MemoryBuffer> MBuffer(MemoryBuffer::getFile(Filename.c_str(),
                                              strlen(Filename.c_str())));
  
  if(!MBuffer) {
    llvm::cerr << "ERROR: Cannot read file for deserialization.\n";
    return;
  }
  
  // Check if the file is of the proper length.
  if (MBuffer->getBufferSize() & 0x3) {
    llvm::cerr << "ERROR: AST file length should be a multiple of 4 bytes.\n";
    return;
  }
  
  // Create the bitstream reader.
  unsigned char *BufPtr = (unsigned char *) MBuffer->getBufferStart();
  llvm::BitstreamReader Stream(BufPtr,BufPtr+MBuffer->getBufferSize());
  
  // Sniff for the signature in the bitcode file.
  if (ReadPreamble(Stream)) {
    llvm::cerr << "ERROR: Invalid AST-bitcode signature.\n";
    return;
  }
    
  // Create the deserializer.
  llvm::Deserializer Dezr(Stream);
  
  // ===---------------------------------------------------===/
  //      Deserialize the "Translation Unit" metadata.
  // ===---------------------------------------------------===/
  
  // Skip to the BasicMetaDataBlock.  First jump to ASTContextBlock
  // (which will appear earlier) and record its location.
  
  bool FoundBlock = Dezr.SkipToBlock(ASTContextBlock);
  assert (FoundBlock);

  llvm::Deserializer::Location ASTContextBlockLoc =
    Dezr.getCurrentBlockLocation();
  
  FoundBlock = Dezr.SkipToBlock(BasicMetadataBlock);
  assert (FoundBlock);
  
  // "Fake" read the SourceManager.
  llvm::cerr << "Faux-Deserializing: SourceManager.\n";
  Dezr.RegisterPtr(&Context->SourceMgr);

  // "Fake" read the TargetInfo.
  llvm::cerr << "Faux-Deserializing: Target.\n";
  Dezr.RegisterPtr(&Context->Target);

  // "Fake" read the Selectors.
  llvm::cerr << "Faux-Deserializing: Selectors.\n";
  Dezr.RegisterPtr(&Context->Selectors);  
  
  // Read the identifier table.
  llvm::cerr << "Deserializing: IdentifierTable\n";
  Dezr.ReadOwnedPtr<IdentifierTable>();
  
  // Now jump back to ASTContextBlock and read the ASTContext.
  Dezr.JumpTo(ASTContextBlockLoc);
  
  // Read the ASTContext.  
  llvm::cerr << "Deserializing: ASTContext.\n";
  Dezr.ReadOwnedPtr<ASTContext>();
    
  // "Rewind" the stream.  Find the block with the serialized top-level decls.
  Dezr.Rewind();
  FoundBlock = Dezr.SkipToBlock(DeclsBlock);
  assert (FoundBlock);
  llvm::Deserializer::Location DeclBlockLoc = Dezr.getCurrentBlockLocation();
  
  // Create a printer to "consume" our deserialized ASTS.
  ASTConsumer* Printer = CreateASTPrinter();
  Janitor<ASTConsumer> PrinterJanitor(Printer);  
  FileSP DeclFP(FNameDeclPrint,"w");
  assert (DeclFP && "Could not open file for printing out decls.");
  Janitor<ASTConsumer> FilePrinter(CreateASTPrinter(DeclFP));
  
  // The remaining objects in the file are top-level decls.
  while (!Dezr.FinishedBlock(DeclBlockLoc)) {
    llvm::cerr << "Deserializing: Decl.\n";
    Decl* decl = Dezr.ReadOwnedPtr<Decl>();
    Printer->HandleTopLevelDecl(decl);
    FilePrinter->HandleTopLevelDecl(decl);
  }
}
  
namespace {
  class TmpDirJanitor {
    llvm::sys::Path& Dir;
  public:
    explicit TmpDirJanitor(llvm::sys::Path& dir) : Dir(dir) {}

    ~TmpDirJanitor() { 
      llvm::cerr << "Removing: " << Dir.c_str() << '\n';
      Dir.eraseFromDisk(true); 
    }
  };
}

SerializationTest::~SerializationTest() {

  std::string ErrMsg;
  llvm::sys::Path Dir = llvm::sys::Path::GetTemporaryDirectory(&ErrMsg);
  
  if (Dir.isEmpty()) {
    llvm::cerr << "Error: " << ErrMsg << "\n";
    return;
  }
  
  TmpDirJanitor RemoveTmpOnExit(Dir);
    
  llvm::sys::Path FNameDeclBefore = Dir;
  FNameDeclBefore.appendComponent("test.decl_before.txt");

  if (FNameDeclBefore.makeUnique(true,&ErrMsg)) {
    llvm::cerr << "Error: " << ErrMsg << "\n";
    return;
  }
  
  llvm::sys::Path FNameDeclAfter = Dir;
  FNameDeclAfter.appendComponent("test.decl_after.txt");
  
  if (FNameDeclAfter.makeUnique(true,&ErrMsg)) {
    llvm::cerr << "Error: " << ErrMsg << "\n";
    return;
  }

  llvm::sys::Path ASTFilename = Dir;
  ASTFilename.appendComponent("test.ast");
  
  if (ASTFilename.makeUnique(true,&ErrMsg)) {
    llvm::cerr << "Error: " << ErrMsg << "\n";
    return;
  }
  
  // Serialize and then deserialize the ASTs.
  Serialize(ASTFilename, FNameDeclBefore);
  Deserialize(ASTFilename, FNameDeclAfter);
  
  // Read both pretty-printed files and compare them.
  
  using llvm::MemoryBuffer;
  
  Janitor<MemoryBuffer>
    MBufferSer(MemoryBuffer::getFile(FNameDeclBefore.c_str(),
                                     strlen(FNameDeclBefore.c_str())));
  
  if(!MBufferSer) {
    llvm::cerr << "ERROR: Cannot read pretty-printed file (pre-pickle).\n";
    return;
  }
  
  Janitor<MemoryBuffer>
    MBufferDSer(MemoryBuffer::getFile(FNameDeclAfter.c_str(),
                                      strlen(FNameDeclAfter.c_str())));
  
  if(!MBufferDSer) {
    llvm::cerr << "ERROR: Cannot read pretty-printed file (post-pickle).\n";
    return;
  }
  
  const char *p1 = MBufferSer->getBufferStart();
  const char *e1 = MBufferSer->getBufferEnd();
  const char *p2 = MBufferDSer->getBufferStart();
  const char *e2 = MBufferDSer->getBufferEnd();

  if (MBufferSer->getBufferSize() == MBufferDSer->getBufferSize())
    for ( ; p1 != e1 ; ++p1, ++p2  )
      if (*p1 != *p2) break;
  
  if (p1 != e1 || p2 != e2 )
    llvm::cerr << "ERROR: Pretty-printed files are not the same.\n";
  else
    llvm::cerr << "SUCCESS: Pretty-printed files are the same.\n";
}
