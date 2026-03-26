#include "AroleClass.hpp"
#include <fcntl.h>

Arole::Arole(void) {}
Arole::Arole(int fd) : _fd(fd) {}
Arole::~Arole(void) {close(this->_fd);}

int			Arole::getFD(void) const {return(_fd);}
struct epoll_event &	Arole::getStructEvent(void) {return(_ev);}

unsigned int	Arole::getAllAcceptators(void) {return (_acceptor);}
unsigned int	Arole::getAllClients(void) {return (_client);}

void	Arole::launchActiveRoutine(Epoll & manager)
{
	time_t now = time(NULL);

	for (std::map<int, std::map<int, Client *> >::iterator it = _activeClient.begin(); it != _activeClient.end(); ++it)
	{
		std::map<int, Client *>::iterator clientIT = it->second.begin(); 
		while (clientIT != it->second.end())
		{
			Client *client = clientIT->second;
			clientIT++;
 			if (client->getInactiveTime(now))
				client->closeConnection(manager);
		}
	}
}
