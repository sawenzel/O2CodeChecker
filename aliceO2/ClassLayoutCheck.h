//===--- ClassLayoutCheck.h - clang-tidy-------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_ALICEO2_CLASSLAYOUT_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_ALICEO2_CLASSLAYOUT_H

#include "../ClangTidy.h"
#include <map>
#include <utility>

namespace clang {
namespace tidy {
namespace aliceO2 {

/// Checks if fields in structs/classes
/// are arranged from larger alignment to smaller alignment requirement
/// in order to prevent useless padding and taking more memory than required
class ClassLayoutCheck : public ClangTidyCheck {
private:
  // table filled/keeping the TypeInfo information per record encountered
  // analysed only in the destructor
  using NameAndTypeInfo = std::pair<std::string, TypeInfo>;
  using IndexToNameAndTypeInfoMap = std::map<int, NameAndTypeInfo>;

  std::map<std::string, IndexToNameAndTypeInfoMap> ClassToLayoutMap;

  std::map<std::string, SourceLocation> ClassNameToSourceLocMap;
  std::map<std::string, TypeInfo> ClassNameToTypeInfoMap; 

  void insertIntoMap(std::string recordname, int pos, std::string fieldname,
                TypeInfo fieldinfo);

  // actual logic to check layout and flag error
  bool checkLayout(IndexToNameAndTypeInfoMap const&) const;

  // print layout for given class
  void printLayout(std::string classname) const;

  // calculate best possible size for a record
  size_t calcOptimalSize(std::string classname) const;
  // calculate doable size (taking into accound alignment requirements)
  size_t calcDoableSize(std::string classname) const;
  // return size lost through padding
  size_t calcPadding() const;

  // return actual size given the current field information
  size_t calcSize(std::string classname) const;
  
public:
  ClassLayoutCheck(StringRef Name, ClangTidyContext *Context)
      : ClangTidyCheck(Name, Context)
  {}

  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

  virtual ~ClassLayoutCheck();
};

} // namespace aliceO2
} // namespace tidy
} // namespace clang

#endif // LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_ALICEO2_CLASSLAYOUT_H
