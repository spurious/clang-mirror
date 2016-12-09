/*===-- clang-c/Index.h - Indexing Public C Interface -------------*- C -*-===*\
|*                                                                            *|
|*                     The LLVM Compiler Infrastructure                       *|
|*                                                                            *|
|* This file is distributed under the University of Illinois Open Source      *|
|* License. See LICENSE.TXT for details.                                      *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides a public inferface to a Clang library for extracting  *|
|* high-level symbol information from source files without exposing the full  *|
|* Clang C++ API.                                                             *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_CLANG_C_INDEX_H
#define LLVM_CLANG_C_INDEX_H

#include <time.h>

#include "clang-c/Platform.h"
#include "clang-c/CXErrorCode.h"
#include "clang-c/CXString.h"
#include "clang-c/BuildSystem.h"

/**
 * \brief The version constants for the libclang API.
 * CINDEX_VERSION_MINOR should increase when there are API additions.
 * CINDEX_VERSION_MAJOR is intended for "major" source/ABI breaking changes.
 *
 * The policy about the libclang API was always to keep it source and ABI
 * compatible, thus CINDEX_VERSION_MAJOR is expected to remain stable.
 */
#define CINDEX_VERSION_MAJOR 0
#define CINDEX_VERSION_MINOR 37

#define CINDEX_VERSION_ENCODE(major, minor) ( \
      ((major) * 10000)                       \
    + ((minor) *     1))

#define CINDEX_VERSION CINDEX_VERSION_ENCODE( \
    CINDEX_VERSION_MAJOR,                     \
    CINDEX_VERSION_MINOR )

#define CINDEX_VERSION_STRINGIZE_(major, minor)   \
    #major"."#minor
#define CINDEX_VERSION_STRINGIZE(major, minor)    \
    CINDEX_VERSION_STRINGIZE_(major, minor)

#define CINDEX_VERSION_STRING CINDEX_VERSION_STRINGIZE( \
    CINDEX_VERSION_MAJOR,                               \
    CINDEX_VERSION_MINOR)

