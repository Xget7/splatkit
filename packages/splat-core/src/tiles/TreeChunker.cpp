#include "splat/tiles/TreeChunker.h"

#include <algorithm>
#include <string>
#include <utility>

namespace splat {
namespace {

// Below two groups a chunk's child would stand for all of it, and the recursion would not shrink.
constexpr uint32_t kMinFanOut = 2;

class Chunker {
 public:
  Chunker(const std::vector<LodNode>& layout, const TreeChunkOptions& options)
      : layout_(layout), chunkSplats_(options.chunkSplats), fanOut_(options.fanOut) {}

  std::vector<TreeChunk> run() {
    if (!layout_.empty()) grow({0});
    return std::move(chunks_);
  }

 private:
  bool isLeaf(uint32_t node) const { return layout_[node].childCount == 0; }

  // Index of the interior node the frontier gives up next: the coarsest whose children fit.
  // frontier.size() when none does.
  std::size_t nextToExpand(const std::vector<uint32_t>& frontier) const {
    std::size_t best = frontier.size();
    for (std::size_t i = 0; i < frontier.size(); ++i) {
      const LodNode& node = layout_[frontier[i]];
      if (isLeaf(frontier[i]) || frontier.size() - 1 + node.childCount > chunkSplats_) continue;
      if (best == frontier.size() || node.size > layout_[frontier[best]].size) best = i;
    }
    return best;
  }

  // Replaces the node at `index` with its children, in place.
  void expand(std::vector<uint32_t>& frontier, std::size_t index) const {
    const LodNode& node = layout_[frontier[index]];
    std::vector<uint32_t> children(node.childCount);
    for (uint32_t k = 0; k < node.childCount; ++k) children[k] = node.childStart + k;
    frontier[index] = children.front();
    frontier.insert(frontier.begin() + static_cast<std::ptrdiff_t>(index) + 1, children.begin() + 1,
                    children.end());
  }

  // Lists the chunk standing for `roots`, after the chunks below it, and returns its index. The
  // frontier is cut into consecutive groups, one child chunk each, so the children's roots in
  // order are exactly this chunk's nodes. The recursion is as deep as the chunk hierarchy, which
  // is logarithmic in the tree.
  uint32_t grow(std::vector<uint32_t> roots) {
    std::vector<uint32_t> frontier = roots;
    for (std::size_t next = nextToExpand(frontier); next != frontier.size();
         next = nextToExpand(frontier))
      expand(frontier, next);

    TreeChunk chunk;
    bool hasInterior = false;
    for (const uint32_t node : frontier) {
      if (isLeaf(node)) continue;
      hasInterior = true;
      chunk.error = std::max(chunk.error, layout_[node].size);
    }
    if (hasInterior) {
      const std::size_t groups = std::min<std::size_t>(fanOut_, frontier.size());
      const std::size_t base = frontier.size() / groups;
      const std::size_t longer =
          frontier.size() % groups;  // the first `longer` groups take one more
      std::size_t begin = 0;
      for (std::size_t g = 0; g < groups; ++g) {
        const std::size_t end = begin + base + (g < longer ? 1 : 0);
        chunk.children.push_back(grow({frontier.begin() + static_cast<std::ptrdiff_t>(begin),
                                       frontier.begin() + static_cast<std::ptrdiff_t>(end)}));
        chunk.level = std::max(chunk.level, chunks_[chunk.children.back()].level + 1);
        begin = end;
      }
    }
    chunk.roots = std::move(roots);
    chunk.nodes = std::move(frontier);
    chunks_.push_back(std::move(chunk));
    return static_cast<uint32_t>(chunks_.size() - 1);
  }

  const std::vector<LodNode>& layout_;
  uint32_t chunkSplats_;
  uint32_t fanOut_;
  std::vector<TreeChunk> chunks_;
};

}  // namespace

Result<std::vector<TreeChunk>> chunkTree(const LodTree& tree, const TreeChunkOptions& options) {
  const uint32_t chunkSplats = options.chunkSplats;
  if (chunkSplats == 0) return Error{ErrorCode::corrupt, "chunkSplats must be at least 1"};
  if (options.fanOut < kMinFanOut)
    return Error{ErrorCode::corrupt, "fanOut must be at least " + std::to_string(kMinFanOut)};
  for (std::size_t n = 0; n < tree.layout.size(); ++n) {
    if (tree.layout[n].childCount > chunkSplats)
      return Error{ErrorCode::corrupt, "node " + std::to_string(n) + " has " +
                                           std::to_string(tree.layout[n].childCount) +
                                           " children, more than chunkSplats " +
                                           std::to_string(chunkSplats)};
  }
  return Chunker(tree.layout, options).run();
}

}  // namespace splat
