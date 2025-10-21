SRC = main.cpp config.cpp Server.cpp poll_echo_buffer.cpp HTTPHandler.cpp CGIHandler.cpp Response.cpp

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++17

OBJ_DIR = obj
OBJ = $(addprefix $(OBJ_DIR)/, $(SRC:.cpp=.o))
NAME = webserv

all: $(NAME)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(NAME): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(NAME) $(OBJ)

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
