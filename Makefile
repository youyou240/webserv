.DEFAULT_GOAL = all

NAME = Webserv

CPPFLAGS = -Wall -Wextra -Werror -g3 -std=c++98

SRCS = main.cpp socket/socketClass.cpp \
       		socket/epollClass.cpp	\
		socket/sockaddrClass.cpp \
		socket/AroleClass/AroleClass.cpp \
		socket/AroleClass/ClientClass.cpp \
		socket/AroleClass/ServerClass.cpp \
 
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
