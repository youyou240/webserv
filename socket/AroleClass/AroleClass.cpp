#include "AroleClass.hpp"
#include <fcntl.h>

Arole::Arole(void) {}
Arole::Arole(int fd) : _fd(fd) {}
virtual Arole::~Arole(void) {close(this->_fd);}

int&			Arole::getFD(void) const {return(_fd);}
struct epoll_event &	Arole::getStructEvent(void) const; {return(_ev);}

void	Arole::closeConnection(Epoll & manager)
{
	manager.del(this->_fd, NULL);
	close(this->_fd);
	delete this;
}
