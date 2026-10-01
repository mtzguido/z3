// Copyright (c) 2026 Microsoft Corporation
// SPDX-License-Identifier: MIT
// Loaded from the default branch by the privileged workflow, never from a PR.
const fs = require('node:fs');
const MARKER = '<!-- z3-ast-argument-order -->';
const RUN_MARKER = /<!-- z3-ast-order-run:(\d+):(\d+) -->/;
const SHA = /^[0-9a-f]{40}$/;
const count = n => Number.isSafeInteger(n) && n >= 0 && n <= 10000000;
const signed = n => n >= 0 ? `+${n}` : `${n}`;
const escape = s => s.replace(/[&<>@`|\\[\]*_]/g, c => `&#${c.charCodeAt(0)};`);

function readReport(file) {
    const stat = fs.lstatSync(file);
    if (!stat.isFile() || stat.size > 1024 * 1024) throw new Error('Invalid comparison artifact');
    const r = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (r.schema_version !== 1 || !SHA.test(r.head_sha) || !SHA.test(r.tested_sha) || !count(r.head_count) ||
        !Array.isArray(r.files) || r.files.length > 10000 ||
        !(r.base_sha === null && r.base_count === null || SHA.test(r.base_sha) && count(r.base_count))) {
        throw new Error('Invalid comparison schema');
    }
    const paths = new Set();
    for (const f of r.files) {
        if (typeof f.path !== 'string' || !f.path || f.path.length > 1024 ||
            f.path.startsWith('/') || f.path.split('/').includes('..') || /[\x00-\x1f\x7f]/.test(f.path) ||
            paths.has(f.path) || !count(f.base) || !count(f.head) || f.base === f.head ||
            f.base > r.base_count || f.head > r.head_count) {
            throw new Error('Invalid per-file counts');
        }
        paths.add(f.path);
    }
    if (r.base_count === null && r.files.length) throw new Error('Unexpected baseline file counts');
    if (r.base_count !== null) {
        const base = r.files.reduce((n, f) => n + f.base, 0);
        const head = r.files.reduce((n, f) => n + f.head, 0);
        if (base > r.base_count || head > r.head_count || head - base !== r.head_count - r.base_count) {
            throw new Error('Inconsistent comparison totals');
        }
    }
    return r;
}

function render(r) {
    const lines = ['### AST argument-order warnings', ''];
    if (r.base_count === null) lines.push(`Warnings: **${r.head_count}** (\`${r.head_sha.slice(0, 12)}\`).`);
    else {
        lines.push(`Base: **${r.base_count}** → PR: **${r.head_count}**; change: **${signed(r.head_count - r.base_count)}**.`, '',
                   `Base \`${r.base_sha.slice(0, 12)}\`; head \`${r.head_sha.slice(0, 12)}\`.`);
        if (r.tested_sha !== r.head_sha) lines.push(`Tested the PR merged into its base (\`${r.tested_sha.slice(0, 12)}\`).`);
        if (r.files.length) {
            lines.push('', '| File | Base | PR | Change |', '| --- | ---: | ---: | ---: |');
            for (const f of r.files.slice(0, 30)) {
                const label = f.path.length > 240 ? f.path.slice(0, 237) + '...' : f.path;
                lines.push(`| ${escape(label)} | ${f.base} | ${f.head} | ${signed(f.head - f.base)} |`);
            }
            if (r.files.length > 30) lines.push('', `Showing 30 of ${r.files.length} files with changed counts.`);
        }
    }
    lines.push('', 'Counts are deduplicated diagnostic locations, not confirmed bugs. File counts do not match individual warnings; full diagnostics are in the run artifacts.',
               'Warnings are advisory. No automatic fixes are offered or applied.');
    return lines.join('\n');
}

async function post({github, context, core, reportPath}) {
    const run = context.payload.workflow_run;
    const {owner, repo} = context.repo;
    if (run.event !== 'pull_request' || !run.head_repository || run.repository.full_name !== `${owner}/${repo}` ||
        run.path.split('@')[0] !== '.github/workflows/ast-order-warning-report.yml' ||
        ['cancelled', 'skipped'].includes(run.conclusion)) return;

    // PR associations come from GitHub, never from the untrusted artifact.
    // Fork workflow_run payloads can have an empty pull_requests array.
    const candidates = run.pull_requests?.length ? run.pull_requests :
        await github.paginate(github.rest.pulls.list, {owner, repo, state: 'open', per_page: 100,
            head: `${run.head_repository.owner.login}:${run.head_branch}`});
    const report = run.conclusion === 'success' ? readReport(reportPath) : null;
    if (report && (report.head_sha !== run.head_sha || report.base_sha === null)) {
        throw new Error('Comparison does not match the triggering PR run');
    }
    for (const candidate of candidates) {
        const {data: pr} = await github.rest.pulls.get({owner, repo, pull_number: candidate.number});
        if (pr.state !== 'open' || pr.base.repo.full_name !== `${owner}/${repo}` ||
            pr.head.sha !== run.head_sha || pr.head.repo?.id !== run.head_repository.id ||
            report && pr.base.sha !== report.base_sha) {
            core.info(`Skipping stale or unrelated report for #${pr.number}`);
            continue;
        }
        const url = `${context.serverUrl}/${owner}/${repo}/actions/runs/${run.id}`;
        const text = report ? render(report) :
            '### AST argument-order warnings\n\nComparison unavailable: a build, scan, or report step failed. No warning delta is reported.';
        const body = `${MARKER}\n${text}\n\n[CI run and diagnostics](${url})\n<!-- z3-ast-order-run:${run.id}:${run.run_attempt} -->`;
        const comments = await github.paginate(github.rest.issues.listComments,
                                              {owner, repo, issue_number: pr.number, per_page: 100});
        const previous = comments.find(c => c.user?.login === 'github-actions[bot]' &&
                                           c.user.type === 'Bot' && c.body?.startsWith(MARKER));
        const oldRun = previous?.body.match(RUN_MARKER);
        if (oldRun && (Number(oldRun[1]) > run.id ||
                       Number(oldRun[1]) === run.id && Number(oldRun[2]) > run.run_attempt)) continue;
        if (previous) {
            if (previous.body !== body) await github.rest.issues.updateComment({owner, repo, comment_id: previous.id, body});
        }
        else await github.rest.issues.createComment({owner, repo, issue_number: pr.number, body});
    }
}

module.exports = {readReport, render, post};
if (require.main === module) console.log(render(readReport(process.argv[2])));
