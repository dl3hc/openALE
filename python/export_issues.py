#!/usr/bin/env python3
"""
export_issues.py
Generiert GitHub Issue Preview-Dateien (Markdown) aus:
  - IMPLEMENTATION_BACKLOG.yml  (Features, Status, Module, Depends-on, Notes)
  - REQUIREMENTS.md             (vollständige REQ-Texte und AC-Texte, ungekürzt)

Ausgabe: issues/<FEAT-ID>.md  (ein File pro Feature)
         issues/_index.md      (Übersichtstabelle aller Issues)

Aufruf: python3 scripts/export_issues.py
"""
import re, os, yaml
from pathlib import Path

ROOT    = os.path.dirname(os.path.abspath(__file__))
MD_SRC  = os.path.join(ROOT, 'REQUIREMENTS.md')
BKL_SRC = os.path.join(ROOT, 'IMPLEMENTATION_BACKLOG.yml')
OUT_DIR = os.path.join(ROOT, 'issues')
Path(OUT_DIR).mkdir(exist_ok=True)

# ── Status-Emoji-Mapping ─────────────────────────────────────────────────────
STATUS_ICON = {
    'done':        '✅',
    'verified':    '✅',
    'in-progress': '🔄',
    'validate':    '🔍',
    'todo':        '⬜',
    'blocked':     '🚫',
    'skip':        '⏭️',
}
PRIORITY_ICON = {'MUST': '🔴', 'SHOULD': '🟡', 'COULD': '🟢', "WON'T": '⚪'}
AC_STATUS_ICON = {'verified': '✅', 'validate': '🔍', 'open': '⬜'}

# ── 1. REQUIREMENTS.md parsen → vollständige REQ-Map ────────────────────────
with open(MD_SRC, encoding='utf-8') as f:
    md_content = f.read()

req_map = {}   # REQ-id → {title, spec, priority, anforderung, acs: [(id, text)]}
ac_map  = {}   # AC-id  → text  (vollständig, ungekürzt)

for block in re.split(r'\n(?=#{3,4} REQ-)', md_content):
    m = re.match(r'#{3,4} (REQ-[A-Z]+-\d+\w*) — (.+)', block)
    if not m:
        continue
    req_id    = m.group(1).strip()
    req_title = m.group(2).strip()

    spec = re.search(r'\*\*Spec-Referenz:\*\* (.+)', block)
    prio = re.search(r'\*\*Priorität:\*\* (MUST|SHOULD|COULD|WON\'T[^\s·]*)', block)
    anf  = re.search(r'\*\*Anforderung:\*\* (.+?)(?=\n\n|\*\*Akzeptanz)', block, re.DOTALL)
    acs  = re.findall(r'`(AC-[A-Z]+-\d+-\d+)` — (.+)', block)

    req_map[req_id] = {
        'title':       req_title,
        'spec':        spec.group(1).strip() if spec else '',
        'priority':    prio.group(1).strip().split()[0] if prio else 'MUST',
        'anforderung': anf.group(1).strip() if anf else '',
        'acs':         acs,
    }
    for ac_id, ac_text in acs:
        ac_map[ac_id] = ac_text.strip()

print(f"REQs geparst: {len(req_map)} | ACs geparst: {len(ac_map)}")

# ── 2. IMPLEMENTATION_BACKLOG.yml laden ──────────────────────────────────────
with open(BKL_SRC, encoding='utf-8') as f:
    backlog = yaml.safe_load(f)

features   = backlog['features']
feat_by_id = {f['id']: f for f in features}

# ── 3. Pro Feature ein Issue generieren ──────────────────────────────────────
generated = []

