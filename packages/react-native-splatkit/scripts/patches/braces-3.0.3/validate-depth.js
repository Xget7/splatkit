'use strict';

// Local backport for GHSA-vfj7-8cjw-p6xm. Bound the recursive walkers,
// including callers supplying an AST directly. Parent/prev links are not children.
const MAX_DEPTH = 128;

const assertDepth = depth => {
  if (depth > MAX_DEPTH) {
    const error = new SyntaxError(`Brace AST nesting exceeds ${MAX_DEPTH} levels`);
    error.code = 'ERR_BRACES_MAX_DEPTH';
    throw error;
  }
};

const validateDepth = ast => {
  const stack = [{ node: ast, depth: 0, next: 0 }];
  const active = new Set();

  while (stack.length > 0) {
    const frame = stack[stack.length - 1];
    const node = frame.node;

    if (!node || !node.nodes) {
      stack.pop();
      continue;
    }

    if (frame.next === 0) {
      assertDepth(frame.depth);
      if (!Array.isArray(node.nodes) || active.has(node)) {
        const error = new SyntaxError('Invalid braces AST: expected an acyclic nodes array');
        error.code = 'ERR_BRACES_INVALID_AST';
        throw error;
      }
      active.add(node);
    }

    if (frame.next < node.nodes.length) {
      stack.push({ node: node.nodes[frame.next++], depth: frame.depth + 1, next: 0 });
    } else {
      active.delete(node);
      stack.pop();
    }
  }
};

module.exports = { MAX_DEPTH, assertDepth, validateDepth };
