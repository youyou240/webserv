# include "Server/ServerClass.hpp"
# include <stdlib.h>
# include <stdio.h>

static void	feelVecWithPort(std::vector<unsigned int short> & ports, int argc, char **argv)
{
	for (int i = 2; i < argc; ++i)
		ports.push_back(atoi(argv[i]));
	return ;
}
	
int main(int argc, char **argv)
{
	if (argc < 2)
		return (perror("expect ./webserv <nOfOpenPort> <Port 1> <Port 2> <Port n> as argument.\n If the nb of port doesnt match the <nOfOpenPort> random port is open.\n"), 1);
	int nbPort = atoi(argv[1]);
	if (nbPort < 1)
		return (perror("expect <nOfOpenPort> to be > than 0."), 1);
	std::vector<unsigned int short> ports;
	feelVecWithPort(ports, argc, argv);
	try {
		Server serv(nbPort, ports);
		serv.run();
	} catch (std::exception & e) {	
		std::cout << e.what() << std::endl;
	}
	return (0);
}
