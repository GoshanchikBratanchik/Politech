#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <time.h>

int zapret_null[26];
unsigned short used_mask;//16
int digit[26];
char R1[256], R2[256], R3[256];
int len1, len2, len3;
int solution_count = 0;
int mas_bukv[26];
int N = 0;
long long coef_weight[26];

void Equation(const char* w1, const char* w2, const char* w3) {
    memset(coef_weight, 0, sizeof(coef_weight));
    int l1 = strlen(w1), l2 = strlen(w2), l3 = strlen(w3);
    for (int i = 0; i < l1; i++) coef_weight[w1[i] - 'A'] += 1LL << (4 * (l1 - i - 1));
    for (int i = 0; i < l2; i++) coef_weight[w2[i] - 'A'] += 1LL << (4 * (l2 - i - 1));
    for (int i = 0; i < l3; i++) coef_weight[w3[i] - 'A'] -= 1LL << (4 * (l3 - i - 1));
}

int CheckEquation() {
    long long sum = 0;
    for (int i = 0; i < N; i++) {
        int L = mas_bukv[i];
        sum += coef_weight[L] * digit[L];
    }
    return (sum == 0);
}

void prepareLetters(const char* w1, const char* w2, const char* w3) {
    int seen[26] = { 0 };
    for (int i = 0; w1[i]; i++) seen[w1[i] - 'A'] = 1;
    for (int i = 0; w2[i]; i++) seen[w2[i] - 'A'] = 1;
    for (int i = 0; w3[i]; i++) seen[w3[i] - 'A'] = 1;
    N = 0;
    for (int i = 0; i < 26; i++)
        if (seen[i]) mas_bukv[N++] = i;
}

void printSolution() {
    solution_count++;
    printf("Solution %d: ", solution_count);
    for (int i = 0; i < N; i++) {
        int L = mas_bukv[i];
        printf("%c=%X ", (char)(L + 'A'), (unsigned)digit[L]);
    }
    printf("\n");
}

void rec_column(int pos, int carry) {
    if (pos >= len1 && pos >= len2 && pos >= len3) {
        if (carry == 0 && CheckEquation()) {
            printSolution();
        }
        return;
    }

    int letter1 = (pos < len1) ? R1[pos] - 'A' : -1;
    int letter2 = (pos < len2) ? R2[pos] - 'A' : -1;
    int letter3 = (pos < len3) ? R3[pos] - 'A' : -1;
    int digits1[16], digits2[16];
    int count1 = 0, count2 = 0;

    // Буква 1
    if (letter1 == -1) {
        digits1[count1++] = -1; // нет буквы в этом разряде
    }
    else if (digit[letter1] != -1) {
        digits1[count1++] = digit[letter1]; // уже назначено
    }
    else {
        for (int d = 0; d < 16; d++) {
            if ((used_mask & (1u << d)) == 0 && !(zapret_null[letter1] && d == 0))
                digits1[count1++] = d;
        }
    }

    // Буква 2
    if (letter2 == -1) {
        digits2[count2++] = -1;
    }
    else if (digit[letter2] != -1) {
        digits2[count2++] = digit[letter2];
    }
    else {
        for (int d = 0; d < 16; d++) {
            if ((used_mask & (1u << d)) == 0 && !(zapret_null[letter2] && d == 0))
                digits2[count2++] = d;
        }
    }

    for (int i1 = 0; i1 < count1; i1++) {
        int d1 = digits1[i1];
        int set1 = 0;

        if (d1 >= 0 && letter1 >= 0 && digit[letter1] == -1) {
            digit[letter1] = d1;
            used_mask |= (1u << d1);
            set1 = 1;
        }

        for (int i2 = 0; i2 < count2; i2++) {
            int d2 = digits2[i2];
            int set2 = 0;

            if (d2 >= 0 && letter2 >= 0 && digit[letter2] == -1) {
                if (used_mask & (1u << d2)) continue; // цифра занята
                digit[letter2] = d2;
                used_mask |= (1u << d2);
                set2 = 1;
            }

            int val1 = (d1 < 0 ? 0 : d1);
            int val2 = (d2 < 0 ? 0 : d2);
            int sum = carry + val1 + val2;
            int expected_result = sum & 15;
            int next_carry = sum >> 4;

            if (letter3 == -1) {
                rec_column(pos + 1, next_carry);
            }
            else {
                if (digit[letter3] != -1) {
                    if (digit[letter3] == expected_result)
                        rec_column(pos + 1, next_carry);
                }
                else {
                    if (!(used_mask & (1u << expected_result)) &&
                        !(zapret_null[letter3] && expected_result == 0)) {

                        digit[letter3] = expected_result;
                        used_mask |= (1u << expected_result);

                        rec_column(pos + 1, next_carry);
                        digit[letter3] = -1;
                        used_mask &= ~(1u << expected_result);
                    }
                }
            }

            if (set2) {
                digit[letter2] = -1;
                used_mask &= ~(1u << d2);
            }
        }
        if (set1) {
            digit[letter1] = -1;
            used_mask &= ~(1u << d1);
        }
    }
}


void Solution(const char* w1, const char* w2, const char* w3) {
    memset(digit, -1, sizeof(digit));
    memset(zapret_null, 0, sizeof(zapret_null));
    used_mask = 0; solution_count = 0;

    zapret_null[w1[0] - 'A'] = 1;
    zapret_null[w2[0] - 'A'] = 1;
    zapret_null[w3[0] - 'A'] = 1;

    Equation(w1, w2, w3);
    prepareLetters(w1, w2, w3);

    len1 = (int)strlen(w1); len2 = (int)strlen(w2); len3 = (int)strlen(w3);
    for (int i = 0; i < len1; i++) R1[i] = w1[len1 - 1 - i];
    for (int i = 0; i < len2; i++) R2[i] = w2[len2 - 1 - i];
    for (int i = 0; i < len3; i++) R3[i] = w3[len3 - 1 - i];

    rec_column(0, 0);

    if (solution_count == 0) printf("РЕШЕНИЕ РЕБУСА ОТСУТСТВУЕТ\n");
}

int main() {
    SetConsoleCP(1251); SetConsoleOutputCP(1251);

    char w1[256], w2[256], w3[256];
    printf("Введите первое слово: "); scanf("%s", w1);
    printf("Введите второе слово: "); scanf("%s", w2);
    printf("Введите третье слово: "); scanf("%s", w3);

    clock_t t1 = clock();
    Solution(w1, w2, w3);
    clock_t t2 = clock();

    printf("\nВремя выполнения: %.4f секунд\n", (double)(t2 - t1) / CLOCKS_PER_SEC);
    return 0;
}

