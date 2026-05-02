#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h> 
#include <string.h>  
#include <math.h>
long long wotova(const char* word, long long coef[27])
{
long long va = 0;
int vWuZG0R = strlen(word);
for (int vnaP92t = 0; vnaP92t < vWuZG0R; vnaP92t++)
{
va = (va * 10) + coef[word[vnaP92t] - 'A'];
}
return va;
}
void banandcoeff(const char* vMdFGqg, const char* s2, const char* s, long long coef[27], int v9AbgdR[27])
{
int vmBRoD5 = strlen(vMdFGqg);
int v0XIx5m = strlen(s2);
int vvszVjA = strlen(s);
v9AbgdR[vMdFGqg[0] - 'A'] = 1;
v9AbgdR[s2[0] - 'A'] = 1;
v9AbgdR[s[0] - 'A'] = 1;
for (int vnaP92t = 0; vnaP92t < vmBRoD5; vnaP92t++)
{
coef[vMdFGqg[vnaP92t] - 'A'] += pow(10, (vmBRoD5 - vnaP92t - 1));
}
for (int vnaP92t = 0; vnaP92t < v0XIx5m; vnaP92t++)
{
if (coef[s2[vnaP92t] - 'A'] == 0)
{
coef[s2[vnaP92t] - 'A'] += pow(10, (v0XIx5m - vnaP92t - 1));
}
}
for (int vnaP92t = 0; vnaP92t < vvszVjA; vnaP92t++)
{
if (coef[s[vnaP92t] - 'A'] >= 0)
{
coef[s[vnaP92t] - 'A'] = 0;
coef[s[vnaP92t] - 'A'] -= pow(10, (vvszVjA - vnaP92t - 1));
}
}
}
void itethva(long long coef[27], int v0NBsAT, int voCN49f, int v9AbgdR[27], int vH49nB3[27], int vKgiLJw, int* a, const char* vMdFGqg, const char* s2, const char* s)
{
if (voCN49f == vKgiLJw)
{
long long n1 = wotova(vMdFGqg, coef);
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
for (int vnaP92t = 0; vnaP92t < 10; vnaP92t++)
{
if (!(v0NBsAT & (1 << vnaP92t)))
{
if (v9AbgdR[vH49nB3[voCN49f]] && vnaP92t == 0)
{
continue;
}
v0NBsAT |= (1 << vnaP92t);
coef[vH49nB3[voCN49f]] = vnaP92t;
itethva(coef, v0NBsAT, voCN49f + 1, v9AbgdR, vH49nB3, vKgiLJw, a, vMdFGqg, s2, s);
v0NBsAT &= ~(1 << vnaP92t);
}
}
}
void solution(const char* vMdFGqg, const char* s2, const char* s)
{
long long coef[27] = { 0 };
int v9AbgdR[27] = { 0 };
int vH49nB3[27];
int vKgiLJw = 0;
int vRLgbYf = 0;
banandcoeff(vMdFGqg, s2, s, coef, v9AbgdR);
for (int vnaP92t = 0; vnaP92t < 27; vnaP92t++)
{
if (coef[vnaP92t] != 0)
{
vH49nB3[vKgiLJw++] = vnaP92t;
}
}
itethva(coef, 0, 0, v9AbgdR, vH49nB3, vKgiLJw, &vRLgbYf, vMdFGqg, s2, s);
if (vRLgbYf == 0)
{
printf("The solution wasn't found\n");
}
}
int fw2ZXYq() {int j2ZYiyy=0; for(int i=0; i<3; i++){j2ZYiyy++;} return j2ZYiyy;}
int f8gbVpL() {int jPwC32O=0; for(int i=0; i<3; i++){jPwC32O++;} return jPwC32O;}
int main()
{int jNrM8cY=0; for(int i=0; i<5; i++){jNrM8cY++;}
char vMdFGqg[256], s2[256], s[256];
printf("Enter first word: ");
scanf("%s", vMdFGqg);
printf("Enter second word: ");
scanf("%s", s2);
printf("Enter third word: ");
scanf("%s", s);
solution(vMdFGqg, s2, s);
return 0;
}