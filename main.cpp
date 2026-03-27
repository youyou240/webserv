# include "Server/ServerClass.hpp"
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
	{
		std::cerr << "expect ./webserv <nOfOpenPort> <Port 1> <Port 2> <Port n> as argument.\n If the nb of port doesnt match the <nOfOpenPort> random port is open.\n" << std::endl;
		return (1);
	}
	int nbPort = atoi(argv[1]);
	if (nbPort < 1)
	{
		std::cerr << "expect <nOfOpenPort> to be > than 0." << std::endl;
		return (1);
	}
	std::vector<unsigned int short> ports;
	feelVecWithPort(ports, argc, argv);
	try {
		Server serv(nbPort, ports);
		std::signal(SIGINT, serv.handleSigEvent);
		serv.run();
	} catch (std::exception & e) {	
		std::cout << e.what() << std::endl;
	}
	return (0);
}
