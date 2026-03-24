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
	this->_ev.data.fd = _fd;
	this->_ev.data.ptr = this;
}

Server::~Server(void)
{
	this->server -= 1;
}

void		Server::setFdTable(std::vector<int> const & fdT) {_fdTable = &fdT;}
unsigned int	Server::getNbServer(void) {return(_server);}
unsigned int	Server::getNbClient(void) {return(_client);}

void		Server::closeAll(Epoll const & manager)
{
	for (std::vector<Client &>::iterator it = _activeClient.begin(); it != _activeClient.end(); ++it)
		*it.closeConnection(manager);
	this->closeConnection(manager);
}

void	Server::handle_event(int event, Epoll & manager)
{
	struct epoll_event client;

	client.events = EPOLLIN;
	client.data.fd = accept(_fd, NULL, NULL);
	if (client.data.fd == -1)
		return ;
	fcntl(client.data.fd, F_SETFL, O_NONBLOCK);
	Client	*newClient = new Client(client.data.fd);
	client.data.ptr = newClient;
	client.data.u64 = ((uint64_t)_countID << 32) | client.data.fd;
	if (_fdTable->size < client.data.fd)
		_fdTable->resize(client.data.fd + 100);
	_fdTable[client.data.fd] = _countID;
	manager.add(client.data.fd, &client);
}
