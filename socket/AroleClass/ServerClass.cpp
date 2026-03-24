#include "AroleClass.hpp"
#include <vector>

Server::Server(void) : Arole(), _countID(0)
{
	this->_server += 1;
	this->_ev.events = EPOLLIN;
	this->_ev.data.ptr = this;
}

Server::Server(int fd) : Arole(fd), _countID(0) 
{
	this->_server += 1;
	this->_ev.events = EPOLLIN;
	this->_ev.data.ptr = this;
}

Server::~Server(void)
{
	this->_server -= 1;
}

void		Server::setFdTable(std::vector<int> const & fdT) {_fdTable = &fdT;}
unsigned int	Server::getNbServer(void) {return(_server);}
unsigned int	Server::getNbClient(void) {return(_client);}

void		Server::closeAll(Epoll const & manager)
{
	for (std::vector<Client *>::iterator it = _activeClient.begin(); it != _activeClient.end(); ++it)
		*it->closeConnection(manager);
	manager.del(*this);
	close(this->_fd);
}

void	Server::handle_event(Epoll & manager)
{
	int fd = accept(_fd, NULL, NULL);
	if (fd == -1)
		return ;
	Client	*client = new Client(fd);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	manager.add(*newClient);
}
