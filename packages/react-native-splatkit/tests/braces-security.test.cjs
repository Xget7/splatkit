const assert = require('node:assert/strict');
const { createRequire } = require('node:module');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

// Resolve the installation being checked, including the private example's Metro tree.
const requireTooling = createRequire(path.join(process.cwd(), 'package.json'));
const braces = requireTooling('braces');
const micromatch = requireTooling('micromatch');
const depth = 4000;
const isDepthError = error => error instanceof SyntaxError && error.code === 'ERR_BRACES_MAX_DEPTH';

test('all installed braces copies have the reviewed patch', () => {
  const result = spawnSync(process.execPath, [
    path.join(__dirname, '../scripts/patch-braces.cjs'),
    '--node-modules', path.join(process.cwd(), 'node_modules'), '--check',
  ], { encoding: 'utf8', timeout: 15000 });
  assert.equal(result.status, 0, result.stderr || result.stdout || String(result.error));
});

test('ordinary Metro glob patterns retain their expansion and compilation', () => {
  const cases = [
    ['src/*.{js,jsx,ts,tsx}', ['src/*.js', 'src/*.jsx', 'src/*.ts', 'src/*.tsx'], 'src/*.(js|jsx|ts|tsx)'],
    ['{ios,android}/file{1..3}.js', ['ios/file1.js', 'ios/file2.js', 'ios/file3.js',
      'android/file1.js', 'android/file2.js', 'android/file3.js'], '(ios|android)/file([1-3]).js'],
    ['a/{b,{c,d}}/e', ['a/b/e', 'a/c/e', 'a/d/e'], 'a/(b|(c|d))/e'],
    ['{01..03}', ['01', '02', '03'], '(0[1-3])'],
    ['\\{literal\\}', ['{literal}'], '{literal}'],
    ['${HOME}', ['${HOME}'], '${HOME}'],
  ];
  for (const [pattern, expanded, compiled] of cases) {
    assert.deepEqual(braces.expand(pattern), expanded, pattern);
    assert.equal(braces.compile(pattern), compiled, pattern);
    assert.deepEqual(micromatch.braceExpand(pattern), expanded, pattern);
  }
  assert.deepEqual(micromatch(['src/a.ts', 'src/b.js', 'src/c.css'], 'src/*.{js,ts}'),
    ['src/a.ts', 'src/b.js']);
});

test('long literal patterns are accepted without interpreting escaped or quoted nesting', () => {
  const literalDepth = 2000;
  const literal = '{'.repeat(literalDepth) + 'x' + '}'.repeat(literalDepth);
  const patterns = [
    ['\\{'.repeat(literalDepth) + 'x' + '\\}'.repeat(literalDepth), literal],
    ['[' + literal + ']', '[' + literal + ']'],
    ['"' + literal + '"', literal],
    ['segment/'.repeat(1000), 'segment/'.repeat(1000)],
  ];
  for (const [pattern, expected] of patterns) {
    assert.equal(braces.compile(pattern), expected);
    assert.deepEqual(braces.expand(pattern), [expected]);
    assert.equal(braces.stringify(pattern), expected);
  }
});

test('deeply nested strings are rejected before recursive walkers exhaust the stack', () => {
  const patterns = [
    ['braces', '{'.repeat(depth) + 'x' + '}'.repeat(depth), true],
    ['parentheses', '('.repeat(depth) + 'x' + ')'.repeat(depth), false],
    ['mixed', '{('.repeat(depth / 2) + 'x' + ')}'.repeat(depth / 2), true],
    ['unclosed', '{'.repeat(depth) + 'x', false],
    ['dollar', '${' + '{'.repeat(depth) + 'x' + '}'.repeat(depth) + '}', true],
  ];
  const operations = [
    ['compile', pattern => braces.compile(pattern)],
    ['expand', pattern => braces.expand(pattern)],
    ['parse', pattern => braces.parse(pattern)],
    ['stringify', pattern => braces.stringify(pattern)],
    ['create', pattern => braces(pattern)],
    ['create expand', pattern => braces(pattern, { expand: true })],
  ];
  for (const [kind, pattern, invokesBraces] of patterns) {
    for (const [name, run] of operations) {
      assert.throws(() => run(pattern), isDepthError, `${kind}: ${name}`);
    }
    if (invokesBraces) {
      assert.throws(() => micromatch.braceExpand(pattern), isDepthError, `${kind}: micromatch`);
    } else {
      // Micromatch returns literal patterns directly when they contain no balanced braces.
      assert.deepEqual(micromatch.braceExpand(pattern), [pattern]);
    }
  }
});

function nestedAst(count) {
  const root = { type: 'root', nodes: [] };
  let parent = root;
  for (let i = 0; i < count; i++) {
    const child = { type: 'paren', nodes: [], parent };
    parent.nodes.push(child);
    parent = child;
  }
  parent.nodes.push({ type: 'text', value: 'x', parent });
  return root;
}

test('direct AST inputs cannot bypass the nesting limit', () => {
  for (const operation of ['compile', 'expand', 'stringify']) {
    assert.throws(() => braces[operation](nestedAst(depth)), isDepthError, operation);
    assert.throws(() => braces[operation](nestedAst(depth), { maxLength: 1 }),
      isDepthError, `${operation}: string length limits do not guard an AST`);
  }
});

test('shallow AST inputs remain supported', () => {
  for (const operation of ['compile', 'expand', 'stringify']) {
    const result = braces[operation](nestedAst(32));
    assert.deepEqual(result, operation === 'expand' ? ['x'] : 'x');
  }
});

test('128 nesting levels remain supported and the next level is rejected', () => {
  const allowed = '('.repeat(128) + 'x' + ')'.repeat(128);
  const rejected = '(' + allowed + ')';
  for (const operation of ['compile', 'expand', 'stringify']) {
    assert.deepEqual(braces[operation](allowed), operation === 'expand' ? [allowed] : allowed);
    assert.throws(() => braces[operation](rejected), isDepthError);
  }
});

test('cyclic ASTs are rejected before reaching a recursive walker', () => {
  for (const operation of ['compile', 'expand', 'stringify']) {
    const ast = { type: 'root', nodes: [] };
    ast.nodes.push(ast);
    assert.throws(() => braces[operation](ast), error =>
      error instanceof SyntaxError && error.code === 'ERR_BRACES_INVALID_AST');
  }
});

test('range expansion limits remain enforced', () => {
  assert.throws(() => braces.expand('{1..1001}'), /exceeds range limit/);
  assert.deepEqual(braces.expand('{1..3}', { rangeLimit: 3 }), ['1', '2', '3']);
});
