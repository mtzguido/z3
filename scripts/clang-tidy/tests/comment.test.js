// Copyright (c) 2026 Microsoft Corporation
// SPDX-License-Identifier: MIT
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {post, readReport, render} = require('../comment.js');
const base = 'a'.repeat(40), head = 'b'.repeat(40);
const report = () => ({schema_version: 1, base_sha: base, head_sha: head, tested_sha: 'c'.repeat(40),
    base_count: 5, head_count: 3, files: [{path: 'src/test.cpp', base: 4, head: 2}]});

function fixture(t, value = report()) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ast-order-comment-'));
    t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
    const file = path.join(dir, 'comparison.json');
    fs.writeFileSync(file, JSON.stringify(value));
    const run = {id: 100, run_attempt: 1, event: 'pull_request', conclusion: 'success',
        path: '.github/workflows/ast-order-warning-report.yml', repository: {full_name: 'owner/z3'},
        head_sha: head, head_branch: 'feature', head_repository: {id: 2, owner: {login: 'contributor'}},
        pull_requests: []}; // Actual fork events can omit the PR associations.
    const pr = {number: 42, state: 'open', head: {sha: head, repo: {id: 2}},
                base: {sha: base, repo: {full_name: 'owner/z3'}}};
    const comments = [], calls = [];
    const github = {rest: {
        pulls: {list: 'pulls.list', get: async () => ({data: pr})},
        issues: {listComments: 'issues.listComments',
                 createComment: async args => calls.push({op: 'create', ...args}),
                 updateComment: async args => calls.push({op: 'update', ...args})}},
        paginate: async (endpoint, args) => {
            if (endpoint === 'pulls.list') {
                assert.equal('contributor:feature', args.head);
                return [pr];
            }
            assert.equal(endpoint, 'issues.listComments');
            return comments;
        }};
    const options = {github, context: {repo: {owner: 'owner', repo: 'z3'},
        serverUrl: 'https://github.com', payload: {workflow_run: run}},
        core: {info() {}}, reportPath: file};
    return {file, run, pr, comments, calls, options};
}

function previous(body) {
    return {id: 9, user: {login: 'github-actions[bot]', type: 'Bot'}, body};
}

test('create a fork PR comment with counts and per-file changes', async t => {
    const f = fixture(t);
    await post(f.options);
    assert.equal(f.calls.length, 1);
    assert.equal(f.calls[0].op, 'create');
    assert.equal(f.calls[0].issue_number, 42);
    assert.match(f.calls[0].body, /Base: \*\*5\*\* → PR: \*\*3\*\*; change: \*\*-2\*\*/);
    assert.match(f.calls[0].body, /src\/test.cpp \| 4 \| 2 \| -2/);
    assert.match(f.calls[0].body, /PR merged into its base/);
});

test('update a single existing comment, leaving a newer run untouched', async t => {
    const f = fixture(t);
    f.run.pull_requests = [{number: 42}];
    f.comments.push(previous('<!-- z3-ast-argument-order -->\n<!-- z3-ast-order-run:99:1 -->'));
    await post(f.options);
    assert.equal(f.calls[0].op, 'update');
    assert.equal(f.calls[0].comment_id, 9);
    f.comments[0].body = f.calls[0].body;
    f.calls.length = 0;
    await post(f.options);
    assert.equal(f.calls.length, 0, 'identical delivery must not rewrite the comment');
    f.comments[0].body = '<!-- z3-ast-argument-order -->\n<!-- z3-ast-order-run:101:1 -->';
    await post(f.options);
    assert.equal(f.calls.length, 0);
    f.comments[0].body = '<!-- z3-ast-argument-order -->\n<!-- z3-ast-order-run:100:2 -->';
    await post(f.options);
    assert.equal(f.calls.length, 0, 'an older rerun attempt must not overwrite a newer one');
});

