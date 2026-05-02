#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>


int check_passwd(char* passwd) {

	char key = 0x5A;

	FILE* f;
	f = fopen("password.txt", "r");

	char s[7];
	fgets(s, 6, f);
	fclose(f);
	char s1[7];
	for (int i = 0; i < 5 ;  i++) {
		s1[i] = passwd[i] ^ key;
	}
	s1[6] = '\0';

	return strcmp(s1, s);

}
int main() {

	char passwd[6];
	printf("Enter password: ");

	scanf("%5[^\n]", passwd);


	if (check_passwd(passwd) == 0) {
		printf("Password is correct");
	}
	else {
		printf("Try write again");
	}
}