#include "AroleClass.hpp"
#include "../../Epoll/epollClass.hpp"
#include <vector>

unsigned int Arole::_acceptor = 0;

Acceptor::Acceptor(void) : Arole(), _active(false)
{
	this->_acceptor += 1;
	this->_ev.events = EPOLLIN;
	this->_ev.data.ptr = this;
}

Acceptor::Acceptor(int fd) : Arole(fd), _active(false)
{
	this->_acceptor += 1;
	this->_ev.events = EPOLLIN;
	this->_ev.data.ptr = this;
}

Acceptor::~Acceptor(void)
{
}

void	Acceptor::setFD(int fd)
{
	_fd = fd;
	_activeClient[_fd];
}

void	Acceptor::addServerConfig(const ServerConfig * cfg)
{
	_configs.push_back(cfg);
}

unsigned int	Acceptor::getNbClient(void) const
{
	return(_activeClient[_fd].size());
}

void		Acceptor::start(Epoll & manager)
{
	manager.add(this);
	this->_active = true;
}

void		Acceptor::closeAll(Epoll & manager)
{
	std::map<int, Client *> servClient = _activeClient[this->_fd];
	for (std::map<int, Client *>::iterator it = servClient.begin(); it != servClient.end(); ++it)
		it->second->closeConnection(manager);
	_activeClient.erase(this->_fd);
	manager.del(this);
	this->_acceptor -= 1;
}

void	Acceptor::handle_event(Epoll & manager, uint32_t)
{
	int fd = accept(_fd, NULL, NULL);
	if (fd == -1)
		return ;
	Client	*client = new Client(fd, this->_fd);
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
	{
		client->closeConnection(manager);
		return ;
	}
	manager.add(client);
}

std::ostream &	operator<<(std::ostream & o, Acceptor const & acceptator)
{
	o << "NBclients=" << acceptator.getNbClient() << " for socket " << acceptator.getFD();
	return (o);
};
