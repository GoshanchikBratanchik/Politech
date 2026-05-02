#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

#define MAX_K 20

int board[MAX_K];  /* board[i] = столбец ферзя в строке i, -1 = нет ферзя */
int n, k;
int found;

/* Проверяет, можно ли поставить ферзя в строку row, столбец col */
static int
is_safe (int row, int col)
{
  int i;
  for (i = 0; i < row; i++)
    {
      if (board[i] == col)
        return 0;
      if (board[i] - col == i - row || board[i] - col == row - i)
        return 0;
    }
  return 1;
}

/* Рекурсивно расставляет ферзей начиная со строки row,
   уже расставлено placed ферзей */
static void
solve (int row, int placed)
{
  int col;

  if (found)
    return;

  /* Все N ферзей расставлены — решение найдено */
  if (placed == n)
    {
      found = 1;
      return;
    }

  /* Все строки перебраны — решения нет */
  if (row >= k)
    return;

  /* Пробуем поставить ферзя в каждый столбец текущей строки */
  for (col = 0; col < k && !found; col++)
    {
      if (is_safe (row, col))
        {
          board[row] = col;
          solve (row + 1, placed + 1);
          if (!found)
            board[row] = -1;
        }
    }

  /* Пробуем пропустить текущую строку (не ставить ферзя) */
  if (!found && board[row] == -1)
    solve (row + 1, placed);
}

int
main (int argc, const char *argv[])
{
  int i, j;

  if (argc != 3)
    {
      printf ("Usage: queen N K\n");
      printf ("  N - number of queens\n");
      printf ("  K - board size (KxK)\n");
      return EXIT_FAILURE;
    }

  n = atoi (argv[1]);
  k = atoi (argv[2]);

  if (k <= 0 || k > MAX_K)
    {
      printf ("Error: K must be between 1 and %d\n", MAX_K);
      return EXIT_FAILURE;
    }

  if (n <= 0 || n > k)
    {
      printf ("Error: N must be between 1 and K=%d\n", k);
      return EXIT_FAILURE;
    }

  /* Инициализируем доску */
  for (i = 0; i < k; i++)
    board[i] = -1;

  found = 0;
  solve (0, 0);

  if (!found)
    {
      printf ("No solution exists for N=%d queens on %dx%d board\n", n, k, k);
      return EXIT_FAILURE;
    }

  /* Выводим решение */
  printf ("Solution for %d queens on %dx%d board:\n", n, k, k);

  /* Выводим доску */
  for (i = 0; i < k; i++)
    {
      for (j = 0; j < k; j++)
        {
          if (board[i] == j)
            printf ("Q ");
          else
            printf (". ");
        }
      printf ("\n");
    }

  /* Выводим координаты ферзей */
  printf ("Queen positions (row col):\n");
  for (i = 0; i < k; i++)
    {
      if (board[i] != -1)
        printf ("  row %d, col %d\n", i + 1, board[i] + 1);
    }

  return EXIT_SUCCESS;
}
