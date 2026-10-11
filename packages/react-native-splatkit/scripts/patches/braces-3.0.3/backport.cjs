'use strict';

// Exact published sources, not a semver range. Unexpected upstream changes
// require reviewing the backport rather than silently applying it again.
const originalSha256 = {
  'index.js': '332ea07c7b006361aad12aa994ca75dc1db8e8382b884909e2f38f10b85c88a4',
  'lib/compile.js': 'dc98f22eee3d511785d92a00758d5f0d48efed5f5813bdecc2de430c529b5c9f',
  'lib/constants.js': 'c18ac5adb57308f1ce42a28552da3a31f5d83709743ebd9a636336813a744d4b',
  'lib/expand.js': '41ccc196ebfa7b7781a634e721eb744e4e7bcb54cba427a7e3d6806a1b9e58f7',
  'lib/parse.js': 'e572166565f15fa6ad9865ae49d678218e32aabfd1b3720f6d0d43d39800d310',
  'lib/stringify.js': '379f22d77bfa1478341ccd49c5e4267464aabcbba03558bab332aac23fc6f23a',
  'lib/utils.js': 'b5a7596aa67730412b3c029ef09e84e6b67b8e445cffd35d1d295549c89066c7',
};

const replacements = {
  'lib/compile.js': [
    ["const utils = require('./utils');", "const utils = require('./utils');\nconst { validateDepth } = require('./validate-depth');"],
    ['const compile = (ast, options = {}) => {', 'const compile = (ast, options = {}) => {\n  validateDepth(ast);'],
  ],
  'lib/expand.js': [
    ["const utils = require('./utils');", "const utils = require('./utils');\nconst { validateDepth } = require('./validate-depth');"],
    ['const expand = (ast, options = {}) => {', 'const expand = (ast, options = {}) => {\n  validateDepth(ast);'],
  ],
  'lib/stringify.js': [
    ["const utils = require('./utils');", "const utils = require('./utils');\nconst { validateDepth } = require('./validate-depth');"],
    ['module.exports = (ast, options = {}) => {', 'module.exports = (ast, options = {}) => {\n  validateDepth(ast);'],
  ],
  'lib/parse.js': [
    ["const stringify = require('./stringify');", "const stringify = require('./stringify');\nconst { assertDepth, validateDepth } = require('./validate-depth');"],
    ["    if (value === CHAR_LEFT_PARENTHESES) {\n      block = push", "    if (value === CHAR_LEFT_PARENTHESES) {\n      assertDepth(stack.length);\n      block = push"],
    ["    if (value === CHAR_LEFT_CURLY_BRACE) {\n      depth++;", "    if (value === CHAR_LEFT_CURLY_BRACE) {\n      assertDepth(stack.length);\n      depth++;"],
    ["  push({ type: 'eos' });\n  return ast;", "  push({ type: 'eos' });\n  validateDepth(ast);\n  return ast;"],
  ],
};

module.exports = { version: '3.0.3', originalSha256, replacements };