#ifdef __cplusplus
extern "C" {
#endif

/** \defgroup CINDEX libclang: C Interface to Clang
 *
 * The C Interface to Clang provides a relatively small API that exposes
 * facilities for parsing source code into an abstract syntax tree (AST),
 * loading already-parsed ASTs, traversing the AST, associating
 * physical source locations with elements within the AST, and other
 * facilities that support Clang-based development tools.
 *
 * This C interface to Clang will never provide all of the information
 * representation stored in Clang's C++ AST, nor should it: the intent is to
 * maintain an API that is relatively stable from one release to the next,
 * providing only the basic functionality needed to support development tools.
 *
 * To avoid namespace pollution, data types are prefixed with "CX" and
 * functions are prefixed with "clang_".
 *
 * @{
 */

/**
 * \brief An "index" that consists of a set of translation units that would
 * typically be linked together into an executable or library.
 */
typedef void *CXIndex;

/**
 * \brief A single translation unit, which resides in an index.
 */
typedef struct CXTranslationUnitImpl *CXTranslationUnit;

/**
 * \brief Opaque pointer representing client data that will be passed through
 * to various callbacks and visitors.
 */
typedef void *CXClientData;

/**
 * \brief Provides the contents of a file that has not yet been saved to disk.
 *
 * Each CXUnsavedFile instance provides the name of a file on the
 * system along with the current contents of that file that have not
 * yet been saved to disk.
 */
struct CXUnsavedFile {
  /**
   * \brief The file whose contents have not yet been saved.
   *
   * This file must already exist in the file system.
   */
  const char *Filename;

  /**
   * \brief A buffer containing the unsaved contents of this file.
   */
  const char *Contents;

  /**
   * \brief The length of the unsaved contents of this buffer.
   */
  unsigned long Length;
};

/**
 * \brief Describes the availability of a particular entity, which indicates
 * whether the use of this entity will result in a warning or error due to
 * it being deprecated or unavailable.
 */
enum CXAvailabilityKind {
  /**
   * \brief The entity is available.
   */
  CXAvailability_Available,
  /**
   * \brief The entity is available, but has been deprecated (and its use is
   * not recommended).
   */
  CXAvailability_Deprecated,
  /**
   * \brief The entity is not available; any use of it will be an error.
   */
  CXAvailability_NotAvailable,
  /**
   * \brief The entity is available, but not accessible; any use of it will be
   * an error.
   */
  CXAvailability_NotAccessible
};

/**
 * \brief Describes a version number of the form major.minor.subminor.
 */
typedef struct CXVersion {
  /**
   * \brief The major version number, e.g., the '10' in '10.7.3'. A negative
   * value indicates that there is no version number at all.
   */
  int Major;
  /**
   * \brief The minor version number, e.g., the '7' in '10.7.3'. This value
   * will be negative if no minor version number was provided, e.g., for 
   * version '10'.
   */
  int Minor;
  /**
   * \brief The subminor version number, e.g., the '3' in '10.7.3'. This value
   * will be negative if no minor or subminor version number was provided,
   * e.g., in version '10' or '10.7'.
   */
  int Subminor;
} CXVersion;
  
/**
 * \brief Provides a shared context for creating translation units.
 *
 * It provides two options:
 *
 * - excludeDeclarationsFromPCH: When non-zero, allows enumeration of "local"
 * declarations (when loading any new translation units). A "local" declaration
 * is one that belongs in the translation unit itself and not in a precompiled
 * header that was used by the translation unit. If zero, all declarations
 * will be enumerated.
 *
 * Here is an example:
 *
 * \code
 *   // excludeDeclsFromPCH = 1, displayDiagnostics=1
 *   Idx = clang_createIndex(1, 1);
 *
 *   // IndexTest.pch was produced with the following command:
 *   // "clang -x c IndexTest.h -emit-ast -o IndexTest.pch"
 *   TU = clang_createTranslationUnit(Idx, "IndexTest.pch");
 *
 *   // This will load all the symbols from 'IndexTest.pch'
 *   clang_visitChildren(clang_getTranslationUnitCursor(TU),
 *                       TranslationUnitVisitor, 0);
 *   clang_disposeTranslationUnit(TU);
 *
 *   // This will load all the symbols from 'IndexTest.c', excluding symbols
 *   // from 'IndexTest.pch'.
 *   char *args[] = { "-Xclang", "-include-pch=IndexTest.pch" };
 *   TU = clang_createTranslationUnitFromSourceFile(Idx, "IndexTest.c", 2, args,
 *                                                  0, 0);
 *   clang_visitChildren(clang_getTranslationUnitCursor(TU),
 *                       TranslationUnitVisitor, 0);
 *   clang_disposeTranslationUnit(TU);
 * \endcode
 *
 * This process of creating the 'pch', loading it separately, and using it (via
 * -include-pch) allows 'excludeDeclsFromPCH' to remove redundant callbacks
 * (which gives the indexer the same performance benefit as the compiler).
 */
CINDEX_LINKAGE CXIndex clang_createIndex(int excludeDeclarationsFromPCH,
                                         int displayDiagnostics);

/**
 * \brief Destroy the given index.
 *
 * The index must not be destroyed until all of the translation units created
 * within that index have been destroyed.
 */
CINDEX_LINKAGE void clang_disposeIndex(CXIndex index);

typedef enum {
  /**
   * \brief Used to indicate that no special CXIndex options are needed.
   */
  CXGlobalOpt_None = 0x0,

  /**
   * \brief Used to indicate that threads that libclang creates for indexing
   * purposes should use background priority.
   *
   * Affects #clang_indexSourceFile, #clang_indexTranslationUnit,
   * #clang_parseTranslationUnit, #clang_saveTranslationUnit.
   */
  CXGlobalOpt_ThreadBackgroundPriorityForIndexing = 0x1,

  /**
   * \brief Used to indicate that threads that libclang creates for editing
   * purposes should use background priority.
   *
   * Affects #clang_reparseTranslationUnit, #clang_codeCompleteAt,
   * #clang_annotateTokens
   */
  CXGlobalOpt_ThreadBackgroundPriorityForEditing = 0x2,

  /**
   * \brief Used to indicate that all threads that libclang creates should use
   * background priority.
   */
  CXGlobalOpt_ThreadBackgroundPriorityForAll =
      CXGlobalOpt_ThreadBackgroundPriorityForIndexing |
      CXGlobalOpt_ThreadBackgroundPriorityForEditing

} CXGlobalOptFlags;

/**
 * \brief Sets general options associated with a CXIndex.
 *
 * For example:
 * \code
 * CXIndex idx = ...;
 * clang_CXIndex_setGlobalOptions(idx,
 *     clang_CXIndex_getGlobalOptions(idx) |
 *     CXGlobalOpt_ThreadBackgroundPriorityForIndexing);
 * \endcode
 *
 * \param options A bitmask of options, a bitwise OR of CXGlobalOpt_XXX flags.
 */
CINDEX_LINKAGE void clang_CXIndex_setGlobalOptions(CXIndex, unsigned options);

/**
 * \brief Gets the general options associated with a CXIndex.
 *
 * \returns A bitmask of options, a bitwise OR of CXGlobalOpt_XXX flags that
 * are associated with the given CXIndex object.
 */
CINDEX_LINKAGE unsigned clang_CXIndex_getGlobalOptions(CXIndex);

/**
 * \defgroup CINDEX_FILES File manipulation routines
 *
 * @{
 */

/**
 * \brief A particular source file that is part of a translation unit.
 */
typedef void *CXFile;

/**
 * \brief Retrieve the complete file and path name of the given file.
 */
CINDEX_LINKAGE CXString clang_getFileName(CXFile SFile);

/**
 * \brief Retrieve the last modification time of the given file.
 */
CINDEX_LINKAGE time_t clang_getFileTime(CXFile SFile);

/**
 * \brief Uniquely identifies a CXFile, that refers to the same underlying file,
 * across an indexing session.
 */
typedef struct {
  unsigned long long data[3];
} CXFileUniqueID;

/**
 * \brief Retrieve the unique ID for the given \c file.
 *
 * \param file the file to get the ID for.
 * \param outID stores the returned CXFileUniqueID.
 * \returns If there was a failure getting the unique ID, returns non-zero,
 * otherwise returns 0.
*/
CINDEX_LINKAGE int clang_getFileUniqueID(CXFile file, CXFileUniqueID *outID);

/**
 * \brief Determine whether the given header is guarded against
 * multiple inclusions, either with the conventional
 * \#ifndef/\#define/\#endif macro guards or with \#pragma once.
 */
CINDEX_LINKAGE unsigned 
clang_isFileMultipleIncludeGuarded(CXTranslationUnit tu, CXFile file);

/**
 * \brief Retrieve a file handle within the given translation unit.
 *
 * \param tu the translation unit
 *
* \param file_name the name of the file.
 *
 * \returns the file handle for the named file in the translation unit \p tu,
 * or a NULL file handle if the file was not a part of this translation unit.
 */
CINDEX_LINKAGE CXFile clang_getFile(CXTranslationUnit tu,
                                    const char *file_name);

/**
 * \brief Returns non-zero if the \c file1 and \c file2 point to the same file,
 * or they are both NULL.
 */
CINDEX_LINKAGE int clang_File_isEqual(CXFile file1, CXFile file2);

/**
 * @}
 */

/**
 * \defgroup CINDEX_LOCATIONS Physical source locations
 *
 * Clang represents physical source locations in its abstract syntax tree in
 * great detail, with file, line, and column information for the majority of
 * the tokens parsed in the source code. These data types and functions are
 * used to represent source location information, either for a particular
 * point in the program or for a range of points in the program, and extract
 * specific location information from those data types.
 *
 * @{
 */

/**
 * \brief Identifies a specific source location within a translation
 * unit.
 *
 * Use clang_getExpansionLocation() or clang_getSpellingLocation()
 * to map a source location to a particular file, line, and column.
 */
typedef struct {
  const void *ptr_data[2];
  unsigned int_data;
} CXSourceLocation;

/**
 * \brief Identifies a half-open character range in the source code.
 *
 * Use clang_getRangeStart() and clang_getRangeEnd() to retrieve the
 * starting and end locations from a source range, respectively.
 */
typedef struct {
  const void *ptr_data[2];
  unsigned begin_int_data;
  unsigned end_int_data;
} CXSourceRange;

/**
 * \brief Retrieve a NULL (invalid) source location.
 */
CINDEX_LINKAGE CXSourceLocation clang_getNullLocation(void);

/**
 * \brief Determine whether two source locations, which must refer into
 * the same translation unit, refer to exactly the same point in the source
 * code.
 *
 * \returns non-zero if the source locations refer to the same location, zero
 * if they refer to different locations.
 */
CINDEX_LINKAGE unsigned clang_equalLocations(CXSourceLocation loc1,
                                             CXSourceLocation loc2);

/**
 * \brief Retrieves the source location associated with a given file/line/column
 * in a particular translation unit.
 */
CINDEX_LINKAGE CXSourceLocation clang_getLocation(CXTranslationUnit tu,
                                                  CXFile file,
                                                  unsigned line,
                                                  unsigned column);
/**
 * \brief Retrieves the source location associated with a given character offset
 * in a particular translation unit.
 */
CINDEX_LINKAGE CXSourceLocation clang_getLocationForOffset(CXTranslationUnit tu,
                                                           CXFile file,
                                                           unsigned offset);

/**
 * \brief Returns non-zero if the given source location is in a system header.
 */
CINDEX_LINKAGE int clang_Location_isInSystemHeader(CXSourceLocation location);

/**
 * \brief Returns non-zero if the given source location is in the main file of
 * the corresponding translation unit.
 */
CINDEX_LINKAGE int clang_Location_isFromMainFile(CXSourceLocation location);

/**
 * \brief Retrieve a NULL (invalid) source range.
 */
CINDEX_LINKAGE CXSourceRange clang_getNullRange(void);

/**
 * \brief Retrieve a source range given the beginning and ending source
 * locations.
 */
CINDEX_LINKAGE CXSourceRange clang_getRange(CXSourceLocation begin,
                                            CXSourceLocation end);

/**
 * \brief Determine whether two ranges are equivalent.
 *
 * \returns non-zero if the ranges are the same, zero if they differ.
 */
CINDEX_LINKAGE unsigned clang_equalRanges(CXSourceRange range1,
                                          CXSourceRange range2);

/**
 * \brief Returns non-zero if \p range is null.
 */
CINDEX_LINKAGE int clang_Range_isNull(CXSourceRange range);

/**
 * \brief Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro expansion, retrieves the
 * location of the macro expansion.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */
CINDEX_LINKAGE void clang_getExpansionLocation(CXSourceLocation location,
                                               CXFile *file,
                                               unsigned *line,
                                               unsigned *column,
                                               unsigned *offset);

/**
 * \brief Retrieve the file, line, column, and offset represented by
 * the given source location, as specified in a # line directive.
 *
 * Example: given the following source code in a file somefile.c
 *
 * \code
 * #123 "dummy.c" 1
 *
 * static int func(void)
 * {
 *     return 0;
 * }
 * \endcode
 *
 * the location information returned by this function would be
 *
 * File: dummy.c Line: 124 Column: 12
 *
 * whereas clang_getExpansionLocation would have returned
 *
 * File: somefile.c Line: 3 Column: 12
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param filename [out] if non-NULL, will be set to the filename of the
 * source location. Note that filenames returned will be for "virtual" files,
 * which don't necessarily exist on the machine running clang - e.g. when
 * parsing preprocessed output obtained from a different environment. If
 * a non-NULL value is passed in, remember to dispose of the returned value
 * using \c clang_disposeString() once you've finished with it. For an invalid
 * source location, an empty string is returned.
 *
 * \param line [out] if non-NULL, will be set to the line number of the
 * source location. For an invalid source location, zero is returned.
 *
 * \param column [out] if non-NULL, will be set to the column number of the
 * source location. For an invalid source location, zero is returned.
 */
CINDEX_LINKAGE void clang_getPresumedLocation(CXSourceLocation location,
                                              CXString *filename,
                                              unsigned *line,
                                              unsigned *column);

/**
 * \brief Legacy API to retrieve the file, line, column, and offset represented
 * by the given source location.
 *
 * This interface has been replaced by the newer interface
 * #clang_getExpansionLocation(). See that interface's documentation for
 * details.
 */
CINDEX_LINKAGE void clang_getInstantiationLocation(CXSourceLocation location,
                                                   CXFile *file,
                                                   unsigned *line,
                                                   unsigned *column,
                                                   unsigned *offset);

/**
 * \brief Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro instantiation, return where the
 * location was originally spelled in the source file.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */
CINDEX_LINKAGE void clang_getSpellingLocation(CXSourceLocation location,
                                              CXFile *file,
                                              unsigned *line,
                                              unsigned *column,
                                              unsigned *offset);

/**
 * \brief Retrieve the file, line, column, and offset represented by
 * the given source location.
 *
 * If the location refers into a macro expansion, return where the macro was
 * expanded or where the macro argument was written, if the location points at
 * a macro argument.
 *
 * \param location the location within a source file that will be decomposed
 * into its parts.
 *
 * \param file [out] if non-NULL, will be set to the file to which the given
 * source location points.
 *
 * \param line [out] if non-NULL, will be set to the line to which the given
 * source location points.
 *
 * \param column [out] if non-NULL, will be set to the column to which the given
 * source location points.
 *
 * \param offset [out] if non-NULL, will be set to the offset into the
 * buffer to which the given source location points.
 */
CINDEX_LINKAGE void clang_getFileLocation(CXSourceLocation location,
                                          CXFile *file,
                                          unsigned *line,
                                          unsigned *column,
                                          unsigned *offset);

/**
 * \brief Retrieve a source location representing the first character within a
 * source range.
 */
CINDEX_LINKAGE CXSourceLocation clang_getRangeStart(CXSourceRange range);

/**
 * \brief Retrieve a source location representing the last character within a
 * source range.
 */
CINDEX_LINKAGE CXSourceLocation clang_getRangeEnd(CXSourceRange range);

/**
 * \brief Identifies an array of ranges.
 */
typedef struct {
  /** \brief The number of ranges in the \c ranges array. */
  unsigned count;
  /**
   * \brief An array of \c CXSourceRanges.
   */
  CXSourceRange *ranges;
} CXSourceRangeList;

/**
 * \brief Retrieve all ranges that were skipped by the preprocessor.
 *
 * The preprocessor will skip lines when they are surrounded by an
 * if/ifdef/ifndef directive whose condition does not evaluate to true.
 */
CINDEX_LINKAGE CXSourceRangeList *clang_getSkippedRanges(CXTranslationUnit tu,
                                                         CXFile file);

/**
 * \brief Retrieve all ranges from all files that were skipped by the
 * preprocessor.
 *
 * The preprocessor will skip lines when they are surrounded by an
 * if/ifdef/ifndef directive whose condition does not evaluate to true.
 */
CINDEX_LINKAGE CXSourceRangeList *clang_getAllSkippedRanges(CXTranslationUnit tu);

/**
 * \brief Destroy the given \c CXSourceRangeList.
 */
CINDEX_LINKAGE void clang_disposeSourceRangeList(CXSourceRangeList *ranges);

/**
 * @}
 */

/**
 * \defgroup CINDEX_DIAG Diagnostic reporting
 *
 * @{
 */

/**
 * \brief Describes the severity of a particular diagnostic.
 */
enum CXDiagnosticSeverity {
  /**
   * \brief A diagnostic that has been suppressed, e.g., by a command-line
   * option.
   */
  CXDiagnostic_Ignored = 0,

  /**
   * \brief This diagnostic is a note that should be attached to the
   * previous (non-note) diagnostic.
   */
  CXDiagnostic_Note    = 1,

  /**
   * \brief This diagnostic indicates suspicious code that may not be
   * wrong.
   */
  CXDiagnostic_Warning = 2,

  /**
   * \brief This diagnostic indicates that the code is ill-formed.
   */
  CXDiagnostic_Error   = 3,

  /**
   * \brief This diagnostic indicates that the code is ill-formed such
   * that future parser recovery is unlikely to produce useful
   * results.
   */
  CXDiagnostic_Fatal   = 4
};

/**
 * \brief A single diagnostic, containing the diagnostic's severity,
 * location, text, source ranges, and fix-it hints.
 */
typedef void *CXDiagnostic;

/**
 * \brief A group of CXDiagnostics.
 */
typedef void *CXDiagnosticSet;
  
/**
 * \brief Determine the number of diagnostics in a CXDiagnosticSet.
 */
CINDEX_LINKAGE unsigned clang_getNumDiagnosticsInSet(CXDiagnosticSet Diags);

/**
 * \brief Retrieve a diagnostic associated with the given CXDiagnosticSet.
 *
 * \param Diags the CXDiagnosticSet to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */
CINDEX_LINKAGE CXDiagnostic clang_getDiagnosticInSet(CXDiagnosticSet Diags,
                                                     unsigned Index);  

/**
 * \brief Describes the kind of error that occurred (if any) in a call to
 * \c clang_loadDiagnostics.
 */
enum CXLoadDiag_Error {
  /**
   * \brief Indicates that no error occurred.
   */
  CXLoadDiag_None = 0,
  
  /**
   * \brief Indicates that an unknown error occurred while attempting to
   * deserialize diagnostics.
   */
  CXLoadDiag_Unknown = 1,
  
  /**
   * \brief Indicates that the file containing the serialized diagnostics
   * could not be opened.
   */
  CXLoadDiag_CannotLoad = 2,
  
  /**
   * \brief Indicates that the serialized diagnostics file is invalid or
   * corrupt.
   */
  CXLoadDiag_InvalidFile = 3
};
  
/**
 * \brief Deserialize a set of diagnostics from a Clang diagnostics bitcode
 * file.
 *
 * \param file The name of the file to deserialize.
 * \param error A pointer to a enum value recording if there was a problem
 *        deserializing the diagnostics.
 * \param errorString A pointer to a CXString for recording the error string
 *        if the file was not successfully loaded.
 *
 * \returns A loaded CXDiagnosticSet if successful, and NULL otherwise.  These
 * diagnostics should be released using clang_disposeDiagnosticSet().
 */
CINDEX_LINKAGE CXDiagnosticSet clang_loadDiagnostics(const char *file,
                                                  enum CXLoadDiag_Error *error,
                                                  CXString *errorString);

/**
 * \brief Release a CXDiagnosticSet and all of its contained diagnostics.
 */
CINDEX_LINKAGE void clang_disposeDiagnosticSet(CXDiagnosticSet Diags);

/**
 * \brief Retrieve the child diagnostics of a CXDiagnostic. 
 *
 * This CXDiagnosticSet does not need to be released by
 * clang_disposeDiagnosticSet.
 */
CINDEX_LINKAGE CXDiagnosticSet clang_getChildDiagnostics(CXDiagnostic D);

/**
 * \brief Determine the number of diagnostics produced for the given
 * translation unit.
 */
CINDEX_LINKAGE unsigned clang_getNumDiagnostics(CXTranslationUnit Unit);

/**
 * \brief Retrieve a diagnostic associated with the given translation unit.
 *
 * \param Unit the translation unit to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */
CINDEX_LINKAGE CXDiagnostic clang_getDiagnostic(CXTranslationUnit Unit,
                                                unsigned Index);

/**
 * \brief Retrieve the complete set of diagnostics associated with a
 *        translation unit.
 *
 * \param Unit the translation unit to query.
 */
CINDEX_LINKAGE CXDiagnosticSet
  clang_getDiagnosticSetFromTU(CXTranslationUnit Unit);  

/**
 * \brief Destroy a diagnostic.
 */
CINDEX_LINKAGE void clang_disposeDiagnostic(CXDiagnostic Diagnostic);

/**
 * \brief Options to control the display of diagnostics.
 *
 * The values in this enum are meant to be combined to customize the
 * behavior of \c clang_formatDiagnostic().
 */
enum CXDiagnosticDisplayOptions {
  /**
   * \brief Display the source-location information where the
   * diagnostic was located.
   *
   * When set, diagnostics will be prefixed by the file, line, and
   * (optionally) column to which the diagnostic refers. For example,
   *
   * \code
   * test.c:28: warning: extra tokens at end of #endif directive
   * \endcode
   *
   * This option corresponds to the clang flag \c -fshow-source-location.
   */
  CXDiagnostic_DisplaySourceLocation = 0x01,

  /**
   * \brief If displaying the source-location information of the
   * diagnostic, also include the column number.
   *
   * This option corresponds to the clang flag \c -fshow-column.
   */
  CXDiagnostic_DisplayColumn = 0x02,

  /**
   * \brief If displaying the source-location information of the
   * diagnostic, also include information about source ranges in a
   * machine-parsable format.
   *
   * This option corresponds to the clang flag
   * \c -fdiagnostics-print-source-range-info.
   */
  CXDiagnostic_DisplaySourceRanges = 0x04,
  
  /**
   * \brief Display the option name associated with this diagnostic, if any.
   *
   * The option name displayed (e.g., -Wconversion) will be placed in brackets
   * after the diagnostic text. This option corresponds to the clang flag
   * \c -fdiagnostics-show-option.
   */
  CXDiagnostic_DisplayOption = 0x08,
  
  /**
   * \brief Display the category number associated with this diagnostic, if any.
   *
   * The category number is displayed within brackets after the diagnostic text.
   * This option corresponds to the clang flag 
   * \c -fdiagnostics-show-category=id.
   */
  CXDiagnostic_DisplayCategoryId = 0x10,

  /**
   * \brief Display the category name associated with this diagnostic, if any.
   *
   * The category name is displayed within brackets after the diagnostic text.
   * This option corresponds to the clang flag 
   * \c -fdiagnostics-show-category=name.
   */
  CXDiagnostic_DisplayCategoryName = 0x20
};

/**
 * \brief Format the given diagnostic in a manner that is suitable for display.
 *
 * This routine will format the given diagnostic to a string, rendering
 * the diagnostic according to the various options given. The
 * \c clang_defaultDiagnosticDisplayOptions() function returns the set of
 * options that most closely mimics the behavior of the clang compiler.
 *
 * \param Diagnostic The diagnostic to print.
 *
 * \param Options A set of options that control the diagnostic display,
 * created by combining \c CXDiagnosticDisplayOptions values.
 *
 * \returns A new string containing for formatted diagnostic.
 */
CINDEX_LINKAGE CXString clang_formatDiagnostic(CXDiagnostic Diagnostic,
                                               unsigned Options);

/**
 * \brief Retrieve the set of display options most similar to the
 * default behavior of the clang compiler.
 *
 * \returns A set of display options suitable for use with \c
 * clang_formatDiagnostic().
 */
CINDEX_LINKAGE unsigned clang_defaultDiagnosticDisplayOptions(void);

/**
 * \brief Determine the severity of the given diagnostic.
 */
CINDEX_LINKAGE enum CXDiagnosticSeverity
clang_getDiagnosticSeverity(CXDiagnostic);

/**
 * \brief Retrieve the source location of the given diagnostic.
 *
 * This location is where Clang would print the caret ('^') when
 * displaying the diagnostic on the command line.
 */
CINDEX_LINKAGE CXSourceLocation clang_getDiagnosticLocation(CXDiagnostic);

/**
 * \brief Retrieve the text of the given diagnostic.
 */
CINDEX_LINKAGE CXString clang_getDiagnosticSpelling(CXDiagnostic);

/**
 * \brief Retrieve the name of the command-line option that enabled this
 * diagnostic.
 *
 * \param Diag The diagnostic to be queried.
 *
 * \param Disable If non-NULL, will be set to the option that disables this
 * diagnostic (if any).
 *
 * \returns A string that contains the command-line option used to enable this
 * warning, such as "-Wconversion" or "-pedantic". 
 */
CINDEX_LINKAGE CXString clang_getDiagnosticOption(CXDiagnostic Diag,
                                                  CXString *Disable);

/**
 * \brief Retrieve the category number for this diagnostic.
 *
 * Diagnostics can be categorized into groups along with other, related
 * diagnostics (e.g., diagnostics under the same warning flag). This routine 
 * retrieves the category number for the given diagnostic.
 *
 * \returns The number of the category that contains this diagnostic, or zero
 * if this diagnostic is uncategorized.
 */
CINDEX_LINKAGE unsigned clang_getDiagnosticCategory(CXDiagnostic);

/**
 * \brief Retrieve the name of a particular diagnostic category.  This
 *  is now deprecated.  Use clang_getDiagnosticCategoryText()
 *  instead.
 *
 * \param Category A diagnostic category number, as returned by 
 * \c clang_getDiagnosticCategory().
 *
 * \returns The name of the given diagnostic category.
 */
CINDEX_DEPRECATED CINDEX_LINKAGE
CXString clang_getDiagnosticCategoryName(unsigned Category);

/**
 * \brief Retrieve the diagnostic category text for a given diagnostic.
 *
 * \returns The text of the given diagnostic category.
 */
CINDEX_LINKAGE CXString clang_getDiagnosticCategoryText(CXDiagnostic);
  
/**
 * \brief Determine the number of source ranges associated with the given
 * diagnostic.
 */
CINDEX_LINKAGE unsigned clang_getDiagnosticNumRanges(CXDiagnostic);

/**
 * \brief Retrieve a source range associated with the diagnostic.
 *
 * A diagnostic's source ranges highlight important elements in the source
 * code. On the command line, Clang displays source ranges by
 * underlining them with '~' characters.
 *
 * \param Diagnostic the diagnostic whose range is being extracted.
 *
 * \param Range the zero-based index specifying which range to
 *
 * \returns the requested source range.
 */
CINDEX_LINKAGE CXSourceRange clang_getDiagnosticRange(CXDiagnostic Diagnostic,
                                                      unsigned Range);

/**
 * \brief Determine the number of fix-it hints associated with the
 * given diagnostic.
 */
CINDEX_LINKAGE unsigned clang_getDiagnosticNumFixIts(CXDiagnostic Diagnostic);

/**
 * \brief Retrieve the replacement information for a given fix-it.
 *
 * Fix-its are described in terms of a source range whose contents
 * should be replaced by a string. This approach generalizes over
 * three kinds of operations: removal of source code (the range covers
 * the code to be removed and the replacement string is empty),
 * replacement of source code (the range covers the code to be
 * replaced and the replacement string provides the new code), and
 * insertion (both the start and end of the range point at the
 * insertion location, and the replacement string provides the text to
 * insert).
 *
 * \param Diagnostic The diagnostic whose fix-its are being queried.
 *
 * \param FixIt The zero-based index of the fix-it.
 *
 * \param ReplacementRange The source range whose contents will be
 * replaced with the returned replacement string. Note that source
 * ranges are half-open ranges [a, b), so the source code should be
 * replaced from a and up to (but not including) b.
 *
 * \returns A string containing text that should be replace the source
 * code indicated by the \c ReplacementRange.
 */
CINDEX_LINKAGE CXString clang_getDiagnosticFixIt(CXDiagnostic Diagnostic,
                                                 unsigned FixIt,
                                               CXSourceRange *ReplacementRange);

/**
 * @}
 */

/**
 * \defgroup CINDEX_TRANSLATION_UNIT Translation unit manipulation
 *
 * The routines in this group provide the ability to create and destroy
 * translation units from files, either by parsing the contents of the files or
 * by reading in a serialized representation of a translation unit.
 *
 * @{
 */

/**
 * \brief Get the original translation unit source file name.
 */
CINDEX_LINKAGE CXString
clang_getTranslationUnitSpelling(CXTranslationUnit CTUnit);

/**
 * \brief Return the CXTranslationUnit for a given source file and the provided
 * command line arguments one would pass to the compiler.
 *
 * Note: The 'source_filename' argument is optional.  If the caller provides a
 * NULL pointer, the name of the source file is expected to reside in the
 * specified command line arguments.
 *
 * Note: When encountered in 'clang_command_line_args', the following options
 * are ignored:
 *
 *   '-c'
 *   '-emit-ast'
 *   '-fsyntax-only'
 *   '-o \<output file>'  (both '-o' and '\<output file>' are ignored)
 *
 * \param CIdx The index object with which the translation unit will be
 * associated.
 *
 * \param source_filename The name of the source file to load, or NULL if the
 * source file is included in \p clang_command_line_args.
 *
 * \param num_clang_command_line_args The number of command-line arguments in
 * \p clang_command_line_args.
 *
 * \param clang_command_line_args The command-line arguments that would be
 * passed to the \c clang executable if it were being invoked out-of-process.
 * These command-line options will be parsed and will affect how the translation
 * unit is parsed. Note that the following options are ignored: '-c',
 * '-emit-ast', '-fsyntax-only' (which is the default), and '-o \<output file>'.
 *
 * \param num_unsaved_files the number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param unsaved_files the files that have not yet been saved to disk
 * but may be required for code completion, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 */
CINDEX_LINKAGE CXTranslationUnit clang_createTranslationUnitFromSourceFile(
                                         CXIndex CIdx,
                                         const char *source_filename,
                                         int num_clang_command_line_args,
                                   const char * const *clang_command_line_args,
                                         unsigned num_unsaved_files,
                                         struct CXUnsavedFile *unsaved_files);

/**
 * \brief Same as \c clang_createTranslationUnit2, but returns
 * the \c CXTranslationUnit instead of an error code.  In case of an error this
 * routine returns a \c NULL \c CXTranslationUnit, without further detailed
 * error codes.
 */
CINDEX_LINKAGE CXTranslationUnit clang_createTranslationUnit(
    CXIndex CIdx,
    const char *ast_filename);

/**
 * \brief Create a translation unit from an AST file (\c -emit-ast).
 *
 * \param[out] out_TU A non-NULL pointer to store the created
 * \c CXTranslationUnit.
 *
 * \returns Zero on success, otherwise returns an error code.
 */
CINDEX_LINKAGE enum CXErrorCode clang_createTranslationUnit2(
    CXIndex CIdx,
    const char *ast_filename,
    CXTranslationUnit *out_TU);

/**
 * \brief Flags that control the creation of translation units.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * constructing the translation unit.
 */
enum CXTranslationUnit_Flags {
  /**
   * \brief Used to indicate that no special translation-unit options are
   * needed.
   */
  CXTranslationUnit_None = 0x0,

  /**
   * \brief Used to indicate that the parser should construct a "detailed"
   * preprocessing record, including all macro definitions and instantiations.
   *
   * Constructing a detailed preprocessing record requires more memory
   * and time to parse, since the information contained in the record
   * is usually not retained. However, it can be useful for
   * applications that require more detailed information about the
   * behavior of the preprocessor.
   */
  CXTranslationUnit_DetailedPreprocessingRecord = 0x01,

  /**
   * \brief Used to indicate that the translation unit is incomplete.
   *
   * When a translation unit is considered "incomplete", semantic
   * analysis that is typically performed at the end of the
   * translation unit will be suppressed. For example, this suppresses
   * the completion of tentative declarations in C and of
   * instantiation of implicitly-instantiation function templates in
   * C++. This option is typically used when parsing a header with the
   * intent of producing a precompiled header.
   */
  CXTranslationUnit_Incomplete = 0x02,
  
  /**
   * \brief Used to indicate that the translation unit should be built with an 
   * implicit precompiled header for the preamble.
   *
   * An implicit precompiled header is used as an optimization when a
   * particular translation unit is likely to be reparsed many times
   * when the sources aren't changing that often. In this case, an
   * implicit precompiled header will be built containing all of the
   * initial includes at the top of the main file (what we refer to as
   * the "preamble" of the file). In subsequent parses, if the
   * preamble or the files in it have not changed, \c
   * clang_reparseTranslationUnit() will re-use the implicit
   * precompiled header to improve parsing performance.
   */
  CXTranslationUnit_PrecompiledPreamble = 0x04,
  
  /**
   * \brief Used to indicate that the translation unit should cache some
   * code-completion results with each reparse of the source file.
   *
   * Caching of code-completion results is a performance optimization that
   * introduces some overhead to reparsing but improves the performance of
   * code-completion operations.
   */
  CXTranslationUnit_CacheCompletionResults = 0x08,

  /**
   * \brief Used to indicate that the translation unit will be serialized with
   * \c clang_saveTranslationUnit.
   *
   * This option is typically used when parsing a header with the intent of
   * producing a precompiled header.
   */
  CXTranslationUnit_ForSerialization = 0x10,

  /**
   * \brief DEPRECATED: Enabled chained precompiled preambles in C++.
   *
   * Note: this is a *temporary* option that is available only while
   * we are testing C++ precompiled preamble support. It is deprecated.
   */
  CXTranslationUnit_CXXChainedPCH = 0x20,

  /**
   * \brief Used to indicate that function/method bodies should be skipped while
   * parsing.
   *
   * This option can be used to search for declarations/definitions while
   * ignoring the usages.
   */
  CXTranslationUnit_SkipFunctionBodies = 0x40,

  /**
   * \brief Used to indicate that brief documentation comments should be
   * included into the set of code completions returned from this translation
   * unit.
   */
  CXTranslationUnit_IncludeBriefCommentsInCodeCompletion = 0x80,

  /**
   * \brief Used to indicate that the precompiled preamble should be created on
   * the first parse. Otherwise it will be created on the first reparse. This
   * trades runtime on the first parse (serializing the preamble takes time) for
   * reduced runtime on the second parse (can now reuse the preamble).
   */
  CXTranslationUnit_CreatePreambleOnFirstParse = 0x100,

  /**
   * \brief Do not stop processing when fatal errors are encountered.
   *
   * When fatal errors are encountered while parsing a translation unit,
   * semantic analysis is typically stopped early when compiling code. A common
   * source for fatal errors are unresolvable include files. For the
   * purposes of an IDE, this is undesirable behavior and as much information
   * as possible should be reported. Use this flag to enable this behavior.
   */
  CXTranslationUnit_KeepGoing = 0x200
};

/**
 * \brief Returns the set of flags that is suitable for parsing a translation
 * unit that is being edited.
 *
 * The set of flags returned provide options for \c clang_parseTranslationUnit()
 * to indicate that the translation unit is likely to be reparsed many times,
 * either explicitly (via \c clang_reparseTranslationUnit()) or implicitly
 * (e.g., by code completion (\c clang_codeCompletionAt())). The returned flag
 * set contains an unspecified set of optimizations (e.g., the precompiled 
 * preamble) geared toward improving the performance of these routines. The
 * set of optimizations enabled may change from one version to the next.
 */
CINDEX_LINKAGE unsigned clang_defaultEditingTranslationUnitOptions(void);

/**
 * \brief Same as \c clang_parseTranslationUnit2, but returns
 * the \c CXTranslationUnit instead of an error code.  In case of an error this
 * routine returns a \c NULL \c CXTranslationUnit, without further detailed
 * error codes.
 */
CINDEX_LINKAGE CXTranslationUnit
clang_parseTranslationUnit(CXIndex CIdx,
                           const char *source_filename,
                           const char *const *command_line_args,
                           int num_command_line_args,
                           struct CXUnsavedFile *unsaved_files,
                           unsigned num_unsaved_files,
                           unsigned options);

/**
 * \brief Parse the given source file and the translation unit corresponding
 * to that file.
 *
 * This routine is the main entry point for the Clang C API, providing the
 * ability to parse a source file into a translation unit that can then be
 * queried by other functions in the API. This routine accepts a set of
 * command-line arguments so that the compilation can be configured in the same
 * way that the compiler is configured on the command line.
 *
 * \param CIdx The index object with which the translation unit will be 
 * associated.
 *
 * \param source_filename The name of the source file to load, or NULL if the
 * source file is included in \c command_line_args.
 *
 * \param command_line_args The command-line arguments that would be
 * passed to the \c clang executable if it were being invoked out-of-process.
 * These command-line options will be parsed and will affect how the translation
 * unit is parsed. Note that the following options are ignored: '-c', 
 * '-emit-ast', '-fsyntax-only' (which is the default), and '-o \<output file>'.
 *
 * \param num_command_line_args The number of command-line arguments in
 * \c command_line_args.
 *
 * \param unsaved_files the files that have not yet been saved to disk
 * but may be required for parsing, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 *
 * \param num_unsaved_files the number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param options A bitmask of options that affects how the translation unit
 * is managed but not its compilation. This should be a bitwise OR of the
 * CXTranslationUnit_XXX flags.
 *
 * \param[out] out_TU A non-NULL pointer to store the created
 * \c CXTranslationUnit, describing the parsed code and containing any
 * diagnostics produced by the compiler.
 *
 * \returns Zero on success, otherwise returns an error code.
 */
CINDEX_LINKAGE enum CXErrorCode
clang_parseTranslationUnit2(CXIndex CIdx,
                            const char *source_filename,
                            const char *const *command_line_args,
                            int num_command_line_args,
                            struct CXUnsavedFile *unsaved_files,
                            unsigned num_unsaved_files,
                            unsigned options,
                            CXTranslationUnit *out_TU);

/**
 * \brief Same as clang_parseTranslationUnit2 but requires a full command line
 * for \c command_line_args including argv[0]. This is useful if the standard
 * library paths are relative to the binary.
 */
CINDEX_LINKAGE enum CXErrorCode clang_parseTranslationUnit2FullArgv(
    CXIndex CIdx, const char *source_filename,
    const char *const *command_line_args, int num_command_line_args,
    struct CXUnsavedFile *unsaved_files, unsigned num_unsaved_files,
    unsigned options, CXTranslationUnit *out_TU);

/**
 * \brief Flags that control how translation units are saved.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * saving the translation unit.
 */
enum CXSaveTranslationUnit_Flags {
  /**
   * \brief Used to indicate that no special saving options are needed.
   */
  CXSaveTranslationUnit_None = 0x0
};

/**
 * \brief Returns the set of flags that is suitable for saving a translation
 * unit.
 *
 * The set of flags returned provide options for
 * \c clang_saveTranslationUnit() by default. The returned flag
 * set contains an unspecified set of options that save translation units with
 * the most commonly-requested data.
 */
CINDEX_LINKAGE unsigned clang_defaultSaveOptions(CXTranslationUnit TU);

/**
 * \brief Describes the kind of error that occurred (if any) in a call to
 * \c clang_saveTranslationUnit().
 */
enum CXSaveError {
  /**
   * \brief Indicates that no error occurred while saving a translation unit.
   */
  CXSaveError_None = 0,
  
  /**
   * \brief Indicates that an unknown error occurred while attempting to save
   * the file.
   *
   * This error typically indicates that file I/O failed when attempting to 
   * write the file.
   */
  CXSaveError_Unknown = 1,
  
  /**
   * \brief Indicates that errors during translation prevented this attempt
   * to save the translation unit.
   * 
   * Errors that prevent the translation unit from being saved can be
   * extracted using \c clang_getNumDiagnostics() and \c clang_getDiagnostic().
   */
  CXSaveError_TranslationErrors = 2,
  
  /**
   * \brief Indicates that the translation unit to be saved was somehow
   * invalid (e.g., NULL).
   */
  CXSaveError_InvalidTU = 3
};
  
/**
 * \brief Saves a translation unit into a serialized representation of
 * that translation unit on disk.
 *
 * Any translation unit that was parsed without error can be saved
 * into a file. The translation unit can then be deserialized into a
 * new \c CXTranslationUnit with \c clang_createTranslationUnit() or,
 * if it is an incomplete translation unit that corresponds to a
 * header, used as a precompiled header when parsing other translation
 * units.
 *
 * \param TU The translation unit to save.
 *
 * \param FileName The file to which the translation unit will be saved.
 *
 * \param options A bitmask of options that affects how the translation unit
 * is saved. This should be a bitwise OR of the
 * CXSaveTranslationUnit_XXX flags.
 *
 * \returns A value that will match one of the enumerators of the CXSaveError
 * enumeration. Zero (CXSaveError_None) indicates that the translation unit was 
 * saved successfully, while a non-zero value indicates that a problem occurred.
 */
CINDEX_LINKAGE int clang_saveTranslationUnit(CXTranslationUnit TU,
                                             const char *FileName,
                                             unsigned options);

/**
 * \brief Destroy the specified CXTranslationUnit object.
 */
CINDEX_LINKAGE void clang_disposeTranslationUnit(CXTranslationUnit);

/**
 * \brief Flags that control the reparsing of translation units.
 *
 * The enumerators in this enumeration type are meant to be bitwise
 * ORed together to specify which options should be used when
 * reparsing the translation unit.
 */
enum CXReparse_Flags {
  /**
   * \brief Used to indicate that no special reparsing options are needed.
   */
  CXReparse_None = 0x0
};
 
/**
 * \brief Returns the set of flags that is suitable for reparsing a translation
 * unit.
 *
 * The set of flags returned provide options for
 * \c clang_reparseTranslationUnit() by default. The returned flag
 * set contains an unspecified set of optimizations geared toward common uses
 * of reparsing. The set of optimizations enabled may change from one version 
 * to the next.
 */
CINDEX_LINKAGE unsigned clang_defaultReparseOptions(CXTranslationUnit TU);

/**
 * \brief Reparse the source files that produced this translation unit.
 *
 * This routine can be used to re-parse the source files that originally
 * created the given translation unit, for example because those source files
 * have changed (either on disk or as passed via \p unsaved_files). The
 * source code will be reparsed with the same command-line options as it
 * was originally parsed. 
 *
 * Reparsing a translation unit invalidates all cursors and source locations
 * that refer into that translation unit. This makes reparsing a translation
 * unit semantically equivalent to destroying the translation unit and then
 * creating a new translation unit with the same command-line arguments.
 * However, it may be more efficient to reparse a translation 
 * unit using this routine.
 *
 * \param TU The translation unit whose contents will be re-parsed. The
 * translation unit must originally have been built with 
 * \c clang_createTranslationUnitFromSourceFile().
 *
 * \param num_unsaved_files The number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param unsaved_files The files that have not yet been saved to disk
 * but may be required for parsing, including the contents of
 * those files.  The contents and name of these files (as specified by
 * CXUnsavedFile) are copied when necessary, so the client only needs to
 * guarantee their validity until the call to this function returns.
 * 
 * \param options A bitset of options composed of the flags in CXReparse_Flags.
 * The function \c clang_defaultReparseOptions() produces a default set of
 * options recommended for most uses, based on the translation unit.
 *
 * \returns 0 if the sources could be reparsed.  A non-zero error code will be
 * returned if reparsing was impossible, such that the translation unit is
 * invalid. In such cases, the only valid call for \c TU is
 * \c clang_disposeTranslationUnit(TU).  The error codes returned by this
 * routine are described by the \c CXErrorCode enum.
 */
CINDEX_LINKAGE int clang_reparseTranslationUnit(CXTranslationUnit TU,
                                                unsigned num_unsaved_files,
                                          struct CXUnsavedFile *unsaved_files,
                                                unsigned options);

/**
  * \brief Categorizes how memory is being used by a translation unit.
  */
enum CXTUResourceUsageKind {
  CXTUResourceUsage_AST = 1,
  CXTUResourceUsage_Identifiers = 2,
  CXTUResourceUsage_Selectors = 3,
  CXTUResourceUsage_GlobalCompletionResults = 4,
  CXTUResourceUsage_SourceManagerContentCache = 5,
  CXTUResourceUsage_AST_SideTables = 6,
  CXTUResourceUsage_SourceManager_Membuffer_Malloc = 7,
  CXTUResourceUsage_SourceManager_Membuffer_MMap = 8,
  CXTUResourceUsage_ExternalASTSource_Membuffer_Malloc = 9, 
  CXTUResourceUsage_ExternalASTSource_Membuffer_MMap = 10, 
  CXTUResourceUsage_Preprocessor = 11,
  CXTUResourceUsage_PreprocessingRecord = 12,
  CXTUResourceUsage_SourceManager_DataStructures = 13,
  CXTUResourceUsage_Preprocessor_HeaderSearch = 14,
  CXTUResourceUsage_MEMORY_IN_BYTES_BEGIN = CXTUResourceUsage_AST,
  CXTUResourceUsage_MEMORY_IN_BYTES_END =
    CXTUResourceUsage_Preprocessor_HeaderSearch,

  CXTUResourceUsage_First = CXTUResourceUsage_AST,
  CXTUResourceUsage_Last = CXTUResourceUsage_Preprocessor_HeaderSearch
};

/**
  * \brief Returns the human-readable null-terminated C string that represents
  *  the name of the memory category.  This string should never be freed.
  */
CINDEX_LINKAGE
const char *clang_getTUResourceUsageName(enum CXTUResourceUsageKind kind);

typedef struct CXTUResourceUsageEntry {
  /* \brief The memory usage category. */
  enum CXTUResourceUsageKind kind;  
  /* \brief Amount of resources used. 
      The units will depend on the resource kind. */
  unsigned long amount;
} CXTUResourceUsageEntry;

/**
  * \brief The memory usage of a CXTranslationUnit, broken into categories.
  */
typedef struct CXTUResourceUsage {
  /* \brief Private data member, used for queries. */
  void *data;

  /* \brief The number of entries in the 'entries' array. */
  unsigned numEntries;

  /* \brief An array of key-value pairs, representing the breakdown of memory
            usage. */
  CXTUResourceUsageEntry *entries;

} CXTUResourceUsage;

/**
  * \brief Return the memory usage of a translation unit.  This object
  *  should be released with clang_disposeCXTUResourceUsage().
  */
CINDEX_LINKAGE CXTUResourceUsage clang_getCXTUResourceUsage(CXTranslationUnit TU);

CINDEX_LINKAGE void clang_disposeCXTUResourceUsage(CXTUResourceUsage usage);

/**
 * @}
 */

/**
 * \brief Describes the kind of entity that a cursor refers to.
 */
enum CXCursorKind {
  /* Declarations */
  /**
   * \brief A declaration whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed declarations have the same operations as any other kind
   * of declaration; one can extract their location information,
   * spelling, find their definitions, etc. However, the specific kind
   * of the declaration is not reported.
   */
  CXCursor_UnexposedDecl                 = 1,
  /** \brief A C or C++ struct. */
  CXCursor_StructDecl                    = 2,
  /** \brief A C or C++ union. */
  CXCursor_UnionDecl                     = 3,
  /** \brief A C++ class. */
  CXCursor_ClassDecl                     = 4,
  /** \brief An enumeration. */
  CXCursor_EnumDecl                      = 5,
  /**
   * \brief A field (in C) or non-static data member (in C++) in a
   * struct, union, or C++ class.
   */
  CXCursor_FieldDecl                     = 6,
  /** \brief An enumerator constant. */
  CXCursor_EnumConstantDecl              = 7,
  /** \brief A function. */
  CXCursor_FunctionDecl                  = 8,
  /** \brief A variable. */
  CXCursor_VarDecl                       = 9,
  /** \brief A function or method parameter. */
  CXCursor_ParmDecl                      = 10,
  /** \brief An Objective-C \@interface. */
  CXCursor_ObjCInterfaceDecl             = 11,
  /** \brief An Objective-C \@interface for a category. */
  CXCursor_ObjCCategoryDecl              = 12,
  /** \brief An Objective-C \@protocol declaration. */
  CXCursor_ObjCProtocolDecl              = 13,
  /** \brief An Objective-C \@property declaration. */
  CXCursor_ObjCPropertyDecl              = 14,
  /** \brief An Objective-C instance variable. */
  CXCursor_ObjCIvarDecl                  = 15,
  /** \brief An Objective-C instance method. */
  CXCursor_ObjCInstanceMethodDecl        = 16,
  /** \brief An Objective-C class method. */
  CXCursor_ObjCClassMethodDecl           = 17,
  /** \brief An Objective-C \@implementation. */
  CXCursor_ObjCImplementationDecl        = 18,
  /** \brief An Objective-C \@implementation for a category. */
  CXCursor_ObjCCategoryImplDecl          = 19,
  /** \brief A typedef. */
  CXCursor_TypedefDecl                   = 20,
  /** \brief A C++ class method. */
  CXCursor_CXXMethod                     = 21,
  /** \brief A C++ namespace. */
  CXCursor_Namespace                     = 22,
  /** \brief A linkage specification, e.g. 'extern "C"'. */
  CXCursor_LinkageSpec                   = 23,
  /** \brief A C++ constructor. */
  CXCursor_Constructor                   = 24,
  /** \brief A C++ destructor. */
  CXCursor_Destructor                    = 25,
  /** \brief A C++ conversion function. */
  CXCursor_ConversionFunction            = 26,
  /** \brief A C++ template type parameter. */
  CXCursor_TemplateTypeParameter         = 27,
  /** \brief A C++ non-type template parameter. */
  CXCursor_NonTypeTemplateParameter      = 28,
  /** \brief A C++ template template parameter. */
  CXCursor_TemplateTemplateParameter     = 29,
  /** \brief A C++ function template. */
  CXCursor_FunctionTemplate              = 30,
  /** \brief A C++ class template. */
  CXCursor_ClassTemplate                 = 31,
  /** \brief A C++ class template partial specialization. */
  CXCursor_ClassTemplatePartialSpecialization = 32,
  /** \brief A C++ namespace alias declaration. */
  CXCursor_NamespaceAlias                = 33,
  /** \brief A C++ using directive. */
  CXCursor_UsingDirective                = 34,
  /** \brief A C++ using declaration. */
  CXCursor_UsingDeclaration              = 35,
  /** \brief A C++ alias declaration */
  CXCursor_TypeAliasDecl                 = 36,
  /** \brief An Objective-C \@synthesize definition. */
  CXCursor_ObjCSynthesizeDecl            = 37,
  /** \brief An Objective-C \@dynamic definition. */
  CXCursor_ObjCDynamicDecl               = 38,
  /** \brief An access specifier. */
  CXCursor_CXXAccessSpecifier            = 39,

  CXCursor_FirstDecl                     = CXCursor_UnexposedDecl,
  CXCursor_LastDecl                      = CXCursor_CXXAccessSpecifier,

  /* References */
  CXCursor_FirstRef                      = 40, /* Decl references */
  CXCursor_ObjCSuperClassRef             = 40,
  CXCursor_ObjCProtocolRef               = 41,
  CXCursor_ObjCClassRef                  = 42,
  /**
   * \brief A reference to a type declaration.
   *
   * A type reference occurs anywhere where a type is named but not
   * declared. For example, given:
   *
   * \code
   * typedef unsigned size_type;
   * size_type size;
   * \endcode
   *
   * The typedef is a declaration of size_type (CXCursor_TypedefDecl),
   * while the type of the variable "size" is referenced. The cursor
   * referenced by the type of size is the typedef for size_type.
   */
  CXCursor_TypeRef                       = 43,
  CXCursor_CXXBaseSpecifier              = 44,
  /** 
   * \brief A reference to a class template, function template, template
   * template parameter, or class template partial specialization.
   */
  CXCursor_TemplateRef                   = 45,
  /**
   * \brief A reference to a namespace or namespace alias.
   */
  CXCursor_NamespaceRef                  = 46,
  /**
   * \brief A reference to a member of a struct, union, or class that occurs in 
   * some non-expression context, e.g., a designated initializer.
   */
  CXCursor_MemberRef                     = 47,
  /**
   * \brief A reference to a labeled statement.
   *
   * This cursor kind is used to describe the jump to "start_over" in the 
   * goto statement in the following example:
   *
   * \code
   *   start_over:
   *     ++counter;
   *
   *     goto start_over;
   * \endcode
   *
   * A label reference cursor refers to a label statement.
   */
  CXCursor_LabelRef                      = 48,
  
  /**
   * \brief A reference to a set of overloaded functions or function templates
   * that has not yet been resolved to a specific function or function template.
   *
   * An overloaded declaration reference cursor occurs in C++ templates where
   * a dependent name refers to a function. For example:
   *
   * \code
   * template<typename T> void swap(T&, T&);
   *
   * struct X { ... };
   * void swap(X&, X&);
   *
   * template<typename T>
   * void reverse(T* first, T* last) {
   *   while (first < last - 1) {
   *     swap(*first, *--last);
   *     ++first;
   *   }
   * }
   *
   * struct Y { };
   * void swap(Y&, Y&);
   * \endcode
   *
   * Here, the identifier "swap" is associated with an overloaded declaration
   * reference. In the template definition, "swap" refers to either of the two
   * "swap" functions declared above, so both results will be available. At
   * instantiation time, "swap" may also refer to other functions found via
   * argument-dependent lookup (e.g., the "swap" function at the end of the
   * example).
   *
   * The functions \c clang_getNumOverloadedDecls() and 
   * \c clang_getOverloadedDecl() can be used to retrieve the definitions
   * referenced by this cursor.
   */
  CXCursor_OverloadedDeclRef             = 49,
  
  /**
   * \brief A reference to a variable that occurs in some non-expression 
   * context, e.g., a C++ lambda capture list.
   */
  CXCursor_VariableRef                   = 50,
  
  CXCursor_LastRef                       = CXCursor_VariableRef,

  /* Error conditions */
  CXCursor_FirstInvalid                  = 70,
  CXCursor_InvalidFile                   = 70,
  CXCursor_NoDeclFound                   = 71,
  CXCursor_NotImplemented                = 72,
  CXCursor_InvalidCode                   = 73,
  CXCursor_LastInvalid                   = CXCursor_InvalidCode,

  /* Expressions */
  CXCursor_FirstExpr                     = 100,

  /**
   * \brief An expression whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed expressions have the same operations as any other kind
   * of expression; one can extract their location information,
   * spelling, children, etc. However, the specific kind of the
   * expression is not reported.
   */
  CXCursor_UnexposedExpr                 = 100,

  /**
   * \brief An expression that refers to some value declaration, such
   * as a function, variable, or enumerator.
   */
  CXCursor_DeclRefExpr                   = 101,

  /**
   * \brief An expression that refers to a member of a struct, union,
   * class, Objective-C class, etc.
   */
  CXCursor_MemberRefExpr                 = 102,

  /** \brief An expression that calls a function. */
  CXCursor_CallExpr                      = 103,

  /** \brief An expression that sends a message to an Objective-C
   object or class. */
  CXCursor_ObjCMessageExpr               = 104,

  /** \brief An expression that represents a block literal. */
  CXCursor_BlockExpr                     = 105,

  /** \brief An integer literal.
   */
  CXCursor_IntegerLiteral                = 106,

  /** \brief A floating point number literal.
   */
  CXCursor_FloatingLiteral               = 107,

  /** \brief An imaginary number literal.
   */
  CXCursor_ImaginaryLiteral              = 108,

  /** \brief A string literal.
   */
  CXCursor_StringLiteral                 = 109,

  /** \brief A character literal.
   */
  CXCursor_CharacterLiteral              = 110,

  /** \brief A parenthesized expression, e.g. "(1)".
   *
   * This AST node is only formed if full location information is requested.
   */
  CXCursor_ParenExpr                     = 111,

  /** \brief This represents the unary-expression's (except sizeof and
   * alignof).
   */
  CXCursor_UnaryOperator                 = 112,

  /** \brief [C99 6.5.2.1] Array Subscripting.
   */
  CXCursor_ArraySubscriptExpr            = 113,

  /** \brief A builtin binary operation expression such as "x + y" or
   * "x <= y".
   */
  CXCursor_BinaryOperator                = 114,

  /** \brief Compound assignment such as "+=".
   */
  CXCursor_CompoundAssignOperator        = 115,

  /** \brief The ?: ternary operator.
   */
  CXCursor_ConditionalOperator           = 116,

  /** \brief An explicit cast in C (C99 6.5.4) or a C-style cast in C++
   * (C++ [expr.cast]), which uses the syntax (Type)expr.
   *
   * For example: (int)f.
   */
  CXCursor_CStyleCastExpr                = 117,

  /** \brief [C99 6.5.2.5]
   */
  CXCursor_CompoundLiteralExpr           = 118,

  /** \brief Describes an C or C++ initializer list.
   */
  CXCursor_InitListExpr                  = 119,

  /** \brief The GNU address of label extension, representing &&label.
   */
  CXCursor_AddrLabelExpr                 = 120,

  /** \brief This is the GNU Statement Expression extension: ({int X=4; X;})
   */
  CXCursor_StmtExpr                      = 121,

  /** \brief Represents a C11 generic selection.
   */
  CXCursor_GenericSelectionExpr          = 122,

  /** \brief Implements the GNU __null extension, which is a name for a null
   * pointer constant that has integral type (e.g., int or long) and is the same
   * size and alignment as a pointer.
   *
   * The __null extension is typically only used by system headers, which define
   * NULL as __null in C++ rather than using 0 (which is an integer that may not
   * match the size of a pointer).
   */
  CXCursor_GNUNullExpr                   = 123,

  /** \brief C++'s static_cast<> expression.
   */
  CXCursor_CXXStaticCastExpr             = 124,

  /** \brief C++'s dynamic_cast<> expression.
   */
  CXCursor_CXXDynamicCastExpr            = 125,

  /** \brief C++'s reinterpret_cast<> expression.
   */
  CXCursor_CXXReinterpretCastExpr        = 126,

  /** \brief C++'s const_cast<> expression.
   */
  CXCursor_CXXConstCastExpr              = 127,

  /** \brief Represents an explicit C++ type conversion that uses "functional"
   * notion (C++ [expr.type.conv]).
   *
   * Example:
   * \code
   *   x = int(0.5);
   * \endcode
   */
  CXCursor_CXXFunctionalCastExpr         = 128,

  /** \brief A C++ typeid expression (C++ [expr.typeid]).
   */
  CXCursor_CXXTypeidExpr                 = 129,

  /** \brief [C++ 2.13.5] C++ Boolean Literal.
   */
  CXCursor_CXXBoolLiteralExpr            = 130,

  /** \brief [C++0x 2.14.7] C++ Pointer Literal.
   */
  CXCursor_CXXNullPtrLiteralExpr         = 131,

  /** \brief Represents the "this" expression in C++
   */
  CXCursor_CXXThisExpr                   = 132,

  /** \brief [C++ 15] C++ Throw Expression.
   *
   * This handles 'throw' and 'throw' assignment-expression. When
   * assignment-expression isn't present, Op will be null.
   */
  CXCursor_CXXThrowExpr                  = 133,

  /** \brief A new expression for memory allocation and constructor calls, e.g:
   * "new CXXNewExpr(foo)".
   */
  CXCursor_CXXNewExpr                    = 134,

  /** \brief A delete expression for memory deallocation and destructor calls,
   * e.g. "delete[] pArray".
   */
  CXCursor_CXXDeleteExpr                 = 135,

  /** \brief A unary expression. (noexcept, sizeof, or other traits)
   */
  CXCursor_UnaryExpr                     = 136,

  /** \brief An Objective-C string literal i.e. @"foo".
   */
  CXCursor_ObjCStringLiteral             = 137,

  /** \brief An Objective-C \@encode expression.
   */
  CXCursor_ObjCEncodeExpr                = 138,

  /** \brief An Objective-C \@selector expression.
   */
  CXCursor_ObjCSelectorExpr              = 139,

  /** \brief An Objective-C \@protocol expression.
   */
  CXCursor_ObjCProtocolExpr              = 140,

  /** \brief An Objective-C "bridged" cast expression, which casts between
   * Objective-C pointers and C pointers, transferring ownership in the process.
   *
   * \code
   *   NSString *str = (__bridge_transfer NSString *)CFCreateString();
   * \endcode
   */
  CXCursor_ObjCBridgedCastExpr           = 141,

  /** \brief Represents a C++0x pack expansion that produces a sequence of
   * expressions.
   *
   * A pack expansion expression contains a pattern (which itself is an
   * expression) followed by an ellipsis. For example:
   *
   * \code
   * template<typename F, typename ...Types>
   * void forward(F f, Types &&...args) {
   *  f(static_cast<Types&&>(args)...);
   * }
   * \endcode
   */
  CXCursor_PackExpansionExpr             = 142,

  /** \brief Represents an expression that computes the length of a parameter
   * pack.
   *
   * \code
   * template<typename ...Types>
   * struct count {
   *   static const unsigned value = sizeof...(Types);
   * };
   * \endcode
   */
  CXCursor_SizeOfPackExpr                = 143,

  /* \brief Represents a C++ lambda expression that produces a local function
   * object.
   *
   * \code
   * void abssort(float *x, unsigned N) {
   *   std::sort(x, x + N,
   *             [](float a, float b) {
   *               return std::abs(a) < std::abs(b);
   *             });
   * }
   * \endcode
   */
  CXCursor_LambdaExpr                    = 144,
  
  /** \brief Objective-c Boolean Literal.
   */
  CXCursor_ObjCBoolLiteralExpr           = 145,

  /** \brief Represents the "self" expression in an Objective-C method.
   */
  CXCursor_ObjCSelfExpr                  = 146,

  /** \brief OpenMP 4.0 [2.4, Array Section].
   */
  CXCursor_OMPArraySectionExpr           = 147,

  /** \brief Represents an @available(...) check.
   */
  CXCursor_ObjCAvailabilityCheckExpr     = 148,

  CXCursor_LastExpr                      = CXCursor_ObjCAvailabilityCheckExpr,

  /* Statements */
  CXCursor_FirstStmt                     = 200,
  /**
   * \brief A statement whose specific kind is not exposed via this
   * interface.
   *
   * Unexposed statements have the same operations as any other kind of
   * statement; one can extract their location information, spelling,
   * children, etc. However, the specific kind of the statement is not
   * reported.
   */
  CXCursor_UnexposedStmt                 = 200,
  
  /** \brief A labelled statement in a function. 
   *
   * This cursor kind is used to describe the "start_over:" label statement in 
   * the following example:
   *
   * \code
   *   start_over:
   *     ++counter;
   * \endcode
   *
   */
  CXCursor_LabelStmt                     = 201,

  /** \brief A group of statements like { stmt stmt }.
   *
   * This cursor kind is used to describe compound statements, e.g. function
   * bodies.
   */
  CXCursor_CompoundStmt                  = 202,

  /** \brief A case statement.
   */
  CXCursor_CaseStmt                      = 203,

  /** \brief A default statement.
   */
  CXCursor_DefaultStmt                   = 204,

  /** \brief An if statement
   */
  CXCursor_IfStmt                        = 205,

  /** \brief A switch statement.
   */
  CXCursor_SwitchStmt                    = 206,

  /** \brief A while statement.
   */
  CXCursor_WhileStmt                     = 207,

  /** \brief A do statement.
   */
  CXCursor_DoStmt                        = 208,

  /** \brief A for statement.
   */
  CXCursor_ForStmt                       = 209,

  /** \brief A goto statement.
   */
  CXCursor_GotoStmt                      = 210,

  /** \brief An indirect goto statement.
   */
  CXCursor_IndirectGotoStmt              = 211,

  /** \brief A continue statement.
   */
  CXCursor_ContinueStmt                  = 212,

  /** \brief A break statement.
   */
  CXCursor_BreakStmt                     = 213,

  /** \brief A return statement.
   */
  CXCursor_ReturnStmt                    = 214,

  /** \brief A GCC inline assembly statement extension.
   */
  CXCursor_GCCAsmStmt                    = 215,
  CXCursor_AsmStmt                       = CXCursor_GCCAsmStmt,

  /** \brief Objective-C's overall \@try-\@catch-\@finally statement.
   */
  CXCursor_ObjCAtTryStmt                 = 216,

  /** \brief Objective-C's \@catch statement.
   */
  CXCursor_ObjCAtCatchStmt               = 217,

  /** \brief Objective-C's \@finally statement.
   */
  CXCursor_ObjCAtFinallyStmt             = 218,

  /** \brief Objective-C's \@throw statement.
   */
  CXCursor_ObjCAtThrowStmt               = 219,

  /** \brief Objective-C's \@synchronized statement.
   */
  CXCursor_ObjCAtSynchronizedStmt        = 220,

  /** \brief Objective-C's autorelease pool statement.
   */
  CXCursor_ObjCAutoreleasePoolStmt       = 221,

  /** \brief Objective-C's collection statement.
   */
  CXCursor_ObjCForCollectionStmt         = 222,

  /** \brief C++'s catch statement.
   */
  CXCursor_CXXCatchStmt                  = 223,

  /** \brief C++'s try statement.
   */
  CXCursor_CXXTryStmt                    = 224,

  /** \brief C++'s for (* : *) statement.
   */
  CXCursor_CXXForRangeStmt               = 225,

  /** \brief Windows Structured Exception Handling's try statement.
   */
  CXCursor_SEHTryStmt                    = 226,

  /** \brief Windows Structured Exception Handling's except statement.
   */
  CXCursor_SEHExceptStmt                 = 227,

  /** \brief Windows Structured Exception Handling's finally statement.
   */
  CXCursor_SEHFinallyStmt                = 228,

  /** \brief A MS inline assembly statement extension.
   */
  CXCursor_MSAsmStmt                     = 229,

  /** \brief The null statement ";": C99 6.8.3p3.
   *
   * This cursor kind is used to describe the null statement.
   */
  CXCursor_NullStmt                      = 230,

  /** \brief Adaptor class for mixing declarations with statements and
   * expressions.
   */
  CXCursor_DeclStmt                      = 231,

  /** \brief OpenMP parallel directive.
   */
  CXCursor_OMPParallelDirective          = 232,

  /** \brief OpenMP SIMD directive.
   */
  CXCursor_OMPSimdDirective              = 233,

  /** \brief OpenMP for directive.
   */
  CXCursor_OMPForDirective               = 234,

  /** \brief OpenMP sections directive.
   */
  CXCursor_OMPSectionsDirective          = 235,

  /** \brief OpenMP section directive.
   */
  CXCursor_OMPSectionDirective           = 236,

  /** \brief OpenMP single directive.
   */
  CXCursor_OMPSingleDirective            = 237,

  /** \brief OpenMP parallel for directive.
   */
  CXCursor_OMPParallelForDirective       = 238,

  /** \brief OpenMP parallel sections directive.
   */
  CXCursor_OMPParallelSectionsDirective  = 239,

  /** \brief OpenMP task directive.
   */
  CXCursor_OMPTaskDirective              = 240,

  /** \brief OpenMP master directive.
   */
  CXCursor_OMPMasterDirective            = 241,

  /** \brief OpenMP critical directive.
   */
  CXCursor_OMPCriticalDirective          = 242,

  /** \brief OpenMP taskyield directive.
   */
  CXCursor_OMPTaskyieldDirective         = 243,

  /** \brief OpenMP barrier directive.
   */
  CXCursor_OMPBarrierDirective           = 244,

  /** \brief OpenMP taskwait directive.
   */
  CXCursor_OMPTaskwaitDirective          = 245,

  /** \brief OpenMP flush directive.
   */
  CXCursor_OMPFlushDirective             = 246,

  /** \brief Windows Structured Exception Handling's leave statement.
   */
  CXCursor_SEHLeaveStmt                  = 247,

  /** \brief OpenMP ordered directive.
   */
  CXCursor_OMPOrderedDirective           = 248,

  /** \brief OpenMP atomic directive.
   */
  CXCursor_OMPAtomicDirective            = 249,

  /** \brief OpenMP for SIMD directive.
   */
  CXCursor_OMPForSimdDirective           = 250,

  /** \brief OpenMP parallel for SIMD directive.
   */
  CXCursor_OMPParallelForSimdDirective   = 251,

  /** \brief OpenMP target directive.
   */
  CXCursor_OMPTargetDirective            = 252,

  /** \brief OpenMP teams directive.
   */
  CXCursor_OMPTeamsDirective             = 253,

  /** \brief OpenMP taskgroup directive.
   */
  CXCursor_OMPTaskgroupDirective         = 254,

  /** \brief OpenMP cancellation point directive.
   */
  CXCursor_OMPCancellationPointDirective = 255,

  /** \brief OpenMP cancel directive.
   */
  CXCursor_OMPCancelDirective            = 256,

  /** \brief OpenMP target data directive.
   */
  CXCursor_OMPTargetDataDirective        = 257,

  /** \brief OpenMP taskloop directive.
   */
  CXCursor_OMPTaskLoopDirective          = 258,

  /** \brief OpenMP taskloop simd directive.
   */
  CXCursor_OMPTaskLoopSimdDirective      = 259,

  /** \brief OpenMP distribute directive.
   */
  CXCursor_OMPDistributeDirective        = 260,

  /** \brief OpenMP target enter data directive.
   */
  CXCursor_OMPTargetEnterDataDirective   = 261,

  /** \brief OpenMP target exit data directive.
   */
  CXCursor_OMPTargetExitDataDirective    = 262,

  /** \brief OpenMP target parallel directive.
   */
  CXCursor_OMPTargetParallelDirective    = 263,

  /** \brief OpenMP target parallel for directive.
   */
  CXCursor_OMPTargetParallelForDirective = 264,

  /** \brief OpenMP target update directive.
   */
  CXCursor_OMPTargetUpdateDirective      = 265,

  /** \brief OpenMP distribute parallel for directive.
   */
  CXCursor_OMPDistributeParallelForDirective = 266,

  /** \brief OpenMP distribute parallel for simd directive.
   */
  CXCursor_OMPDistributeParallelForSimdDirective = 267,

  /** \brief OpenMP distribute simd directive.
   */
  CXCursor_OMPDistributeSimdDirective = 268,

  /** \brief OpenMP target parallel for simd directive.
   */
  CXCursor_OMPTargetParallelForSimdDirective = 269,

  /** \brief OpenMP target simd directive.
   */
  CXCursor_OMPTargetSimdDirective = 270,

  /** \brief OpenMP teams distribute directive.
   */
  CXCursor_OMPTeamsDistributeDirective = 271,

  /** \brief OpenMP teams distribute simd directive.
   */
  CXCursor_OMPTeamsDistributeSimdDirective = 272,

  /** \brief OpenMP teams distribute parallel for simd directive.
   */
  CXCursor_OMPTeamsDistributeParallelForSimdDirective = 273,

  /** \brief OpenMP teams distribute parallel for directive.
   */
  CXCursor_OMPTeamsDistributeParallelForDirective = 274,

  CXCursor_LastStmt = CXCursor_OMPTeamsDistributeParallelForDirective,

  /**
   * \brief Cursor that represents the translation unit itself.
   *
   * The translation unit cursor exists primarily to act as the root
   * cursor for traversing the contents of a translation unit.
   */
  CXCursor_TranslationUnit               = 300,

  /* Attributes */
  CXCursor_FirstAttr                     = 400,
  /**
   * \brief An attribute whose specific kind is not exposed via this
   * interface.
   */
  CXCursor_UnexposedAttr                 = 400,

  CXCursor_IBActionAttr                  = 401,
  CXCursor_IBOutletAttr                  = 402,
  CXCursor_IBOutletCollectionAttr        = 403,
  CXCursor_CXXFinalAttr                  = 404,
  CXCursor_CXXOverrideAttr               = 405,
  CXCursor_AnnotateAttr                  = 406,
  CXCursor_AsmLabelAttr                  = 407,
  CXCursor_PackedAttr                    = 408,
  CXCursor_PureAttr                      = 409,
  CXCursor_ConstAttr                     = 410,
  CXCursor_NoDuplicateAttr               = 411,
  CXCursor_CUDAConstantAttr              = 412,
  CXCursor_CUDADeviceAttr                = 413,
  CXCursor_CUDAGlobalAttr                = 414,
  CXCursor_CUDAHostAttr                  = 415,
  CXCursor_CUDASharedAttr                = 416,
  CXCursor_VisibilityAttr                = 417,
  CXCursor_DLLExport                     = 418,
  CXCursor_DLLImport                     = 419,
  CXCursor_LastAttr                      = CXCursor_DLLImport,

  /* Preprocessing */
  CXCursor_PreprocessingDirective        = 500,
  CXCursor_MacroDefinition               = 501,
  CXCursor_MacroExpansion                = 502,
  CXCursor_MacroInstantiation            = CXCursor_MacroExpansion,
  CXCursor_InclusionDirective            = 503,
  CXCursor_FirstPreprocessing            = CXCursor_PreprocessingDirective,
  CXCursor_LastPreprocessing             = CXCursor_InclusionDirective,

  /* Extra Declarations */
  /**
   * \brief A module import declaration.
   */
  CXCursor_ModuleImportDecl              = 600,
  CXCursor_TypeAliasTemplateDecl         = 601,
  /**
   * \brief A static_assert or _Static_assert node
   */
  CXCursor_StaticAssert                  = 602,
  /**
   * \brief a friend declaration.
   */
  CXCursor_FriendDecl                    = 603,
  CXCursor_FirstExtraDecl                = CXCursor_ModuleImportDecl,
  CXCursor_LastExtraDecl                 = CXCursor_FriendDecl,

  /**
   * \brief A code completion overload candidate.
   */
  CXCursor_OverloadCandidate             = 700
};

/**
 * \brief A cursor representing some element in the abstract syntax tree for
 * a translation unit.
 *
 * The cursor abstraction unifies the different kinds of entities in a
 * program--declaration, statements, expressions, references to declarations,
 * etc.--under a single "cursor" abstraction with a common set of operations.
 * Common operation for a cursor include: getting the physical location in
 * a source file where the cursor points, getting the name associated with a
 * cursor, and retrieving cursors for any child nodes of a particular cursor.
 *
 * Cursors can be produced in two specific ways.
 * clang_getTranslationUnitCursor() produces a cursor for a translation unit,
 * from which one can use clang_visitChildren() to explore the rest of the
 * translation unit. clang_getCursor() maps from a physical source location
 * to the entity that resides at that location, allowing one to map from the
 * source code into the AST.
 */
typedef struct {
  enum CXCursorKind kind;
  int xdata;
  const void *data[3];
} CXCursor;

/**
 * \defgroup CINDEX_CURSOR_MANIP Cursor manipulations
 *
 * @{
 */

/**
 * \brief Retrieve the NULL cursor, which represents no entity.
 */
CINDEX_LINKAGE CXCursor clang_getNullCursor(void);

/**
 * \brief Retrieve the cursor that represents the given translation unit.
 *
 * The translation unit cursor can be used to start traversing the
 * various declarations within the given translation unit.
 */
CINDEX_LINKAGE CXCursor clang_getTranslationUnitCursor(CXTranslationUnit);

/**
 * \brief Determine whether two cursors are equivalent.
 */
CINDEX_LINKAGE unsigned clang_equalCursors(CXCursor, CXCursor);

/**
 * \brief Returns non-zero if \p cursor is null.
 */
CINDEX_LINKAGE int clang_Cursor_isNull(CXCursor cursor);

/**
 * \brief Compute a hash value for the given cursor.
 */
CINDEX_LINKAGE unsigned clang_hashCursor(CXCursor);
  
/**
 * \brief Retrieve the kind of the given cursor.
 */
CINDEX_LINKAGE enum CXCursorKind clang_getCursorKind(CXCursor);

/**
 * \brief Determine whether the given cursor kind represents a declaration.
 */
CINDEX_LINKAGE unsigned clang_isDeclaration(enum CXCursorKind);

/**
 * \brief Determine whether the given cursor kind represents a simple
 * reference.
 *
 * Note that other kinds of cursors (such as expressions) can also refer to
 * other cursors. Use clang_getCursorReferenced() to determine whether a
 * particular cursor refers to another entity.
 */
CINDEX_LINKAGE unsigned clang_isReference(enum CXCursorKind);

/**
 * \brief Determine whether the given cursor kind represents an expression.
 */
CINDEX_LINKAGE unsigned clang_isExpression(enum CXCursorKind);

/**
 * \brief Determine whether the given cursor kind represents a statement.
 */
CINDEX_LINKAGE unsigned clang_isStatement(enum CXCursorKind);

/**
 * \brief Determine whether the given cursor kind represents an attribute.
 */
CINDEX_LINKAGE unsigned clang_isAttribute(enum CXCursorKind);

/**
 * \brief Determine whether the given cursor has any attributes.
 */
CINDEX_LINKAGE unsigned clang_Cursor_hasAttrs(CXCursor C);

/**
 * \brief Determine whether the given cursor kind represents an invalid
 * cursor.
 */
CINDEX_LINKAGE unsigned clang_isInvalid(enum CXCursorKind);

/**
 * \brief Determine whether the given cursor kind represents a translation
 * unit.
 */
CINDEX_LINKAGE unsigned clang_isTranslationUnit(enum CXCursorKind);

/***
 * \brief Determine whether the given cursor represents a preprocessing
 * element, such as a preprocessor directive or macro instantiation.
 */
CINDEX_LINKAGE unsigned clang_isPreprocessing(enum CXCursorKind);
  
/***
 * \brief Determine whether the given cursor represents a currently
 *  unexposed piece of the AST (e.g., CXCursor_UnexposedStmt).
 */
CINDEX_LINKAGE unsigned clang_isUnexposed(enum CXCursorKind);

/**
 * \brief Describe the linkage of the entity referred to by a cursor.
 */
enum CXLinkageKind {
  /** \brief This value indicates that no linkage information is available
   * for a provided CXCursor. */
  CXLinkage_Invalid,
  /**
   * \brief This is the linkage for variables, parameters, and so on that
   *  have automatic storage.  This covers normal (non-extern) local variables.
   */
  CXLinkage_NoLinkage,
  /** \brief This is the linkage for static variables and static functions. */
  CXLinkage_Internal,
  /** \brief This is the linkage for entities with external linkage that live
   * in C++ anonymous namespaces.*/
  CXLinkage_UniqueExternal,
  /** \brief This is the linkage for entities with true, external linkage. */
  CXLinkage_External
};

/**
 * \brief Determine the linkage of the entity referred to by a given cursor.
 */
CINDEX_LINKAGE enum CXLinkageKind clang_getCursorLinkage(CXCursor cursor);

enum CXVisibilityKind {
  /** \brief This value indicates that no visibility information is available
   * for a provided CXCursor. */
  CXVisibility_Invalid,

  /** \brief Symbol not seen by the linker. */
  CXVisibility_Hidden,
  /** \brief Symbol seen by the linker but resolves to a symbol inside this object. */
  CXVisibility_Protected,
  /** \brief Symbol seen by the linker and acts like a normal symbol. */
  CXVisibility_Default
};

/**
 * \brief Describe the visibility of the entity referred to by a cursor.
 *
 * This returns the default visibility if not explicitly specified by
 * a visibility attribute. The default visibility may be changed by
 * commandline arguments.
 *
 * \param cursor The cursor to query.
 *
 * \returns The visibility of the cursor.
 */
CINDEX_LINKAGE enum CXVisibilityKind clang_getCursorVisibility(CXCursor cursor);

/**
 * \brief Determine the availability of the entity that this cursor refers to,
 * taking the current target platform into account.
 *
 * \param cursor The cursor to query.
 *
 * \returns The availability of the cursor.
 */
CINDEX_LINKAGE enum CXAvailabilityKind 
clang_getCursorAvailability(CXCursor cursor);

/**
 * Describes the availability of a given entity on a particular platform, e.g.,
 * a particular class might only be available on Mac OS 10.7 or newer.
 */
typedef struct CXPlatformAvailability {
  /**
   * \brief A string that describes the platform for which this structure
   * provides availability information.
   *
   * Possible values are "ios" or "macos".
   */
  CXString Platform;
  /**
   * \brief The version number in which this entity was introduced.
   */
  CXVersion Introduced;
  /**
   * \brief The version number in which this entity was deprecated (but is
   * still available).
   */
  CXVersion Deprecated;
  /**
   * \brief The version number in which this entity was obsoleted, and therefore
   * is no longer available.
   */
  CXVersion Obsoleted;
  /**
   * \brief Whether the entity is unconditionally unavailable on this platform.
   */
  int Unavailable;
  /**
   * \brief An optional message to provide to a user of this API, e.g., to
   * suggest replacement APIs.
   */
  CXString Message;
} CXPlatformAvailability;

/**
 * \brief Determine the availability of the entity that this cursor refers to
 * on any platforms for which availability information is known.
 *
 * \param cursor The cursor to query.
 *
 * \param always_deprecated If non-NULL, will be set to indicate whether the 
 * entity is deprecated on all platforms.
 *
 * \param deprecated_message If non-NULL, will be set to the message text 
 * provided along with the unconditional deprecation of this entity. The client
 * is responsible for deallocating this string.
 *
 * \param always_unavailable If non-NULL, will be set to indicate whether the
 * entity is unavailable on all platforms.
 *
 * \param unavailable_message If non-NULL, will be set to the message text
 * provided along with the unconditional unavailability of this entity. The 
 * client is responsible for deallocating this string.
 *
 * \param availability If non-NULL, an array of CXPlatformAvailability instances
 * that will be populated with platform availability information, up to either
 * the number of platforms for which availability information is available (as
 * returned by this function) or \c availability_size, whichever is smaller.
 *
 * \param availability_size The number of elements available in the 
 * \c availability array.
 *
 * \returns The number of platforms (N) for which availability information is
 * available (which is unrelated to \c availability_size).
 *
 * Note that the client is responsible for calling 
 * \c clang_disposeCXPlatformAvailability to free each of the 
 * platform-availability structures returned. There are 
 * \c min(N, availability_size) such structures.
 */
CINDEX_LINKAGE int
clang_getCursorPlatformAvailability(CXCursor cursor,
                                    int *always_deprecated,
                                    CXString *deprecated_message,
                                    int *always_unavailable,
                                    CXString *unavailable_message,
                                    CXPlatformAvailability *availability,
                                    int availability_size);

/**
 * \brief Free the memory associated with a \c CXPlatformAvailability structure.
 */
CINDEX_LINKAGE void
clang_disposeCXPlatformAvailability(CXPlatformAvailability *availability);
  
/**
 * \brief Describe the "language" of the entity referred to by a cursor.
 */
enum CXLanguageKind {
  CXLanguage_Invalid = 0,
  CXLanguage_C,
  CXLanguage_ObjC,
  CXLanguage_CPlusPlus
};

/**
 * \brief Determine the "language" of the entity referred to by a given cursor.
 */
CINDEX_LINKAGE enum CXLanguageKind clang_getCursorLanguage(CXCursor cursor);

/**
 * \brief Returns the translation unit that a cursor originated from.
 */
CINDEX_LINKAGE CXTranslationUnit clang_Cursor_getTranslationUnit(CXCursor);

/**
 * \brief A fast container representing a set of CXCursors.
 */
typedef struct CXCursorSetImpl *CXCursorSet;

/**
 * \brief Creates an empty CXCursorSet.
 */
CINDEX_LINKAGE CXCursorSet clang_createCXCursorSet(void);

/**
 * \brief Disposes a CXCursorSet and releases its associated memory.
 */
CINDEX_LINKAGE void clang_disposeCXCursorSet(CXCursorSet cset);

/**
 * \brief Queries a CXCursorSet to see if it contains a specific CXCursor.
 *
 * \returns non-zero if the set contains the specified cursor.
*/
CINDEX_LINKAGE unsigned clang_CXCursorSet_contains(CXCursorSet cset,
                                                   CXCursor cursor);

/**
 * \brief Inserts a CXCursor into a CXCursorSet.
 *
 * \returns zero if the CXCursor was already in the set, and non-zero otherwise.
*/
CINDEX_LINKAGE unsigned clang_CXCursorSet_insert(CXCursorSet cset,
                                                 CXCursor cursor);

/**
 * \brief Determine the semantic parent of the given cursor.
 *
 * The semantic parent of a cursor is the cursor that semantically contains
 * the given \p cursor. For many declarations, the lexical and semantic parents
 * are equivalent (the lexical parent is returned by 
 * \c clang_getCursorLexicalParent()). They diverge when declarations or
 * definitions are provided out-of-line. For example:
 *
 * \code
 * class C {
 *  void f();
 * };
 *
 * void C::f() { }
 * \endcode
 *
 * In the out-of-line definition of \c C::f, the semantic parent is
 * the class \c C, of which this function is a member. The lexical parent is
 * the place where the declaration actually occurs in the source code; in this
 * case, the definition occurs in the translation unit. In general, the
 * lexical parent for a given entity can change without affecting the semantics
 * of the program, and the lexical parent of different declarations of the
 * same entity may be different. Changing the semantic parent of a declaration,
 * on the other hand, can have a major impact on semantics, and redeclarations
 * of a particular entity should all have the same semantic context.
 *
 * In the example above, both declarations of \c C::f have \c C as their
 * semantic context, while the lexical context of the first \c C::f is \c C
 * and the lexical context of the second \c C::f is the translation unit.
 *
 * For global declarations, the semantic parent is the translation unit.
 */
CINDEX_LINKAGE CXCursor clang_getCursorSemanticParent(CXCursor cursor);

/**
 * \brief Determine the lexical parent of the given cursor.
 *
 * The lexical parent of a cursor is the cursor in which the given \p cursor
 * was actually written. For many declarations, the lexical and semantic parents
 * are equivalent (the semantic parent is returned by 
 * \c clang_getCursorSemanticParent()). They diverge when declarations or
 * definitions are provided out-of-line. For example:
 *
 * \code
 * class C {
 *  void f();
 * };
 *
 * void C::f() { }
 * \endcode
 *
 * In the out-of-line definition of \c C::f, the semantic parent is
 * the class \c C, of which this function is a member. The lexical parent is
 * the place where the declaration actually occurs in the source code; in this
 * case, the definition occurs in the translation unit. In general, the
 * lexical parent for a given entity can change without affecting the semantics
 * of the program, and the lexical parent of different declarations of the
 * same entity may be different. Changing the semantic parent of a declaration,
 * on the other hand, can have a major impact on semantics, and redeclarations
 * of a particular entity should all have the same semantic context.
 *
 * In the example above, both declarations of \c C::f have \c C as their
 * semantic context, while the lexical context of the first \c C::f is \c C
 * and the lexical context of the second \c C::f is the translation unit.
 *
 * For declarations written in the global scope, the lexical parent is
 * the translation unit.
 */
CINDEX_LINKAGE CXCursor clang_getCursorLexicalParent(CXCursor cursor);

/**
 * \brief Determine the set of methods that are overridden by the given
 * method.
 *
 * In both Objective-C and C++, a method (aka virtual member function,
 * in C++) can override a virtual method in a base class. For
 * Objective-C, a method is said to override any method in the class's
 * base class, its protocols, or its categories' protocols, that has the same
 * selector and is of the same kind (class or instance).
 * If no such method exists, the search continues to the class's superclass,
 * its protocols, and its categories, and so on. A method from an Objective-C
 * implementation is considered to override the same methods as its
 * corresponding method in the interface.
 *
 * For C++, a virtual member function overrides any virtual member
 * function with the same signature that occurs in its base
 * classes. With multiple inheritance, a virtual member function can
 * override several virtual member functions coming from different
 * base classes.
 *
 * In all cases, this function determines the immediate overridden
 * method, rather than all of the overridden methods. For example, if
 * a method is originally declared in a class A, then overridden in B
 * (which in inherits from A) and also in C (which inherited from B),
 * then the only overridden method returned from this function when
 * invoked on C's method will be B's method. The client may then
 * invoke this function again, given the previously-found overridden
 * methods, to map out the complete method-override set.
 *
 * \param cursor A cursor representing an Objective-C or C++
 * method. This routine will compute the set of methods that this
 * method overrides.
 * 
 * \param overridden A pointer whose pointee will be replaced with a
 * pointer to an array of cursors, representing the set of overridden
 * methods. If there are no overridden methods, the pointee will be
 * set to NULL. The pointee must be freed via a call to 
 * \c clang_disposeOverriddenCursors().
 *
 * \param num_overridden A pointer to the number of overridden
 * functions, will be set to the number of overridden functions in the
 * array pointed to by \p overridden.
 */
CINDEX_LINKAGE void clang_getOverriddenCursors(CXCursor cursor, 
                                               CXCursor **overridden,
                                               unsigned *num_overridden);

/**
 * \brief Free the set of overridden cursors returned by \c
 * clang_getOverriddenCursors().
 */
CINDEX_LINKAGE void clang_disposeOverriddenCursors(CXCursor *overridden);

/**
 * \brief Retrieve the file that is included by the given inclusion directive
 * cursor.
 */
CINDEX_LINKAGE CXFile clang_getIncludedFile(CXCursor cursor);
  
/**
 * @}
 */

/**
 * \defgroup CINDEX_CURSOR_SOURCE Mapping between cursors and source code
 *
 * Cursors represent a location within the Abstract Syntax Tree (AST). These
 * routines help map between cursors and the physical locations where the
 * described entities occur in the source code. The mapping is provided in
 * both directions, so one can map from source code to the AST and back.
 *
 * @{
 */

/**
 * \brief Map a source location to the cursor that describes the entity at that
 * location in the source code.
 *
 * clang_getCursor() maps an arbitrary source location within a translation
 * unit down to the most specific cursor that describes the entity at that
 * location. For example, given an expression \c x + y, invoking
 * clang_getCursor() with a source location pointing to "x" will return the
 * cursor for "x"; similarly for "y". If the cursor points anywhere between
 * "x" or "y" (e.g., on the + or the whitespace around it), clang_getCursor()
 * will return a cursor referring to the "+" expression.
 *
 * \returns a cursor representing the entity at the given source location, or
 * a NULL cursor if no such entity can be found.
 */
CINDEX_LINKAGE CXCursor clang_getCursor(CXTranslationUnit, CXSourceLocation);

/**
 * \brief Retrieve the physical location of the source constructor referenced
 * by the given cursor.
 *
 * The location of a declaration is typically the location of the name of that
 * declaration, where the name of that declaration would occur if it is
 * unnamed, or some keyword that introduces that particular declaration.
 * The location of a reference is where that reference occurs within the
 * source code.
 */
CINDEX_LINKAGE CXSourceLocation clang_getCursorLocation(CXCursor);

/**
 * \brief Retrieve the physical extent of the source construct referenced by
 * the given cursor.
 *
 * The extent of a cursor starts with the file/line/column pointing at the
 * first character within the source construct that the cursor refers to and
 * ends with the last character within that source construct. For a
 * declaration, the extent covers the declaration itself. For a reference,
 * the extent covers the location of the reference (e.g., where the referenced
 * entity was actually used).
 */
CINDEX_LINKAGE CXSourceRange clang_getCursorExtent(CXCursor);

/**
 * @}
 */
    
/**
 * \defgroup CINDEX_TYPES Type information for CXCursors
 *
 * @{
 */

/**
 * \brief Describes the kind of type
 */
enum CXTypeKind {
  /**
   * \brief Represents an invalid type (e.g., where no type is available).
   */
  CXType_Invalid = 0,

  /**
   * \brief A type whose specific kind is not exposed via this
   * interface.
   */
  CXType_Unexposed = 1,

  /* Builtin types */
  CXType_Void = 2,
  CXType_Bool = 3,
  CXType_Char_U = 4,
  CXType_UChar = 5,
  CXType_Char16 = 6,
  CXType_Char32 = 7,
  CXType_UShort = 8,
  CXType_UInt = 9,
  CXType_ULong = 10,
  CXType_ULongLong = 11,
  CXType_UInt128 = 12,
  CXType_Char_S = 13,
  CXType_SChar = 14,
  CXType_WChar = 15,
  CXType_Short = 16,
  CXType_Int = 17,
  CXType_Long = 18,
  CXType_LongLong = 19,
  CXType_Int128 = 20,
  CXType_Float = 21,
  CXType_Double = 22,
  CXType_LongDouble = 23,
  CXType_NullPtr = 24,
  CXType_Overload = 25,
  CXType_Dependent = 26,
  CXType_ObjCId = 27,
  CXType_ObjCClass = 28,
  CXType_ObjCSel = 29,
  CXType_Float128 = 30,
  CXType_FirstBuiltin = CXType_Void,
  CXType_LastBuiltin  = CXType_ObjCSel,

  CXType_Complex = 100,
  CXType_Pointer = 101,
  CXType_BlockPointer = 102,
  CXType_LValueReference = 103,
  CXType_RValueReference = 104,
  CXType_Record = 105,
  CXType_Enum = 106,
  CXType_Typedef = 107,
  CXType_ObjCInterface = 108,
  CXType_ObjCObjectPointer = 109,
  CXType_FunctionNoProto = 110,
  CXType_FunctionProto = 111,
  CXType_ConstantArray = 112,
  CXType_Vector = 113,
  CXType_IncompleteArray = 114,
  CXType_VariableArray = 115,
  CXType_DependentSizedArray = 116,
  CXType_MemberPointer = 117,
  CXType_Auto = 118,

  /**
   * \brief Represents a type that was referred to using an elaborated type keyword.
   *
   * E.g., struct S, or via a qualified name, e.g., N::M::type, or both.
   */
  CXType_Elaborated = 119
};

/**
 * \brief Describes the calling convention of a function type
 */
enum CXCallingConv {
  CXCallingConv_Default = 0,
  CXCallingConv_C = 1,
  CXCallingConv_X86StdCall = 2,
  CXCallingConv_X86FastCall = 3,
  CXCallingConv_X86ThisCall = 4,
  CXCallingConv_X86Pascal = 5,
  CXCallingConv_AAPCS = 6,
  CXCallingConv_AAPCS_VFP = 7,
  CXCallingConv_X86RegCall = 8,
  CXCallingConv_IntelOclBicc = 9,
  CXCallingConv_X86_64Win64 = 10,
  CXCallingConv_X86_64SysV = 11,
  CXCallingConv_X86VectorCall = 12,
  CXCallingConv_Swift = 13,
  CXCallingConv_PreserveMost = 14,
  CXCallingConv_PreserveAll = 15,

  CXCallingConv_Invalid = 100,
  CXCallingConv_Unexposed = 200
};

/**
 * \brief The type of an element in the abstract syntax tree.
 *
 */
typedef struct {
  enum CXTypeKind kind;
  void *data[2];
} CXType;

/**
 * \brief Retrieve the type of a CXCursor (if any).
 */
CINDEX_LINKAGE CXType clang_getCursorType(CXCursor C);

/**
 * \brief Pretty-print the underlying type using the rules of the
 * language of the translation unit from which it came.
 *
 * If the type is invalid, an empty string is returned.
 */
CINDEX_LINKAGE CXString clang_getTypeSpelling(CXType CT);

/**
 * \brief Retrieve the underlying type of a typedef declaration.
 *
 * If the cursor does not reference a typedef declaration, an invalid type is
 * returned.
 */
CINDEX_LINKAGE CXType clang_getTypedefDeclUnderlyingType(CXCursor C);

/**
 * \brief Retrieve the integer type of an enum declaration.
 *
 * If the cursor does not reference an enum declaration, an invalid type is
 * returned.
 */
CINDEX_LINKAGE CXType clang_getEnumDeclIntegerType(CXCursor C);

/**
 * \brief Retrieve the integer value of an enum constant declaration as a signed
 *  long long.
 *
 * If the cursor does not reference an enum constant declaration, LLONG_MIN is returned.
 * Since this is also potentially a valid constant value, the kind of the cursor
 * must be verified before calling this function.
 */
CINDEX_LINKAGE long long clang_getEnumConstantDeclValue(CXCursor C);

/**
 * \brief Retrieve the integer value of an enum constant declaration as an unsigned
 *  long long.
 *
 * If the cursor does not reference an enum constant declaration, ULLONG_MAX is returned.
 * Since this is also potentially a valid constant value, the kind of the cursor
 * must be verified before calling this function.
 */
CINDEX_LINKAGE unsigned long long clang_getEnumConstantDeclUnsignedValue(CXCursor C);

/**
 * \brief Retrieve the bit width of a bit field declaration as an integer.
 *
 * If a cursor that is not a bit field declaration is passed in, -1 is returned.
 */
CINDEX_LINKAGE int clang_getFieldDeclBitWidth(CXCursor C);

/**
 * \brief Retrieve the number of non-variadic arguments associated with a given
 * cursor.
 *
 * The number of arguments can be determined for calls as well as for
 * declarations of functions or methods. For other cursors -1 is returned.
 */
CINDEX_LINKAGE int clang_Cursor_getNumArguments(CXCursor C);

/**
 * \brief Retrieve the argument cursor of a function or method.
 *
 * The argument cursor can be determined for calls as well as for declarations
 * of functions or methods. For other cursors and for invalid indices, an
 * invalid cursor is returned.
 */
CINDEX_LINKAGE CXCursor clang_Cursor_getArgument(CXCursor C, unsigned i);

/**
 * \brief Describes the kind of a template argument.
 *
 * See the definition of llvm::clang::TemplateArgument::ArgKind for full
 * element descriptions.
 */
enum CXTemplateArgumentKind {
  CXTemplateArgumentKind_Null,
  CXTemplateArgumentKind_Type,
  CXTemplateArgumentKind_Declaration,
  CXTemplateArgumentKind_NullPtr,
  CXTemplateArgumentKind_Integral,
  CXTemplateArgumentKind_Template,
  CXTemplateArgumentKind_TemplateExpansion,
  CXTemplateArgumentKind_Expression,
  CXTemplateArgumentKind_Pack,
  /* Indicates an error case, preventing the kind from being deduced. */
  CXTemplateArgumentKind_Invalid
};

/**
 *\brief Returns the number of template args of a function decl representing a
 * template specialization.
 *
 * If the argument cursor cannot be converted into a template function
 * declaration, -1 is returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * The value 3 would be returned from this call.
 */
CINDEX_LINKAGE int clang_Cursor_getNumTemplateArguments(CXCursor C);

/**
 * \brief Retrieve the kind of the I'th template argument of the CXCursor C.
 *
 * If the argument CXCursor does not represent a FunctionDecl, an invalid
 * template argument kind is returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * For I = 0, 1, and 2, Type, Integral, and Integral will be returned,
 * respectively.
 */
CINDEX_LINKAGE enum CXTemplateArgumentKind clang_Cursor_getTemplateArgumentKind(
    CXCursor C, unsigned I);

/**
 * \brief Retrieve a CXType representing the type of a TemplateArgument of a
 *  function decl representing a template specialization.
 *
 * If the argument CXCursor does not represent a FunctionDecl whose I'th
 * template argument has a kind of CXTemplateArgKind_Integral, an invalid type
 * is returned.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * If called with I = 0, "float", will be returned.
 * Invalid types will be returned for I == 1 or 2.
 */
CINDEX_LINKAGE CXType clang_Cursor_getTemplateArgumentType(CXCursor C,
                                                           unsigned I);

/**
 * \brief Retrieve the value of an Integral TemplateArgument (of a function
 *  decl representing a template specialization) as a signed long long.
 *
 * It is undefined to call this function on a CXCursor that does not represent a
 * FunctionDecl or whose I'th template argument is not an integral value.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, -7, true>();
 *
 * If called with I = 1 or 2, -7 or true will be returned, respectively.
 * For I == 0, this function's behavior is undefined.
 */
CINDEX_LINKAGE long long clang_Cursor_getTemplateArgumentValue(CXCursor C,
                                                               unsigned I);

/**
 * \brief Retrieve the value of an Integral TemplateArgument (of a function
 *  decl representing a template specialization) as an unsigned long long.
 *
 * It is undefined to call this function on a CXCursor that does not represent a
 * FunctionDecl or whose I'th template argument is not an integral value.
 *
 * For example, for the following declaration and specialization:
 *   template <typename T, int kInt, bool kBool>
 *   void foo() { ... }
 *
 *   template <>
 *   void foo<float, 2147483649, true>();
 *
 * If called with I = 1 or 2, 2147483649 or true will be returned, respectively.
 * For I == 0, this function's behavior is undefined.
 */
CINDEX_LINKAGE unsigned long long clang_Cursor_getTemplateArgumentUnsignedValue(
    CXCursor C, unsigned I);

/**
 * \brief Determine whether two CXTypes represent the same type.
 *
 * \returns non-zero if the CXTypes represent the same type and
 *          zero otherwise.
 */
CINDEX_LINKAGE unsigned clang_equalTypes(CXType A, CXType B);

/**
 * \brief Return the canonical type for a CXType.
 *
 * Clang's type system explicitly models typedefs and all the ways
 * a specific type can be represented.  The canonical type is the underlying
 * type with all the "sugar" removed.  For example, if 'T' is a typedef
 * for 'int', the canonical type for 'T' would be 'int'.
 */
CINDEX_LINKAGE CXType clang_getCanonicalType(CXType T);

/**
 * \brief Determine whether a CXType has the "const" qualifier set,
 * without looking through typedefs that may have added "const" at a
 * different level.
 */
CINDEX_LINKAGE unsigned clang_isConstQualifiedType(CXType T);

/**
 * \brief Determine whether a  CXCursor that is a macro, is
 * function like.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isMacroFunctionLike(CXCursor C);

/**
 * \brief Determine whether a  CXCursor that is a macro, is a
 * builtin one.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isMacroBuiltin(CXCursor C);

/**
 * \brief Determine whether a  CXCursor that is a function declaration, is an
 * inline declaration.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isFunctionInlined(CXCursor C);

/**
 * \brief Determine whether a CXType has the "volatile" qualifier set,
 * without looking through typedefs that may have added "volatile" at
 * a different level.
 */
CINDEX_LINKAGE unsigned clang_isVolatileQualifiedType(CXType T);

/**
 * \brief Determine whether a CXType has the "restrict" qualifier set,
 * without looking through typedefs that may have added "restrict" at a
 * different level.
 */
CINDEX_LINKAGE unsigned clang_isRestrictQualifiedType(CXType T);

/**
 * \brief For pointer types, returns the type of the pointee.
 */
CINDEX_LINKAGE CXType clang_getPointeeType(CXType T);

/**
 * \brief Return the cursor for the declaration of the given type.
 */
CINDEX_LINKAGE CXCursor clang_getTypeDeclaration(CXType T);

/**
 * Returns the Objective-C type encoding for the specified declaration.
 */
CINDEX_LINKAGE CXString clang_getDeclObjCTypeEncoding(CXCursor C);

/**
 * Returns the Objective-C type encoding for the specified CXType.
 */
CINDEX_LINKAGE CXString clang_Type_getObjCEncoding(CXType type); 

/**
 * \brief Retrieve the spelling of a given CXTypeKind.
 */
CINDEX_LINKAGE CXString clang_getTypeKindSpelling(enum CXTypeKind K);

/**
 * \brief Retrieve the calling convention associated with a function type.
 *
 * If a non-function type is passed in, CXCallingConv_Invalid is returned.
 */
CINDEX_LINKAGE enum CXCallingConv clang_getFunctionTypeCallingConv(CXType T);

/**
 * \brief Retrieve the return type associated with a function type.
 *
 * If a non-function type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getResultType(CXType T);

/**
 * \brief Retrieve the number of non-variadic parameters associated with a
 * function type.
 *
 * If a non-function type is passed in, -1 is returned.
 */
CINDEX_LINKAGE int clang_getNumArgTypes(CXType T);

/**
 * \brief Retrieve the type of a parameter of a function type.
 *
 * If a non-function type is passed in or the function does not have enough
 * parameters, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getArgType(CXType T, unsigned i);

/**
 * \brief Return 1 if the CXType is a variadic function type, and 0 otherwise.
 */
CINDEX_LINKAGE unsigned clang_isFunctionTypeVariadic(CXType T);

/**
 * \brief Retrieve the return type associated with a given cursor.
 *
 * This only returns a valid type if the cursor refers to a function or method.
 */
CINDEX_LINKAGE CXType clang_getCursorResultType(CXCursor C);

/**
 * \brief Return 1 if the CXType is a POD (plain old data) type, and 0
 *  otherwise.
 */
CINDEX_LINKAGE unsigned clang_isPODType(CXType T);

/**
 * \brief Return the element type of an array, complex, or vector type.
 *
 * If a type is passed in that is not an array, complex, or vector type,
 * an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getElementType(CXType T);

/**
 * \brief Return the number of elements of an array or vector type.
 *
 * If a type is passed in that is not an array or vector type,
 * -1 is returned.
 */
CINDEX_LINKAGE long long clang_getNumElements(CXType T);

/**
 * \brief Return the element type of an array type.
 *
 * If a non-array type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_getArrayElementType(CXType T);

/**
 * \brief Return the array size of a constant array.
 *
 * If a non-array type is passed in, -1 is returned.
 */
CINDEX_LINKAGE long long clang_getArraySize(CXType T);

/**
 * \brief Retrieve the type named by the qualified-id.
 *
 * If a non-elaborated type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getNamedType(CXType T);

/**
 * \brief List the possible error codes for \c clang_Type_getSizeOf,
 *   \c clang_Type_getAlignOf, \c clang_Type_getOffsetOf and
 *   \c clang_Cursor_getOffsetOf.
 *
 * A value of this enumeration type can be returned if the target type is not
 * a valid argument to sizeof, alignof or offsetof.
 */
enum CXTypeLayoutError {
  /**
   * \brief Type is of kind CXType_Invalid.
   */
  CXTypeLayoutError_Invalid = -1,
  /**
   * \brief The type is an incomplete Type.
   */
  CXTypeLayoutError_Incomplete = -2,
  /**
   * \brief The type is a dependent Type.
   */
  CXTypeLayoutError_Dependent = -3,
  /**
   * \brief The type is not a constant size type.
   */
  CXTypeLayoutError_NotConstantSize = -4,
  /**
   * \brief The Field name is not valid for this record.
   */
  CXTypeLayoutError_InvalidFieldName = -5
};

/**
 * \brief Return the alignment of a type in bytes as per C++[expr.alignof]
 *   standard.
 *
 * If the type declaration is invalid, CXTypeLayoutError_Invalid is returned.
 * If the type declaration is an incomplete type, CXTypeLayoutError_Incomplete
 *   is returned.
 * If the type declaration is a dependent type, CXTypeLayoutError_Dependent is
 *   returned.
 * If the type declaration is not a constant size type,
 *   CXTypeLayoutError_NotConstantSize is returned.
 */
CINDEX_LINKAGE long long clang_Type_getAlignOf(CXType T);

/**
 * \brief Return the class type of an member pointer type.
 *
 * If a non-member-pointer type is passed in, an invalid type is returned.
 */
CINDEX_LINKAGE CXType clang_Type_getClassType(CXType T);

/**
 * \brief Return the size of a type in bytes as per C++[expr.sizeof] standard.
 *
 * If the type declaration is invalid, CXTypeLayoutError_Invalid is returned.
 * If the type declaration is an incomplete type, CXTypeLayoutError_Incomplete
 *   is returned.
 * If the type declaration is a dependent type, CXTypeLayoutError_Dependent is
 *   returned.
 */
CINDEX_LINKAGE long long clang_Type_getSizeOf(CXType T);

/**
 * \brief Return the offset of a field named S in a record of type T in bits
 *   as it would be returned by __offsetof__ as per C++11[18.2p4]
 *
 * If the cursor is not a record field declaration, CXTypeLayoutError_Invalid
 *   is returned.
 * If the field's type declaration is an incomplete type,
 *   CXTypeLayoutError_Incomplete is returned.
 * If the field's type declaration is a dependent type,
 *   CXTypeLayoutError_Dependent is returned.
 * If the field's name S is not found,
 *   CXTypeLayoutError_InvalidFieldName is returned.
 */
CINDEX_LINKAGE long long clang_Type_getOffsetOf(CXType T, const char *S);

/**
 * \brief Return the offset of the field represented by the Cursor.
 *
 * If the cursor is not a field declaration, -1 is returned.
 * If the cursor semantic parent is not a record field declaration,
 *   CXTypeLayoutError_Invalid is returned.
 * If the field's type declaration is an incomplete type,
 *   CXTypeLayoutError_Incomplete is returned.
 * If the field's type declaration is a dependent type,
 *   CXTypeLayoutError_Dependent is returned.
 * If the field's name S is not found,
 *   CXTypeLayoutError_InvalidFieldName is returned.
 */
CINDEX_LINKAGE long long clang_Cursor_getOffsetOfField(CXCursor C);

/**
 * \brief Determine whether the given cursor represents an anonymous record
 * declaration.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isAnonymous(CXCursor C);

enum CXRefQualifierKind {
  /** \brief No ref-qualifier was provided. */
  CXRefQualifier_None = 0,
  /** \brief An lvalue ref-qualifier was provided (\c &). */
  CXRefQualifier_LValue,
  /** \brief An rvalue ref-qualifier was provided (\c &&). */
  CXRefQualifier_RValue
};

