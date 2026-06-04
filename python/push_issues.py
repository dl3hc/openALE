#!/usr/bin/env python3
"""
push_issues.py
Erstellt GitHub Issues direkt aus:
  - REQUIREMENTS.md                 (Quelle aller fachlichen Inhalte, ungekürzt)
  - IMPLEMENTATION_BACKLOG_SLIM.yml (Status, Module, Dependencies, Tests, Notes)

Keine Zwischenstufen. Kein INDEX.yml. Kein AGENT_BACKLOG.yml.

Aufruf:
  python3 scripts/push_issues.py --token <PAT>             # alle 38 Issues
  python3 scripts/push_issues.py --token <PAT> --dry-run   # Vorschau
  python3 scripts/push_issues.py --token <PAT> --single FEAT-FEC-001

PAT: GitHub → Settings → Developer settings → Fine-grained token
     Repository: PC-ALE, Permission: Issues (Read and Write)

pip install requests pyyaml
"""
import re, os, sys, yaml, json, time, argparse
import requests

OWNER  = 'dl3hc'
REPO   = 'PC-ALE'
BASE   = f'https://api.github.com/repos/{OWNER}/{REPO}'
ROOT   = os.path.dirname(os.path.abspath(__file__))
MD_SRC = os.path.join(ROOT, 'REQUIREMENTS.md')
BL_SRC = os.path.join(ROOT, 'IMPLEMENTATION_BACKLOG_SLIM.yml')
STATE  = os.path.join(ROOT, '.push_issues_state.json')

LABELS = [
    {'name': 'status:todo',        'color': 'e4e669', 'description': 'Noch nicht begonnen'},
    {'name': 'status:validate',    'color': 'f9d0c4', 'description': 'Code vorhanden – gegen Spec prüfen'},
    {'name': 'status:in-progress', 'color': '0075ca', 'description': 'Aktuell in Arbeit'},
    {'name': 'status:done',        'color': '0e8a16', 'description': 'Implementiert + alle ACs verified'},
    {'name': 'blocked',            'color': 'd93f0b', 'description': 'Blockiert durch offene Abhängigkeit'},
    {'name': 'priority:MUST',      'color': 'b60205', 'description': ''},
    {'name': 'priority:SHOULD',    'color': 'e99695', 'description': ''},
    {'name': 'priority:COULD',     'color': 'f9d0c4', 'description': ''},
    {'name': 'domain:WAVEFORM',    'color': '1d76db', 'description': ''},
    {'name': 'domain:WORD',        'color': '1d76db', 'description': ''},
    {'name': 'domain:FEC',         'color': '1d76db', 'description': ''},
    {'name': 'domain:FRAME',       'color': '1d76db', 'description': ''},
    {'name': 'domain:SYNC',        'color': '1d76db', 'description': ''},
    {'name': 'domain:SOUND',       'color': '1d76db', 'description': ''},
    {'name': 'domain:CHAN',        'color': '1d76db', 'description': ''},
    {'name': 'domain:LINK',        'color': '1d76db', 'description': ''},
    {'name': 'domain:ADDR',        'color': '1d76db', 'description': ''},
]

MILESTONES = [
    {'title': 'Phase 1 — Waveform + Word + FEC',
     'description': 'Basis-Layer: Modulation, Wortstrukturen, Fehlerkorrektur',
     'domains': {'WAVEFORM', 'WORD', 'FEC'}},
    {'title': 'Phase 2 — Frame + Sync + Addr',
     'description': 'Protokoll-Layer: Frame-Struktur, Synchronisation, Adressierung',
     'domains': {'FRAME', 'SYNC', 'ADDR'}},
    {'title': 'Phase 3 — Link + Chan + Sound',
     'description': 'Verbindungs-Layer: Link-Establishment, Kanalauswahl, Sounding',
     'domains': {'LINK', 'CHAN', 'SOUND'}},
]

SI = {'done':'✅','verified':'✅','in-progress':'🔄','validate':'🔍','todo':'⬜','blocked':'🚫'}
PI = {'MUST':'🔴','SHOULD':'🟡','COULD':'🟢'}

