#include "splat/tiles/TreeChunker.h"

#include <algorithm>
#include <string>
#include <utility>

namespace splat {
namespace {

class Chunker {
 public:
  Chunker(const std::vector<LodNode>& layout, uint32_t chunkSplats)
      : layout_(layout), chunkSplats_(chunkSplats) {}

  std::vector<TreeChunk> run() {
    if (!layout_.empty()) list(grow(0));
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

  // Grows the chunk from `start`, listing the chunks below it first. The chunk itself is not listed
  // yet: its parent may pack it with its siblings.
  TreeChunk grow(uint32_t start) {
    std::vector<uint32_t> frontier{start};
    for (std::size_t next = nextToExpand(frontier); next != frontier.size();
         next = nextToExpand(frontier))
      expand(frontier, next);

    TreeChunk chunk;
    chunk.roots = {start};
    std::vector<TreeChunk> parts;
    std::vector<uint32_t> leaves;
    for (const uint32_t node : frontier) {
      if (isLeaf(node)) {
        leaves.push_back(node);
        continue;
      }
      parts.push_back(grow(node));
      chunk.error = std::max(chunk.error, layout_[node].size);
    }
    if (!parts.empty() && !leaves.empty()) {
      TreeChunk remainder;
      remainder.nodes = std::move(leaves);
      parts.push_back(std::move(remainder));
    }
    chunk.children = pack(std::move(parts));
    for (const uint32_t child : chunk.children)
      chunk.level = std::max(chunk.level, chunks_[child].level + 1);
    chunk.nodes = std::move(frontier);
    return chunk;
  }

  // Lists `parts` as chunks, merging neighbours while they fit the budget so that the chunk count
  // follows the budget and not the number of small subtrees. Parts with children merge only with
  // parts with children, and childless parts only with childless ones: a merged chunk lists the
  // children of all its parts, so a childless part inside it would have its leaves in no level 0
  // chunk. Each kind keeps the frontier order. Returns the indices, in order.
  std::vector<uint32_t> pack(std::vector<TreeChunk> parts) {
    const auto split = std::stable_partition(
        parts.begin(), parts.end(), [](const TreeChunk& part) { return !part.children.empty(); });
    std::vector<uint32_t> listed = packRun(parts.begin(), split);
    const std::vector<uint32_t> leafChunks = packRun(split, parts.end());
    listed.insert(listed.end(), leafChunks.begin(), leafChunks.end());
    return listed;
  }

  using PartIterator = std::vector<TreeChunk>::iterator;

  // Merges the parts in [first, last), in order, closing a chunk when the next part would not fit.
  std::vector<uint32_t> packRun(PartIterator first, PartIterator last) {
    std::vector<uint32_t> listed;
    TreeChunk merged;
    for (auto part = first; part != last; ++part) {
      if (!merged.nodes.empty() && merged.nodes.size() + part->nodes.size() > chunkSplats_) {
        listed.push_back(list(std::move(merged)));
        merged = TreeChunk{};
      }
      append(merged, std::move(*part));
    }
    if (!merged.nodes.empty()) listed.push_back(list(std::move(merged)));
    return listed;
  }

  static void append(TreeChunk& into, TreeChunk&& part) {
    into.roots.insert(into.roots.end(), part.roots.begin(), part.roots.end());
    into.nodes.insert(into.nodes.end(), part.nodes.begin(), part.nodes.end());
    into.children.insert(into.children.end(), part.children.begin(), part.children.end());
    into.level = std::max(into.level, part.level);
    into.error = std::max(into.error, part.error);
  }

  uint32_t list(TreeChunk&& chunk) {
    chunks_.push_back(std::move(chunk));
    return static_cast<uint32_t>(chunks_.size() - 1);
  }

  const std::vector<LodNode>& layout_;
  uint32_t chunkSplats_;
  std::vector<TreeChunk> chunks_;
};

}  // namespace

Result<std::vector<TreeChunk>> chunkTree(const LodTree& tree, uint32_t chunkSplats) {
  if (chunkSplats == 0) return Error{ErrorCode::corrupt, "chunkSplats must be at least 1"};
  for (std::size_t n = 0; n < tree.layout.size(); ++n) {
    if (tree.layout[n].childCount > chunkSplats)
      return Error{ErrorCode::corrupt, "node " + std::to_string(n) + " has " +
                                           std::to_string(tree.layout[n].childCount) +
                                           " children, more than chunkSplats " +
                                           std::to_string(chunkSplats)};
  }
  return Chunker(tree.layout, chunkSplats).run();
}

}  // namespace splat