/**
 * \brief Returns the number of template arguments for given template
 * specialization, or -1 if type \c T is not a template specialization.
 */
CINDEX_LINKAGE int clang_Type_getNumTemplateArguments(CXType T);

/**
 * \brief Returns the type template argument of a template class specialization
 * at given index.
 *
 * This function only returns template type arguments and does not handle
 * template template arguments or variadic packs.
 */
CINDEX_LINKAGE CXType clang_Type_getTemplateArgumentAsType(CXType T, unsigned i);

/**
 * \brief Retrieve the ref-qualifier kind of a function or method.
 *
 * The ref-qualifier is returned for C++ functions or methods. For other types
 * or non-C++ declarations, CXRefQualifier_None is returned.
 */
CINDEX_LINKAGE enum CXRefQualifierKind clang_Type_getCXXRefQualifier(CXType T);

/**
 * \brief Returns non-zero if the cursor specifies a Record member that is a
 *   bitfield.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isBitField(CXCursor C);

/**
 * \brief Returns 1 if the base class specified by the cursor with kind
 *   CX_CXXBaseSpecifier is virtual.
 */
CINDEX_LINKAGE unsigned clang_isVirtualBase(CXCursor);
    
/**
 * \brief Represents the C++ access control level to a base class for a
 * cursor with kind CX_CXXBaseSpecifier.
 */
