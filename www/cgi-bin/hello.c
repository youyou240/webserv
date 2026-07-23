#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int	main(void)
{
	char	*method = getenv("REQUEST_METHOD");
	char	*query = getenv("QUERY_STRING");

	printf("Content-Type: text/plain\r\n");
	printf("\r\n");
	printf("Hello from a C CGI binary!\n");
	printf("REQUEST_METHOD = %s\n", method ? method : "(unset)");
	printf("QUERY_STRING   = %s\n", query ? query : "(unset)");
	if (method && strcmp(method, "POST") == 0)
	{
		int	c;

		printf("BODY = ");
		while ((c = getchar()) != EOF)
			putchar(c);
		printf("\n");
	}
	return (0);
}
