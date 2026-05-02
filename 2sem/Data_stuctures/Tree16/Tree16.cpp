#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <locale.h>

typedef struct splay {
    int count;
    char hexnum[100];
    struct splay* left;
    struct splay* right;
    struct splay* parent;
} splay;

#define STATE_DEFAULT 0
#define STATE_IN_STRING 1
#define STATE_IN_CHAR 2
#define STATE_IN_SINGLE_COMMENT 3
#define STATE_IN_MULTI_COMMENT 4

void removecomments(const char* file1, const char* file2) {
    FILE* input = fopen(file1, "r");
    FILE* output = fopen(file2, "w+");
    
    int c, next;
    int state = STATE_DEFAULT;

    int count_sleshik = 0;
    while ((c = fgetc(input)) != EOF) {
        if (c == '\\')count_sleshik++;
        switch (state) {
        case STATE_DEFAULT:
            if (c == '/') {
                next = fgetc(input);
                if (next == EOF) {
                    fputc(c, output);
                    return;
                }
                if (next == '/') {
                    state = STATE_IN_SINGLE_COMMENT;
                }
                else if (next == '*') {
                    state = STATE_IN_MULTI_COMMENT;
                }
                else {
                    fputc(c, output);
                    fseek(input, -1, SEEK_CUR);
                }
            }
            else if (c == '"') {
                state = STATE_IN_STRING;
                fputc(c, output);
            }
            else if (c == '\'') {
                state = STATE_IN_CHAR;
                fputc(c, output);
            }
            else {
                fputc(c, output);
            }
            break;

        case STATE_IN_STRING:
            fputc(c, output);
            if (count_sleshik % 2 == 0) {
                if (c == '"') {
                    state = STATE_DEFAULT;
                }
                else if (c == '\n') {
                    state = STATE_DEFAULT;
                }
            }
            break;

        case STATE_IN_CHAR:
            fputc(c, output);
            if (c == '\n') state = STATE_DEFAULT;
            else if (c == '\'' && (count_sleshik % 2 == 0)) state = STATE_DEFAULT;
            break;

        case STATE_IN_SINGLE_COMMENT:
            if (c == '\n') {
                if (count_sleshik % 2 == 0) {
                    fputc('\n', output);
                    state = STATE_DEFAULT;
                }
            }
            else if (c == EOF) {
                return;
            }
            break;

        case STATE_IN_MULTI_COMMENT:
            if (c == '*') {
                next = fgetc(input);
                if (next == EOF)return;
                else if (next == '/') {
                    state = STATE_DEFAULT;
                }
                else {
                    fseek(input, -1, SEEK_CUR);
                }

            }
            break;

        default:
            state = STATE_DEFAULT;
            break;
        }
        if (c != '\\' || c == '\n')count_sleshik = 0;
    }

    fclose(input);
    fclose(output);
}