# ── REQUIREMENTS.md parsen ────────────────────────────────────────────────────
def parse_requirements(path):
    """
    Liest REQUIREMENTS.md direkt.
    Gibt zurück:
      req_map: {req_id: {title, spec, priority, anforderung, acs: [(ac_id, text)]}}
    Alle AC-Texte sind vollständig — kein Kürzen, keine Zwischenstufe.
    """
    with open(path, encoding='utf-8') as f:
        content = f.read()

    req_map = {}
    for block in re.split(r'\n(?=#{3,4} REQ-)', content):
        m = re.match(r'#{3,4} (REQ-[A-Z]+-\d+\w*) — (.+)', block)
        if not m:
            continue
        rid  = m.group(1).strip()
        spec = re.search(r'\*\*Spec-Referenz:\*\* (.+)', block)
        prio = re.search(r'\*\*Priorität:\*\* (MUST|SHOULD|COULD|WON\'T[^\s·]*)', block)
        anf  = re.search(r'\*\*Anforderung:\*\* (.+?)(?=\n\n|\*\*Akzeptanz)', block, re.DOTALL)
        acs  = re.findall(r'`(AC-[A-Z]+-\d+-\d+)` — (.+)', block)
        req_map[rid] = {
            'title':       m.group(2).strip(),
            'spec':        spec.group(1).strip() if spec else '',
            'priority':    prio.group(1).strip().split()[0] if prio else 'MUST',
            'anforderung': anf.group(1).strip() if anf else '',
            'acs':         [(ac_id, txt.strip()) for ac_id, txt in acs],
        }
    return req_map

# ── Issue-Body ─────────────────────────────────────────────────────────────────
def build_body(feat, feat_by_id, req_map):
    fid      = feat['id']
    status   = feat.get('status', 'todo')
    priority = feat.get('priority', 'MUST')
    modules  = feat.get('module', [])
    deps     = feat.get('depends_on', [])
    tests    = feat.get('tests', [])
    notes    = feat.get('notes', '')
    impl     = feat.get('implements', [])
    ac_st    = feat.get('ac_status', {})   # nur nicht-open Einträge
    domain   = fid.split('-')[1]

    L = []

    # Metadaten-Zeile
    L += [
        f'**Status:** {SI.get(status,"❓")} `{status}` &nbsp;|&nbsp; '
        f'**Priorität:** {PI.get(priority,"⚪")} `{priority}` &nbsp;|&nbsp; '
        f'**Domain:** `{domain}`', '',
    ]

    # Module
    if modules:
        L.append('## 📁 Module')
        for m in (modules if isinstance(modules, list) else [modules]):
            L.append(f'- `{m}`')
        L.append('')

    # Depends on
    if deps:
        L.append('## 🔗 Depends on')
        for dep in deps:
            df  = feat_by_id.get(dep, {})
            ico = SI.get(df.get('status', '?'), '❓')
            L.append(f'- {ico} `{dep}` — {df.get("title", "")}')
        L.append('')

    # Spezifikation: REQ-Prosa direkt aus REQUIREMENTS.md
    if impl:
        L += ['## 📋 Spezifikation (MIL-STD-188-141B Appendix A)', '']
        for rid in impl:
            r = req_map.get(rid)
            if not r:
                L += [f'### {rid} ⚠️ nicht in REQUIREMENTS.md gefunden', '']
                continue
            L += [
                f'### {rid} — {r["title"]}',
                f'**Spec-Abschnitt:** `{r["spec"]}` &nbsp;|&nbsp; '
                f'**Priorität:** {PI.get(r["priority"],"⚪")} `{r["priority"]}`',
                '',
                f'> {r["anforderung"]}' if r["anforderung"] else '',
                '',
            ]
        L.append('')

    # Acceptance Criteria: Text 1:1 aus REQUIREMENTS.md, Status aus BACKLOG_SLIM
    L += [
        '## ✅ Acceptance Criteria',
        '',
        '> **Done-Kriterium:** Alle Checkboxen ✅ + `ctest` grün '
        '+ kein `TODO`/`PLACEHOLDER` + kein `-W4`-Warning',
        '',
    ]

    for rid in impl:
        r = req_map.get(rid)
        if not r or not r['acs']:
            continue
        L.append(f'### {rid} — {r["title"]} (`{r["spec"]}`)')
        for ac_id, ac_text in r['acs']:
            st  = ac_st.get(ac_id, 'open')
            chk = 'x' if st == 'verified' else ' '
            ico = {'verified': '✅', 'validate': '🔍', 'open': '⬜'}.get(st, '⬜')
            tag = f' `{st}`' if st != 'open' else ''
            L.append(f'- [{chk}] {ico} **`{ac_id}`**{tag} — {ac_text}')
        L.append('')

    # Tests
    if tests:
        L.append('## 🧪 Tests')
        for t in (tests if isinstance(tests, list) else [tests]):
            L.append(f'- `{t}`')
        L.append('')

    # Notizen
    if notes:
        L += ['## 💡 Implementierungshinweise', '', str(notes), '']

    # Agent-Footer
    L += [
        '---', '## 🤖 Agent-Workflow', '',
        '```',
        '1. module-Pfade lesen',
        '2. Spec-Abschnitte im MIL-STD-188-141B Appendix A nachschlagen',
        '3. Nur implementieren was in den Acceptance Criteria steht',
        '4. Für jedes AC mindestens einen Test schreiben',
        '5. ctest --output-on-failure',
        '6. AC-Checkboxen abhaken → Issue schließen',
        '```',
    ]

    return '\n'.join(line for line in L) + '\n'

