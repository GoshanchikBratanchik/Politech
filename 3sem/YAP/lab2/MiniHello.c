#include <windows.h>

void main(void) {
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "Hello World!\r\n", 15, NULL, NULL);
    ExitProcess(0);
}