# include "socket/socketClass.hpp"
# include <iostream>
# include <stdlib.h>
# include <stdio.h>

int main(int argc, char **argv)
{
	if (argc != 2)
		return (perror("expect a Port as argument\n"), 1);
	try {
		Socket sock(atoi(argv[1]));
		sock.run();
	} catch (std::exception & e) {
		std::cout << e.what() << std::endl;
	}
	return (0);
}
