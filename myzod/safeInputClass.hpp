#pragma once

#include <exception>
#include <string>
#include <iostream>
#include <ostream>

class safeInputClass {

public :

	static std::string		safeInput(void);
	static int			safeInputNumber(void);
	static unsigned short int	safeInputPort(void);
};

