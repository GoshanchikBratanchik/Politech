#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <intrin.h>
#include <locale.h>

typedef struct num {
    unsigned long long b;
    struct num* next;
    struct num* prev;
} num;

unsigned long long lcg(unsigned long long x) {
    unsigned long long a = 92233720368547765;
    unsigned long long c = 3737373737;
    unsigned long long m = 18446744073709551615;
    return (x * a + c) % m;
}

void gen(num* mt, unsigned long long x, unsigned long long* mas, int n) {
    for (int i = 0; i < n; i++) {
        unsigned long long a = lcg(x);
        x = lcg(x);
        mas[i] = a;
        mt[i].b = a;
        mt[i].prev = (i > 0) ? &mt[i - 1] : NULL;
        mt[i].next = (i < n - 1) ? &mt[i + 1] : NULL;
    }
}

void massort(unsigned long long* mas, int n) {
    for (int i = 1; i < n; i++) {
        unsigned long long x = mas[i];
        int j = i - 1;
        while (j >= 0 && mas[j] > x) {
            mas[j + 1] = mas[j];
            j--;
        }
        mas[j + 1] = x;
    }
}

void sortlist(num* head) {

    num* current = head->next;  

    while (current != NULL) {
        num* next = current->next;
        num* search = current->prev;

        while (search != NULL && search->b > current->b) {
            search = search->prev;
        }

        if (search != current->prev) {
            if (current->prev != NULL)
                current->prev->next = current->next;
            if (current->next != NULL)
                current->next->prev = current->prev;

            
            if (search == NULL) {
                current->next = head;
                head->prev = current;
                current->prev = NULL;
                head = current;
            }
            else {
                current->next = search->next;
                current->prev = search;
                if (search->next != NULL)
                    search->next->prev = current;
                search->next = current;
            }
        }

        current = next; 
    }

}


void printlist(num* head) {
    num* cur = head;
    while (cur != NULL) {
        printf("%llu  ", cur->b);
        cur = cur->next;
    }
    printf("\n");
}

void printmas(unsigned long long* mas, int n) {
    for (int i = 0; i < n; i++) {
        printf("%llu ", mas[i]);
    }
    printf("\n");
}

void freelist(num* mt, int n) {
    for (int i = 0; i < n; i++) {
        mt[i].next = NULL;
        mt[i].prev = NULL;
        mt[i].b = 0;
    }
}

int main() {
    setlocale(LC_ALL, "Russian");
    unsigned long long x, sum1 = 0, sum2 = 0;
    unsigned long long cpu = 2300000000;
    unsigned __int64 start1, start2, end1, end2;
    unsigned long long mas[100000] = { 0 };
    int size[4] = { 100, 1000, 10000, 100000 };
    int n;

    num* mt = (num*)malloc(100000 * sizeof(num));

    for (int s = 0; s < 4; s++) {
        n = size[s];
        sum1 = 0;
        sum2 = 0;

        for (int i = 0; i < 10; i++) {
            x = time(NULL);
            gen(mt, x, mas, n);

            start1 = __rdtsc();
            massort(mas, n);
            end1 = __rdtsc();
            if (mas[n - 1] > 1000) printf("sss");
            num* head = &mt[0];
            printf("list");
            start2 = __rdtsc();
            sortlist(head);
            end2 = __rdtsc();

            sum1 += end1 - start1;
            sum2 += end2 - start2;

            freelist(mt, n);
        }

        sum1 /= 10;
        sum2 /= 10;
        printf("Количество чисел: %d\n", n);
        printf("Количество тактов массива: %I64u\n", sum1);
        printf("Количество тактов списка: %I64u\n", sum2);
        printf("Время массива: %f сек\n", (double)sum1 / (double)cpu);
        printf("Время списка: %f сек\n\n", (double)sum2 / (double)cpu);
    }

    free(mt);
    return 0;
}
