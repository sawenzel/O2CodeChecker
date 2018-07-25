//===--- ClassLayoutCheck.cpp - clang-tidy---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClassLayoutCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <iostream>
#include <string>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace aliceO2 {

void ClassLayoutCheck::registerMatchers(MatchFinder *Finder) {
  // for the moment we match on all field declarations
  auto field = fieldDecl();
  Finder->addMatcher(field.bind("field_decl1"), this);
}

void ClassLayoutCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<FieldDecl>("field_decl1");
  if (MatchedDecl) {
    // check that we are inside the AliceO2 namespace to exclude system stuff
    // FIXME: needs to be configurable
    // NOTE: AliceO2:: is the old one. We agreed to use o2::
    if ((MatchedDecl->getQualifiedNameAsString().find("AliceO2::") != 0) &&
        (MatchedDecl->getQualifiedNameAsString().find("o2::") != 0))
      return;

    int index= MatchedDecl->getFieldIndex();
    auto outerrecord = MatchedDecl->getParent();
    auto outerrecordname = outerrecord->getQualifiedNameAsString();
    auto fieldname = MatchedDecl->getQualifiedNameAsString();
    // std::cout << "saw " << fieldname << " at " << index << "\n";

    // get typeinfo via AST context
    auto tinfo = Result.Context->getTypeInfo(MatchedDecl->getType());

    insertIntoMap(outerrecordname,
    		      index,
				  MatchedDecl->getNameAsString(),
				  tinfo);

    auto loc = outerrecord->getSourceRange().getBegin();
    if (ClassNameToSourceLocMap.find(outerrecordname) == ClassNameToSourceLocMap.end()) {
      ClassNameToSourceLocMap.insert(std::pair<std::string, SourceLocation>(outerrecordname, loc));
    }

    // typeinfo for the outer record are also kept
    if (ClassNameToSourceLocMap.find(outerrecordname) == ClassNameToSourceLocMap.end()) {
      ClassNameToSourceLocMap.insert(std::pair<std::string, SourceLocation>(outerrecordname, loc));
    }

    auto outertinfo = Result.Context->getTypeInfo(outerrecord->getTypeForDecl());
    if (ClassNameToTypeInfoMap.find(outerrecordname) == ClassNameToTypeInfoMap.end()) {
      ClassNameToTypeInfoMap.insert(std::pair<std::string, TypeInfo>(outerrecordname, outertinfo));
    }
  }
}

void ClassLayoutCheck::insertIntoMap(std::string recordname, int pos,
                                     std::string fieldname,
                                     TypeInfo fieldinfo) {
  auto rightmap = ClassToLayoutMap.find(recordname);
  if (rightmap == ClassToLayoutMap.end()) {
    ClassToLayoutMap.insert(std::pair<std::string, IndexToNameAndTypeInfoMap>(
        recordname, IndexToNameAndTypeInfoMap()));
  }
  rightmap = ClassToLayoutMap.find(recordname);
  auto& actualmap = rightmap->second;

  if( actualmap.find(pos) == actualmap.end()) {
    actualmap.insert(std::pair<int, NameAndTypeInfo>(pos, NameAndTypeInfo(fieldname, fieldinfo)));
  }
}

bool ClassLayoutCheck::checkLayout(
    IndexToNameAndTypeInfoMap const &layoutmap) const {
  // for the moment we simply fail if we encounter any wrong order
  int lastalign = -1; // -1 indicates special startvalue
  for (auto &p : layoutmap) {
    auto thisalign = p.second.second.Align;
    if (lastalign != -1 && thisalign > lastalign) {
      return false;
    }
    lastalign = thisalign;
  }
  return true;
}

void ClassLayoutCheck::printLayout(std::string classname) const {
  std::cout << "Field order for record " << classname << "\n";
  auto layoutmapiter = ClassToLayoutMap.find(classname);
  for (auto &indexfieldinfopair : layoutmapiter->second) {
    std::cout << " " << indexfieldinfopair.first << " "
              << indexfieldinfopair.second.first << "\t"
              << indexfieldinfopair.second.second.Width << "\t"
              << indexfieldinfopair.second.second.Align << "\t"
              << "\n";
  }
}