# ── GitHub-Client ──────────────────────────────────────────────────────────────
class GH:
    def __init__(self, token, dry_run):
        self.dry_run = dry_run
        self.s = requests.Session()
        self.s.headers.update({
            'Authorization':        f'token {token}',
            'Accept':               'application/vnd.github+json',
            'X-GitHub-Api-Version': '2022-11-28',
        })

    def get(self, path, params=None):
        if self.dry_run:
            return []
        r = self.s.get(f'{BASE}{path}', params=params)
        r.raise_for_status()
        return r.json()

    def post(self, path, data):
        if self.dry_run:
            print(f'    [DRY] POST {path}: {str(data)[:80]}')
            return {'number': 0, 'html_url': f'https://github.com/{OWNER}/{REPO}/issues/0'}
        r = self.s.post(f'{BASE}{path}', json=data)
        if r.status_code == 422:
            return None
        r.raise_for_status()
        time.sleep(0.6)
        return r.json()

    def patch(self, path, data):
        if self.dry_run:
            return {}
        r = self.s.patch(f'{BASE}{path}', json=data)
        r.raise_for_status()
        time.sleep(0.3)
        return r.json()

# ── Main ───────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--token',   required=True)
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--single',  metavar='FEAT-ID')
    ap.add_argument('--force',   action='store_true',
                    help='State ignorieren, Issues neu anlegen')
    args = ap.parse_args()

    gh = GH(args.token, args.dry_run)

    state = {}
    if os.path.exists(STATE) and not args.force:
        with open(STATE) as f:
            state = json.load(f)

    def save():
        if not args.dry_run:
            with open(STATE, 'w') as f:
                json.dump(state, f, indent=2)

    # Daten laden — direkt aus den beiden Quellen
    print(f'Quellen:')
    print(f'  {MD_SRC}')
    print(f'  {BL_SRC}')
    print()

    req_map = parse_requirements(MD_SRC)
    print(f'REQUIREMENTS.md: {len(req_map)} REQs, '
          f'{sum(len(r["acs"]) for r in req_map.values())} ACs (vollständig, ungekürzt)')

    with open(BL_SRC, encoding='utf-8') as f:
        bl = yaml.safe_load(f)
    features   = bl['features']
    feat_by_id = {f['id']: f for f in features}
    print(f'BACKLOG_SLIM:    {len(features)} Features\n')

    if args.single:
        features = [f for f in features if f['id'] == args.single]
        if not features:
            sys.exit(f'ERROR: {args.single} nicht gefunden')

    # Labels
    print('── Labels ──')
    existing = {l['name'] for l in gh.get('/labels', {'per_page': 100})}
    for lbl in LABELS:
        if lbl['name'] not in existing:
            gh.post('/labels', lbl)
            print(f'  + {lbl["name"]}')
        else:
            print(f'  ✓ {lbl["name"]}')

    # Milestones
    print('\n── Milestones ──')
    existing_ms = {m['title']: m['number']
                   for m in gh.get('/milestones', {'state': 'open', 'per_page': 100})}
    ms_map = {}   # domain → milestone_number
    for ms in MILESTONES:
        if ms['title'] not in existing_ms:
            r = gh.post('/milestones', {'title': ms['title'],
                                        'description': ms['description']})
            num = r.get('number', 0) if r else 0
            print(f'  + {ms["title"]} → #{num}')
        else:
            num = existing_ms[ms['title']]
            print(f'  ✓ {ms["title"]} (#{num})')
        for d in ms['domains']:
            ms_map[d] = num

    # Issues
    print('\n── Issues ──')
    done_ids = {f['id'] for f in bl['features']
                if f.get('status') in ('done', 'verified')}
    created  = {}

    for feat in features:
        fid      = feat['id']
        status   = feat.get('status', 'todo')
        priority = feat.get('priority', 'MUST')
        deps     = feat.get('depends_on', [])
        domain   = fid.split('-')[1]

        if fid in state and not args.force:
            print(f'  {fid:35s} ✓ bereits #{state[fid]} — skip')
            continue

        labels = [f'status:{status}', f'priority:{priority}', f'domain:{domain}']
        if any(d not in done_ids for d in deps):
            labels.append('blocked')

        body = build_body(feat, feat_by_id, req_map)
        data = {'title': f'[{fid}] {feat["title"]}', 'body': body, 'labels': labels}
        ms = ms_map.get(domain)
        if ms:
            data['milestone'] = ms

        r = gh.post('/issues', data)
        if not r:
            print(f'  {fid:35s} FEHLER')
            continue

        num = r['number']
        url = r['html_url']
        state[fid] = num
        created[fid] = num
        save()

        if status in ('done', 'verified') and not args.dry_run:
            gh.patch(f'/issues/{num}', {'state': 'closed'})
            print(f'  {fid:35s} → #{num:4d} [closed]  {url}')
        else:
            print(f'  {fid:35s} → #{num:4d}           {url}')

    # Dependency-Kommentare
    if not args.single and created and not args.dry_run:
        print('\n── Dependencies verlinken ──')
        for feat in features:
            fid  = feat['id']
            deps = feat.get('depends_on', [])
            num  = state.get(fid)
            if not deps or not num or fid not in created:
                continue
            links = [f'Blocked by #{state[d]} (`{d}`)' for d in deps if d in state]
            if links:
                gh.post(f'/issues/{num}/comments', {'body': '\n'.join(links)})
                print(f'  #{num} ← {", ".join(links)}')

    print(f'\n✅ Fertig.')
    if args.single or len(created) <= 3:
        print()
        print('─' * 65)
        print('CLINE SYSTEM PROMPT (einmalig in .clinerules eintragen):')
        print('─' * 65)
        print(CLINE_PROMPT)

