#include "PathCache.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

///////////////////////////////////////////////////////////////////////////
// Graph algorithms


// Code to find all simple paths between two basic blocks.
// Could generalize more to graphs if we wanted, but I don't right
// now.

PathID PathCache::addToPath(BasicBlock *b, PathID id) {
  PathCacheEntry key = std::make_pair(b, id);

  auto entry = cache_.find(key);
  if (entry != cache_.end()) {
    //errs() << "Found (" << b->getName() << ", " << id << ") as " << entry->second << "\n";
    return entry->second;
  }

  PathID newID = entries_.size();
  entries_.push_back(key);

  cache_.insert(std::make_pair(key, newID));
  //errs() << "Added (" << b->getName() << ", " << id << ") as " << newID << "\n";
  return newID;
}

Path PathCache::extractPath(PathID k) const {
  Path path;
  while (!isEmpty(k)) {
    path.push_back(getHead(k));
    k = getTail(k);
  }
  return path;
}

PathList PathCache::findAllSimplePaths(GreySet *grey,
                                       BasicBlock *src, BasicBlock *dst,
                                       bool allowSelfCycle) {
  PathList paths;
  if (src == dst && !allowSelfCycle) {
    PathID path = addToPath(dst, kEmptyPath);
    paths.push_back(path);
    return paths;
  }
  if (grey->count(src)) return paths;

  grey->insert(src);

  // We consider all exits from a function to loop back to the start
  // edge, so we need to handle that unfortunate case.
  if (isa<ReturnInst>(src->getTerminator())) {
    BasicBlock *entry = &src->getParent()->getEntryBlock();
    paths = findAllSimplePaths(grey, entry, dst);
  }
  // Go search all the normal successors
  for (auto i = succ_begin(src), e = succ_end(src); i != e; i++) {
    PathList subpaths = findAllSimplePaths(grey, *i, dst);
    std::move(subpaths.begin(), subpaths.end(), std::back_inserter(paths));
  }

  // Add our step to all of the vectors
  for (auto & path : paths) {
    path = addToPath(src, path);
  }

  // Remove it from the set of things we've seen. We might come
  // through here again.
  // We can't really do any sort of memoization, since in a cyclic
  // graph the possible simple paths depend not just on what node we
  // are on, but our previous path (to avoid looping).
  grey->erase(src);

  return paths;
}

PathList PathCache::findAllSimplePaths(BasicBlock *src, BasicBlock *dst,
                                       bool allowSelfCycle) {
  GreySet grey;
  return findAllSimplePaths(&grey, src, dst, allowSelfCycle);
}

////
void PathCache::dumpPaths(const PathList &paths) const {
  for (auto & pathid : paths) {
    Path path = extractPath(pathid);
    for (auto block : path) {
      errs() << block->getName() << " -> ";
    }
    errs() << "\n";
  }
}