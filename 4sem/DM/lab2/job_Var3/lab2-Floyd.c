#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXN 200
#define INF  0x3f3f3f3f3f3f3f3fLL

typedef long long ll;

int  n;
ll   D[MAXN][MAXN];
int  P[MAXN][MAXN];

static void print_state(FILE *fout, int iter) {
    fprintf(fout, "%d\n", iter);

    fprintf(fout, "D:\n");
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (j > 0) fputc(' ', fout);
            if (D[i][j] >= INF / 2)
                fputc('*', fout);
            else
                fprintf(fout, "%lld", D[i][j]);
        }
        fprintf(fout, " \n");
    }

    fprintf(fout, "P:\n");
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (j > 0) fputc(' ', fout);
            fprintf(fout, "%d", P[i][j]);
        }
        fprintf(fout, " \n");
    }
}

int main(void) {
    FILE *fin  = fopen("job_Var3.in",  "r");
    FILE *fout = fopen("job_Var3.out", "w");
    if (!fin || !fout) { perror("fopen"); return 1; }

    int variant;
    if (fscanf(fin, "%d %d", &n, &variant) != 2) { fputs("bad header\n", stderr); return 1; }

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            char tok[32];
            if (fscanf(fin, "%31s", tok) != 1) { fputs("bad matrix\n", stderr); return 1; }
            D[i][j] = (tok[0] == '*') ? INF : atoll(tok);
        }
    fclose(fin);

    /* P[i][j] = i+1 изначально */
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            P[i][j] = i + 1;

    /* n итераций: на итерации k (1-indexed) релаксируем через вершину k,
       затем выводим состояние с номером k */
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < n; i++) {
            if (D[i][k] >= INF / 2) continue;
            for (int j = 0; j < n; j++) {
                if (D[k][j] >= INF / 2) continue;
                ll via = D[i][k] + D[k][j];
                if (via < D[i][j]) {
                    D[i][j] = via;
                    P[i][j] = P[k][j];
                }
            }
        }
        print_state(fout, k + 1);
    }

    fclose(fout);
    printf("Готово: %d вершин, вывод в job_Var3.out\n", n);
    return 0;
}