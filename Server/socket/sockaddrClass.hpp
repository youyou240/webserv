# pragma once

# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <string>
# include <string.h>
# include <stdint.h>
# include <ostream>
# include <exception>

class Sockaddr {

	struct sockaddr_in _addr;

public:
	Sockaddr(void);
	Sockaddr(Sockaddr const &);
	~Sockaddr(void);

	Sockaddr& operator=(Sockaddr const &);

	struct sockaddr_in const&	getStruct(void) const;
	short unsigned int	getFamily(void) const;
	unsigned short int	getPort(void) const;
	struct in_addr const &	getAddrStruct(void) const;
	in_addr_t const &	getAddr(void) const;
	std::string		getIp(void) const;

	void			setPort(unsigned int short);
	void			setStruct(struct sockaddr_in const & addr);
	void			resolve(std::string const & host, uint16_t port);

	class badAddr : public std::exception {
		std::string	_msg;
	public:
		badAddr(std::string const & msg) throw();
		virtual ~badAddr(void) throw();
		virtual const char *	what(void) const throw();
	};
};

std::ostream &	operator<<(std::ostream &, Sockaddr const &);
