#include "AroleClass.hpp"
#include <vector>

unsigned int Arole::_server = 0;

Server::Server(void) : Arole()
{
	this->_server += 1;
	this->_ev.events = EPOLLIN;
	this->_ev.data.ptr = this;
}

Server::Server(int fd) : Arole(fd)
{
	this->_server += 1;
	this->_ev.events = EPOLLIN;
	this->_ev.data.ptr = this;
}

Server::~Server(void)
{
	this->_server -= 1;
}

unsigned int	Server::getNbServer(void) const {return(_server);}
unsigned int	Server::getNbClient(void) const {return(_client);}

void		Server::closeAll(Epoll & manager)
{
	for (std::vector<Client *>::iterator it = _activeClient.begin(); it != _activeClient.end(); ++it)
		(*it)->closeConnection(manager);
	manager.del(this);
	close(this->_fd);
}

void	Server::handle_event(Epoll & manager)
{
	int fd = accept(_fd, NULL, NULL);
	if (fd == -1)
		return ;
	Client	*client = new Client(fd);
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
		client->closeConnection(manager);
	manager.add(client);
}

std::ostream &	operator<<(std::ostream & o, Server const & serv)
{
	o << "NBserver=" << serv.getNbServer() << "\tNBclients=" << serv.getNbClient();
	return (o);
};