for feat in features:
    fid      = feat['id']
    title    = feat['title']
    status   = feat.get('status', 'todo')
    priority = feat.get('priority', 'MUST')
    modules  = feat.get('module', [])
    deps     = feat.get('depends_on', [])
    tests    = feat.get('tests', [])
    notes    = feat.get('notes', '')
    impl     = feat.get('implements', [])

    # AC-Status aus dem Backlog (authoritative für Status, Text aus REQUIREMENTS.md)
    backlog_ac_status = {
        ac['id']: ac.get('status', 'open')
        for ac in feat.get('acceptance_criteria', [])
    }

    s_icon = STATUS_ICON.get(status, '❓')
    p_icon = PRIORITY_ICON.get(priority, '⚪')
    domain = fid.split('-')[1]

    lines = []

    # ── Header ────────────────────────────────────────────────────────────────
    lines.append(f'# {fid} — {title}')
    lines.append('')
    lines.append(f'**Status:** {s_icon} `{status}` &nbsp;|&nbsp; '
                 f'**Priorität:** {p_icon} `{priority}` &nbsp;|&nbsp; '
                 f'**Domain:** `{domain}`')
    lines.append('')

    # ── Module ────────────────────────────────────────────────────────────────
    if modules:
        lines.append('## 📁 Module')
        if isinstance(modules, list):
            for m in modules:
                lines.append(f'- `{m}`')
        else:
            lines.append(f'- `{modules}`')
        lines.append('')

    # ── Abhängigkeiten ────────────────────────────────────────────────────────
    if deps:
        lines.append('## 🔗 Depends on')
        for dep in deps:
            dep_feat = feat_by_id.get(dep, {})
            dep_status = dep_feat.get('status', '?')
            dep_icon   = STATUS_ICON.get(dep_status, '❓')
            dep_title  = dep_feat.get('title', '')
            lines.append(f'- {dep_icon} `{dep}` — {dep_title}')
        lines.append('')

    # ── Spec-Referenzen + vollständige REQ-Texte ──────────────────────────────
    if impl:
        lines.append('## 📋 Spezifikation (MIL-STD-188-141B)')
        lines.append('')
        for req_id in impl:
            r = req_map.get(req_id)
            if not r:
                lines.append(f'### {req_id} ⚠️ nicht in REQUIREMENTS.md gefunden')
                lines.append('')
                continue
            p_ico = PRIORITY_ICON.get(r['priority'], '⚪')
            lines.append(f'### {req_id} — {r["title"]}')
            lines.append(f'**Spec:** `{r["spec"]}` &nbsp;|&nbsp; **Priorität:** {p_ico} `{r["priority"]}`')
            lines.append('')
            if r['anforderung']:
                lines.append(f'> {r["anforderung"]}')
                lines.append('')
        lines.append('')

    # ── Acceptance Criteria ────────────────────────────────────────────────────
    lines.append('## ✅ Acceptance Criteria')
    lines.append('')
    lines.append('> **Done-Kriterium:** Alle Checkboxen angehakt + `ctest` grün '
                 '+ kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning')
    lines.append('')

    # Gruppieren nach REQ
    ac_by_req = {}
    for req_id in impl:
        r = req_map.get(req_id, {})
        ac_by_req[req_id] = r.get('acs', [])

    # Alle ACs aus dem Backlog die zu diesem FEAT gehören
    backlog_ac_ids = [ac['id'] for ac in feat.get('acceptance_criteria', [])]

    # Für jede implementierte REQ die ACs ausgeben
    rendered_ac_ids = set()
    for req_id in impl:
        r = req_map.get(req_id)
        if not r or not r['acs']:
            continue
        req_acs = [(ac_id, text) for ac_id, text in r['acs'] if ac_id in backlog_ac_ids]
        if not req_acs:
            continue
        lines.append(f'### {req_id} — {r["title"]} (`{r["spec"]}`)')
        for ac_id, ac_text in req_acs:
            ac_status = backlog_ac_status.get(ac_id, 'open')
            ac_icon   = AC_STATUS_ICON.get(ac_status, '⬜')
            checked   = 'x' if ac_status == 'verified' else ' '
            status_tag = f' `{ac_status}`' if ac_status != 'open' else ''
            lines.append(f'- [{checked}] {ac_icon} **`{ac_id}`**{status_tag} — {ac_text}')
            rendered_ac_ids.add(ac_id)
        lines.append('')

    # ACs aus Backlog die keiner bekannten REQ zugeordnet sind (Fallback)
    orphan_acs = [
        ac for ac in feat.get('acceptance_criteria', [])
        if ac['id'] not in rendered_ac_ids
    ]
    if orphan_acs:
        lines.append('### Weitere Acceptance Criteria')
        for ac in orphan_acs:
            ac_id     = ac['id']
            ac_status = ac.get('status', 'open')
            ac_icon   = AC_STATUS_ICON.get(ac_status, '⬜')
            checked   = 'x' if ac_status == 'verified' else ' '
            # Vollständigen Text aus REQUIREMENTS.md holen, Fallback auf Backlog-Text
            ac_text = ac_map.get(ac_id, ac.get('text', ''))
            status_tag = f' `{ac_status}`' if ac_status != 'open' else ''
            lines.append(f'- [{checked}] {ac_icon} **`{ac_id}`**{status_tag} — {ac_text}')
        lines.append('')

    # ── Tests ─────────────────────────────────────────────────────────────────
    if tests:
        lines.append('## 🧪 Tests')
        for t in tests:
            f_path = t.get('file', '')
            lines.append(f'- `{f_path}`')
        lines.append('')

    # ── Implementierungshinweise ───────────────────────────────────────────────
    if notes:
        lines.append('## 💡 Implementierungshinweise')
        lines.append('')
        lines.append(str(notes))
        lines.append('')

    # ── Workflow-Footer ────────────────────────────────────────────────────────
    lines.append('---')
    lines.append('## 🤖 Agent-Workflow')
    lines.append('')
    lines.append('```')
    lines.append('1. Dieses Issue auf status: in-progress setzen')
    lines.append('2. Alle module-Pfade lesen')
    lines.append('3. Für jede Spec-Referenz: MIL-STD-188-141B Appendix A, Abschnitt X lesen')
    lines.append('4. Nur implementieren, was in den Acceptance Criteria steht')
    lines.append('5. Für jedes AC mindestens einen Test schreiben')
    lines.append('6. ctest ausführen — alle Tests müssen grün sein')
    lines.append('7. Alle AC-Checkboxen abhaken → status: done')
    lines.append('```')

    # ── Datei schreiben ────────────────────────────────────────────────────────
    out_path = os.path.join(OUT_DIR, f'{fid}.md')
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')

    generated.append({
        'id': fid, 'title': title, 'status': status,
        'priority': priority, 'domain': domain,
        'deps': deps, 'ac_count': len(backlog_ac_ids),
    })

