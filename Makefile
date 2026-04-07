INCLUDE := ./include
MODULES := ./modules
SRC := ./src
BUILD := ./build
BIN := ./bin

CXX = g++
CXXFLAGS = -g -Wall -Wextra -I$(INCLUDE)
MAN_OBJS := $(BUILD)/ModNfsManager.o $(BUILD)/Utils.o $(BUILD)/nfs_manager.o 
CON_OBJS := $(BUILD)/ModNfsConsole.o $(BUILD)/Utils.o $(BUILD)/nfs_console.o
CLIENT_OBJS := $(BUILD)/Utils.o $(BUILD)/nfs_client.o

all:  $(BIN)/nfs_manager $(BIN)/nfs_console $(BIN)/nfs_client

# make binaries with respect to dependencies
$(BIN)/nfs_manager: $(MAN_OBJS) | $(BIN)
	$(CXX) $(MAN_OBJS) -o $(BIN)/nfs_manager -lpthread 

$(BIN)/nfs_console: $(CON_OBJS) | $(BIN)
	$(CXX) $(CON_OBJS) -o $(BIN)/nfs_console

$(BIN)/nfs_client: $(CLIENT_OBJS) | $(BIN)
	$(CXX) $(CLIENT_OBJS) -o $(BIN)/nfs_client -lpthread 

# make .o in build dir from src,modules
$(BUILD)/%.o: $(MODULES)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# make build,bin dirs if they don't exist
$(BUILD):
	mkdir -p $(BUILD)

$(BIN):
	mkdir -p $(BIN)


.PHONY: clean all

clean: 
	rm -rf $(BUILD) $(BIN)

