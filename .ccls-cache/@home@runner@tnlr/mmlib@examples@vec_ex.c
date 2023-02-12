#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MM_IMPLEMENT
#include <vector.h>

//For sorting a vector of strings
int deref_strcmp(void const *a, void const *b) {
	char * const *lhs = (char * const *) a;
	char * const *rhs = (char * const *) b;
	
	return strcmp(*lhs, *rhs);
}

//Counts the bytes in a vector of strings (including NULs).
int count_bytes(VECTOR_PTR_PARAM(char *, strs)) {
	int count = 0;
	
	int i;
	for (i = 0; i < *strs_len; i++) {
		count += strlen((*strs)[i]) + 1; //+1 for NUL
	}
	
	return count;
}

//Prints all the strings in a vector, then the total number
//of bytes.
void print_strvec(VECTOR_PTR_PARAM(char *, strs)) {
	int i;
	for (i = 0; i < *strs_len; i++) {
		printf("%s", (*strs)[i]);
	}
	
	//Notice that we pass *strs to VECTOR_ARG instead of strs.
	int num_bytes = count_bytes(VECTOR_ARG(*strs));
	printf("\nTotal bytes: %d\n", num_bytes);
}

int main(int argc, char **argv) {
	//Declare and initialize a vector of strings
	VECTOR_DECL(char *, strs);
	vector_init(strs);
	
	//Read lines from stdin until EOF
	while(1) {
		char *line = malloc(128);
		char *ret = fgets(line, 128, stdin);
		if (ret == NULL) break;
		
		vector_push(strs, line);
		
		//Could have also used:
		//
		//char **back = vector_lengthen(strs);
		//*back = line;
	}
	
	//Sort the char*s in the vector
	qsort(strs, strs_len, sizeof(char*), deref_strcmp);
	
	//Print the sorted vector
	puts("\nSorted:");
	puts("-------");
	print_strvec(VECTOR_ARG(strs));
	
	//Release the memory
	int i;
	for (i = 0; i < strs_len; i++) free(strs[i]);
	vector_free(strs);
	
	return 0;
}
