import type { DiffHunk, DiffLine, DiffLineKind } from '../../protocol/types'

export interface DiffViewRow {
  key: string
  kind: DiffLineKind
  oldLine: number | null
  newLine: number | null
  marker: ' ' | '+' | '-'
  text: string
}

function marker(kind: DiffLineKind): ' ' | '+' | '-' {
  if (kind === 'add') return '+'
  if (kind === 'remove') return '-'
  return ' '
}

export function hunkRows(hunk: DiffHunk, hunkIndex = 0): DiffViewRow[] {
  let oldLine = hunk.old_start
  let newLine = hunk.new_start
  return hunk.lines.map((line: DiffLine, lineIndex: number) => {
    const row: DiffViewRow = {
      key: `${hunkIndex}:${lineIndex}:${oldLine}:${newLine}`,
      kind: line.kind,
      oldLine: line.kind === 'add' ? null : oldLine,
      newLine: line.kind === 'remove' ? null : newLine,
      marker: marker(line.kind),
      text: line.text,
    }
    if (line.kind !== 'add') oldLine += 1
    if (line.kind !== 'remove') newLine += 1
    return row
  })
}