test('do not modify comments posted by a contributor', async t => {
    const f = fixture(t);
    f.comments.push({id: 8, user: {login: 'contributor', type: 'User'}, body: '<!-- z3-ast-argument-order -->'});
    await post(f.options);
    assert.equal(f.calls[0].op, 'create');
});

test('skip changed head, base, closed PRs, and different fork identities', async t => {
    for (const change of [f => f.pr.head.sha = 'c'.repeat(40), f => f.pr.base.sha = 'c'.repeat(40),
                          f => f.pr.state = 'closed', f => f.pr.head.repo.id = 3,
                          f => f.pr.base.repo.full_name = 'other/z3']) {
        const f = fixture(t); change(f); await post(f.options);
        assert.equal(f.calls.length, 0);
    }
});

test('reject an artifact for another commit before posting', async t => {
    const f = fixture(t, {...report(), head_sha: 'c'.repeat(40)});
    await assert.rejects(post(f.options), /does not match/);
    assert.equal(f.calls.length, 0);
});

test('failed scans post an unavailable result without reading partial counts', async t => {
    const f = fixture(t); f.run.conclusion = 'failure'; fs.unlinkSync(f.file);
    await post(f.options);
    assert.match(f.calls[0].body, /Comparison unavailable/);
    assert.doesNotMatch(f.calls[0].body, /change:/);
});

test('cancelled, scheduled, and unrelated workflows do not post', async t => {
    for (const change of [f => f.run.conclusion = 'cancelled', f => f.run.event = 'schedule',
                          f => f.run.path = '.github/workflows/other.yml',
                          f => f.run.repository.full_name = 'other/z3']) {
        const f = fixture(t); change(f); fs.unlinkSync(f.file); await post(f.options);
        assert.equal(f.calls.length, 0);
    }
});

test('malformed, inconsistent and oversized artifacts are rejected', t => {
    for (const patch of [{head_count: -1}, {head_count: '3'}, {base_sha: 'bad'},
                         {head_count: 4}, {files: [{path: '../bad', base: 5, head: 3}]},
                         {files: [{path: 'src/a.cpp', base: 4, head: 3}, {path: 'src/b.cpp', base: 4, head: 3}]},
                         {files: [{path: 'src/line\nbreak', base: 5, head: 3}]}]) {
        const f = fixture(t, {...report(), ...patch});
        assert.throws(() => readReport(f.file));
    }
    const f = fixture(t); fs.writeFileSync(f.file, ' '.repeat(1024 * 1024 + 1));
    assert.throws(() => readReport(f.file), /Invalid comparison artifact/);
    const target = f.file + '.target'; fs.renameSync(f.file, target); fs.symlinkSync(target, f.file);
    assert.throws(() => readReport(f.file), /Invalid comparison artifact/);
});

test('render increased, unchanged and scheduled counts; escape contributor paths', t => {
    const increased = {...report(), base_count: 3, head_count: 5,
        files: [{path: 'src/@everyone|<b>`x`[link].cpp', base: 2, head: 4}]};
    const f = fixture(t, increased);
    const text = render(readReport(f.file));
    assert.match(text, /change: \*\*\+2\*\*/);
    assert.doesNotMatch(text, /@everyone|<b>|`x`|\[link\]/);
    assert.match(text, /&#64;everyone&#124;&#60;b&#62;/);
    assert.match(render({...report(), base_count: 3, files: []}), /change: \*\*\+0\*\*/);
    assert.match(render({...report(), base_count: null, base_sha: null, files: []}), /Warnings: \*\*3\*\*/);
    const many = {...report(), base_count: 80, head_count: 40,
        files: Array.from({length: 40}, (_, i) => ({path: `src/${i}/${'@'.repeat(1000)}`, base: 2, head: 1}))};
    assert.ok(render(many).length < 65536, 'comment must fit the GitHub API size limit');
    assert.match(render(many), /Showing 30 of 40/);
});
