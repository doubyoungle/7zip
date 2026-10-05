#!/usr/bin/env python3
"""patch-lang.py -- merge Smart Extract strings into a 7-Zip language file.

The 7-Zip language file format (Common/Lang.cpp, CLang::OpenFromString):
  - first line: ;!@Lang2@!UTF-8! (signature)
  - lines starting with ';' are comments; every comment or empty line
    increments the running string id by one
  - a line that is a pure number sets the running id (must be >= current)
  - any other line is the translation for the current id, then id += 1

So translations are (id line, value line) pairs, and value lines consume
consecutive ids. This script inserts our new strings at positions where
the running id matches, using explicit id jumps when needed. It is
idempotent: strings that are already present are left unchanged.

Usage: python patch-lang.py <path-to-lang-file> [zh-cn|zh-tw|...]
"""

import sys

# target id -> (anchor id after whose value the line is inserted, text)
# 560  : 7zFM File menu item (after 559 = "Alternate streams")
# 2331 : Explorer context menu item + Tools>Options row (after 2330)
# 7220 : (reserved) FM string table entry (after 7206 = "Info")
# 7221 : fallback message for unlistable (encrypted) archives
INSERTS = {
    560: '智能解压缩(&S)',
    2331: '智能解压缩',
    7220: '智能解压缩',
    7221: '无法列出压缩包内容 "{0}"（可能已加密）。\\n智能解压缩将改用"解压到文件夹"模式。',
}


def is_number(line):
    s = line.strip()
    return s.isdigit() and len(s) < 10


def patch(lines):
    """Returns (new_lines, report) or (None, reason) when nothing to do."""
    # simulate the parser to compute the running id at the start of each line
    id_at_line = [None] * len(lines)
    cur = -1024
    for i, raw in enumerate(lines):
        id_at_line[i] = cur
        line = raw.rstrip('\r\n')
        if i == 0 and line.startswith(';!@Lang2@!UTF-8!'):
            continue
        stripped = line.strip()
        if not stripped or stripped.startswith(';'):
            if cur >= 0:
                cur += 1
            continue
        if is_number(line):
            val = int(stripped)
            if val < cur:
                raise ValueError(f'id goes backwards at line {i + 1}: {val} < {cur}')
            cur = val
        else:
            if cur >= 0:
                cur += 1
    # after the loop, cur is the running id past the last value line;
    # appending at the end targets ids > cur only.

    inserts = []  # (line_index, [lines to insert before it])
    report = []
    for target, text in sorted(INSERTS.items()):
        if any(line.rstrip('\r\n') == text for line in lines):
            report.append(f'id {target}: text already present, skipped')
            continue
        # find the first line where the running id at line start is > target
        # and the line is an explicit id line or a value line; insert before
        # the previous line, i.e. at the position where running id == target.
        pos = None
        for i in range(1, len(lines)):
            if id_at_line[i] is None:
                continue
            start_id = id_at_line[i]
            line = lines[i].rstrip('\r\n')
            stripped = line.strip()
            if not stripped or stripped.startswith(';'):
                continue
            if is_number(line):
                if start_id <= target < int(stripped):
                    pos = i
                    break
            else:
                # a value line: it consumes (start_id); if start_id == target
                # and the text is not ours, the slot is taken by another
                # translation -- the file is for an older version; insert
                # before it is wrong, so jump explicitly instead.
                pass
        if pos is None:
            # insert at end of file (after the last line) with explicit id
            inserts.append((len(lines), [f'{target}', text]))
            report.append(f'id {target}: appended at end of file')
        elif is_number(lines[pos]):
            inserts.append((pos, [f'{target}', text]))
            report.append(f'id {target}: inserted before line {pos + 1} '
                          f'({lines[pos].strip()})')
        else:
            # inserting here would shift the following translations by one:
            # too risky to do automatically
            report.append(f'id {target}: SKIPPED -- no explicit id anchor '
                          f'found (a value line occupies the slot); add it '
                          f'manually before line {pos + 1}')
    if not inserts:
        return None, report
    out = list(lines)
    for pos, new in sorted(inserts, reverse=True):
        out[pos:pos] = [x + '\n' for x in new]
    return out, report


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    path = sys.argv[1]
    with open(path, 'rb') as f:
        data = f.read()
    bom = data.startswith(b'\xef\xbb\xbf')
    text = data.decode('utf-8-sig')
    lines = text.splitlines(keepends=True)
    # normalize: ensure every line ends with \n (the parser strips \r)
    lines = [l if l.endswith('\n') else l + '\n' for l in lines]
    try:
        new_lines, report = patch(lines)
    except ValueError as e:
        print(f'ERROR: {e}')
        sys.exit(1)
    for r in report:
        print(r)
    if new_lines is None:
        print('nothing to do')
        return
    out = ''.join(new_lines)
    with open(path, 'wb') as f:
        f.write((b'\xef\xbb\xbf' if bom else b'') + out.encode('utf-8'))
    print(f'written: {path}')


if __name__ == '__main__':
    main()
