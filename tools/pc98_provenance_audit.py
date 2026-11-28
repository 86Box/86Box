"""Reproduce a bounded source-provenance comparison; this is not proof of originality.

Run from the repository root. Reference archives must be supplied explicitly.
Only reads source and Git; writes a JSON evidence file at --output.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile

EXTENSIONS = {'.c', '.cc', '.cpp', '.h', '.hpp', '.ipp', '.inl'}
KEYWORDS = set(('auto break case char const continue default do double else enum '
                'extern float for goto if inline int long register restrict return '
                'short signed sizeof static struct switch typedef union unsigned '
                'void volatile while bool class public private protected virtual '
                'template typename namespace using new delete override final '
                'nullptr true false constexpr').split())
LEX = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'
                 r"'(?:\\.|[^'\\])*'|[A-Za-z_][A-Za-z_0-9]*|"
                 r'0[xX][0-9a-fA-F]+[uUlL]*|\d+(?:\.\d+)?[uUlLfF]*|'
                 r'>>=|<<=|->|::|\+\+|--|&&|\|\||==|!=|<=|>=|<<|>>|'
                 r'\+=|-=|\*=|/=|&=|\|=|\^=|[^\s]')


def git(*args: str) -> bytes:
    return subprocess.check_output(['git', *args])


def tokens(data: bytes, rename: bool = False) -> tuple[list[str], list[int]]:
    # latin-1 is byte-preserving; strings/numbers remain literal in both modes.
    text = data.decode('latin-1')
    out, lines = [], []
    line, previous = 1, 0
    for match in LEX.finditer(text):
        line += text.count('\n', previous, match.start())
        value = match.group()
        previous = match.start()
        if value.startswith(('//', '/*')):
            continue
        if rename and re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', value) and value not in KEYWORDS:
            value = '_ID_'
        out.append(value)
        lines.append(line)
    return out, lines


def tree(revision: str) -> dict[str, str]:
    result = {}
    for line in git('ls-tree', '-r', revision, 'src').decode().splitlines():
        info, path = line.split('\t', 1)
        if Path(path).suffix in EXTENSIONS:
            result[path] = info.split()[2]
    return result


def core(path: str) -> bool:
    return ('pc98' in Path(path).name.lower() or
            Path(path).name in {'vid_upd7220.c', 'vid_upd7220.h', 'fdd_d88.c', 'fdd_d88.h'})


def compare(targets: dict[str, bytes], refs: dict[str, bytes], rename: bool, width: int) -> dict:
    query = {name: tokens(data, rename) for name, data in targets.items()}
    index = defaultdict(list)
    for name, (seq, _) in query.items():
        for start in range(len(seq) - width + 1):
            index[tuple(seq[start:start + width])].append((name, start))
    best = {}
    executable_matches = []
    for ref_name, data in refs.items():
        seq, lines = tokens(data, rename)
        for start in range(len(seq) - width + 1):
            for name, pos in index.get(tuple(seq[start:start + width]), ()):
                target_seq, target_lines = query[name]
                # Only expand the beginning of each maximal matching run.
                if start and pos and seq[start - 1] == target_seq[pos - 1]:
                    continue
                count = width
                while (start + count < len(seq) and pos + count < len(target_seq)
                       and seq[start + count] == target_seq[pos + count]):
                    count += 1
                key = (name, ref_name)
                hit = {'target': name, 'target_line': target_lines[pos],
                       'reference': ref_name, 'reference_line': lines[start],
                       'tokens': count,
                       'sample': ' '.join(target_seq[pos:pos + min(count, 100)])}
                # Preserve every run with executable syntax, even when a longer
                # declaration run wins for the same file pair.
                if any(value in {'if', 'else', 'for', 'while', 'switch', 'case',
                                 'return', '=', '+=', '-=', '++', '--', '->', '.'}
                       for value in target_seq[pos:pos + count]):
                    executable_matches.append(hit)
                if count > best.get(key, {}).get('tokens', 0):
                    best[key] = hit
    return {'identifier_normalization': rename, 'minimum_tokens': width,
            'matching_file_pairs': len(best),
            'executable_syntax_runs': executable_matches,
            'matches': sorted(best.values(), key=lambda hit: (-hit['tokens'], hit['target'], hit['reference']))}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', action='append', required=True,
                        help='NAME=PATH to a .tar.gz source snapshot (not extracted or executed)')
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    refs, archives = {}, []
    for entry in args.reference:
        name, path = entry.split('=', 1)
        archive = Path(path)
        before = len(refs)
        with tarfile.open(archive) as tar:
            for member in tar:
                if member.isfile() and Path(member.name).suffix in EXTENSIONS:
                    refs[name + '/' + member.name.split('/', 1)[1]] = tar.extractfile(member).read()
        archives.append({'name': name, 'archive': path, 'source_files': len(refs) - before,
                         'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()})
    current = {p.as_posix(): p.read_bytes() for p in Path('src').rglob('*')
               if p.is_file() and p.suffix in EXTENSIONS}
    initial_tree, upstream_tree = tree('15036ee65'), tree('571ab627f')
    identical = sum(upstream_tree.get(p) == sha for p, sha in initial_tree.items())
    targets = {'working/' + p: data for p, data in current.items() if core(p)}
    for path in tree('70aa49b32'):
        if core(path):
            targets['70aa49b32/' + path] = git('show', '70aa49b32:' + path)
    # Entire source tree: exact token equality after whitespace/comments removed.
    ref_hashes = defaultdict(list)
    for path, data in refs.items():
        seq, _ = tokens(data)
        if len(seq) >= 40:
            ref_hashes[hashlib.sha256(json.dumps(seq).encode()).hexdigest()].append(path)
    exact = []
    for path, data in current.items():
        seq, _ = tokens(data)
        matches = ref_hashes.get(hashlib.sha256(json.dumps(seq).encode()).hexdigest(), [])
        if matches:
            exact.append({'target': path, 'reference': matches})
    result = {
        'head': git('rev-parse', 'HEAD').decode().strip(),
        'upstream_baseline': git('rev-parse', '571ab627f').decode().strip(),
        'initial_import': git('rev-parse', '15036ee65').decode().strip(),
        'working_status': git('status', '--short').decode(),
        'baseline': {'initial_source_files': len(initial_tree), 'identical_blob_ids': identical,
                     'added_or_changed': [p for p, sha in initial_tree.items() if upstream_tree.get(p) != sha],
                     'deleted': [p for p in upstream_tree if p not in initial_tree]},
        'reference_archives': archives,
        'working_source_files': len(current),
        'core_targets': [{'path': p, 'sha256': hashlib.sha256(data).hexdigest(),
                          'tokens': len(tokens(data)[0])} for p, data in targets.items()],
        'whole_file_token_matches': exact,
        'comparisons': [compare(targets, refs, False, 40), compare(targets, refs, True, 60)],
        'limits': ['Lexical heuristic, not an AST or semantic equivalence analysis.',
                   'Identifier mode erases names independently, preserving literal spelling.',
                   'Small, reordered, rewritten or historical borrowing may be missed.',
                   'Matching hardware constants and common language constructs are not proof of copying.',
                   'Not all NP2 versions are covered; third-party shared code can match legitimately.']}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('head', 'baseline', 'working_source_files', 'whole_file_token_matches')}))
    print('Core target files:', len(targets))
    for mode in result['comparisons']:
        print('Mode:', mode['identifier_normalization'], 'matching file pairs:', mode['matching_file_pairs'])
        for match in mode['matches'][:12]:
            print(match['tokens'], match['target'], match['target_line'], match['reference'], match['reference_line'])


if __name__ == '__main__':
    main()
