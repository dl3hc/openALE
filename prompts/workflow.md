Der Workflow ist konkret:
1. Spec-Stellen zu einem Topic sammeln
2. PROMPT_create_user_stories  → US-xxx in die Sektion einfügen
3. PROMPT_extract_requirements → REQ-xxx + AC darunter einfügen
4. PROMPT_extract_features     → FEAT-xxx in FEATURES_DESIGN.md einfügen
5. run "generate_requirements-index.py"
6. PROMPT_create_backog_entry auf -> REQUIREMENTS_INDEX.yml anwenden
7. PROMPT_coding_agent -> auf implementation_backlog.yml arbeiten lassen