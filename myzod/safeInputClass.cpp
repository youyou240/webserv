#include "safeInputClass.hpp"
#include <cstdlib>

std::string safeInputClass::safeInput(void)
{
	std::string	str;

	do {
		getline(std::cin, str);
		if (std::cin.eof())
			return ("eof");
	} while (!str.size());
	if (str == "exit")
		return ("exit");
	return (str);
}

int		only_digit(std::string str)
{
	for (size_t i = 0; i < str.length(); i++) {
		if (!std::isdigit(str[i]))
			return (0);
	}
	return (1);
}

int	safeInputClass::safeInputNumber(void)
{
	std::string	str;
	int		pass;

	pass = 0;
	do {
		if (pass == 1)
			std::cout << "Enter only number for port/server" << std::endl;
		str = safeInput();
		if (str == "exit")
			return (-1);
		pass = 1;
	} while (!only_digit(str));
	return (std::atoi(str.c_str()));
}

unsigned short int	safeInputClass::safeInputPort(void)
{
	int port;
	do {
		port = safeInputNumber();
		if (port < 0 || port > 65535)
			std::cout << "A port can only be beetwin 0 and 65535 - retry an other port" << std::endl;
		if (port == -1)
			return (port);
	} while (port < 0 || port > 65535);
	return (port);
}