enum CX_CXXAccessSpecifier {
  CX_CXXInvalidAccessSpecifier,
  CX_CXXPublic,
  CX_CXXProtected,
  CX_CXXPrivate
};

/**
 * \brief Returns the access control level for the referenced object.
 *
 * If the cursor refers to a C++ declaration, its access control level within its
 * parent scope is returned. Otherwise, if the cursor refers to a base specifier or
 * access specifier, the specifier itself is returned.
 */
CINDEX_LINKAGE enum CX_CXXAccessSpecifier clang_getCXXAccessSpecifier(CXCursor);

/**
 * \brief Represents the storage classes as declared in the source. CX_SC_Invalid
 * was added for the case that the passed cursor in not a declaration.
 */
enum CX_StorageClass {
  CX_SC_Invalid,
  CX_SC_None,
  CX_SC_Extern,
  CX_SC_Static,
  CX_SC_PrivateExtern,
  CX_SC_OpenCLWorkGroupLocal,
  CX_SC_Auto,
  CX_SC_Register
};

/**
 * \brief Returns the storage class for a function or variable declaration.
 *
 * If the passed in Cursor is not a function or variable declaration,
 * CX_SC_Invalid is returned else the storage class.
 */
CINDEX_LINKAGE enum CX_StorageClass clang_Cursor_getStorageClass(CXCursor);

