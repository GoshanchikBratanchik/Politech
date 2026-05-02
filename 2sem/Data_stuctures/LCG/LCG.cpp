#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>

unsigned long long lcg(unsigned long long x, unsigned long long a, unsigned long long c)
{
    return (x * a + c);
}

void power(unsigned long long a, unsigned long long m)
{
    unsigned long long b = a - 1;
    unsigned long long b1 = b;
    int s = 1;
    for (int i = 0; i < 128; i++)
    {
        (s)++;
        b = b * b1;
        if (b == 0)
        {
            printf("Мощность = %d\n", s);
            break;
        }
    }
}

void spread(unsigned long long x, unsigned long long a, unsigned long long c)
{
    unsigned long long border;
    printf("Введите границу: ");
    scanf("%llu", &border);
    unsigned long long n;
    printf("Введите количество генераций: ");
    scanf("%llu", &n);
    unsigned long long* numbers;
    numbers = (unsigned long long*)malloc(n * sizeof(unsigned long long));
    memset(numbers, 0, n * sizeof(unsigned long long));
    while (n--)
    {
        numbers[x % border]++;
        x = lcg(x, a, c);
    }
    for (int i = 0; i < border; i++)
    {
        printf("Число %llu повторилось %llu раз\n", i, numbers[i]);
    }
    free(numbers);
}

void period(unsigned long long x, unsigned long long a, unsigned long long c)
{
    unsigned long long x1, x2, x3, x4, x5, x6, p = 0;
    x1 = lcg(x, a, c);
    x2 = lcg(x1, a, c);
    x3 = lcg(x2, a, c);
    x = x3;
    while (1)
    {
        x4 = lcg(x, a, c);
        x5 = lcg(x4, a, c);
        x6 = lcg(x5, a, c);
        if (x4 == x1 && x5 == x2 && x6 == x3)
        {
            break;
        }
        p++;
        if (p % 100000000 == 0)printf("%llu       ", p);
    }
}

void xsquare(unsigned long long x, unsigned long long a, unsigned long long c)
{
    int k = 11;
    int r = 100;
    int bad = 0;
    int good = 0;
    int neutral = 0;
    unsigned long long bord;
    unsigned long long n, g, num;
    double v10[7] = { 2.558, 3.94, 4.865, 15.99, 18.31, 23.31 };
    double V, np;
    printf("Введите границу: ");
    scanf("%llu", &bord);
    printf("Введите количество генераций: ");
    scanf("%llu", &n);
    while (r--) {
        unsigned long long Y[11] = { 0 };
        np = n / k;
        g = n;
        num = 0;
        while (g--) {
            x = lcg(x, a, c);
            num = x % bord;
            for (int i = 0; i < k; i++) {
                if (i * (bord / k) <= num && num < (i + 1) * (bord / k)) {
                    Y[i]++;
                    break;
                }
            }
        }
        V = 0;
        for (int i = 0; i < k; i++) {
            V += (((double)Y[i] - np) * ((double)Y[i] - np) / np);
        }
        if (v10[0] <= V && V <= v10[1]) { printf("Плохой %.2f\n", V), bad++; }
        else if (v10[1] <= V && V <= v10[2]) { printf("Подозрительный %.2f\n", V), neutral++; }
        else if (v10[2] <= V && V <= v10[4]) { printf("Хороший %.2f\n", V), good++; }
        else if (v10[4] <= V && V <= v10[5]) { printf("Подозрительный %.2f\n", V), neutral++; }
        if (v10[5] <= V && V <= v10[6]) { printf("Плохой %.2f\n", V), bad++; }
    }
    printf("Ужасные: %d\n", 100 - bad - good - neutral);
    printf("Плохих: %d\n", bad);
    printf("Подозрительных: %d\n", neutral);
    printf("Хороших: %d\n", good);
    
}

void permutation(unsigned long long x, unsigned long long a, unsigned long long c) {
    int t = 3; 
    int groups = 10000;
    int bad = 0, good = 0, neutral = 0;
    int n = 100;

    while (n--) {
        int permutations[6] = { 0 };
        for (int i = 0; i < groups; i++) {
            unsigned long long group[3];

            for (int j = 0; j < t; j++) {
                x = lcg(x, a, c);
                group[j] = x;
            }
            
            
            if (group[0] < group[1] && group[1] < group[2]) permutations[0]++; // 1,2,3
            else if (group[0] < group[2] && group[2] < group[1]) permutations[1]++; // 1,3,2
            else if (group[1] < group[0] && group[0] < group[2]) permutations[2]++; // 2,1,3
            else if (group[1] < group[2] && group[2] < group[0]) permutations[3]++; // 2,3,1
            else if (group[2] < group[0] && group[0] < group[1]) permutations[4]++; // 3,1,2
            else if (group[2] < group[1] && group[1] < group[0]) permutations[5]++; // 3,2,1
        }

        double expected = groups / 6.0;
        double xsquare = 0.0;

        for (int i = 0; i < 6; i++) {
            xsquare += ((permutations[i] - expected) * (permutations[i] - expected)) / expected;
        }
        if (xsquare >= 1.455 && xsquare <= 11.07) {
            good++;
        }
        else if (xsquare >= 0.5543 && xsquare <= 15.09) {
            neutral++;
        }
        else {
            bad++;
        }
    }
    printf("Хороший: %d\n", good);
    printf("Подозрительный: %d\n", neutral);
    printf("Ужасных: %d\n", bad);

}

void choice(unsigned long long x, unsigned long long a, unsigned long long c, unsigned long long m)
{
    int choose;
    while (1) {
        printf("Что вы хотите увидеть?\n1)Мощность 2)Разброс 3)Период 4)Хи квадрат 5)Перестановки 6)Выход\n");
        scanf("%d", &choose);
        switch (choose)
        {
        case(1):
            power(a, m);
            break;
        case(2):
            x = time(NULL);
            spread(x, a, c);
            break;
        case(3):
            x = time(NULL);
            period(x, a, c);
            break;
        case(4):
            x = time(NULL);
            xsquare(x, a, c);
            break;
        case(5):
            x = time(NULL);
            permutation(x, a, c);
            break;
        case(6):
            return;

        default:
            printf("Выберите 1-6\n");
        }
    }
}

int main()
{
    setlocale(LC_ALL, "Russian");
    unsigned long long m = 18446744073709551615 ;
    unsigned long long a = 92233720368547765;
    unsigned long long c = 3737373737;
    unsigned long long x = 0;
    choice(x, a, c, m);

    return 0;
}
