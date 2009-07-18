//=== ASTRecordLayoutBuilder.cpp - Helper class for building record layouts ==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RecordLayoutBuilder.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/TargetInfo.h"
#include <llvm/Support/MathExtras.h>

using namespace clang;

ASTRecordLayoutBuilder::ASTRecordLayoutBuilder(ASTContext &Ctx) 
  : Ctx(Ctx), Size(0), Alignment(8), StructPacking(0), NextOffset(0), 
  IsUnion(false) {}

void ASTRecordLayoutBuilder::Layout(const RecordDecl *D) {
  IsUnion = D->isUnion();
  
  if (const PackedAttr* PA = D->getAttr<PackedAttr>())
    StructPacking = PA->getAlignment();
  
  if (const AlignedAttr *AA = D->getAttr<AlignedAttr>())
    UpdateAlignment(AA->getAlignment());
  
  // Layout each field, for now, just sequentially, respecting alignment.  In
  // the future, this will need to be tweakable by targets.
  for (RecordDecl::field_iterator Field = D->field_begin(), 
       FieldEnd = D->field_end(); Field != FieldEnd; ++Field)
    LayoutField(*Field);
  
  // Finally, round the size of the total struct up to the alignment of the
  // struct itself.
  FinishLayout();
}

void ASTRecordLayoutBuilder::LayoutField(const FieldDecl *D) {
  unsigned FieldPacking = StructPacking;
  uint64_t FieldOffset = IsUnion ? 0 : Size;
  uint64_t FieldSize;
  unsigned FieldAlign;
  
  // FIXME: Should this override struct packing? Probably we want to
  // take the minimum?
  if (const PackedAttr *PA = D->getAttr<PackedAttr>())
    FieldPacking = PA->getAlignment();
  
  if (const Expr *BitWidthExpr = D->getBitWidth()) {
    // TODO: Need to check this algorithm on other targets!
    //       (tested on Linux-X86)
    FieldSize = BitWidthExpr->EvaluateAsInt(Ctx).getZExtValue();
    
    std::pair<uint64_t, unsigned> FieldInfo = Ctx.getTypeInfo(D->getType());
    uint64_t TypeSize = FieldInfo.first;
    
    // Determine the alignment of this bitfield. The packing
    // attributes define a maximum and the alignment attribute defines
    // a minimum.
    // FIXME: What is the right behavior when the specified alignment
    // is smaller than the specified packing?
    FieldAlign = FieldInfo.second;
    if (FieldPacking)
      FieldAlign = std::min(FieldAlign, FieldPacking);
    if (const AlignedAttr *AA = D->getAttr<AlignedAttr>())
      FieldAlign = std::max(FieldAlign, AA->getAlignment());
    
    // Check if we need to add padding to give the field the correct
    // alignment.
    if (FieldSize == 0 || (FieldOffset & (FieldAlign-1)) + FieldSize > TypeSize)
      FieldOffset = (FieldOffset + (FieldAlign-1)) & ~(FieldAlign-1);
    
    // Padding members don't affect overall alignment
    if (!D->getIdentifier())
      FieldAlign = 1;
  } else {
    if (D->getType()->isIncompleteArrayType()) {
      // This is a flexible array member; we can't directly
      // query getTypeInfo about these, so we figure it out here.
      // Flexible array members don't have any size, but they
      // have to be aligned appropriately for their element type.
      FieldSize = 0;
      const ArrayType* ATy = Ctx.getAsArrayType(D->getType());
      FieldAlign = Ctx.getTypeAlign(ATy->getElementType());
    } else if (const ReferenceType *RT = D->getType()->getAsReferenceType()) {
      unsigned AS = RT->getPointeeType().getAddressSpace();
      FieldSize = Ctx.Target.getPointerWidth(AS);
      FieldAlign = Ctx.Target.getPointerAlign(AS);
    } else {
      std::pair<uint64_t, unsigned> FieldInfo = Ctx.getTypeInfo(D->getType());
      FieldSize = FieldInfo.first;
      FieldAlign = FieldInfo.second;
    }
    
    // Determine the alignment of this bitfield. The packing
    // attributes define a maximum and the alignment attribute defines
    // a minimum. Additionally, the packing alignment must be at least
    // a byte for non-bitfields.
    //
    // FIXME: What is the right behavior when the specified alignment
    // is smaller than the specified packing?
    if (FieldPacking)
      FieldAlign = std::min(FieldAlign, std::max(8U, FieldPacking));
    if (const AlignedAttr *AA = D->getAttr<AlignedAttr>())
      FieldAlign = std::max(FieldAlign, AA->getAlignment());
    
    // Round up the current record size to the field's alignment boundary.
    FieldOffset = (FieldOffset + (FieldAlign-1)) & ~(FieldAlign-1);
  }
  
  // Place this field at the current location.
  FieldOffsets.push_back(FieldOffset);
  
  // Reserve space for this field.
  if (IsUnion)
    Size = std::max(Size, FieldSize);
  else
    Size = FieldOffset + FieldSize;
  
  // Remember the next available offset.
  NextOffset = Size;
  
  // Remember max struct/class alignment.
  UpdateAlignment(FieldAlign);
}

void ASTRecordLayoutBuilder::FinishLayout() {
  // In C++, records cannot be of size 0.
  if (Ctx.getLangOptions().CPlusPlus && Size == 0)
    Size = 8;
  // Finally, round the size of the record up to the alignment of the
  // record itself.
  Size = (Size + (Alignment-1)) & ~(Alignment-1);
}

void ASTRecordLayoutBuilder::UpdateAlignment(unsigned NewAlignment) {
  if (NewAlignment <= Alignment)
    return;
  
  assert(llvm::isPowerOf2_32(NewAlignment && "Alignment not a power of 2"));
  
  Alignment = NewAlignment;
}
         
const ASTRecordLayout *
ASTRecordLayoutBuilder::ComputeLayout(ASTContext &Ctx, 
                                      const RecordDecl *D) {
  ASTRecordLayoutBuilder Builder(Ctx);

  Builder.Layout(D);

  return new ASTRecordLayout(Builder.Size, Builder.Alignment,
                             Builder.FieldOffsets.data(), 
                             Builder.FieldOffsets.size());
}
