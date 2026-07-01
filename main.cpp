# include <iostream>
# include <string>
# include <exception>
# include <csignal>
# include "Epoll/epollClass.hpp"
# include "Server/ServerManagerClass.hpp"
# include "Config/ConfigParserClass.hpp"

# define DEFAULT_CONFIG "configs/default.conf"

int	main(int argc, char **argv)
{
	std::string	path = argc > 1 ? argv[1] : DEFAULT_CONFIG;

	try {
		ConfigParser	parser(path);
		parser.parse();
		std::cout << parser << std::endl;
		Epoll &		epoll = Epoll::createEpoll();
		ServerManager	manager(epoll);
		std::signal(SIGINT, ServerManager::handleSigEvent);
		manager.build(parser.getServers());
		manager.run();
	} catch (std::exception & e) {
		std::cerr << e.what() << std::endl;
		return (1);
	}
	return (0);
}