CLINE_PROMPT = """\
Du bist ein Coding Agent für das Projekt PC-ALE (MIL-STD-188-141B Appendix A).
Repository: github.com/dl3hc/PC-ALE  —  Branch: develop

Starte mit diesem Workflow bei jedem Aufruf:

1. Nächstes offenes Issue ermitteln (niedrigste Nummer, kein "blocked"-Label):
   gh issue list \\
     --label "status:todo,status:validate" --state open \\
     --json number,title,labels \\
     --jq '[.[] | select([.labels[].name] | contains(["blocked"]) | not)]
           | sort_by(.number) | first | "#\\(.number) \\(.title)"'

2. Issue vollständig lesen:
   gh issue view <N> --json body | jq -r '.body'

3. Nur die im Issue genannten module-Pfade lesen.

4. Implementiere exakt was in den Acceptance Criteria steht — nicht mehr.

5. Für jedes AC mindestens einen Test schreiben.

6. ctest --output-on-failure  (bei Fehler: im selben Issue bleiben und fixen)

7. Alle AC-Checkboxen auf [x] setzen (gh issue edit).

8. gh issue close <N> --comment "done: alle ACs verified, ctest grün"

9. Zurück zu Schritt 1.

Regeln:
- Maximal 1 Issue gleichzeitig.
- Entdeckte Bugs → gh issue create, nicht sofort lösen.
- Kein Code außerhalb der im Issue genannten module-Pfade.
- Keine Annahmen über den MIL-STD — nur was im Issue steht gilt als Spec.\
"""

if __name__ == '__main__':
    main()
