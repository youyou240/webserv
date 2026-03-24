#include "AroleClass.hpp"
#include <fcntl.h>

Arole::Arole(void) {}
Arole::Arole(int fd) : _fd(fd) {}
virtual Arole::~Arole(void) {close(this->_fd);}

int&			Arole::getFD(void) {return(_fd);}
struct epoll_event &	Arole::getStructEvent(void) {return(_ev);}

