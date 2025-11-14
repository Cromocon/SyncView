#!/usr/bin/env python3
"""Script per debloat automatico del file main_window.py"""

import re
from pathlib import Path

def debloat_file(file_path):
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    new_lines = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        
        # Salta linee con solo separatori decorativi
        if re.match(r'^#\s*[=-]{3,}$', stripped):
            continue
            
        # Salta commenti decorativi tipo "# --- MODIFICA ---"
        if re.match(r'^#\s*---.*---\s*$', stripped):
            continue
            
        # Rimuovi commenti inline che ripetono l'ovvio
        if '#' in line:
            # Mantieni type: ignore
            if 'type: ignore' in line:
                new_lines.append(line)
                continue
                
            code_part = line.split('#')[0]
            comment_part = '#'.join(line.split('#')[1:])
            
            # Rimuovi commenti ridondanti
            skip_patterns = [
                r'Aggiungi',
                r'Rimuovi',
                r'Imposta',
                r'Crea',
                r'Setup',
                r'Applica',
                r'Default',
                r'Sempre',
                r'Automatico',
                r'Forzat',
                r'\d+%'
            ]
            
            is_redundant = False
            for pattern in skip_patterns:
                if re.search(pattern, comment_part, re.IGNORECASE):
                    # Controlla se il commento aggiunge informazioni
                    if len(comment_part.strip()) < 50:
                        is_redundant = True
                        break
            
            if is_redundant and code_part.strip():
                new_lines.append(code_part.rstrip() + '\n')
            else:
                new_lines.append(line)
        else:
            new_lines.append(line)
    
    # Rimuovi linee vuote multiple consecutive
    final_lines = []
    prev_empty = False
    for line in new_lines:
        is_empty = not line.strip()
        if is_empty and prev_empty:
            continue
        final_lines.append(line)
        prev_empty = is_empty
    
    return final_lines

if __name__ == '__main__':
    file_path = Path('/home/cromocon/Desktop/VAR/SyncView/ui/main_window.py')
    backup_path = file_path.with_suffix('.py.bak')
    
    # Backup
    import shutil
    shutil.copy2(file_path, backup_path)
    print(f"Backup creato: {backup_path}")
    
    # Debloat
    new_content = debloat_file(file_path)
    
    # Scrivi
    with open(file_path, 'w', encoding='utf-8') as f:
        f.writelines(new_content)
    
    # Statistiche
    with open(backup_path, 'r') as f:
        old_lines = len(f.readlines())
    new_lines = len(new_content)
    
    print(f"Linee prima: {old_lines}")
    print(f"Linee dopo: {new_lines}")
    print(f"Riduzione: {old_lines - new_lines} linee ({(old_lines - new_lines) / old_lines * 100:.1f}%)")