size_t ClassLayoutCheck::calcOptimalSize(std::string classname) const {
  auto layoutmapiter = ClassToLayoutMap.find(classname);
  size_t accum{0};
  for (auto &indexfieldinfopair : layoutmapiter->second) {
    accum += indexfieldinfopair.second.second.Width;
  }
  return accum;
}

// calculate doable size (taking into account alignment requirements)
size_t ClassLayoutCheck::calcDoableSize(std::string classname) const {
  // first step: iterate over current layout and fill TypeInfo
  // into a sorted vector of TypeInfos (from largest to smallest alignment)
  std::vector<TypeInfo> sortedtypeinfos;

  auto layoutmapiter = ClassToLayoutMap.find(classname);
  for (auto &indexfieldinfopair : layoutmapiter->second) {
    auto tinfo = indexfieldinfopair.second.second;
    sortedtypeinfos.push_back( tinfo );
  }

  // sort vector
  std::sort(sortedtypeinfos.begin(), sortedtypeinfos.end(), [](TypeInfo a, TypeInfo b){ return a.Align > b.Align; });

  // now we should ideally forward to calcSize
  size_t accum{0};
  size_t classalignment = -1;
  for (auto &tinfo  : sortedtypeinfos) {
    std::cout << "## " << tinfo.Width << " " << tinfo.Align << "\n";
    if (classalignment == -1) {
      classalignment = tinfo.Align;
    }
    // calc padding
    auto offalignment = accum % tinfo.Align;
    auto padding = (offalignment!=0)? tinfo.Align - offalignment : 0;
    accum += padding;
    accum += tinfo.Width;
  }
  // plus we need to add padding to next object of the same kind
  auto offclassalignment = accum % classalignment;
  auto finalpadding = (offclassalignment!=0)? classalignment - offclassalignment : 0;
  accum += finalpadding;
  return accum;
}
 
// calculate the actual size of this class
// (should correspond the one that we got in the TypeInfo)
// not taking into account virtual table(s)
size_t ClassLayoutCheck::calcSize(std::string classname) const {
  auto layoutmapiter = ClassToLayoutMap.find(classname);
  size_t accum{0};
  size_t classalignment = -1;
  for (auto &indexfieldinfopair : layoutmapiter->second) {
    auto tinfo = indexfieldinfopair.second.second;
    if (classalignment == -1) {
      classalignment = tinfo.Align;
    }
    // calc padding
    auto offalignment = accum % tinfo.Align;
    auto padding = (offalignment!=0)? tinfo.Align - offalignment : 0;
    accum += padding;
    accum += tinfo.Width;
  }
  // plus we need to add padding to next object of the same kind
  // here things get complicated since we migh have inheritance etc
  auto offclassalignment = accum % classalignment;
  auto finalpadding = (offclassalignment!=0)? classalignment - offclassalignment : 0;
  accum += finalpadding;
  return accum;
}
  
ClassLayoutCheck::~ClassLayoutCheck() {
  for (auto &keyval : ClassToLayoutMap) {
    auto passed = checkLayout(keyval.second);
    if (!passed) {
      // the next line gets shown delayed
      diag(ClassNameToSourceLocMap.find(keyval.first)->second,
           "class does not have good structure", DiagnosticIDs::Error);
      printLayout(keyval.first);

      std::cout << "Optimal size would be " << calcOptimalSize(keyval.first)
		<< " actual current size is " << ClassNameToTypeInfoMap.find(keyval.first)->second.Width
        	<< " actual current align is " << ClassNameToTypeInfoMap.find(keyval.first)->second.Align
		<< " actual calculated size is " << calcSize(keyval.first) << "\n"
      		<< " calculated doable size is " << calcDoableSize(keyval.first) << "\n";
    }
  }
}

} // namespace aliceO2
} // namespace tidy
} // namespace clang
