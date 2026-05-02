#define _CRT_SECURE_NO_WARNINGS 
#include <stdio.h> 
#include <string.h>  
#include <math.h>

long long wotova(const char* word, long long coef[27])
{
    long long va = 0;
    int len = strlen(word);
    for (int i = 0; i < len; i++)
    {
        va = (va * 10) + coef[word[i] - 'A'];
    }

    return va;
}

void banandcoeff(const char* s1, const char* s2, const char* s, long long coef[27], int bannull[27])
{
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    int len3 = strlen(s);

    bannull[s1[0] - 'A'] = 1;
    bannull[s2[0] - 'A'] = 1;
    bannull[s[0] - 'A'] = 1;

    for (int i = 0; i < len1; i++)
    {
        coef[s1[i] - 'A'] += pow(10, (len1 - i - 1));
    }
    for (int i = 0; i < len2; i++)
    {
        if (coef[s2[i] - 'A'] == 0)
        {
            coef[s2[i] - 'A'] += pow(10, (len2 - i - 1));
        }
    }
    for (int i = 0; i < len3; i++)
    {
        if (coef[s[i] - 'A'] >= 0)
        {
            coef[s[i] - 'A'] = 0;
            coef[s[i] - 'A'] -= pow(10, (len3 - i - 1));
        }
    }
}

void itethva(long long coef[27], int useddighits, int endrec, int bannull[27], int indlet[27], int N, int* a, const char* s1, const char* s2, const char* s)
{
    if (endrec == N)
    {
        long long n1 = wotova(s1, coef);
        long long n2 = wotova(s2, coef);
        long long sum = wotova(s, coef);
        if (n1 + n2 == sum)
        {
            (*a)++;
            printf("Solution %d: \n", *a);
            printf("%lld + %lld = %lld\n", n1, n2, sum);
            printf("\n\n");
        }

        return;
    }
    for (int i = 0; i < 10; i++)
    {
        if (!(useddighits & (1 << i)))
        {
            if (bannull[indlet[endrec]] && i == 0)
            {
                continue;
            }
            useddighits |= (1 << i);
            coef[indlet[endrec]] = i;
            itethva(coef, useddighits, endrec + 1, bannull, indlet, N, a, s1, s2, s);
            useddighits &= ~(1 << i);
        }
    }
}

void solution(const char* s1, const char* s2, const char* s)
{
    long long coef[27] = { 0 };
    int bannull[27] = { 0 };
    int indlet[27];
    int N = 0;
    int amountsol = 0;
    banandcoeff(s1, s2, s, coef, bannull);
    for (int i = 0; i < 27; i++)
    {
        if (coef[i] != 0)
        {
            indlet[N++] = i;
        }
    }
    itethva(coef, 0, 0, bannull, indlet, N, &amountsol, s1, s2, s);
    if (amountsol == 0)
    {
        printf("The solution wasn't found\n");
    }
}

int main()
{
    char s1[256], s2[256], s[256];
    printf("Enter first word: ");
    scanf("%s", s1);
    printf("Enter second word: ");
    scanf("%s", s2);
    printf("Enter third word: ");
    scanf("%s", s);
    solution(s1, s2, s);

    return 0;
}
