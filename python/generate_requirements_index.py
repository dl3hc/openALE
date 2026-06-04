#!/usr/bin/env python3
"""
generate_requirements_index.py
Generiert REQUIREMENTS_INDEX.yml aus REQUIREMENTS.md.
Aufruf: python3 scripts/generate_requirements_index.py
Ausgabe: REQUIREMENTS_INDEX.yml (im Root des Repos)
"""
import re
import sys
import os

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC  = os.path.join(ROOT, 'REQUIREMENTS.md')
DST  = os.path.join(ROOT, 'REQUIREMENTS_INDEX.yml')

with open(SRC, 'r', encoding='utf-8') as f:
    content = f.read()

reqs = []
# Unterstützt sowohl ### als auch #### vor REQ-
req_blocks = re.split(r'\n(?=#{3,4} REQ-[A-Z]+-\d+)', content)

for block in req_blocks:
    req_match = re.match(r'#{3,4} (REQ-[A-Z]+-\d+\w*) — (.+)', block)
    if not req_match:
        continue

    req_id    = req_match.group(1).strip()
    req_title = req_match.group(2).strip()

    spec_ref  = re.search(r'\*\*Spec-Referenz:\*\* (.+)', block)
    priority  = re.search(r'\*\*Priorität:\*\* (MUST|SHOULD|COULD|WON\'T[^\s·]*)', block)
    us_match  = re.search(r'US-[A-Z]+-\d+', block)
    acs       = re.findall(r'`(AC-[A-Z]+-\d+-\d+)` — (.+)', block)

    reqs.append({
        'id':       req_id,
        'title':    req_title,
        'spec':     spec_ref.group(1).strip() if spec_ref else '',
        'priority': priority.group(1).strip() if priority else 'MUST',
        'us':       us_match.group(0) if us_match else '',
        'ac':       acs,
    })

lines = [
    '# REQUIREMENTS INDEX — PC-ALE',
    '# Kompakte maschinenlesbare Destillation aus REQUIREMENTS.md',
    '# NICHT manuell bearbeiten — wird generiert via:',
    '#   python3 scripts/generate_requirements_index.py',
    '#',
    f'# Einträge: {len(reqs)} Requirements',
    '',
    'requirements:',
]

current_prefix = None
for r in reqs:
    prefix = r['id'].split('-')[1]
    if prefix != current_prefix:
        lines.append(f'\n  # --- {prefix} ---')
        current_prefix = prefix

    lines.append(f"  - id: {r['id']}")
    lines.append(f"    title: \"{r['title']}\"")
    if r['spec']:
        spec_clean = r['spec'].replace('_','').replace('(','').replace(')','').strip()
        lines.append(f"    spec: \"{spec_clean}\"")
    lines.append(f"    priority: {r['priority'].split()[0]}")
    if r['us']:
        lines.append(f"    us: {r['us']}")
    lines.append(f"    status: offen")
    if r['ac']:
        lines.append(f"    ac:")
        for ac_id, ac_text in r['ac']:
            ac_text = ac_text.strip()
            if len(ac_text) > 120:
                ac_text = ac_text[:117] + '...'
            lines.append(f'      - id: {ac_id}')
            lines.append(f'        text: "{ac_text}"')
            lines.append(f'        status: open')

output = '\n'.join(lines) + '\n'
with open(DST, 'w', encoding='utf-8') as f:
    f.write(output)

print(f"OK: {len(reqs)} Requirements → {DST}")
print(f"    {len(output.splitlines())} Zeilen (REQUIREMENTS.md: {len(content.splitlines())} Zeilen)")
