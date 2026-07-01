.DEFAULT_GOAL = all

NAME = Webserv

CPPFLAGS = -Wall -Wextra -Werror -g3 -std=c++98

SRCS = main.cpp Server/ServerClass.cpp	\
		Server/ServerManagerClass.cpp \
		Server/socket/socketClass.cpp \
		Server/socket/sockaddrClass.cpp \
		Epoll/epollClass.cpp	\
		Server/AroleClass/AroleClass.cpp \
		Server/AroleClass/ClientClass.cpp \
		Server/AroleClass/AcceptorClass.cpp \
		Config/ServerConfigClass.cpp \
		Config/LocationConfigClass.cpp \
		Config/ConfigParserClass.cpp \
		Config/ConfigParserDirectives.cpp \
		myzod/safeInputClass.cpp
 
OBJS = $(SRCS:%.cpp=%.o)

$(NAME) : $(OBJS)
	c++ $(CPPFLAGS) $(OBJS) -o $(NAME)

.PHONY: all
all : $(NAME)


.PHONY: clean
clean:
	$(RM) $(SRCS:%.cpp=%.o)

.PHONY: fclean
fclean: clean
	$(RM) $(NAME)

.PHONY: re
re: fclean all