# ── 4. Index-Datei ────────────────────────────────────────────────────────────
idx_lines = [
    '# PC-ALE — GitHub Issues Preview',
    '',
    f'Generiert aus `IMPLEMENTATION_BACKLOG.yml` + `REQUIREMENTS.md`  ',
    f'**{len(generated)} Features** | AC-Texte vollständig aus MIL-STD-188-141B',
    '',
    '## Legende',
    '',
    '| Icon | Status |',
    '|------|--------|',
]
for s, i in STATUS_ICON.items():
    idx_lines.append(f'| {i} | `{s}` |')
idx_lines += ['', '---', '']

# Gruppieren nach Domain
domains_order = ['WAVEFORM', 'WORD', 'FEC', 'FRAME', 'SYNC', 'SOUND', 'CHAN', 'LINK', 'ADDR']
by_domain = {d: [] for d in domains_order}
for g in generated:
    by_domain.setdefault(g['domain'], []).append(g)

for domain in domains_order:
    items = by_domain.get(domain, [])
    if not items:
        continue
    idx_lines.append(f'## {domain}')
    idx_lines.append('')
    idx_lines.append('| Status | Feature | Priorität | ACs | Depends on |')
    idx_lines.append('|--------|---------|-----------|-----|------------|')
    for g in items:
        s_ico = STATUS_ICON.get(g['status'], '❓')
        p_ico = PRIORITY_ICON.get(g['priority'], '⚪')
        dep_str = ', '.join(f'`{d}`' for d in g['deps']) if g['deps'] else '—'
        idx_lines.append(
            f"| {s_ico} `{g['status']}` "
            f"| [{g['id']} — {g['title']}](./{g['id']}.md) "
            f"| {p_ico} `{g['priority']}` "
            f"| {g['ac_count']} "
            f"| {dep_str} |"
        )
    idx_lines.append('')

# Status-Zusammenfassung
from collections import Counter
status_counts = Counter(g['status'] for g in generated)
idx_lines += ['---', '', '## Status-Übersicht', '']
idx_lines.append('| Status | Anzahl |')
idx_lines.append('|--------|--------|')
for s in ['done', 'verified', 'in-progress', 'validate', 'todo', 'blocked']:
    if status_counts.get(s, 0) > 0:
        idx_lines.append(f'| {STATUS_ICON.get(s,"")} `{s}` | {status_counts[s]} |')
idx_lines.append('')

with open(os.path.join(OUT_DIR, '_index.md'), 'w', encoding='utf-8') as f:
    f.write('\n'.join(idx_lines) + '\n')

print(f"\nOK: {len(generated)} Issue-Dateien → {OUT_DIR}/")
print(f"    + _index.md (Übersicht)")
print(f"\nNächster actionable Task (todo/validate, deps erfüllt):")
done_ids = {g['id'] for g in generated if g['status'] in ('done', 'verified')}
for g in generated:
    if g['status'] in ('todo', 'validate'):
        deps_done = all(d in done_ids for d in g['deps'])
        if deps_done:
            print(f"  → {STATUS_ICON.get(g['status'],'')} {g['id']} — {g['title']}")
            break
