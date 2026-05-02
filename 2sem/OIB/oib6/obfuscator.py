import random
import string
import re
from obfuscator_config import OBFUSCATION_SETTINGS

used_names = set()

def generate_random_name(prefix="var"):
    while True:
        name = prefix + ''.join(random.choices(string.ascii_letters + string.digits, k=6))
        if name not in used_names:
            used_names.add(name)
            return name

def remove_whitespace(code):
    lines = code.split('\n')
    result = []
    for line in lines:
        if line.strip().startswith('#'):  
            result.append(line)
        else:
            stripped = ' '.join(line.split())
            if stripped:
                result.append(stripped)
    return '\n'.join(result)

def remove_comments(code):
    lines = code.split('\n')
    result = []
    for line in lines:
        if line.strip().startswith('#'):
            result.append(line)  
        else:
            line = re.sub(r'//.*$', '', line)  # Удаляем однострочные комментарии
            line = re.sub(r'/\*.*?\*/', '', line, flags=re.DOTALL)  # Удаляем многострочные
            result.append(line)
    return '\n'.join(result)

def rename_variables(code):
    lines = code.split('\n')
    processed_lines = []
    
    replacements = {}
    
    for line in lines:
        if not line.strip().startswith('#'):   
            var_matches = re.finditer(r'\b(int|char|float|double|FILE)\s+(\w+)\b(?!\s*\()', line)
            for match in var_matches:
                var_name = match.group(2)
                if var_name not in ['main', 'printf', 'scanf', 'fopen', 'fclose']:
                    replacements[var_name] = generate_random_name('v')
            
            func_matches = re.finditer(r'\b(int|char|float|double)\s+(\w+)\s*\(', line)
            for match in func_matches:
                func_name = match.group(2)
                if func_name not in ['main', 'printf', 'scanf', 'fopen', 'fclose']:
                    replacements[func_name] = generate_random_name('f')
    
    for line in lines:
        if line.strip().startswith('#'):
            processed_lines.append(line)  
        else:
            new_line = line
            for old_name, new_name in replacements.items():
                new_line = re.sub(r'\b' + re.escape(old_name) + r'\b', new_name, new_line)
            processed_lines.append(new_line)
    
    return '\n'.join(processed_lines)

def add_junk_code(code):
    # Находим main и добавляем мусорный код
    main_start = code.find('int main(')
    if main_start == -1:
        return code
    
    main_brace = code.find('{', main_start) + 1
    junk_var = generate_random_name('j')
    junk_code = f"int {junk_var}=0; for(int i=0; i<5; i++){{{junk_var}++;}}"
    
    return code[:main_brace] + junk_code + code[main_brace:]

def add_junk_functions():
    junk_func_name = generate_random_name('f')
    junk_var = generate_random_name('j')
    return f"int {junk_func_name}() {{int {junk_var}=0; for(int i=0; i<3; i++){{{junk_var}++;}} return {junk_var};}}"

def shuffle_functions(code):
    func_pattern = r'(?<!#)(int\s+\w+\s*\([^)]*\)\s*{[^{}]*?(?:{[^{}]*?}[^{}]*?)*})'
    functions = re.findall(func_pattern, code, re.DOTALL)
    
    if not functions:
        return code
    
    main_func = None
    other_funcs = []
    for func in functions:
        if 'int main(' in func:
            main_func = func
        else:
            other_funcs.append(func)
    
    if OBFUSCATION_SETTINGS["add_junk"]:
        other_funcs.extend([add_junk_functions(), add_junk_functions()])
    
    random.shuffle(other_funcs)
    
    # Разделяем заголовки и код
    header_end = re.search(r'(?!#)(int\s+\w+\s*\([^)]*\)\s*{)', code).start()
    header = code[:header_end]
    
    return header + '\n'.join(other_funcs) + '\n' + main_func

def obfuscate(code):
    if OBFUSCATION_SETTINGS["remove_comments"]:
        code = remove_comments(code)
    if OBFUSCATION_SETTINGS["remove_whitespace"]:
        code = remove_whitespace(code)
    if OBFUSCATION_SETTINGS["rename_vars"]:
        code = rename_variables(code)
    if OBFUSCATION_SETTINGS["shuffle_funcs"]:
        code = shuffle_functions(code)
    if OBFUSCATION_SETTINGS["add_junk"]:
        code = add_junk_code(code)
    return code

with open('lab6.cpp', 'r') as f:
    source_code = f.read()

obfuscated_code = obfuscate(source_code)

with open('obfuscated.c', 'w') as f:
    f.write(obfuscated_code)

print("Обфускация завершена! Результат в obfuscated.c")