/**
 * \brief Determine the number of overloaded declarations referenced by a 
 * \c CXCursor_OverloadedDeclRef cursor.
 *
 * \param cursor The cursor whose overloaded declarations are being queried.
 *
 * \returns The number of overloaded declarations referenced by \c cursor. If it
 * is not a \c CXCursor_OverloadedDeclRef cursor, returns 0.
 */
CINDEX_LINKAGE unsigned clang_getNumOverloadedDecls(CXCursor cursor);

/**
 * \brief Retrieve a cursor for one of the overloaded declarations referenced
 * by a \c CXCursor_OverloadedDeclRef cursor.
 *
 * \param cursor The cursor whose overloaded declarations are being queried.
 *
 * \param index The zero-based index into the set of overloaded declarations in
 * the cursor.
 *
 * \returns A cursor representing the declaration referenced by the given 
 * \c cursor at the specified \c index. If the cursor does not have an 
 * associated set of overloaded declarations, or if the index is out of bounds,
 * returns \c clang_getNullCursor();
 */
CINDEX_LINKAGE CXCursor clang_getOverloadedDecl(CXCursor cursor, 
                                                unsigned index);
  
/**
 * @}
 */
  
/**
 * \defgroup CINDEX_ATTRIBUTES Information for attributes
 *
 * @{
 */

/**
 * \brief For cursors representing an iboutletcollection attribute,
 *  this function returns the collection element type.
 *
 */