int strcasecmp(char* s1, char* s2) {
    while (*s1 && *s2) {
        int diff = tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
        if (diff != 0) return diff;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

void zig(splay* x) {
    splay* y = x->parent;
    splay* z = y->parent;
    if (x->right) {
        x->right->parent = y;
    }
    y->left = x->right;
    if (z != NULL) {
        if (y == z->right) z->right = x;
        else z->left = x;
    }
    x->parent = z;
    x->right = y;
    y->parent = x;
}

void zag(splay* x) {
    splay* y = x->parent;
    splay* z = y->parent;
    if (x->left) {
        x->left->parent = y;
    }
    y->right = x->left;
    if (z != NULL) {
        if (y == z->left) z->left = x;
        else z->right = x;
    }
    x->parent = z;
    x->left = y;
    y->parent = x;
}

splay* splaytree(splay* root, splay* num) {
    if (!num || num == root) return root;

    while (num->parent != NULL) {
        splay* parent = num->parent;
        splay* grandparent = parent->parent;

        if (grandparent == NULL) {
            if (num == parent->left) {
                zig(num);
            }
            else {
                zag(num);
            }
        }
        else {
            if (num == parent->left && parent == grandparent->left) {
                zig(parent);
                zig(num);
            }
            else if (num == parent->right && parent == grandparent->right) {
                zag(parent);
                zag(num);
            }
            else if (num == parent->right && parent == grandparent->left) {
                zag(num);
                zig(num);
            }
            else {
                zig(num);
                zag(num);
            }
        }

        if (num->parent == NULL) {
            root = num;
        }
    }

    return root;
}

splay* createnode(char* hexnum) {
    splay* num = (splay*)malloc(sizeof(splay));
    strncpy(num->hexnum, hexnum, sizeof(num->hexnum) - 1);
    num->hexnum[sizeof(num->hexnum) - 1] = '\0';
    num->count = 1;
    num->left = num->right = num->parent = NULL;
    return num;
}

splay* insert(splay* root, char* hexnum) {
    if (!root) {
        return createnode(hexnum);
    }

    splay* current = root;
    splay* parent = NULL;

    while (current) {
        parent = current;
        int cmp = strcasecmp(hexnum, current->hexnum);

        if (cmp == 0) {
            current->count++;
            return splaytree(root, current);
        }
        else if (cmp < 0) {
            current = current->left;
        }
        else {
            current = current->right;
        }
    }

    splay* newnum = createnode(hexnum);
    newnum->parent = parent;

    if (strcasecmp(hexnum, parent->hexnum) < 0) {
        parent->left = newnum;
    }
    else {
        parent->right = newnum;
    }

    return splaytree(root, newnum);
}

unsigned long hextodecimal(char* hexnum) {
    unsigned long result = 0;
    for (int i = 0; hexnum[i] != '\0'; i++) {
        char c = tolower((unsigned char)hexnum[i]);
        result *= 16;
        if (isdigit((unsigned char)c)) {
            result += c - '0';
        }
        else if (c >= 'a' && c <= 'f') {
            result += c - 'a' + 10;
        }
    }
    return result;
}

splay* find(splay* root, char* hexnum) {
    splay* current = root;
    splay* result = NULL;

    while (current) {
        int cmp = strcasecmp(hexnum, current->hexnum);

        if (cmp == 0) {
            result = current;
            break;
        }
        else if (cmp < 0) {
            current = current->left;
        }
        else {
            current = current->right;
        }
    }

    if (result) {
        printf("Число 0x%s (%lu) встречается %d раз\n",
            result->hexnum, hextodecimal(result->hexnum), result->count);
        return splaytree(root, result);  
    }
    else {
        printf("Число 0x%s не найдено\n", hexnum);
        return root;  
    }
}

void freenode(splay* num) {
    free(num);
}

void freetree(splay* root) {
    if (root) {
        freetree(root->left);
        freetree(root->right);
        freenode(root);
    }
}

void printtree(splay* root) {
    if (root) {
        printtree(root->left);
        unsigned long num10 = hextodecimal(root->hexnum);
        printf("Число 0x%s (%lu) встречается %d раз\n",
            root->hexnum, num10, root->count);
        printtree(root->right);
    }
}

int difftotal(splay* root) {
    if (!root) return 0;
    return 1 + difftotal(root->left) + difftotal(root->right);
}

int total(splay* root) {
    if (!root) return 0;
    return root->count + total(root->left) + total(root->right);
}

bool check16(char c) {
    return isdigit(c) || (tolower(c) >= 'a' && tolower(c) <= 'f');
}

void define16(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Ошибка: не удалось открыть файл %s\n", filename);
        return;
    }

    splay* root = NULL;
    char hexbuffer[100];
    int c, prev = 0;

    while ((c = fgetc(file)) != EOF) {
        if (c == '0') {
            int next = fgetc(file);
            if (next == 'x' || next == 'X') {
                // Проверка левой границы — перед 0 не должно быть буквы, цифры, или _
                if (isalnum(prev) || prev == '_') {
                    prev = next;
                    continue; // часть идентификатора
                }

                // собираем 16-ричные цифры
                int hexindex = 0;
                int ch;
                while ((ch = fgetc(file)) != EOF && isxdigit(ch) && hexindex < (int)sizeof(hexbuffer) - 1) {
                    hexbuffer[hexindex++] = (char)ch;
                }
                hexbuffer[hexindex] = '\0';

                if (hexindex == 0) {
                    // нет цифр после 0x → не число
                    if (ch != EOF) ungetc(ch, file);
                    prev = next;
                    continue;
                }

                // собираем возможный суффикс U/L (до 3 символов)
                char suffix[4];
                int sindex = 0;
                while (ch != EOF && (ch == 'u' || ch == 'U' || ch == 'l' || ch == 'L') && sindex < 3) {
                    suffix[sindex++] = (char)ch;
                    ch = fgetc(file);
                }

                // проверка правой границы — после числа и суффикса не должно быть буквы, цифры или '_'
                if (ch != EOF && (isalnum(ch) || ch == '_')) {
                    // невалидное — например 0xFFkk
                    // откатываем последний символ и пропускаем
                    ungetc(ch, file);
                    prev = next;
                    continue;
                }

                // число корректно
                root = insert(root, hexbuffer);

                if (ch != EOF) ungetc(ch, file);
                prev = next;
            }
            else {
                if (next != EOF) ungetc(next, file);
                prev = c;
            }
        }
        else {
            prev = c;
        }
    }

    fclose(file);

    printf("\nРезультаты поиска шестнадцатеричных чисел:\n");
    printf("==========================================\n");
    if (root) {
        printtree(root);
        printf("==========================================\n");
        printf("Разных чисел: %d\n", difftotal(root));
        printf("Всего чисел: %d\n", total(root));
    }
    else {
        printf("Шестнадцатеричные числа не найдены\n");
    }

    while (1) {
        int chose;
        printf("Выберите: 1)Найти число  2)Выход\n");
        if (scanf("%d", &chose) != 1) {
            while (getchar() != '\n');
            printf("Неверный ввод\n");
            continue;
        }
        switch (chose) {
        case 1: {
            char hexnum[100];
            printf("Введите число без префикса: ");
            if (scanf("%99s", hexnum) == 1) {
                root = find(root, hexnum);
            }
            break;
        }
        case 2:
            freetree(root);
            return;
        default:
            printf("Неверный ввод\n");
        }
    }
}

//void define16(const char* filename) {
//    FILE* file = fopen(filename, "r");
//    if (!file) {
//        printf("Ошибка: не удалось открыть файл %s\n", filename);
//        return;
//    }
//
//    splay* root = NULL;
//    char hexbuffer[100];
//    int hexindex = 0;
//
//    bool in_string = false;
//    bool in_char = false;
//    bool escape = false;            // для обработки \ внутри строк/символов
//    int prev = 0;                   // предыдущий прочитанный символ (0 - начало файла)
//
//    int c;
//    while ((c = fgetc(file)) != EOF) {
//        int ch = c;
//
//        // --- режим строки ---
//        if (in_string) {
//            if (escape) {
//                escape = false;
//            }
//            else if (ch == '\\') {
//                escape = true;
//            }
//            else if (ch == '"') {
//                in_string = false;
//            }
//
//            // внутри строки: ищем "0x" в произвольном месте и принимаем максимум hex-цифр
//            if (ch == '0') {
//                int nx = fgetc(file);
//                if (nx != EOF && (nx == 'x' || nx == 'X')) {
//                    // собираем hex-цифры
//                    hexindex = 0;
//                    int nc;
//                    while ((nc = fgetc(file)) != EOF && isxdigit((unsigned char)nc) && hexindex < (int)sizeof(hexbuffer) - 1) {
//                        hexbuffer[hexindex++] = (char)nc;
//                    }
//                    hexbuffer[hexindex] = '\0';
//
//                    if (hexindex > 0) {
//                        // вставляем найденную hex-часть (в строках мы принимаем просто максимальный префикс hex-цифр)
//                        root = insert(root, hexbuffer);
//                    }
//
//                    if (nc != EOF) ungetc(nc, file);
//                }
//                else {
//                    if (nx != EOF) ungetc(nx, file);
//                }
//            }
//
//            prev = ch;
//            continue;
//        }
//
//        // --- режим символьного литерала ---
//        if (in_char) {
//            if (escape) {
//                escape = false;
//            }
//            else if (ch == '\\') {
//                escape = true;
//            }
//            else if (ch == '\'') {
//                in_char = false;
//            }
//            prev = ch;
//            continue;
//        }
//
//        // --- вне строк/символов ---
//        if (ch == '"') {
//            in_string = true;
//            prev = ch;
//            continue;
//        }
//        if (ch == '\'') {
//            in_char = true;
//            prev = ch;
//            continue;
//        }
//
//        // распознаём возможный префикс 0x вне строки
//        if (ch == '0') {
//            int nx = fgetc(file);
//            if (nx != EOF && (nx == 'x' || nx == 'X')) {
//                // проверка границы слева: prev должен быть либо 0 (начало), либо не буква/цифра/_
//                bool left_boundary = (prev == 0) || !(isalnum((unsigned char)prev) || prev == '_');
//
//                if (!left_boundary) {
//                    // не граница — это часть идентификатора (например hg0x...) => пропускаем
//                    // возвращаем nx в поток и продолжаем
//                    ungetc(nx, file);
//                }
//                else {
//                    // собираем hex-цифры
//                    hexindex = 0;
//                    int nc;
//                    while ((nc = fgetc(file)) != EOF && isxdigit((unsigned char)nc) && hexindex < (int)sizeof(hexbuffer) - 1) {
//                        hexbuffer[hexindex++] = (char)nc;
//                    }
//
//                    if (hexindex == 0) {
//                        // после 0x нет hex-цифр -> невалид, просто вернуть последний прочитанный символ (nc) и пропустить
//                        if (nc != EOF) ungetc(nc, file);
//                        ungetc(nx, file); // вернуть 'x'
//                    }
//                    else {
//                        // сейчас nc — первый символ после последовательности hex-цифр (или EOF)
//                        // проверим допустимый суффикс: последовательность букв u/U/l/L (макс разумная длина, скажем 3)
//                        int peek = nc;
//                        int suffix_len = 0;
//                        while (peek != EOF && (peek == 'u' || peek == 'U' || peek == 'l' || peek == 'L') && suffix_len < 3) {
//                            suffix_len++;
//                            peek = fgetc(file);
//                        }
//
//                        // если после суффикса идёт буква/цифра/_, и эта буква НЕ принадлежит набору u/l,
//                        // значит это смешанный токен типа 0x8EFG — не корректный, отвергаем весь токен
//                        if (peek != EOF && (isalnum((unsigned char)peek) || peek == '_')) {
//                            // отвергаем: сбрасываем hexindex (ничего не вставляем)
//                            hexindex = 0;
//                            // откатываем указатель так чтобы следующий цикл увидел peek как обычный символ
//                            ungetc(peek, file);
//                            // также уже прочитанные suffix символы (если были) — их мы не возвращаем, но это нормально,
//                            // потому что они были u/L и мы отвергли (ситуация маловероятна), оставим как есть.
//                        }
//                        else {
//                            // корректный: принимаем число (hexbuffer содержит hex-часть)
//                            hexbuffer[hexindex] = '\0';
//                            root = insert(root, hexbuffer);
//                            // если peek — не EOF, вернём его в поток для дальнейшей обработки
//                            if (peek != EOF) ungetc(peek, file);
//                        }
//                    }
//                } // end left_boundary
//            }
//            else {
//                if (nx != EOF) ungetc(nx, file);
//            }
//            prev = ch;
//            continue;
//        }
//
//        prev = ch;
//    } // end while getchar
//
//    // закрытие и вывод результатов (как раньше)
//    fclose(file);
//
//    printf("\nРезультаты поиска шестнадцатеричных чисел:\n");
//    printf("==========================================\n");
//    if (root) {
//        printtree(root);
//        printf("==========================================\n");
//        printf("Разных чисел: %d\n", difftotal(root));
//        printf("Всего чисел: %d\n", total(root));
//    }
//    else {
//        printf("Шестнадцатеричные числа не найдены\n");
//    }
//
//    // меню поиска (без опасных зависаний)
//    while (true) {
//        int chose;
//        printf("\nВыберите: 1) Найти число  2) Выход\n> ");
//        if (scanf("%d", &chose) != 1) {
//            while (getchar() != '\n');
//            printf("Неверный ввод!\n");
//            continue;
//        }
//
//        if (chose == 1) {
//            char hexnum[100];
//            printf("Введите число без префикса (например, 1A3F): ");
//            if (scanf("%99s", hexnum) == 1) {
//                root = find(root, hexnum);
//            }
//        }
//        else if (chose == 2) {
//            freetree(root);
//            break;
//        }
//        else {
//            printf("Неверный ввод!\n");
//        }
//    }
//}

int main() {
    setlocale(LC_ALL, "Russian");
    const char* file1 = "input.c";
    const char* file2 = "output.c";
    removecomments(file1, file2);
    define16(file2);

    return 0;
}