CINDEX_LINKAGE CXType clang_getIBOutletCollectionType(CXCursor);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CURSOR_TRAVERSAL Traversing the AST with cursors
 *
 * These routines provide the ability to traverse the abstract syntax tree
 * using cursors.
 *
 * @{
 */

/**
 * \brief Describes how the traversal of the children of a particular
 * cursor should proceed after visiting a particular child cursor.
 *
 * A value of this enumeration type should be returned by each
 * \c CXCursorVisitor to indicate how clang_visitChildren() proceed.
 */
enum CXChildVisitResult {
  /**
   * \brief Terminates the cursor traversal.
   */
  CXChildVisit_Break,
  /**
   * \brief Continues the cursor traversal with the next sibling of
   * the cursor just visited, without visiting its children.
   */
  CXChildVisit_Continue,
  /**
   * \brief Recursively traverse the children of this cursor, using
   * the same visitor and client data.
   */
  CXChildVisit_Recurse
};

/**
 * \brief Visitor invoked for each cursor found by a traversal.
 *
 * This visitor function will be invoked for each cursor found by
 * clang_visitCursorChildren(). Its first argument is the cursor being
 * visited, its second argument is the parent visitor for that cursor,
 * and its third argument is the client data provided to
 * clang_visitCursorChildren().
 *
 * The visitor should return one of the \c CXChildVisitResult values
 * to direct clang_visitCursorChildren().
 */
typedef enum CXChildVisitResult (*CXCursorVisitor)(CXCursor cursor,
                                                   CXCursor parent,
                                                   CXClientData client_data);

/**
 * \brief Visit the children of a particular cursor.
 *
 * This function visits all the direct children of the given cursor,
 * invoking the given \p visitor function with the cursors of each
 * visited child. The traversal may be recursive, if the visitor returns
 * \c CXChildVisit_Recurse. The traversal may also be ended prematurely, if
 * the visitor returns \c CXChildVisit_Break.
 *
 * \param parent the cursor whose child may be visited. All kinds of
 * cursors can be visited, including invalid cursors (which, by
 * definition, have no children).
 *
 * \param visitor the visitor function that will be invoked for each
 * child of \p parent.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the visitor each time it is invoked.
 *
 * \returns a non-zero value if the traversal was terminated
 * prematurely by the visitor returning \c CXChildVisit_Break.
 */
CINDEX_LINKAGE unsigned clang_visitChildren(CXCursor parent,
                                            CXCursorVisitor visitor,
                                            CXClientData client_data);
#ifdef __has_feature
#  if __has_feature(blocks)
/**
 * \brief Visitor invoked for each cursor found by a traversal.
 *
 * This visitor block will be invoked for each cursor found by
 * clang_visitChildrenWithBlock(). Its first argument is the cursor being
 * visited, its second argument is the parent visitor for that cursor.
 *
 * The visitor should return one of the \c CXChildVisitResult values
 * to direct clang_visitChildrenWithBlock().
 */
typedef enum CXChildVisitResult 
     (^CXCursorVisitorBlock)(CXCursor cursor, CXCursor parent);

/**
 * Visits the children of a cursor using the specified block.  Behaves
 * identically to clang_visitChildren() in all other respects.
 */
CINDEX_LINKAGE unsigned clang_visitChildrenWithBlock(CXCursor parent,
                                                    CXCursorVisitorBlock block);
#  endif
#endif

/**
 * @}
 */

/**
 * \defgroup CINDEX_CURSOR_XREF Cross-referencing in the AST
 *
 * These routines provide the ability to determine references within and
 * across translation units, by providing the names of the entities referenced
 * by cursors, follow reference cursors to the declarations they reference,
 * and associate declarations with their definitions.
 *
 * @{
 */

/**
 * \brief Retrieve a Unified Symbol Resolution (USR) for the entity referenced
 * by the given cursor.
 *
 * A Unified Symbol Resolution (USR) is a string that identifies a particular
 * entity (function, class, variable, etc.) within a program. USRs can be
 * compared across translation units to determine, e.g., when references in
 * one translation refer to an entity defined in another translation unit.
 */
CINDEX_LINKAGE CXString clang_getCursorUSR(CXCursor);

/**
 * \brief Construct a USR for a specified Objective-C class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCClass(const char *class_name);

/**
 * \brief Construct a USR for a specified Objective-C category.
 */
CINDEX_LINKAGE CXString
  clang_constructUSR_ObjCCategory(const char *class_name,
                                 const char *category_name);

/**
 * \brief Construct a USR for a specified Objective-C protocol.
 */
CINDEX_LINKAGE CXString
  clang_constructUSR_ObjCProtocol(const char *protocol_name);

/**
 * \brief Construct a USR for a specified Objective-C instance variable and
 *   the USR for its containing class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCIvar(const char *name,
                                                    CXString classUSR);

/**
 * \brief Construct a USR for a specified Objective-C method and
 *   the USR for its containing class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCMethod(const char *name,
                                                      unsigned isInstanceMethod,
                                                      CXString classUSR);

/**
 * \brief Construct a USR for a specified Objective-C property and the USR
 *  for its containing class.
 */
CINDEX_LINKAGE CXString clang_constructUSR_ObjCProperty(const char *property,
                                                        CXString classUSR);

/**
 * \brief Retrieve a name for the entity referenced by this cursor.
 */
CINDEX_LINKAGE CXString clang_getCursorSpelling(CXCursor);

/**
 * \brief Retrieve a range for a piece that forms the cursors spelling name.
 * Most of the times there is only one range for the complete spelling but for
 * Objective-C methods and Objective-C message expressions, there are multiple
 * pieces for each selector identifier.
 * 
 * \param pieceIndex the index of the spelling name piece. If this is greater
 * than the actual number of pieces, it will return a NULL (invalid) range.
 *  
 * \param options Reserved.
 */
CINDEX_LINKAGE CXSourceRange clang_Cursor_getSpellingNameRange(CXCursor,
                                                          unsigned pieceIndex,
                                                          unsigned options);

/**
 * \brief Retrieve the display name for the entity referenced by this cursor.
 *
 * The display name contains extra information that helps identify the cursor,
 * such as the parameters of a function or template or the arguments of a 
 * class template specialization.
 */
CINDEX_LINKAGE CXString clang_getCursorDisplayName(CXCursor);
  
/** \brief For a cursor that is a reference, retrieve a cursor representing the
 * entity that it references.
 *
 * Reference cursors refer to other entities in the AST. For example, an
 * Objective-C superclass reference cursor refers to an Objective-C class.
 * This function produces the cursor for the Objective-C class from the
 * cursor for the superclass reference. If the input cursor is a declaration or
 * definition, it returns that declaration or definition unchanged.
 * Otherwise, returns the NULL cursor.
 */
CINDEX_LINKAGE CXCursor clang_getCursorReferenced(CXCursor);

/**
 *  \brief For a cursor that is either a reference to or a declaration
 *  of some entity, retrieve a cursor that describes the definition of
 *  that entity.
 *
 *  Some entities can be declared multiple times within a translation
 *  unit, but only one of those declarations can also be a
 *  definition. For example, given:
 *
 *  \code
 *  int f(int, int);
 *  int g(int x, int y) { return f(x, y); }
 *  int f(int a, int b) { return a + b; }
 *  int f(int, int);
 *  \endcode
 *
 *  there are three declarations of the function "f", but only the
 *  second one is a definition. The clang_getCursorDefinition()
 *  function will take any cursor pointing to a declaration of "f"
 *  (the first or fourth lines of the example) or a cursor referenced
 *  that uses "f" (the call to "f' inside "g") and will return a
 *  declaration cursor pointing to the definition (the second "f"
 *  declaration).
 *
 *  If given a cursor for which there is no corresponding definition,
 *  e.g., because there is no definition of that entity within this
 *  translation unit, returns a NULL cursor.
 */
CINDEX_LINKAGE CXCursor clang_getCursorDefinition(CXCursor);

/**
 * \brief Determine whether the declaration pointed to by this cursor
 * is also a definition of that entity.
 */
CINDEX_LINKAGE unsigned clang_isCursorDefinition(CXCursor);

/**
 * \brief Retrieve the canonical cursor corresponding to the given cursor.
 *
 * In the C family of languages, many kinds of entities can be declared several
 * times within a single translation unit. For example, a structure type can
 * be forward-declared (possibly multiple times) and later defined:
 *
 * \code
 * struct X;
 * struct X;
 * struct X {
 *   int member;
 * };
 * \endcode
 *
 * The declarations and the definition of \c X are represented by three 
 * different cursors, all of which are declarations of the same underlying 
 * entity. One of these cursor is considered the "canonical" cursor, which
 * is effectively the representative for the underlying entity. One can 
 * determine if two cursors are declarations of the same underlying entity by
 * comparing their canonical cursors.
 *
 * \returns The canonical cursor for the entity referred to by the given cursor.
 */
CINDEX_LINKAGE CXCursor clang_getCanonicalCursor(CXCursor);

/**
 * \brief If the cursor points to a selector identifier in an Objective-C
 * method or message expression, this returns the selector index.
 *
 * After getting a cursor with #clang_getCursor, this can be called to
 * determine if the location points to a selector identifier.
 *
 * \returns The selector index if the cursor is an Objective-C method or message
 * expression and the cursor is pointing to a selector identifier, or -1
 * otherwise.
 */
CINDEX_LINKAGE int clang_Cursor_getObjCSelectorIndex(CXCursor);

/**
 * \brief Given a cursor pointing to a C++ method call or an Objective-C
 * message, returns non-zero if the method/message is "dynamic", meaning:
 * 
 * For a C++ method: the call is virtual.
 * For an Objective-C message: the receiver is an object instance, not 'super'
 * or a specific class.
 * 
 * If the method/message is "static" or the cursor does not point to a
 * method/message, it will return zero.
 */
CINDEX_LINKAGE int clang_Cursor_isDynamicCall(CXCursor C);

/**
 * \brief Given a cursor pointing to an Objective-C message, returns the CXType
 * of the receiver.
 */
CINDEX_LINKAGE CXType clang_Cursor_getReceiverType(CXCursor C);

/**
 * \brief Property attributes for a \c CXCursor_ObjCPropertyDecl.
 */
typedef enum {
  CXObjCPropertyAttr_noattr    = 0x00,
  CXObjCPropertyAttr_readonly  = 0x01,
  CXObjCPropertyAttr_getter    = 0x02,
  CXObjCPropertyAttr_assign    = 0x04,
  CXObjCPropertyAttr_readwrite = 0x08,
  CXObjCPropertyAttr_retain    = 0x10,
  CXObjCPropertyAttr_copy      = 0x20,
  CXObjCPropertyAttr_nonatomic = 0x40,
  CXObjCPropertyAttr_setter    = 0x80,
  CXObjCPropertyAttr_atomic    = 0x100,
  CXObjCPropertyAttr_weak      = 0x200,
  CXObjCPropertyAttr_strong    = 0x400,
  CXObjCPropertyAttr_unsafe_unretained = 0x800,
  CXObjCPropertyAttr_class = 0x1000
} CXObjCPropertyAttrKind;

/**
 * \brief Given a cursor that represents a property declaration, return the
 * associated property attributes. The bits are formed from
 * \c CXObjCPropertyAttrKind.
 *
 * \param reserved Reserved for future use, pass 0.
 */
CINDEX_LINKAGE unsigned clang_Cursor_getObjCPropertyAttributes(CXCursor C,
                                                             unsigned reserved);

/**
 * \brief 'Qualifiers' written next to the return and parameter types in
 * Objective-C method declarations.
 */
typedef enum {
  CXObjCDeclQualifier_None = 0x0,
  CXObjCDeclQualifier_In = 0x1,
  CXObjCDeclQualifier_Inout = 0x2,
  CXObjCDeclQualifier_Out = 0x4,
  CXObjCDeclQualifier_Bycopy = 0x8,
  CXObjCDeclQualifier_Byref = 0x10,
  CXObjCDeclQualifier_Oneway = 0x20
} CXObjCDeclQualifierKind;

/**
 * \brief Given a cursor that represents an Objective-C method or parameter
 * declaration, return the associated Objective-C qualifiers for the return
 * type or the parameter respectively. The bits are formed from
 * CXObjCDeclQualifierKind.
 */
CINDEX_LINKAGE unsigned clang_Cursor_getObjCDeclQualifiers(CXCursor C);

/**
 * \brief Given a cursor that represents an Objective-C method or property
 * declaration, return non-zero if the declaration was affected by "@optional".
 * Returns zero if the cursor is not such a declaration or it is "@required".
 */
CINDEX_LINKAGE unsigned clang_Cursor_isObjCOptional(CXCursor C);

/**
 * \brief Returns non-zero if the given cursor is a variadic function or method.
 */
CINDEX_LINKAGE unsigned clang_Cursor_isVariadic(CXCursor C);

/**
 * \brief Given a cursor that represents a declaration, return the associated
 * comment's source range.  The range may include multiple consecutive comments
 * with whitespace in between.
 */
CINDEX_LINKAGE CXSourceRange clang_Cursor_getCommentRange(CXCursor C);

/**
 * \brief Given a cursor that represents a declaration, return the associated
 * comment text, including comment markers.
 */
CINDEX_LINKAGE CXString clang_Cursor_getRawCommentText(CXCursor C);

/**
 * \brief Given a cursor that represents a documentable entity (e.g.,
 * declaration), return the associated \\brief paragraph; otherwise return the
 * first paragraph.
 */
CINDEX_LINKAGE CXString clang_Cursor_getBriefCommentText(CXCursor C);

/**
 * @}
 */

/** \defgroup CINDEX_MANGLE Name Mangling API Functions
 *
 * @{
 */

/**
 * \brief Retrieve the CXString representing the mangled name of the cursor.
 */
CINDEX_LINKAGE CXString clang_Cursor_getMangling(CXCursor);

/**
 * \brief Retrieve the CXStrings representing the mangled symbols of the C++
 * constructor or destructor at the cursor.
 */
CINDEX_LINKAGE CXStringSet *clang_Cursor_getCXXManglings(CXCursor);

/**
 * @}
 */

/**
 * \defgroup CINDEX_MODULE Module introspection
 *
 * The functions in this group provide access to information about modules.
 *
 * @{
 */

typedef void *CXModule;

/**
 * \brief Given a CXCursor_ModuleImportDecl cursor, return the associated module.
 */
CINDEX_LINKAGE CXModule clang_Cursor_getModule(CXCursor C);

/**
 * \brief Given a CXFile header file, return the module that contains it, if one
 * exists.
 */
CINDEX_LINKAGE CXModule clang_getModuleForFile(CXTranslationUnit, CXFile);

/**
 * \param Module a module object.
 *
 * \returns the module file where the provided module object came from.
 */
CINDEX_LINKAGE CXFile clang_Module_getASTFile(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the parent of a sub-module or NULL if the given module is top-level,
 * e.g. for 'std.vector' it will return the 'std' module.
 */
CINDEX_LINKAGE CXModule clang_Module_getParent(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the name of the module, e.g. for the 'std.vector' sub-module it
 * will return "vector".
 */
CINDEX_LINKAGE CXString clang_Module_getName(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the full name of the module, e.g. "std.vector".
 */
CINDEX_LINKAGE CXString clang_Module_getFullName(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns non-zero if the module is a system one.
 */
CINDEX_LINKAGE int clang_Module_isSystem(CXModule Module);

/**
 * \param Module a module object.
 *
 * \returns the number of top level headers associated with this module.
 */
CINDEX_LINKAGE unsigned clang_Module_getNumTopLevelHeaders(CXTranslationUnit,
                                                           CXModule Module);

/**
 * \param Module a module object.
 *
 * \param Index top level header index (zero-based).
 *
 * \returns the specified top level header associated with the module.
 */
CINDEX_LINKAGE
CXFile clang_Module_getTopLevelHeader(CXTranslationUnit,
                                      CXModule Module, unsigned Index);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CPP C++ AST introspection
 *
 * The routines in this group provide access information in the ASTs specific
 * to C++ language features.
 *
 * @{
 */

/**
 * \brief Determine if a C++ constructor is a converting constructor.
 */
CINDEX_LINKAGE unsigned clang_CXXConstructor_isConvertingConstructor(CXCursor C);

/**
 * \brief Determine if a C++ constructor is a copy constructor.
 */
CINDEX_LINKAGE unsigned clang_CXXConstructor_isCopyConstructor(CXCursor C);

/**
 * \brief Determine if a C++ constructor is the default constructor.
 */
CINDEX_LINKAGE unsigned clang_CXXConstructor_isDefaultConstructor(CXCursor C);

/**
 * \brief Determine if a C++ constructor is a move constructor.
 */
CINDEX_LINKAGE unsigned clang_CXXConstructor_isMoveConstructor(CXCursor C);

/**
 * \brief Determine if a C++ field is declared 'mutable'.
 */
CINDEX_LINKAGE unsigned clang_CXXField_isMutable(CXCursor C);

/**
 * \brief Determine if a C++ method is declared '= default'.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isDefaulted(CXCursor C);

/**
 * \brief Determine if a C++ member function or member function template is
 * pure virtual.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isPureVirtual(CXCursor C);

/**
 * \brief Determine if a C++ member function or member function template is 
 * declared 'static'.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isStatic(CXCursor C);

/**
 * \brief Determine if a C++ member function or member function template is
 * explicitly declared 'virtual' or if it overrides a virtual method from
 * one of the base classes.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isVirtual(CXCursor C);

/**
 * \brief Determine if a C++ member function or member function template is
 * declared 'const'.
 */
CINDEX_LINKAGE unsigned clang_CXXMethod_isConst(CXCursor C);

/**
 * \brief Given a cursor that represents a template, determine
 * the cursor kind of the specializations would be generated by instantiating
 * the template.
 *
 * This routine can be used to determine what flavor of function template,
 * class template, or class template partial specialization is stored in the
 * cursor. For example, it can describe whether a class template cursor is
 * declared with "struct", "class" or "union".
 *
 * \param C The cursor to query. This cursor should represent a template
 * declaration.
 *
 * \returns The cursor kind of the specializations that would be generated
 * by instantiating the template \p C. If \p C is not a template, returns
 * \c CXCursor_NoDeclFound.
 */
CINDEX_LINKAGE enum CXCursorKind clang_getTemplateCursorKind(CXCursor C);
  
/**
 * \brief Given a cursor that may represent a specialization or instantiation
 * of a template, retrieve the cursor that represents the template that it
 * specializes or from which it was instantiated.
 *
 * This routine determines the template involved both for explicit 
 * specializations of templates and for implicit instantiations of the template,
 * both of which are referred to as "specializations". For a class template
 * specialization (e.g., \c std::vector<bool>), this routine will return 
 * either the primary template (\c std::vector) or, if the specialization was
 * instantiated from a class template partial specialization, the class template
 * partial specialization. For a class template partial specialization and a
 * function template specialization (including instantiations), this
 * this routine will return the specialized template.
 *
 * For members of a class template (e.g., member functions, member classes, or
 * static data members), returns the specialized or instantiated member. 
 * Although not strictly "templates" in the C++ language, members of class
 * templates have the same notions of specializations and instantiations that
 * templates do, so this routine treats them similarly.
 *
 * \param C A cursor that may be a specialization of a template or a member
 * of a template.
 *
 * \returns If the given cursor is a specialization or instantiation of a 
 * template or a member thereof, the template or member that it specializes or
 * from which it was instantiated. Otherwise, returns a NULL cursor.
 */
CINDEX_LINKAGE CXCursor clang_getSpecializedCursorTemplate(CXCursor C);

/**
 * \brief Given a cursor that references something else, return the source range
 * covering that reference.
 *
 * \param C A cursor pointing to a member reference, a declaration reference, or
 * an operator call.
 * \param NameFlags A bitset with three independent flags: 
 * CXNameRange_WantQualifier, CXNameRange_WantTemplateArgs, and
 * CXNameRange_WantSinglePiece.
 * \param PieceIndex For contiguous names or when passing the flag 
 * CXNameRange_WantSinglePiece, only one piece with index 0 is 
 * available. When the CXNameRange_WantSinglePiece flag is not passed for a
 * non-contiguous names, this index can be used to retrieve the individual
 * pieces of the name. See also CXNameRange_WantSinglePiece.
 *
 * \returns The piece of the name pointed to by the given cursor. If there is no
 * name, or if the PieceIndex is out-of-range, a null-cursor will be returned.
 */
CINDEX_LINKAGE CXSourceRange clang_getCursorReferenceNameRange(CXCursor C,
                                                unsigned NameFlags, 
                                                unsigned PieceIndex);

enum CXNameRefFlags {
  /**
   * \brief Include the nested-name-specifier, e.g. Foo:: in x.Foo::y, in the
   * range.
   */
  CXNameRange_WantQualifier = 0x1,
  
  /**
   * \brief Include the explicit template arguments, e.g. \<int> in x.f<int>,
   * in the range.
   */
  CXNameRange_WantTemplateArgs = 0x2,

  /**
   * \brief If the name is non-contiguous, return the full spanning range.
   *
   * Non-contiguous names occur in Objective-C when a selector with two or more
   * parameters is used, or in C++ when using an operator:
   * \code
   * [object doSomething:here withValue:there]; // Objective-C
   * return some_vector[1]; // C++
   * \endcode
   */
  CXNameRange_WantSinglePiece = 0x4
};
  
/**
 * @}
 */

/**
 * \defgroup CINDEX_LEX Token extraction and manipulation
 *
 * The routines in this group provide access to the tokens within a
 * translation unit, along with a semantic mapping of those tokens to
 * their corresponding cursors.
 *
 * @{
 */

/**
 * \brief Describes a kind of token.
 */
typedef enum CXTokenKind {
  /**
   * \brief A token that contains some kind of punctuation.
   */
  CXToken_Punctuation,

  /**
   * \brief A language keyword.
   */
  CXToken_Keyword,

  /**
   * \brief An identifier (that is not a keyword).
   */
  CXToken_Identifier,

  /**
   * \brief A numeric, string, or character literal.
   */
  CXToken_Literal,

  /**
   * \brief A comment.
   */
  CXToken_Comment
} CXTokenKind;

/**
 * \brief Describes a single preprocessing token.
 */
typedef struct {
  unsigned int_data[4];
  void *ptr_data;
} CXToken;

/**
 * \brief Determine the kind of the given token.
 */
CINDEX_LINKAGE CXTokenKind clang_getTokenKind(CXToken);

/**
 * \brief Determine the spelling of the given token.
 *
 * The spelling of a token is the textual representation of that token, e.g.,
 * the text of an identifier or keyword.
 */
CINDEX_LINKAGE CXString clang_getTokenSpelling(CXTranslationUnit, CXToken);

/**
 * \brief Retrieve the source location of the given token.
 */
CINDEX_LINKAGE CXSourceLocation clang_getTokenLocation(CXTranslationUnit,
                                                       CXToken);

/**
 * \brief Retrieve a source range that covers the given token.
 */
CINDEX_LINKAGE CXSourceRange clang_getTokenExtent(CXTranslationUnit, CXToken);

/**
 * \brief Tokenize the source code described by the given range into raw
 * lexical tokens.
 *
 * \param TU the translation unit whose text is being tokenized.
 *
 * \param Range the source range in which text should be tokenized. All of the
 * tokens produced by tokenization will fall within this source range,
 *
 * \param Tokens this pointer will be set to point to the array of tokens
 * that occur within the given source range. The returned pointer must be
 * freed with clang_disposeTokens() before the translation unit is destroyed.
 *
 * \param NumTokens will be set to the number of tokens in the \c *Tokens
 * array.
 *
 */
CINDEX_LINKAGE void clang_tokenize(CXTranslationUnit TU, CXSourceRange Range,
                                   CXToken **Tokens, unsigned *NumTokens);

/**
 * \brief Annotate the given set of tokens by providing cursors for each token
 * that can be mapped to a specific entity within the abstract syntax tree.
 *
 * This token-annotation routine is equivalent to invoking
 * clang_getCursor() for the source locations of each of the
 * tokens. The cursors provided are filtered, so that only those
 * cursors that have a direct correspondence to the token are
 * accepted. For example, given a function call \c f(x),
 * clang_getCursor() would provide the following cursors:
 *
 *   * when the cursor is over the 'f', a DeclRefExpr cursor referring to 'f'.
 *   * when the cursor is over the '(' or the ')', a CallExpr referring to 'f'.
 *   * when the cursor is over the 'x', a DeclRefExpr cursor referring to 'x'.
 *
 * Only the first and last of these cursors will occur within the
 * annotate, since the tokens "f" and "x' directly refer to a function
 * and a variable, respectively, but the parentheses are just a small
 * part of the full syntax of the function call expression, which is
 * not provided as an annotation.
 *
 * \param TU the translation unit that owns the given tokens.
 *
 * \param Tokens the set of tokens to annotate.
 *
 * \param NumTokens the number of tokens in \p Tokens.
 *
 * \param Cursors an array of \p NumTokens cursors, whose contents will be
 * replaced with the cursors corresponding to each token.
 */
CINDEX_LINKAGE void clang_annotateTokens(CXTranslationUnit TU,
                                         CXToken *Tokens, unsigned NumTokens,
                                         CXCursor *Cursors);

/**
 * \brief Free the given set of tokens.
 */
CINDEX_LINKAGE void clang_disposeTokens(CXTranslationUnit TU,
                                        CXToken *Tokens, unsigned NumTokens);

/**
 * @}
 */

/**
 * \defgroup CINDEX_DEBUG Debugging facilities
 *
 * These routines are used for testing and debugging, only, and should not
 * be relied upon.
 *
 * @{
 */

/* for debug/testing */
CINDEX_LINKAGE CXString clang_getCursorKindSpelling(enum CXCursorKind Kind);
CINDEX_LINKAGE void clang_getDefinitionSpellingAndExtent(CXCursor,
                                          const char **startBuf,
                                          const char **endBuf,
                                          unsigned *startLine,
                                          unsigned *startColumn,
                                          unsigned *endLine,
                                          unsigned *endColumn);
CINDEX_LINKAGE void clang_enableStackTraces(void);
CINDEX_LINKAGE void clang_executeOnThread(void (*fn)(void*), void *user_data,
                                          unsigned stack_size);

/**
 * @}
 */

/**
 * \defgroup CINDEX_CODE_COMPLET Code completion
 *
 * Code completion involves taking an (incomplete) source file, along with
 * knowledge of where the user is actively editing that file, and suggesting
 * syntactically- and semantically-valid constructs that the user might want to
 * use at that particular point in the source code. These data structures and
 * routines provide support for code completion.
 *
 * @{
 */

/**
 * \brief A semantic string that describes a code-completion result.
 *
 * A semantic string that describes the formatting of a code-completion
 * result as a single "template" of text that should be inserted into the
 * source buffer when a particular code-completion result is selected.
 * Each semantic string is made up of some number of "chunks", each of which
 * contains some text along with a description of what that text means, e.g.,
 * the name of the entity being referenced, whether the text chunk is part of
 * the template, or whether it is a "placeholder" that the user should replace
 * with actual code,of a specific kind. See \c CXCompletionChunkKind for a
 * description of the different kinds of chunks.
 */
typedef void *CXCompletionString;

/**
 * \brief A single result of code completion.
 */
typedef struct {
  /**
   * \brief The kind of entity that this completion refers to.
   *
   * The cursor kind will be a macro, keyword, or a declaration (one of the
   * *Decl cursor kinds), describing the entity that the completion is
   * referring to.
   *
   * \todo In the future, we would like to provide a full cursor, to allow
   * the client to extract additional information from declaration.
   */
  enum CXCursorKind CursorKind;

  /**
   * \brief The code-completion string that describes how to insert this
   * code-completion result into the editing buffer.
   */
  CXCompletionString CompletionString;
} CXCompletionResult;

/**
 * \brief Describes a single piece of text within a code-completion string.
 *
 * Each "chunk" within a code-completion string (\c CXCompletionString) is
 * either a piece of text with a specific "kind" that describes how that text
 * should be interpreted by the client or is another completion string.
 */
enum CXCompletionChunkKind {
  /**
   * \brief A code-completion string that describes "optional" text that
   * could be a part of the template (but is not required).
   *
   * The Optional chunk is the only kind of chunk that has a code-completion
   * string for its representation, which is accessible via
   * \c clang_getCompletionChunkCompletionString(). The code-completion string
   * describes an additional part of the template that is completely optional.
   * For example, optional chunks can be used to describe the placeholders for
   * arguments that match up with defaulted function parameters, e.g. given:
   *
   * \code
   * void f(int x, float y = 3.14, double z = 2.71828);
   * \endcode
   *
   * The code-completion string for this function would contain:
   *   - a TypedText chunk for "f".
   *   - a LeftParen chunk for "(".
   *   - a Placeholder chunk for "int x"
   *   - an Optional chunk containing the remaining defaulted arguments, e.g.,
   *       - a Comma chunk for ","
   *       - a Placeholder chunk for "float y"
   *       - an Optional chunk containing the last defaulted argument:
   *           - a Comma chunk for ","
   *           - a Placeholder chunk for "double z"
   *   - a RightParen chunk for ")"
   *
   * There are many ways to handle Optional chunks. Two simple approaches are:
   *   - Completely ignore optional chunks, in which case the template for the
   *     function "f" would only include the first parameter ("int x").
   *   - Fully expand all optional chunks, in which case the template for the
   *     function "f" would have all of the parameters.
   */
  CXCompletionChunk_Optional,
  /**
   * \brief Text that a user would be expected to type to get this
   * code-completion result.
   *
   * There will be exactly one "typed text" chunk in a semantic string, which
   * will typically provide the spelling of a keyword or the name of a
   * declaration that could be used at the current code point. Clients are
   * expected to filter the code-completion results based on the text in this
   * chunk.
   */
  CXCompletionChunk_TypedText,
  /**
   * \brief Text that should be inserted as part of a code-completion result.
   *
   * A "text" chunk represents text that is part of the template to be
   * inserted into user code should this particular code-completion result
   * be selected.
   */
  CXCompletionChunk_Text,
  /**
   * \brief Placeholder text that should be replaced by the user.
   *
   * A "placeholder" chunk marks a place where the user should insert text
   * into the code-completion template. For example, placeholders might mark
   * the function parameters for a function declaration, to indicate that the
   * user should provide arguments for each of those parameters. The actual
   * text in a placeholder is a suggestion for the text to display before
   * the user replaces the placeholder with real code.
   */
  CXCompletionChunk_Placeholder,
  /**
   * \brief Informative text that should be displayed but never inserted as
   * part of the template.
   *
   * An "informative" chunk contains annotations that can be displayed to
   * help the user decide whether a particular code-completion result is the
   * right option, but which is not part of the actual template to be inserted
   * by code completion.
   */
  CXCompletionChunk_Informative,
  /**
   * \brief Text that describes the current parameter when code-completion is
   * referring to function call, message send, or template specialization.
   *
   * A "current parameter" chunk occurs when code-completion is providing
   * information about a parameter corresponding to the argument at the
   * code-completion point. For example, given a function
   *
   * \code
   * int add(int x, int y);
   * \endcode
   *
   * and the source code \c add(, where the code-completion point is after the
   * "(", the code-completion string will contain a "current parameter" chunk
   * for "int x", indicating that the current argument will initialize that
   * parameter. After typing further, to \c add(17, (where the code-completion
   * point is after the ","), the code-completion string will contain a
   * "current paremeter" chunk to "int y".
   */
  CXCompletionChunk_CurrentParameter,
  /**
   * \brief A left parenthesis ('('), used to initiate a function call or
   * signal the beginning of a function parameter list.
   */
  CXCompletionChunk_LeftParen,
  /**
   * \brief A right parenthesis (')'), used to finish a function call or
   * signal the end of a function parameter list.
   */
  CXCompletionChunk_RightParen,
  /**
   * \brief A left bracket ('[').
   */
  CXCompletionChunk_LeftBracket,
  /**
   * \brief A right bracket (']').
   */
  CXCompletionChunk_RightBracket,
  /**
   * \brief A left brace ('{').
   */
  CXCompletionChunk_LeftBrace,
  /**
   * \brief A right brace ('}').
   */
  CXCompletionChunk_RightBrace,
  /**
   * \brief A left angle bracket ('<').
   */
  CXCompletionChunk_LeftAngle,
  /**
   * \brief A right angle bracket ('>').
   */
  CXCompletionChunk_RightAngle,
  /**
   * \brief A comma separator (',').
   */
  CXCompletionChunk_Comma,
  /**
   * \brief Text that specifies the result type of a given result.
   *
   * This special kind of informative chunk is not meant to be inserted into
   * the text buffer. Rather, it is meant to illustrate the type that an
   * expression using the given completion string would have.
   */
  CXCompletionChunk_ResultType,
  /**
   * \brief A colon (':').
   */
  CXCompletionChunk_Colon,
  /**
   * \brief A semicolon (';').
   */
  CXCompletionChunk_SemiColon,
  /**
   * \brief An '=' sign.
   */
  CXCompletionChunk_Equal,
  /**
   * Horizontal space (' ').
   */
  CXCompletionChunk_HorizontalSpace,
  /**
   * Vertical space ('\n'), after which it is generally a good idea to
   * perform indentation.
   */
  CXCompletionChunk_VerticalSpace
};

/**
 * \brief Determine the kind of a particular chunk within a completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the kind of the chunk at the index \c chunk_number.
 */
CINDEX_LINKAGE enum CXCompletionChunkKind
clang_getCompletionChunkKind(CXCompletionString completion_string,
                             unsigned chunk_number);

/**
 * \brief Retrieve the text associated with a particular chunk within a
 * completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the text associated with the chunk at index \c chunk_number.
 */
CINDEX_LINKAGE CXString
clang_getCompletionChunkText(CXCompletionString completion_string,
                             unsigned chunk_number);

/**
 * \brief Retrieve the completion string associated with a particular chunk
 * within a completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param chunk_number the 0-based index of the chunk in the completion string.
 *
 * \returns the completion string associated with the chunk at index
 * \c chunk_number.
 */
CINDEX_LINKAGE CXCompletionString
clang_getCompletionChunkCompletionString(CXCompletionString completion_string,
                                         unsigned chunk_number);

/**
 * \brief Retrieve the number of chunks in the given code-completion string.
 */
CINDEX_LINKAGE unsigned
clang_getNumCompletionChunks(CXCompletionString completion_string);

/**
 * \brief Determine the priority of this code completion.
 *
 * The priority of a code completion indicates how likely it is that this 
 * particular completion is the completion that the user will select. The
 * priority is selected by various internal heuristics.
 *
 * \param completion_string The completion string to query.
 *
 * \returns The priority of this completion string. Smaller values indicate
 * higher-priority (more likely) completions.
 */
CINDEX_LINKAGE unsigned
clang_getCompletionPriority(CXCompletionString completion_string);
  
/**
 * \brief Determine the availability of the entity that this code-completion
 * string refers to.
 *
 * \param completion_string The completion string to query.
 *
 * \returns The availability of the completion string.
 */
CINDEX_LINKAGE enum CXAvailabilityKind 
clang_getCompletionAvailability(CXCompletionString completion_string);

/**
 * \brief Retrieve the number of annotations associated with the given
 * completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \returns the number of annotations associated with the given completion
 * string.
 */
CINDEX_LINKAGE unsigned
clang_getCompletionNumAnnotations(CXCompletionString completion_string);

/**
 * \brief Retrieve the annotation associated with the given completion string.
 *
 * \param completion_string the completion string to query.
 *
 * \param annotation_number the 0-based index of the annotation of the
 * completion string.
 *
 * \returns annotation string associated with the completion at index
 * \c annotation_number, or a NULL string if that annotation is not available.
 */
CINDEX_LINKAGE CXString
clang_getCompletionAnnotation(CXCompletionString completion_string,
                              unsigned annotation_number);

/**
 * \brief Retrieve the parent context of the given completion string.
 *
 * The parent context of a completion string is the semantic parent of 
 * the declaration (if any) that the code completion represents. For example,
 * a code completion for an Objective-C method would have the method's class
 * or protocol as its context.
 *
 * \param completion_string The code completion string whose parent is
 * being queried.
 *
 * \param kind DEPRECATED: always set to CXCursor_NotImplemented if non-NULL.
 *
 * \returns The name of the completion parent, e.g., "NSObject" if
 * the completion string represents a method in the NSObject class.
 */
CINDEX_LINKAGE CXString
clang_getCompletionParent(CXCompletionString completion_string,
                          enum CXCursorKind *kind);

/**
 * \brief Retrieve the brief documentation comment attached to the declaration
 * that corresponds to the given completion string.
 */
CINDEX_LINKAGE CXString
clang_getCompletionBriefComment(CXCompletionString completion_string);

/**
 * \brief Retrieve a completion string for an arbitrary declaration or macro
 * definition cursor.
 *
 * \param cursor The cursor to query.
 *
 * \returns A non-context-sensitive completion string for declaration and macro
 * definition cursors, or NULL for other kinds of cursors.
 */
CINDEX_LINKAGE CXCompletionString
clang_getCursorCompletionString(CXCursor cursor);
  
/**
 * \brief Contains the results of code-completion.
 *
 * This data structure contains the results of code completion, as
 * produced by \c clang_codeCompleteAt(). Its contents must be freed by
 * \c clang_disposeCodeCompleteResults.
 */
typedef struct {
  /**
   * \brief The code-completion results.
   */
  CXCompletionResult *Results;

  /**
   * \brief The number of code-completion results stored in the
   * \c Results array.
   */
  unsigned NumResults;
} CXCodeCompleteResults;

/**
 * \brief Flags that can be passed to \c clang_codeCompleteAt() to
 * modify its behavior.
 *
 * The enumerators in this enumeration can be bitwise-OR'd together to
 * provide multiple options to \c clang_codeCompleteAt().
 */
enum CXCodeComplete_Flags {
  /**
   * \brief Whether to include macros within the set of code
   * completions returned.
   */
  CXCodeComplete_IncludeMacros = 0x01,

  /**
   * \brief Whether to include code patterns for language constructs
   * within the set of code completions, e.g., for loops.
   */
  CXCodeComplete_IncludeCodePatterns = 0x02,

  /**
   * \brief Whether to include brief documentation within the set of code
   * completions returned.
   */
  CXCodeComplete_IncludeBriefComments = 0x04
};

/**
 * \brief Bits that represent the context under which completion is occurring.
 *
 * The enumerators in this enumeration may be bitwise-OR'd together if multiple
 * contexts are occurring simultaneously.
 */
enum CXCompletionContext {
  /**
   * \brief The context for completions is unexposed, as only Clang results
   * should be included. (This is equivalent to having no context bits set.)
   */
  CXCompletionContext_Unexposed = 0,
  
  /**
   * \brief Completions for any possible type should be included in the results.
   */
  CXCompletionContext_AnyType = 1 << 0,
  
  /**
   * \brief Completions for any possible value (variables, function calls, etc.)
   * should be included in the results.
   */
  CXCompletionContext_AnyValue = 1 << 1,
  /**
   * \brief Completions for values that resolve to an Objective-C object should
   * be included in the results.
   */
  CXCompletionContext_ObjCObjectValue = 1 << 2,
  /**
   * \brief Completions for values that resolve to an Objective-C selector
   * should be included in the results.
   */
  CXCompletionContext_ObjCSelectorValue = 1 << 3,
  /**
   * \brief Completions for values that resolve to a C++ class type should be
   * included in the results.
   */
  CXCompletionContext_CXXClassTypeValue = 1 << 4,
  
  /**
   * \brief Completions for fields of the member being accessed using the dot
   * operator should be included in the results.
   */
  CXCompletionContext_DotMemberAccess = 1 << 5,
  /**
   * \brief Completions for fields of the member being accessed using the arrow
   * operator should be included in the results.
   */
  CXCompletionContext_ArrowMemberAccess = 1 << 6,
  /**
   * \brief Completions for properties of the Objective-C object being accessed
   * using the dot operator should be included in the results.
   */
  CXCompletionContext_ObjCPropertyAccess = 1 << 7,
  
  /**
   * \brief Completions for enum tags should be included in the results.
   */
  CXCompletionContext_EnumTag = 1 << 8,
  /**
   * \brief Completions for union tags should be included in the results.
   */
  CXCompletionContext_UnionTag = 1 << 9,
  /**
   * \brief Completions for struct tags should be included in the results.
   */
  CXCompletionContext_StructTag = 1 << 10,
  
  /**
   * \brief Completions for C++ class names should be included in the results.
   */
  CXCompletionContext_ClassTag = 1 << 11,
  /**
   * \brief Completions for C++ namespaces and namespace aliases should be
   * included in the results.
   */
  CXCompletionContext_Namespace = 1 << 12,
  /**
   * \brief Completions for C++ nested name specifiers should be included in
   * the results.
   */
  CXCompletionContext_NestedNameSpecifier = 1 << 13,
  
  /**
   * \brief Completions for Objective-C interfaces (classes) should be included
   * in the results.
   */
  CXCompletionContext_ObjCInterface = 1 << 14,
  /**
   * \brief Completions for Objective-C protocols should be included in
   * the results.
   */
  CXCompletionContext_ObjCProtocol = 1 << 15,
  /**
   * \brief Completions for Objective-C categories should be included in
   * the results.
   */
  CXCompletionContext_ObjCCategory = 1 << 16,
  /**
   * \brief Completions for Objective-C instance messages should be included
   * in the results.
   */
  CXCompletionContext_ObjCInstanceMessage = 1 << 17,
  /**
   * \brief Completions for Objective-C class messages should be included in
   * the results.
   */
  CXCompletionContext_ObjCClassMessage = 1 << 18,
  /**
   * \brief Completions for Objective-C selector names should be included in
   * the results.
   */
  CXCompletionContext_ObjCSelectorName = 1 << 19,
  
  /**
   * \brief Completions for preprocessor macro names should be included in
   * the results.
   */
  CXCompletionContext_MacroName = 1 << 20,
  
  /**
   * \brief Natural language completions should be included in the results.
   */
  CXCompletionContext_NaturalLanguage = 1 << 21,
  
  /**
   * \brief The current context is unknown, so set all contexts.
   */
  CXCompletionContext_Unknown = ((1 << 22) - 1)
};
  
/**
 * \brief Returns a default set of code-completion options that can be
 * passed to\c clang_codeCompleteAt(). 
 */
CINDEX_LINKAGE unsigned clang_defaultCodeCompleteOptions(void);

/**
 * \brief Perform code completion at a given location in a translation unit.
 *
 * This function performs code completion at a particular file, line, and
 * column within source code, providing results that suggest potential
 * code snippets based on the context of the completion. The basic model
 * for code completion is that Clang will parse a complete source file,
 * performing syntax checking up to the location where code-completion has
 * been requested. At that point, a special code-completion token is passed
 * to the parser, which recognizes this token and determines, based on the
 * current location in the C/Objective-C/C++ grammar and the state of
 * semantic analysis, what completions to provide. These completions are
 * returned via a new \c CXCodeCompleteResults structure.
 *
 * Code completion itself is meant to be triggered by the client when the
 * user types punctuation characters or whitespace, at which point the
 * code-completion location will coincide with the cursor. For example, if \c p
 * is a pointer, code-completion might be triggered after the "-" and then
 * after the ">" in \c p->. When the code-completion location is afer the ">",
 * the completion results will provide, e.g., the members of the struct that
 * "p" points to. The client is responsible for placing the cursor at the
 * beginning of the token currently being typed, then filtering the results
 * based on the contents of the token. For example, when code-completing for
 * the expression \c p->get, the client should provide the location just after
 * the ">" (e.g., pointing at the "g") to this code-completion hook. Then, the
 * client can filter the results based on the current token text ("get"), only
 * showing those results that start with "get". The intent of this interface
 * is to separate the relatively high-latency acquisition of code-completion
 * results from the filtering of results on a per-character basis, which must
 * have a lower latency.
 *
 * \param TU The translation unit in which code-completion should
 * occur. The source files for this translation unit need not be
 * completely up-to-date (and the contents of those source files may
 * be overridden via \p unsaved_files). Cursors referring into the
 * translation unit may be invalidated by this invocation.
 *
 * \param complete_filename The name of the source file where code
 * completion should be performed. This filename may be any file
 * included in the translation unit.
 *
 * \param complete_line The line at which code-completion should occur.
 *
 * \param complete_column The column at which code-completion should occur.
 * Note that the column should point just after the syntactic construct that
 * initiated code completion, and not in the middle of a lexical token.
 *
 * \param unsaved_files the Files that have not yet been saved to disk
 * but may be required for parsing or code completion, including the
 * contents of those files.  The contents and name of these files (as
 * specified by CXUnsavedFile) are copied when necessary, so the
 * client only needs to guarantee their validity until the call to
 * this function returns.
 *
 * \param num_unsaved_files The number of unsaved file entries in \p
 * unsaved_files.
 *
 * \param options Extra options that control the behavior of code
 * completion, expressed as a bitwise OR of the enumerators of the
 * CXCodeComplete_Flags enumeration. The 
 * \c clang_defaultCodeCompleteOptions() function returns a default set
 * of code-completion options.
 *
 * \returns If successful, a new \c CXCodeCompleteResults structure
 * containing code-completion results, which should eventually be
 * freed with \c clang_disposeCodeCompleteResults(). If code
 * completion fails, returns NULL.
 */
CINDEX_LINKAGE
CXCodeCompleteResults *clang_codeCompleteAt(CXTranslationUnit TU,
                                            const char *complete_filename,
                                            unsigned complete_line,
                                            unsigned complete_column,
                                            struct CXUnsavedFile *unsaved_files,
                                            unsigned num_unsaved_files,
                                            unsigned options);

/**
 * \brief Sort the code-completion results in case-insensitive alphabetical 
 * order.
 *
 * \param Results The set of results to sort.
 * \param NumResults The number of results in \p Results.
 */
CINDEX_LINKAGE
void clang_sortCodeCompletionResults(CXCompletionResult *Results,
                                     unsigned NumResults);
  
/**
 * \brief Free the given set of code-completion results.
 */
CINDEX_LINKAGE
void clang_disposeCodeCompleteResults(CXCodeCompleteResults *Results);
  
/**
 * \brief Determine the number of diagnostics produced prior to the
 * location where code completion was performed.
 */
CINDEX_LINKAGE
unsigned clang_codeCompleteGetNumDiagnostics(CXCodeCompleteResults *Results);

/**
 * \brief Retrieve a diagnostic associated with the given code completion.
 *
 * \param Results the code completion results to query.
 * \param Index the zero-based diagnostic number to retrieve.
 *
 * \returns the requested diagnostic. This diagnostic must be freed
 * via a call to \c clang_disposeDiagnostic().
 */
CINDEX_LINKAGE
CXDiagnostic clang_codeCompleteGetDiagnostic(CXCodeCompleteResults *Results,
                                             unsigned Index);

/**
 * \brief Determines what completions are appropriate for the context
 * the given code completion.
 * 
 * \param Results the code completion results to query
 *
 * \returns the kinds of completions that are appropriate for use
 * along with the given code completion results.
 */
CINDEX_LINKAGE
unsigned long long clang_codeCompleteGetContexts(
                                                CXCodeCompleteResults *Results);

/**
 * \brief Returns the cursor kind for the container for the current code
 * completion context. The container is only guaranteed to be set for
 * contexts where a container exists (i.e. member accesses or Objective-C
 * message sends); if there is not a container, this function will return
 * CXCursor_InvalidCode.
 *
 * \param Results the code completion results to query
 *
 * \param IsIncomplete on return, this value will be false if Clang has complete
 * information about the container. If Clang does not have complete
 * information, this value will be true.
 *
 * \returns the container kind, or CXCursor_InvalidCode if there is not a
 * container
 */
CINDEX_LINKAGE
enum CXCursorKind clang_codeCompleteGetContainerKind(
                                                 CXCodeCompleteResults *Results,
                                                     unsigned *IsIncomplete);

/**
 * \brief Returns the USR for the container for the current code completion
 * context. If there is not a container for the current context, this
 * function will return the empty string.
 *
 * \param Results the code completion results to query
 *
 * \returns the USR for the container
 */
CINDEX_LINKAGE
CXString clang_codeCompleteGetContainerUSR(CXCodeCompleteResults *Results);

/**
 * \brief Returns the currently-entered selector for an Objective-C message
 * send, formatted like "initWithFoo:bar:". Only guaranteed to return a
 * non-empty string for CXCompletionContext_ObjCInstanceMessage and
 * CXCompletionContext_ObjCClassMessage.
 *
 * \param Results the code completion results to query
 *
 * \returns the selector (or partial selector) that has been entered thus far
 * for an Objective-C message send.
 */
CINDEX_LINKAGE
CXString clang_codeCompleteGetObjCSelector(CXCodeCompleteResults *Results);
  
/**
 * @}
 */

/**
 * \defgroup CINDEX_MISC Miscellaneous utility functions
 *
 * @{
 */

/**
 * \brief Return a version string, suitable for showing to a user, but not
 *        intended to be parsed (the format is not guaranteed to be stable).
 */
CINDEX_LINKAGE CXString clang_getClangVersion(void);

/**
 * \brief Enable/disable crash recovery.
 *
 * \param isEnabled Flag to indicate if crash recovery is enabled.  A non-zero
 *        value enables crash recovery, while 0 disables it.
 */
CINDEX_LINKAGE void clang_toggleCrashRecovery(unsigned isEnabled);
  
 /**
  * \brief Visitor invoked for each file in a translation unit
  *        (used with clang_getInclusions()).
  *
  * This visitor function will be invoked by clang_getInclusions() for each
  * file included (either at the top-level or by \#include directives) within
  * a translation unit.  The first argument is the file being included, and
  * the second and third arguments provide the inclusion stack.  The
  * array is sorted in order of immediate inclusion.  For example,
  * the first element refers to the location that included 'included_file'.
  */
typedef void (*CXInclusionVisitor)(CXFile included_file,
                                   CXSourceLocation* inclusion_stack,
                                   unsigned include_len,
                                   CXClientData client_data);

/**
 * \brief Visit the set of preprocessor inclusions in a translation unit.
 *   The visitor function is called with the provided data for every included
 *   file.  This does not include headers included by the PCH file (unless one
 *   is inspecting the inclusions in the PCH file itself).
 */
CINDEX_LINKAGE void clang_getInclusions(CXTranslationUnit tu,
                                        CXInclusionVisitor visitor,
                                        CXClientData client_data);

typedef enum {
  CXEval_Int = 1 ,
  CXEval_Float = 2,
  CXEval_ObjCStrLiteral = 3,
  CXEval_StrLiteral = 4,
  CXEval_CFStr = 5,
  CXEval_Other = 6,

  CXEval_UnExposed = 0

} CXEvalResultKind ;

/**
 * \brief Evaluation result of a cursor
 */
typedef void * CXEvalResult;

/**
 * \brief If cursor is a statement declaration tries to evaluate the 
 * statement and if its variable, tries to evaluate its initializer,
 * into its corresponding type.
 */
CINDEX_LINKAGE CXEvalResult clang_Cursor_Evaluate(CXCursor C);

/**
 * \brief Returns the kind of the evaluated result.
 */
CINDEX_LINKAGE CXEvalResultKind clang_EvalResult_getKind(CXEvalResult E);

/**
 * \brief Returns the evaluation result as integer if the
 * kind is Int.
 */
CINDEX_LINKAGE int clang_EvalResult_getAsInt(CXEvalResult E);

/**
 * \brief Returns the evaluation result as a long long integer if the
 * kind is Int. This prevents overflows that may happen if the result is
 * returned with clang_EvalResult_getAsInt.
 */
CINDEX_LINKAGE long long clang_EvalResult_getAsLongLong(CXEvalResult E);

/**
 * \brief Returns a non-zero value if the kind is Int and the evaluation
 * result resulted in an unsigned integer.
 */
CINDEX_LINKAGE unsigned clang_EvalResult_isUnsignedInt(CXEvalResult E);

/**
 * \brief Returns the evaluation result as an unsigned integer if
 * the kind is Int and clang_EvalResult_isUnsignedInt is non-zero.
 */
CINDEX_LINKAGE unsigned long long clang_EvalResult_getAsUnsigned(CXEvalResult E);

/**
 * \brief Returns the evaluation result as double if the
 * kind is double.
 */
CINDEX_LINKAGE double clang_EvalResult_getAsDouble(CXEvalResult E);

/**
 * \brief Returns the evaluation result as a constant string if the
 * kind is other than Int or float. User must not free this pointer,
 * instead call clang_EvalResult_dispose on the CXEvalResult returned
 * by clang_Cursor_Evaluate.
 */
CINDEX_LINKAGE const char* clang_EvalResult_getAsStr(CXEvalResult E);

/**
 * \brief Disposes the created Eval memory.
 */
CINDEX_LINKAGE void clang_EvalResult_dispose(CXEvalResult E);
/**
 * @}
 */

/** \defgroup CINDEX_REMAPPING Remapping functions
 *
 * @{
 */

/**
 * \brief A remapping of original source files and their translated files.
 */
typedef void *CXRemapping;

/**
 * \brief Retrieve a remapping.
 *
 * \param path the path that contains metadata about remappings.
 *
 * \returns the requested remapping. This remapping must be freed
 * via a call to \c clang_remap_dispose(). Can return NULL if an error occurred.
 */
CINDEX_LINKAGE CXRemapping clang_getRemappings(const char *path);

/**
 * \brief Retrieve a remapping.
 *
 * \param filePaths pointer to an array of file paths containing remapping info.
 *
 * \param numFiles number of file paths.
 *
 * \returns the requested remapping. This remapping must be freed
 * via a call to \c clang_remap_dispose(). Can return NULL if an error occurred.
 */
CINDEX_LINKAGE
CXRemapping clang_getRemappingsFromFileList(const char **filePaths,
                                            unsigned numFiles);

/**
 * \brief Determine the number of remappings.
 */
CINDEX_LINKAGE unsigned clang_remap_getNumFiles(CXRemapping);

/**
 * \brief Get the original and the associated filename from the remapping.
 * 
 * \param original If non-NULL, will be set to the original filename.
 *
 * \param transformed If non-NULL, will be set to the filename that the original
 * is associated with.
 */
CINDEX_LINKAGE void clang_remap_getFilenames(CXRemapping, unsigned index,
                                     CXString *original, CXString *transformed);

/**
 * \brief Dispose the remapping.
 */
CINDEX_LINKAGE void clang_remap_dispose(CXRemapping);

/**
 * @}
 */

/** \defgroup CINDEX_HIGH Higher level API functions
 *
 * @{
 */

enum CXVisitorResult {
  CXVisit_Break,
  CXVisit_Continue
};

typedef struct CXCursorAndRangeVisitor {
  void *context;
  enum CXVisitorResult (*visit)(void *context, CXCursor, CXSourceRange);
} CXCursorAndRangeVisitor;

typedef enum {
  /**
   * \brief Function returned successfully.
   */
  CXResult_Success = 0,
  /**
   * \brief One of the parameters was invalid for the function.
   */
  CXResult_Invalid = 1,
  /**
   * \brief The function was terminated by a callback (e.g. it returned
   * CXVisit_Break)
   */
  CXResult_VisitBreak = 2

} CXResult;

/**
 * \brief Find references of a declaration in a specific file.
 * 
 * \param cursor pointing to a declaration or a reference of one.
 *
 * \param file to search for references.
 *
 * \param visitor callback that will receive pairs of CXCursor/CXSourceRange for
 * each reference found.
 * The CXSourceRange will point inside the file; if the reference is inside
 * a macro (and not a macro argument) the CXSourceRange will be invalid.
 *
 * \returns one of the CXResult enumerators.
 */
CINDEX_LINKAGE CXResult clang_findReferencesInFile(CXCursor cursor, CXFile file,
                                               CXCursorAndRangeVisitor visitor);

/**
 * \brief Find #import/#include directives in a specific file.
 *
 * \param TU translation unit containing the file to query.
 *
 * \param file to search for #import/#include directives.
 *
 * \param visitor callback that will receive pairs of CXCursor/CXSourceRange for
 * each directive found.
 *
 * \returns one of the CXResult enumerators.
 */
CINDEX_LINKAGE CXResult clang_findIncludesInFile(CXTranslationUnit TU,
                                                 CXFile file,
                                              CXCursorAndRangeVisitor visitor);

#ifdef __has_feature
#  if __has_feature(blocks)

typedef enum CXVisitorResult
    (^CXCursorAndRangeVisitorBlock)(CXCursor, CXSourceRange);

CINDEX_LINKAGE
CXResult clang_findReferencesInFileWithBlock(CXCursor, CXFile,
                                             CXCursorAndRangeVisitorBlock);

CINDEX_LINKAGE
CXResult clang_findIncludesInFileWithBlock(CXTranslationUnit, CXFile,
                                           CXCursorAndRangeVisitorBlock);

#  endif
#endif

/**
 * \brief The client's data object that is associated with a CXFile.
 */
typedef void *CXIdxClientFile;

/**
 * \brief The client's data object that is associated with a semantic entity.
 */
typedef void *CXIdxClientEntity;

/**
 * \brief The client's data object that is associated with a semantic container
 * of entities.
 */
typedef void *CXIdxClientContainer;

/**
 * \brief The client's data object that is associated with an AST file (PCH
 * or module).
 */
typedef void *CXIdxClientASTFile;

/**
 * \brief Source location passed to index callbacks.
 */
typedef struct {
  void *ptr_data[2];
  unsigned int_data;
} CXIdxLoc;

/**
 * \brief Data for ppIncludedFile callback.
 */
typedef struct {
  /**
   * \brief Location of '#' in the \#include/\#import directive.
   */
  CXIdxLoc hashLoc;
  /**
   * \brief Filename as written in the \#include/\#import directive.
   */
  const char *filename;
  /**
   * \brief The actual file that the \#include/\#import directive resolved to.
   */
  CXFile file;
  int isImport;
  int isAngled;
  /**
   * \brief Non-zero if the directive was automatically turned into a module
   * import.
   */
  int isModuleImport;
} CXIdxIncludedFileInfo;

/**
 * \brief Data for IndexerCallbacks#importedASTFile.
 */
typedef struct {
  /**
   * \brief Top level AST file containing the imported PCH, module or submodule.
   */
  CXFile file;
  /**
   * \brief The imported module or NULL if the AST file is a PCH.
   */
  CXModule module;
  /**
   * \brief Location where the file is imported. Applicable only for modules.
   */
  CXIdxLoc loc;
  /**
   * \brief Non-zero if an inclusion directive was automatically turned into
   * a module import. Applicable only for modules.
   */
  int isImplicit;

} CXIdxImportedASTFileInfo;

typedef enum {
  CXIdxEntity_Unexposed     = 0,
  CXIdxEntity_Typedef       = 1,
  CXIdxEntity_Function      = 2,
  CXIdxEntity_Variable      = 3,
  CXIdxEntity_Field         = 4,
  CXIdxEntity_EnumConstant  = 5,

  CXIdxEntity_ObjCClass     = 6,
  CXIdxEntity_ObjCProtocol  = 7,
  CXIdxEntity_ObjCCategory  = 8,

  CXIdxEntity_ObjCInstanceMethod = 9,
  CXIdxEntity_ObjCClassMethod    = 10,
  CXIdxEntity_ObjCProperty  = 11,
  CXIdxEntity_ObjCIvar      = 12,

  CXIdxEntity_Enum          = 13,
  CXIdxEntity_Struct        = 14,
  CXIdxEntity_Union         = 15,

  CXIdxEntity_CXXClass              = 16,
  CXIdxEntity_CXXNamespace          = 17,
  CXIdxEntity_CXXNamespaceAlias     = 18,
  CXIdxEntity_CXXStaticVariable     = 19,
  CXIdxEntity_CXXStaticMethod       = 20,
  CXIdxEntity_CXXInstanceMethod     = 21,
  CXIdxEntity_CXXConstructor        = 22,
  CXIdxEntity_CXXDestructor         = 23,
  CXIdxEntity_CXXConversionFunction = 24,
  CXIdxEntity_CXXTypeAlias          = 25,
  CXIdxEntity_CXXInterface          = 26

} CXIdxEntityKind;

typedef enum {
  CXIdxEntityLang_None = 0,
  CXIdxEntityLang_C    = 1,
  CXIdxEntityLang_ObjC = 2,
  CXIdxEntityLang_CXX  = 3
} CXIdxEntityLanguage;

/**
 * \brief Extra C++ template information for an entity. This can apply to:
 * CXIdxEntity_Function
 * CXIdxEntity_CXXClass
 * CXIdxEntity_CXXStaticMethod
 * CXIdxEntity_CXXInstanceMethod
 * CXIdxEntity_CXXConstructor
 * CXIdxEntity_CXXConversionFunction
 * CXIdxEntity_CXXTypeAlias
 */
typedef enum {
  CXIdxEntity_NonTemplate   = 0,
  CXIdxEntity_Template      = 1,
  CXIdxEntity_TemplatePartialSpecialization = 2,
  CXIdxEntity_TemplateSpecialization = 3
} CXIdxEntityCXXTemplateKind;

typedef enum {
  CXIdxAttr_Unexposed     = 0,
  CXIdxAttr_IBAction      = 1,
  CXIdxAttr_IBOutlet      = 2,
  CXIdxAttr_IBOutletCollection = 3
} CXIdxAttrKind;

typedef struct {
  CXIdxAttrKind kind;
  CXCursor cursor;
  CXIdxLoc loc;
} CXIdxAttrInfo;

typedef struct {
  CXIdxEntityKind kind;
  CXIdxEntityCXXTemplateKind templateKind;
  CXIdxEntityLanguage lang;
  const char *name;
  const char *USR;
  CXCursor cursor;
  const CXIdxAttrInfo *const *attributes;
  unsigned numAttributes;
} CXIdxEntityInfo;

typedef struct {
  CXCursor cursor;
} CXIdxContainerInfo;

typedef struct {
  const CXIdxAttrInfo *attrInfo;
  const CXIdxEntityInfo *objcClass;
  CXCursor classCursor;
  CXIdxLoc classLoc;
} CXIdxIBOutletCollectionAttrInfo;

typedef enum {
  CXIdxDeclFlag_Skipped = 0x1
} CXIdxDeclInfoFlags;

typedef struct {
  const CXIdxEntityInfo *entityInfo;
  CXCursor cursor;
  CXIdxLoc loc;
  const CXIdxContainerInfo *semanticContainer;
  /**
   * \brief Generally same as #semanticContainer but can be different in
   * cases like out-of-line C++ member functions.
   */
  const CXIdxContainerInfo *lexicalContainer;
  int isRedeclaration;
  int isDefinition;
  int isContainer;
  const CXIdxContainerInfo *declAsContainer;
  /**
   * \brief Whether the declaration exists in code or was created implicitly
   * by the compiler, e.g. implicit Objective-C methods for properties.
   */
  int isImplicit;
  const CXIdxAttrInfo *const *attributes;
  unsigned numAttributes;

  unsigned flags;

} CXIdxDeclInfo;

typedef enum {
  CXIdxObjCContainer_ForwardRef = 0,
  CXIdxObjCContainer_Interface = 1,
  CXIdxObjCContainer_Implementation = 2
} CXIdxObjCContainerKind;

typedef struct {
  const CXIdxDeclInfo *declInfo;
  CXIdxObjCContainerKind kind;
} CXIdxObjCContainerDeclInfo;

typedef struct {
  const CXIdxEntityInfo *base;
  CXCursor cursor;
  CXIdxLoc loc;
} CXIdxBaseClassInfo;

typedef struct {
  const CXIdxEntityInfo *protocol;
  CXCursor cursor;
  CXIdxLoc loc;
} CXIdxObjCProtocolRefInfo;

typedef struct {
  const CXIdxObjCProtocolRefInfo *const *protocols;
  unsigned numProtocols;
} CXIdxObjCProtocolRefListInfo;

typedef struct {
  const CXIdxObjCContainerDeclInfo *containerInfo;
  const CXIdxBaseClassInfo *superInfo;
  const CXIdxObjCProtocolRefListInfo *protocols;
} CXIdxObjCInterfaceDeclInfo;

typedef struct {
  const CXIdxObjCContainerDeclInfo *containerInfo;
  const CXIdxEntityInfo *objcClass;
  CXCursor classCursor;
  CXIdxLoc classLoc;
  const CXIdxObjCProtocolRefListInfo *protocols;
} CXIdxObjCCategoryDeclInfo;

typedef struct {
  const CXIdxDeclInfo *declInfo;
  const CXIdxEntityInfo *getter;
  const CXIdxEntityInfo *setter;
} CXIdxObjCPropertyDeclInfo;

typedef struct {
  const CXIdxDeclInfo *declInfo;
  const CXIdxBaseClassInfo *const *bases;
  unsigned numBases;
} CXIdxCXXClassDeclInfo;

/**
 * \brief Data for IndexerCallbacks#indexEntityReference.
 */
typedef enum {
  /**
   * \brief The entity is referenced directly in user's code.
   */
  CXIdxEntityRef_Direct = 1,
  /**
   * \brief An implicit reference, e.g. a reference of an Objective-C method
   * via the dot syntax.
   */
  CXIdxEntityRef_Implicit = 2
} CXIdxEntityRefKind;

/**
 * \brief Data for IndexerCallbacks#indexEntityReference.
 */
typedef struct {
  CXIdxEntityRefKind kind;
  /**
   * \brief Reference cursor.
   */
  CXCursor cursor;
  CXIdxLoc loc;
  /**
   * \brief The entity that gets referenced.
   */
  const CXIdxEntityInfo *referencedEntity;
  /**
   * \brief Immediate "parent" of the reference. For example:
   * 
   * \code
   * Foo *var;
   * \endcode
   * 
   * The parent of reference of type 'Foo' is the variable 'var'.
   * For references inside statement bodies of functions/methods,
   * the parentEntity will be the function/method.
   */
  const CXIdxEntityInfo *parentEntity;
  /**
   * \brief Lexical container context of the reference.
   */
  const CXIdxContainerInfo *container;
} CXIdxEntityRefInfo;

/**
 * \brief A group of callbacks used by #clang_indexSourceFile and
 * #clang_indexTranslationUnit.
 */
typedef struct {
  /**
   * \brief Called periodically to check whether indexing should be aborted.
   * Should return 0 to continue, and non-zero to abort.
   */
  int (*abortQuery)(CXClientData client_data, void *reserved);

  /**
   * \brief Called at the end of indexing; passes the complete diagnostic set.
   */
  void (*diagnostic)(CXClientData client_data,
                     CXDiagnosticSet, void *reserved);

  CXIdxClientFile (*enteredMainFile)(CXClientData client_data,
                                     CXFile mainFile, void *reserved);
  
  /**
   * \brief Called when a file gets \#included/\#imported.
   */
  CXIdxClientFile (*ppIncludedFile)(CXClientData client_data,
                                    const CXIdxIncludedFileInfo *);
  
  /**
   * \brief Called when a AST file (PCH or module) gets imported.
   * 
   * AST files will not get indexed (there will not be callbacks to index all
   * the entities in an AST file). The recommended action is that, if the AST
   * file is not already indexed, to initiate a new indexing job specific to
   * the AST file.
   */
  CXIdxClientASTFile (*importedASTFile)(CXClientData client_data,
                                        const CXIdxImportedASTFileInfo *);

  /**
   * \brief Called at the beginning of indexing a translation unit.
   */
  CXIdxClientContainer (*startedTranslationUnit)(CXClientData client_data,
                                                 void *reserved);

  void (*indexDeclaration)(CXClientData client_data,
                           const CXIdxDeclInfo *);

  /**
   * \brief Called to index a reference of an entity.
   */
  void (*indexEntityReference)(CXClientData client_data,
                               const CXIdxEntityRefInfo *);

} IndexerCallbacks;

CINDEX_LINKAGE int clang_index_isEntityObjCContainerKind(CXIdxEntityKind);
CINDEX_LINKAGE const CXIdxObjCContainerDeclInfo *
clang_index_getObjCContainerDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxObjCInterfaceDeclInfo *
clang_index_getObjCInterfaceDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE
const CXIdxObjCCategoryDeclInfo *
clang_index_getObjCCategoryDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxObjCProtocolRefListInfo *
clang_index_getObjCProtocolRefListInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxObjCPropertyDeclInfo *
clang_index_getObjCPropertyDeclInfo(const CXIdxDeclInfo *);

CINDEX_LINKAGE const CXIdxIBOutletCollectionAttrInfo *
clang_index_getIBOutletCollectionAttrInfo(const CXIdxAttrInfo *);

CINDEX_LINKAGE const CXIdxCXXClassDeclInfo *
clang_index_getCXXClassDeclInfo(const CXIdxDeclInfo *);

/**
 * \brief For retrieving a custom CXIdxClientContainer attached to a
 * container.
 */
CINDEX_LINKAGE CXIdxClientContainer
clang_index_getClientContainer(const CXIdxContainerInfo *);

/**
 * \brief For setting a custom CXIdxClientContainer attached to a
 * container.
 */
CINDEX_LINKAGE void
clang_index_setClientContainer(const CXIdxContainerInfo *,CXIdxClientContainer);

/**
 * \brief For retrieving a custom CXIdxClientEntity attached to an entity.
 */
CINDEX_LINKAGE CXIdxClientEntity
clang_index_getClientEntity(const CXIdxEntityInfo *);

/**
 * \brief For setting a custom CXIdxClientEntity attached to an entity.
 */
CINDEX_LINKAGE void
clang_index_setClientEntity(const CXIdxEntityInfo *, CXIdxClientEntity);

/**
 * \brief An indexing action/session, to be applied to one or multiple
 * translation units.
 */
typedef void *CXIndexAction;

/**
 * \brief An indexing action/session, to be applied to one or multiple
 * translation units.
 *
 * \param CIdx The index object with which the index action will be associated.
 */
CINDEX_LINKAGE CXIndexAction clang_IndexAction_create(CXIndex CIdx);

/**
 * \brief Destroy the given index action.
 *
 * The index action must not be destroyed until all of the translation units
 * created within that index action have been destroyed.
 */
CINDEX_LINKAGE void clang_IndexAction_dispose(CXIndexAction);

typedef enum {
  /**
   * \brief Used to indicate that no special indexing options are needed.
   */
  CXIndexOpt_None = 0x0,
  
  /**
   * \brief Used to indicate that IndexerCallbacks#indexEntityReference should
   * be invoked for only one reference of an entity per source file that does
   * not also include a declaration/definition of the entity.
   */
  CXIndexOpt_SuppressRedundantRefs = 0x1,

  /**
   * \brief Function-local symbols should be indexed. If this is not set
   * function-local symbols will be ignored.
   */
  CXIndexOpt_IndexFunctionLocalSymbols = 0x2,

  /**
   * \brief Implicit function/class template instantiations should be indexed.
   * If this is not set, implicit instantiations will be ignored.
   */
  CXIndexOpt_IndexImplicitTemplateInstantiations = 0x4,

  /**
   * \brief Suppress all compiler warnings when parsing for indexing.
   */
  CXIndexOpt_SuppressWarnings = 0x8,

  /**
   * \brief Skip a function/method body that was already parsed during an
   * indexing session associated with a \c CXIndexAction object.
   * Bodies in system headers are always skipped.
   */
  CXIndexOpt_SkipParsedBodiesInSession = 0x10

} CXIndexOptFlags;

/**
 * \brief Index the given source file and the translation unit corresponding
 * to that file via callbacks implemented through #IndexerCallbacks.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the invoked callbacks.
 *
 * \param index_callbacks Pointer to indexing callbacks that the client
 * implements.
 *
 * \param index_callbacks_size Size of #IndexerCallbacks structure that gets
 * passed in index_callbacks.
 *
 * \param index_options A bitmask of options that affects how indexing is
 * performed. This should be a bitwise OR of the CXIndexOpt_XXX flags.
 *
 * \param[out] out_TU pointer to store a \c CXTranslationUnit that can be
 * reused after indexing is finished. Set to \c NULL if you do not require it.
 *
 * \returns 0 on success or if there were errors from which the compiler could
 * recover.  If there is a failure from which there is no recovery, returns
 * a non-zero \c CXErrorCode.
 *
 * The rest of the parameters are the same as #clang_parseTranslationUnit.
 */
CINDEX_LINKAGE int clang_indexSourceFile(CXIndexAction,
                                         CXClientData client_data,
                                         IndexerCallbacks *index_callbacks,
                                         unsigned index_callbacks_size,
                                         unsigned index_options,
                                         const char *source_filename,
                                         const char * const *command_line_args,
                                         int num_command_line_args,
                                         struct CXUnsavedFile *unsaved_files,
                                         unsigned num_unsaved_files,
                                         CXTranslationUnit *out_TU,
                                         unsigned TU_options);

/**
 * \brief Same as clang_indexSourceFile but requires a full command line
 * for \c command_line_args including argv[0]. This is useful if the standard
 * library paths are relative to the binary.
 */
CINDEX_LINKAGE int clang_indexSourceFileFullArgv(
    CXIndexAction, CXClientData client_data, IndexerCallbacks *index_callbacks,
    unsigned index_callbacks_size, unsigned index_options,
    const char *source_filename, const char *const *command_line_args,
    int num_command_line_args, struct CXUnsavedFile *unsaved_files,
    unsigned num_unsaved_files, CXTranslationUnit *out_TU, unsigned TU_options);

/**
 * \brief Index the given translation unit via callbacks implemented through
 * #IndexerCallbacks.
 * 
 * The order of callback invocations is not guaranteed to be the same as
 * when indexing a source file. The high level order will be:
 * 
 *   -Preprocessor callbacks invocations
 *   -Declaration/reference callbacks invocations
 *   -Diagnostic callback invocations
 *
 * The parameters are the same as #clang_indexSourceFile.
 * 
 * \returns If there is a failure from which there is no recovery, returns
 * non-zero, otherwise returns 0.
 */
CINDEX_LINKAGE int clang_indexTranslationUnit(CXIndexAction,
                                              CXClientData client_data,
                                              IndexerCallbacks *index_callbacks,
                                              unsigned index_callbacks_size,
                                              unsigned index_options,
                                              CXTranslationUnit);

/**
 * \brief Retrieve the CXIdxFile, file, line, column, and offset represented by
 * the given CXIdxLoc.
 *
 * If the location refers into a macro expansion, retrieves the
 * location of the macro expansion and if it refers into a macro argument
 * retrieves the location of the argument.
 */
CINDEX_LINKAGE void clang_indexLoc_getFileLocation(CXIdxLoc loc,
                                                   CXIdxClientFile *indexFile,
                                                   CXFile *file,
                                                   unsigned *line,
                                                   unsigned *column,
                                                   unsigned *offset);

/**
 * \brief Retrieve the CXSourceLocation represented by the given CXIdxLoc.
 */
CINDEX_LINKAGE
CXSourceLocation clang_indexLoc_getCXSourceLocation(CXIdxLoc loc);

/**
 * \brief Visitor invoked for each field found by a traversal.
 *
 * This visitor function will be invoked for each field found by
 * \c clang_Type_visitFields. Its first argument is the cursor being
 * visited, its second argument is the client data provided to
 * \c clang_Type_visitFields.
 *
 * The visitor should return one of the \c CXVisitorResult values
 * to direct \c clang_Type_visitFields.
 */
typedef enum CXVisitorResult (*CXFieldVisitor)(CXCursor C,
                                               CXClientData client_data);

/**
 * \brief Visit the fields of a particular type.
 *
 * This function visits all the direct fields of the given cursor,
 * invoking the given \p visitor function with the cursors of each
 * visited field. The traversal may be ended prematurely, if
 * the visitor returns \c CXFieldVisit_Break.
 *
 * \param T the record type whose field may be visited.
 *
 * \param visitor the visitor function that will be invoked for each
 * field of \p T.
 *
 * \param client_data pointer data supplied by the client, which will
 * be passed to the visitor each time it is invoked.
 *
 * \returns a non-zero value if the traversal was terminated
 * prematurely by the visitor returning \c CXFieldVisit_Break.
 */
CINDEX_LINKAGE unsigned clang_Type_visitFields(CXType T,
                                               CXFieldVisitor visitor,
                                               CXClientData client_data);

/**
 * @}
 */

/**
 * @}
 */

#ifdef __cplusplus
}
#endif
#endif
